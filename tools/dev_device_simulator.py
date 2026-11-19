#!/usr/bin/env python3
"""Local TEST HARNESS for the dashboard UI. It is NOT the device firmware.

It serves the exact dashboard HTML from cottage_premium_dashboard.hpp and
answers the setup-related /api/* routes in the device's JSON shape, so the
wizard can be driven in a real browser without flashing a board.

* /api/regions is parsed from forecast/market_zones.hpp (no second table).
* /api/geocode replays the recorded Photon responses in tests/data/geocode.
* /api/status price fields are taken from the recorded Nord Pool fixtures.
* Validation here is a SIMPLIFIED copy of cottage/site_settings.hpp; the real
  rules are covered by tests/site_settings_tests.cpp. Never use this to claim
  firmware behaviour.

Usage: python tools/dev_device_simulator.py [--port 8765] [--configured]
"""
import argparse
import json
import math
import re
import subprocess
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

ROOT = Path(__file__).resolve().parents[1]
HEADER = (ROOT / "firmware/cottage/include/cottage_premium_dashboard.hpp").read_text(encoding="utf-8")
HTML = HEADER.split('R"ENEVINA_UI(', 1)[1].split(')ENEVINA_UI"', 1)[0]
ZONES_SRC = (ROOT / "forecast/market_zones.hpp").read_text(encoding="utf-8")


def parse_regions():
    zones = {}
    for name, body in re.findall(r"inline constexpr MarketZone (\w+)\[\]=\{(.*?)\};", ZONES_SRC, re.S):
        zones[name] = [
            {"id": a, "np": b, "name_en": c, "name_fi": d}
            for a, b, c, d in re.findall(r'\{"([^"]+)","([^"]+)","([^"]+)","([^"]+)"\}', body)
        ]
    out = []
    for m in re.finditer(r'ENEVINA_COUNTRY\("(\w+)","([^"]+)","([^"]+)","(\w+)",(\d),"([^"]+)",(\w+)\)', ZONES_SRC):
        iso, en, fi, cur, off, iana, arr = m.groups()
        out.append({"iso": iso, "name_en": en, "name_fi": fi, "currency": cur, "iana": iana,
                    "std_utc_offset_hours": int(off),
                    "zones": [{k: z[k] for k in ("id", "name_en", "name_fi")} for z in zones[arr]],
                    "_np": {z["id"]: z["np"] for z in zones[arr]}})
    return out


COUNTRIES = parse_regions()
ADAPTERS = [
    {"id": "mitsubishi_cn105_esphome", "manufacturer": "Mitsubishi Electric", "models": "MSZ/MUZ-FD25VA",
     "link": "ESPHome CN105 bridge", "maturity": "FIELD_PILOT", "selectable": True, "measurements": "", "commands": ""},
    {"id": "midea_lan", "manufacturer": "Midea", "models": "V2/V3 Wi-Fi module",
     "link": "local LAN", "maturity": "FIRMWARE_CONTROL_EXPERIMENTAL", "selectable": True, "measurements": "", "commands": ""},
]
STATE = {
    "tariff": {"margin_c_per_kwh": 0.4, "transfer_day_c_per_kwh": 4.0, "transfer_night_c_per_kwh": 2.0,
               "electricity_tax_c_per_kwh": 2.2, "vat_percent": 25.5, "components_include_vat": False,
               "transfer_kind": "time_of_day", "day_start_hour": 7, "night_start_hour": 22},
    "min_price_advantage_percent": 10, "cn105_url": "", "shelly_url": "", "fmi_latlon": "", "pump_adapter": "",
    "region": {"country": "FI", "price_zone": "FI", "city": "", "timezone": "Europe/Helsinki", "language": "",
               "tariff_confirmed": False, "setup_complete": False, "setup_step": 0},
}
GEO_STATE = {"q": None, "since": 0.0}
TIMELINE = {"cli": None, "start": 300}
HISTORY = {"cli": None}
FRESH = {"cli": None, "args": ["0.5", "1", "1", "0", "1", "1", "0"]}   # hours, wifi, sensor, price_error, zone, clock, armed     # set by --timeline-cli; start = index into the FI week
# Sensors: values are the official Ruuvi Data Format 5 test vector (24.3 C) with
# different MACs/RSSI. The 3rd one stopped advertising (stale) to show that case.
SENSORS = {"CB:B8:33:4C:88:01": {"rssi": -48, "age0": 4, "live": True, "t": 21.4, "rh": 45.2, "v": 2.98},
           "CB:B8:33:4C:88:02": {"rssi": -71, "age0": 20, "live": True, "t": 4.9, "rh": 81.0, "v": 3.01},
           "CB:B8:33:4C:88:03": {"rssi": -85, "age0": 640, "live": False, "t": 19.0, "rh": 50.0, "v": 2.6}}
