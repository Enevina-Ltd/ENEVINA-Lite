#!/usr/bin/env python3
"""Independent read-only cottage field capture. Never writes to a device.

Use: python tools/cottage_field_capture.py --cn105-url http://HOST/events
     --shelly-base http://HOST --output /PRIVATE/PATH.jsonl --duration-s 120

Output includes UTC receipt timestamps. Values from an initial SSE snapshot
are not proof of live CN105 UART updates. Do not treat pump temperature as an
independent freeze sensor. Shelly measures total cottage energy, not HP COP.
All addresses are runtime-only; never publish the output or credentials.
"""
from __future__ import annotations

import argparse
import datetime as dt
import ipaddress
import json
import math
import os
import queue
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path
from zoneinfo import ZoneInfo

USER_AGENT = "EnevinaLite-C2-readonly/0.1"
FIELDS = {
    "climate/Mitsubishi Electric MSZ-FD25VA": "climate",
    "sensor/Compressor Frequency": "compressor_hz",
    "sensor/Outside Air Temperature": "pump_outside_c",
    "sensor/CN105 Connection Uptime": "cn105_connection_uptime_s",
}


def local_url(raw: str, required_path: str) -> str:
    u = urllib.parse.urlsplit(raw)
    if u.scheme != "http" or u.username is not None or u.password is not None:
        raise ValueError("LAN endpoint must be plain http:// without credentials")
    if not u.hostname or u.query or u.fragment or u.path != required_path:
        raise ValueError("unexpected LAN path, query or fragment")
    if u.port is not None and not 1 <= u.port <= 65535:
        raise ValueError("invalid port")
    try:
        host = ipaddress.ip_address(u.hostname)
    except ValueError as exc:
        raise ValueError("use the explicitly verified LAN IPv4 address") from exc
    if host.version != 4 or not host.is_private or host.is_loopback:
        raise ValueError("only private non-loopback IPv4 permitted")
    return raw


def normalize_cn105(record: object) -> dict | None:
    if not isinstance(record, dict):
        return None
    field = FIELDS.get(record.get("id"))
    if field is None:
        return None
    if field == "climate":
        mode = record.get("mode")
        if mode not in ("HEAT", "OFF", "COOL", "DRY", "FAN", "AUTO"):
            return None
        try:
            indoor = float(record["current_temperature"])
            setpoint = float(record["target_temperature"])
        except (KeyError, ValueError, TypeError):
            return None
        if not all(map(math.isfinite, (indoor, setpoint))) or not (-20 <= indoor <= 50 and 0 <= setpoint <= 35):
            return None
        return {"field": field, "mode": mode, "indoor_c": indoor, "setpoint_c": setpoint}
    try:
        val = float(record["value"])
    except (KeyError, ValueError, TypeError):
        return None
    if not math.isfinite(val):
        return None
    if field == "compressor_hz" and not 0 <= val <= 200:
        return None
    if field == "pump_outside_c" and not -55 <= val <= 60:
        return None
    if field == "cn105_connection_uptime_s" and not 0 <= val <= 1e10:
        return None
    return {"field": field, "value": val}


def normalize_shelly(data: object) -> dict | None:
    if not isinstance(data, dict) or not isinstance(data.get("output"), bool):
        return None
    try:
        watts = float(data["apower"])
        wh = float(data["aenergy"]["total"])
    except (KeyError, TypeError, ValueError):
        return None
    if not all(map(math.isfinite, (watts, wh))) or not (0 <= watts <= 20000 and 0 <= wh <= 1e12):
        return None
    return {"relay_on": data["output"], "cottage_total_w": watts, "cottage_total_wh": wh}


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, msg, headers, newurl):
        return None


OPENER = urllib.request.build_opener(NoRedirect)


def get(url: str, timeout: float = 6):
    return OPENER.open(urllib.request.Request(
        url, headers={"User-Agent": USER_AGENT, "Accept": "application/json, text/event-stream"},
        method="GET"), timeout=timeout)


def utc_now() -> str:
    return dt.datetime.now(dt.timezone.utc).isoformat(timespec="milliseconds")


def price_for_now() -> dict | None:
    """Current Finnish day-ahead price, in EUR/kWh; read-only public API."""
    clock = dt.datetime.now(ZoneInfo("Europe/Helsinki"))
    day = clock.strftime("%Y-%m-%d")
    url = ("https://dataportal-api.nordpoolgroup.com/api/DayAheadPrices?"
           f"currency=EUR&market=DayAhead&deliveryArea=FI&date={day}")
    with get(url, timeout=12) as response:
        if response.status != 200:
            return None
        content = response.read(300000)
    data = json.loads(content)
    now = dt.datetime.now(dt.timezone.utc)
    for item in data.get("multiAreaEntries", []):
        start = dt.datetime.fromisoformat(item["deliveryStart"].replace("Z", "+00:00"))
        end = dt.datetime.fromisoformat(item["deliveryEnd"].replace("Z", "+00:00"))
        if start <= now < end:
            per_mwh = float(item["entryPerArea"]["FI"])
            if not math.isfinite(per_mwh):
                return None
            return {"spot_eur_per_kwh": per_mwh / 1000,
                    "slot_start_utc": start.isoformat(), "slot_end_utc": end.isoformat()}
    return None


