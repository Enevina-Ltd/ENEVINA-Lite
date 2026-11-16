#!/usr/bin/env python3
"""Read-only ESP32-S3 USB serial stability logger with real elapsed time.

Starts NO heat commands, performs NO network or serial writes, and
does not imply a 24/48h test has passed until that much real time elapses.
Requires pyserial; logs are local/private and must not be committed.
"""
from __future__ import annotations
import argparse
import datetime as dt
import json
import os
from pathlib import Path
import time
try:
    import serial
except ImportError:
    serial = None  # Pure classifier/unit tests do not need pyserial.


def classify(line: str) -> str | None:
    if "C2_SHADOW_FATAL" in line or "Guru Meditation" in line or "PANIC" in line:
        return "fatal"
    if "rst:0x" in line or line.startswith("ESP-ROM:"):
        return "boot"
    if "C2_SHADOW wifi=" in line:
        return "heartbeat"
    if "watchdog" in line.lower() or "task_wdt" in line:
        return "watchdog"
    if "NO ACTUATOR PATH" in line:
        return "shadow_start"
    return None


def utc() -> str:
    return dt.datetime.now(dt.timezone.utc).isoformat(timespec="milliseconds")


def monitor(port: str, output: Path, duration_s: float):
    if serial is None:
        raise RuntimeError("pyserial required for hardware logging")
    stop_at = time.monotonic() + duration_s
    os.umask(0o077)
    output.parent.mkdir(parents=True, exist_ok=True)
    last_heartbeat = None
    stats = {"boot": 0, "heartbeat": 0, "shadow_start": 0,
             "watchdog": 0, "fatal": 0, "heartbeat_gap": 0, "serial_error": 0}
    with output.open("a", encoding="utf-8") as fh:
        def log(kind, **fields):
            record = {"utc": utc(), "kind": kind, **fields}
            fh.write(json.dumps(record, ensure_ascii=False) + "\n")
            fh.flush()
        log("capture_start", mode="READ_ONLY", target="ESP32-S3 USB", duration_s=duration_s)
        while time.monotonic() < stop_at:
            try:
                with serial.Serial(port, 115200, timeout=0.5) as com:
                    com.dtr = False
                    com.rts = False
                    log("serial_open")
                    while time.monotonic() < stop_at:
                        raw = com.readline()
                        if not raw:
                            continue
                        text = raw.decode("utf-8", "replace").strip()
                        event = classify(text)
                        if event:
                            stats[event] = stats.get(event, 0) + 1
                            if event == "heartbeat":
                                now = time.monotonic()
                                if last_heartbeat is not None and now-last_heartbeat > 90:
                                    stats["heartbeat_gap"] += 1
                                    log("heartbeat_gap", elapsed_s=round(now-last_heartbeat, 1))
                                last_heartbeat = now
                                log("heartbeat", message=text[:420])
                            else:
                                # Keep addresses and credentials out of logs.
                                log(event, message=text[:100])
            except (OSError, serial.SerialException) as exc:
                stats["serial_error"] += 1
                log("serial_error", error=type(exc).__name__)
                time.sleep(min(5, max(0, stop_at-time.monotonic())))
        log("capture_complete", elapsed_s=duration_s, **stats)
    return stats


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--port", required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--duration-s", type=float, default=86400)
    args=p.parse_args()
    if not 2<=args.duration_s<=172800:
        p.error("2 seconds to 48 real hours")
    print(json.dumps(monitor(args.port,args.output,args.duration_s),sort_keys=True))


if __name__=="__main__":
    main()