SENSOR_NAMES = {}
BOUND = {"indoor": None, "outdoor": None}
SHELLY = {"calls": 0, "since": 0.0}
AIR = {"mode": 3, "hold_target_c": 12.0, "occupied_target_c": 21.0, "drying_delta_c": 4.0}
STATE["shelly_scope"] = "unknown"
STATE["optimization"] = {"profile": "balanced", "comfort_min_c": None, "comfort_max_c": None}


def country(iso):
    return next((c for c in COUNTRIES if c["iso"] == iso), None)


def region_view():
    r = dict(STATE["region"])
    c = country(r["country"])
    z = c and next((z for z in c["zones"] if z["id"] == r["price_zone"]), None)
    r["price_region_resolved"] = bool(z)
    if z:
        r.update(currency=c["currency"], nordpool_area=c["_np"][z["id"]], std_utc_offset_hours=c["std_utc_offset_hours"])
    return r


def apply_form(form):
    """Simplified order-independent apply (country, price_zone, rest)."""
    new = json.loads(json.dumps(STATE))
    order = sorted(form.items(), key=lambda kv: {"country": 0, "price_zone": 1}.get(kv[0], 2))
    for k, v in order:
        reg = new["region"]
        if k == "country":
            c = country(v)
            if not c:
                return None, "BAD_CHOICE", k
            if reg["country"] != v:
                t = new["tariff"]
                for key in ("margin_c_per_kwh", "transfer_day_c_per_kwh", "transfer_night_c_per_kwh",
                            "electricity_tax_c_per_kwh", "vat_percent"):
                    t[key] = 0
                reg["tariff_confirmed"] = False
            reg["country"] = v
            reg["timezone"] = c["iana"]
            if reg["price_zone"] not in [z["id"] for z in c["zones"]]:
                reg["price_zone"] = c["zones"][0]["id"] if len(c["zones"]) == 1 else ""
        elif k == "price_zone":
            c = country(reg["country"])
            if v and not (c and v in [z["id"] for z in c["zones"]]):
                return None, "BAD_CHOICE", k
            reg["price_zone"] = v
        elif k in ("city", "language"):
            reg[k] = v
        elif k == "timezone":
            reg[k] = v
        elif k in ("tariff_confirmed", "setup_complete"):
            reg[k] = v == "true"
        elif k == "setup_step":
            reg[k] = int(v)
        elif k in ("margin_c_per_kwh", "transfer_day_c_per_kwh", "transfer_night_c_per_kwh",
                   "electricity_tax_c_per_kwh", "vat_percent"):
            try:
                new["tariff"][k] = float(v.replace(",", "."))
            except ValueError:
                return None, "BAD_NUMBER", k
        elif k == "optimization_profile":
            if v not in ("comfort", "balanced", "saving"):
                return None, "BAD_CHOICE", k
            new["optimization"]["profile"] = v
        elif k in ("comfort_min_c", "comfort_max_c"):
            if v == "":
                new["optimization"][k] = None
            else:
                try:
                    x = float(v.replace(",", "."))
                except ValueError:
                    return None, "BAD_NUMBER", k
                if not (5 <= x <= 26) or abs(x * 2 - round(x * 2)) > 1e-9:
                    return None, "OUT_OF_RANGE", k
                new["optimization"][k] = x
        elif k == "components_include_vat":
            new["tariff"][k] = v == "true"
        elif k == "shelly_url":
            if new["shelly_url"] != v:
                new["shelly_scope"] = "unknown"
            new[k] = v
        elif k == "shelly_scope":
            if v not in ("unknown", "heat_pump", "whole_house"):
                return None, "BAD_CHOICE", k
            new[k] = v
        elif k in ("cn105_url", "fmi_latlon", "pump_adapter"):
            new[k] = v
        else:
            return None, "UNKNOWN_KEY", k
    return new, None, None


