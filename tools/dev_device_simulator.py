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
import re
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
AIR = {"mode": 3, "hold_target_c": 12.0, "occupied_target_c": 21.0, "drying_delta_c": 4.0}
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
        elif k in ("cn105_url", "shelly_url", "fmi_latlon", "pump_adapter"):
            new[k] = v
        else:
            return None, "UNKNOWN_KEY", k
    return new, None, None


def status():
    reg = region_view()
    st = {"air": {"config_token": "sim-token", "sensor_fresh": False, "bound_mac": "", "discovered_mac": "", **AIR},
          "firmware_version": "simulator", "control_armed": False, "price_error": 0, "price_http_status": 0,
          "price_currency": reg.get("currency", "EUR"), "price_area": reg.get("nordpool_area", ""),
          "spot": None, "total_price": None, "weather_c": None, "market_last_fetch_utc": 0}
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
        if u.path == "/api/prices":
            return self.send(200, {"valid": False, "slots": []})
        return self.send(404, {"error": "not in simulator"})

    def do_POST(self):
        u = urlparse(self.path)
        n = int(self.headers.get("Content-Length", "0"))
        form = {k: v[0] for k, v in parse_qs(self.rfile.read(n).decode("utf-8")).items()}
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
    a = ap.parse_args()
    if a.configured:
        STATE["region"].update(setup_complete=True, setup_step=9)
    print(f"simulator on http://127.0.0.1:{a.port}/ ({len(COUNTRIES)} countries)", flush=True)
    ThreadingHTTPServer(("127.0.0.1", a.port), H).serve_forever()