def capture(cn105_url: str, shelly_base: str, output: Path, duration_s: float, poll_s: float) -> dict:
    stop = threading.Event()
    events: queue.Queue[dict] = queue.Queue(maxsize=4096)
    metrics: dict[str, int] = {}
    deadline = time.monotonic() + duration_s

    def put(kind: str, **values):
        record = {"ts_utc": utc_now(), "kind": kind, **values}
        try:
            events.put(record, timeout=0.2)
        except queue.Full:
            metrics["queue_drops"] = metrics.get("queue_drops", 0) + 1

    def cn105():
        session = 0
        while not stop.is_set() and time.monotonic() < deadline:
            session += 1
            session_started=time.monotonic()
            try:
                with get(cn105_url, 6) as response:
                    if response.status != 200 or "text/event-stream" not in response.headers.get("Content-Type", ""):
                        raise RuntimeError("SSE HTTP/content type invalid")
                    put("cn105_connect", session=session, chunked="chunked" in
                        response.headers.get("Transfer-Encoding", "").lower())
                    event_name = ""
                    data: list[str] = []
                    # urllib decodes Transfer-Encoding: chunked, unlike raw ESP32 WiFiClient.
                    for wire in response:
                        if stop.is_set() or time.monotonic() >= deadline:
                            break
                        line = wire.decode("utf8", "replace").rstrip("\r\n")
                        if not line:
                            if event_name == "state" and data:
                                try:
                                    doc = normalize_cn105(json.loads("\n".join(data)))
                                    if doc:
                                        put("cn105", session=session, stream_elapsed_s=round(time.monotonic()-session_started,2), **doc)
                                except (ValueError, TypeError):
                                    put("cn105_invalid", session=session)
                            event_name = ""
                            data = []
                        elif line.startswith("event:"):
                            event_name = line[6:].strip()
                        elif line.startswith("data:") and len(data) < 8:
                            data.append(line[5:].lstrip(" "))
            except (OSError, ValueError, urllib.error.URLError, RuntimeError) as exc:
                put("cn105_disconnect", session=session, reason=type(exc).__name__)
            if not stop.is_set():
                stop.wait(1.5)

    def shelly():
        while not stop.is_set() and time.monotonic() < deadline:
            started = time.monotonic()
            try:
                with get(shelly_base + "/rpc/Switch.GetStatus?id=0", 6) as response:
                    if response.status != 200 or int(response.headers.get("Content-Length", "0")) not in range(1, 4097):
                        raise ValueError("Shelly reply invalid length")
                    record = normalize_shelly(json.loads(response.read(4096)))
                    if record is None:
                        raise ValueError("Shelly JSON invalid")
                    put("shelly", latency_ms=round((time.monotonic()-started)*1000, 1), **record)
            except (OSError, ValueError, TypeError, urllib.error.URLError) as exc:
                put("shelly_error", reason=type(exc).__name__)
            stop.wait(max(0.1, poll_s-(time.monotonic()-started)))

    def prices():
        while not stop.is_set() and time.monotonic() < deadline:
            try:
                record = price_for_now()
                if record is not None:
                    put("nordpool", **record)
                else:
                    put("nordpool_error", reason="NoCurrentSlot")
            except (OSError, ValueError, KeyError, TypeError, urllib.error.URLError) as exc:
                put("nordpool_error", reason=type(exc).__name__)
            stop.wait(900)

    threads = [threading.Thread(target=fn, name=name, daemon=True)
               for name, fn in (("CN105", cn105), ("Shelly", shelly), ("NordPool", prices))]
    for thread in threads:
        thread.start()
    output.parent.mkdir(parents=True, exist_ok=True)
    counts: dict[str, int] = {}
    with output.open("a", encoding="utf8") as fh:
        put("capture_start", mode="READ_ONLY", version="C2.2")
        while time.monotonic() < deadline:
            try:
                record = events.get(timeout=0.5)
            except queue.Empty:
                continue
            counts[record["kind"]] = counts.get(record["kind"], 0) + 1
            fh.write(json.dumps(record, ensure_ascii=False, separators=(",", ":")) + "\n")
            fh.flush()
        stop.set()
        for thread in threads:
            thread.join(timeout=7)
        while not events.empty():
            record = events.get_nowait()
            counts[record["kind"]] = counts.get(record["kind"], 0) + 1
            fh.write(json.dumps(record, ensure_ascii=False, separators=(",", ":")) + "\n")
        fh.flush()
    return counts


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--cn105-url", required=True)
    ap.add_argument("--shelly-base", required=True)
    ap.add_argument("--output", type=Path, required=True)
    ap.add_argument("--duration-s", type=float, default=120)
    ap.add_argument("--poll-s", type=float, default=15)
    args = ap.parse_args()
    local_url(args.cn105_url, "/events")
    local_url(args.shelly_base, "")
    if not 2 <= args.duration_s <= 48*3600 or not 1 <= args.poll_s <= 3600:
        ap.error("duration must be 2 s..48 h; poll 1 s..1 h")
    os.umask(0o077)  # Raw local timestamps and sensor readings stay owner-only.
    counts = capture(args.cn105_url, args.shelly_base, args.output,
                     args.duration_s, args.poll_s)
    print("READONLY_CAPTURE_DONE " + json.dumps(counts, sort_keys=True))


if __name__ == "__main__":
    main()