def status():
    reg = region_view()
    st = {"air": {"config_token": "sim-token", "sensor_fresh": False, "bound_mac": "", "discovered_mac": "", **AIR},
          "firmware_version": "simulator", "control_armed": False, "price_error": 0, "price_http_status": 0,
          "price_currency": reg.get("currency", "EUR"), "price_area": reg.get("nordpool_area", ""),
          "spot": None, "total_price": None, "weather_c": None, "market_last_fetch_utc": 0,
          "ota_signed_pull_enabled": True, "ota_signed_status": 0, "ota_signed_good_version": 20261101,
          "ota_signed_candidate": 20261101, "ota_last_check_utc": int(time.time()) - 600, "ota_dashboard_pending": False,
          "ota_downloaded_bytes": 0, "ota_last_update_utc": int(time.time()) - 5 * 86400, "ota_last_update_version": 20261101}
    area = reg.get("nordpool_area")
    if area:
        f = ROOT / "tests/data/nordpool" / f"{area}_2026-11-18.json"
        if f.exists():
            e = json.loads(f.read_text())["multiAreaEntries"][40]["entryPerArea"][area]
            st["spot"] = e / 1000.0
            st["total_price"] = e / 1000.0 * 1.2
            st["market_last_fetch_utc"] = int(time.time())
        else:
            st["price_error"] = 104
    return st


def geocode(q, cc):
    stem = {"kang": "kang", "malm": "malmo", "tall": "tallinn", "hels": "helsin"}.get(q.lower()[:4])
    if not stem:
        return {"state": "done", "results": []}
    data = json.loads((ROOT / f"tests/data/geocode/photon_{stem}.json").read_text(encoding="utf-8"))
    out = []
    for f in data["features"]:
        p = f["properties"]
        if p.get("countrycode") != cc and cc:
            continue
        if not (p.get("osm_key") == "place" and p.get("osm_value") in {"city", "town", "village", "municipality"}
                or (p.get("osm_key") == "boundary" and p.get("osm_value") == "administrative")):
            continue
        lon, lat = f["geometry"]["coordinates"]
        if any(r["name"] == p["name"] and r["region"] == p.get("state", p.get("county", "")) for r in out):
            continue
        out.append({"name": p["name"], "region": p.get("state", p.get("county", "")), "country": p["countrycode"],
                    "country_name": p.get("country", ""), "lat": lat, "lon": lon, "latlon": f"{lat:.4f},{lon:.4f}"})
    return {"state": "done", "results": out[:8]}


class H(BaseHTTPRequestHandler):
    def log_message(self, *a):
        sys.stderr.write("sim: " + (a[0] % a[1:]) + "\n")

    def send(self, code, body, ctype="application/json; charset=utf-8"):
        raw = body if isinstance(body, bytes) else json.dumps(body).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def do_GET(self):
        u = urlparse(self.path)
        q = parse_qs(u.query)
        if u.path == "/":
            return self.send(200, HTML.encode("utf-8"), "text/html; charset=utf-8")
        if u.path == "/api/status":
            return self.send(200, status())
        if u.path == "/api/regions":
            return self.send(200, {"countries": [{k: v for k, v in c.items() if not k.startswith("_") and k != "iana"}
                                                 for c in COUNTRIES]})
        if u.path == "/api/settings":
            return self.send(200, {**STATE, "stored_in_device": True, "generation": 1,
                                   "region": region_view(), "adapters": ADAPTERS})
        if u.path == "/api/geocode":
            term = q.get("q", [""])[0]
            if len([c for c in term]) < 3:
                return self.send(200, {"state": "failed", "error": "BAD_QUERY", "results": []})
            now = time.time()
            if GEO_STATE["q"] != term:           # first call answers "pending", like the worker
                GEO_STATE.update(q=term, since=now)
                return self.send(202, {"state": "pending", "results": []})
            if now - GEO_STATE["since"] < 0.6:
                return self.send(202, {"state": "pending", "results": []})
            return self.send(200, geocode(term, q.get("country", [""])[0]))
        if u.path == "/api/air/sensors":
            out = []
            for mac, v in SENSORS.items():
                role = "indoor" if BOUND["indoor"] == mac else "outdoor" if BOUND["outdoor"] == mac else ""
                out.append({"mac": mac, "name": SENSOR_NAMES.get(mac, ""), "role": role, "temperature_c": v["t"],
                            "humidity_pct": v["rh"], "battery_v": v["v"], "rssi_dbm": v["rssi"],
                            "age_s": v["age0"], "live": v["live"], "frames": 5})
            out.sort(key=lambda x: -x["rssi_dbm"])
            bound = {"indoor": BOUND["indoor"], "outdoor": BOUND["outdoor"]}
            for r in ("indoor", "outdoor"):
                if BOUND[r]:
                    bound[r + "_name"] = SENSOR_NAMES.get(BOUND[r], "")
            return self.send(200, {"sensors": out, "bound": bound, "ble_ready": True, "scan_attempts": 9,
                                   "scan_completed": 9, "scan_memory_denials": 0, "scan_busy_denials": 0})
        if u.path == "/api/shelly/discover":
            now = time.time()
            if now - SHELLY["since"] > 30:
                SHELLY.update(since=now)
                return self.send(202, {"state": "pending", "devices": []})
            if now - SHELLY["since"] < 1.5:
                return self.send(202, {"state": "pending", "devices": []})
            # Documented Shelly formats (Gen2 supported, Gen2 with password, Gen1) - not device recordings.
            return self.send(200, {"state": "done", "cooldown": False, "age_s": 0, "devices": [
                {"host": "shellyplus1pm-a8032abe7d94", "url": "http://192.168.1.30", "id": "shellyplus1pm-a8032abe7d94",
                 "model": "SNSW-001P16EU", "app": "Plus1PM", "generation": 2, "support": "SUPPORTED", "power_w": 412.7},
                {"host": "shellyplus1pm-bbbbbbbbbbbb", "url": "http://192.168.1.31", "id": "shellyplus1pm-bbbbbbbbbbbb",
                 "model": "SNSW-001P16EU", "app": "Plus1PM", "generation": 2, "support": "AUTH_REQUIRED", "power_w": None},
                {"host": "shelly1pm-a4cf12f4d2b1", "url": "http://192.168.1.32", "id": "SHSW-PM", "model": "",
                 "app": "", "generation": 1, "support": "GEN1_NOT_SUPPORTED", "power_w": None}]})
        if u.path == "/api/timeline":
            if not TIMELINE["cli"]:
                return self.send(200, {"valid": False, "slots": [], "segments": []})
            opt = STATE["optimization"]
            mode = AIR["mode"]
            target = AIR["occupied_target_c"] if mode == 3 else AIR["hold_target_c"] if mode == 2 else -1
            csv = ROOT / "tests/data/prices/FI_EUR_2026-11-12_to_18.csv"
            out = subprocess.run([TIMELINE["cli"], str(csv), str(TIMELINE["start"]), "192", opt["profile"], str(target),
                                  str(opt["comfort_min_c"] or 0), str(opt["comfort_max_c"] or 0), "0.06", "EUR", "FI",
                                  str(target - 0.3 if target > 0 else 21)], capture_output=True, text=True, timeout=10)
            return self.send(200, out.stdout.strip().encode("utf-8"))
        if u.path == "/sim/freshness":                      # harness control: change the scenario
            names = ["hours", "wifi", "sensor", "price_error", "zone", "clock", "armed"]
            FRESH["args"] = [q.get(n, [FRESH["args"][i]])[0] for i, n in enumerate(names)]
            return self.send(200, {"scenario": dict(zip(names, FRESH["args"]))})
        if u.path == "/api/freshness":
            if not FRESH["cli"]:
                return self.send(404, {"error": "no freshness cli"})
            out = subprocess.run([FRESH["cli"], *FRESH["args"]], capture_output=True, text=True, timeout=10)
            return self.send(200, out.stdout.strip().encode("utf-8"))
        if u.path == "/api/history":
            rng = q.get("range", ["24h"])[0]
            if not HISTORY["cli"] or rng not in ("24h", "7d", "30d"):
                return self.send(200, {"valid": False, "points": [], "events": []})
            scratch = ROOT / "build" / "histdir"
            scratch.mkdir(parents=True, exist_ok=True)
            csv = ROOT / "tests/data/prices/FI_EUR_2026-11-12_to_18.csv"
            out = subprocess.run([HISTORY["cli"], str(csv), rng, str(scratch)], capture_output=True, text=True, timeout=30)
            return self.send(200, out.stdout.strip().encode("utf-8"))
        if u.path == "/api/weather":
            # Synthetic outdoor series aligned with the recorded price slice (harness data, not a forecast).
            rows = [r.split(",") for r in (ROOT / "tests/data/prices/FI_EUR_2026-11-12_to_18.csv").read_text().splitlines()[1:]]
            t0 = int(rows[TIMELINE["start"]][0]) // 3600 * 3600
            slots = [{"utc": t0 + h * 3600, "temperature_c": round(-3 + 4 * math.sin(6.2832 * (h - 9) / 24), 1)} for h in range(0, 52)]
            return self.send(200, {"valid": True, "slots": slots, "source": "simulator"})
        if u.path == "/api/prices":
            return self.send(200, {"valid": False, "slots": []})
        return self.send(404, {"error": "not in simulator"})

    def do_POST(self):
        u = urlparse(self.path)
        n = int(self.headers.get("Content-Length", "0"))
        form = {k: v[0] for k, v in parse_qs(self.rfile.read(n).decode("utf-8")).items()}
        if u.path == "/api/air/bind":
            if form.pop("token", "") != "sim-token":
                return self.send(403, {"saved": False})
            role, mac = form.get("which"), form.get("mac")
            if role not in BOUND or (mac != "none" and mac not in SENSORS):
                return self.send(400, {"saved": False})
            BOUND[role] = None if mac == "none" else mac
            return self.send(200, {"saved": True, "physical_writes": False})
        if u.path == "/api/air/sensor-name":
            if form.pop("token", "") != "sim-token":
                return self.send(403, {"saved": False})
            name = form.get("name", "")
            if any(c in name for c in "<>\"&'") or len(name.encode()) > 23:
                return self.send(400, {"saved": False, "error": "BAD_NAME"})
            if name:
                SENSOR_NAMES[form.get("mac", "")] = name
            else:
                SENSOR_NAMES.pop(form.get("mac", ""), None)
            return self.send(200, {"saved": True})
        if u.path == "/api/air/config":
            if form.pop("token", "") != "sim-token":
                return self.send(403, {"saved": False})
            AIR.update(mode=int(form["mode"]), hold_target_c=float(form["hold"]),
                       occupied_target_c=float(form["occupied"]), drying_delta_c=float(form["delta"]))
            return self.send(200, {"saved": True, "physical_writes": False})
        if u.path != "/api/settings":
            return self.send(404, {"error": "not in simulator"})
        if form.pop("token", "") != "sim-token":
            return self.send(403, {"saved": False, "error": "TOKEN"})
        new, err, key = apply_form(form)
        if err:
            return self.send(400, {"saved": False, "error": err, "key": key})
        STATE.update(new)
        return self.send(200, {"saved": True})


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--configured", action="store_true", help="start with setup already complete")
    ap.add_argument("--freshness-cli", help="path to freshness_cli(.exe) built from tools/freshness_cli.cpp")
    ap.add_argument("--history-cli", help="path to history_cli(.exe) built from tools/history_cli.cpp")
    ap.add_argument("--timeline-cli", help="path to plan_timeline_cli(.exe) built from tools/plan_timeline_cli.cpp")
    a = ap.parse_args()
    TIMELINE["cli"] = a.timeline_cli
    HISTORY["cli"] = a.history_cli
    FRESH["cli"] = a.freshness_cli
    if a.configured:
        STATE["region"].update(setup_complete=True, setup_step=9)
    print(f"simulator on http://127.0.0.1:{a.port}/ ({len(COUNTRIES)} countries)", flush=True)
    ThreadingHTTPServer(("127.0.0.1", a.port), H).serve_forever()
