#!/usr/bin/env python3
"""Strictly read-only CN105 SSE + Shelly Plus 1PM probe.

Supply local device URLs at runtime; NEVER commit device addresses or credentials.
Example: python tools/cottage_probe.py --cn105-url http://HOST/events \
    --shelly-url http://HOST
No device changes, relay APIs, MQTT commands or firmware operations.
"""
import argparse
import json
import math
import sys
import time
import urllib.parse
import urllib.request

def require_http_local(url):
    p=urllib.parse.urlsplit(url)
    if p.scheme!="http" or not p.hostname or p.username or p.password:
        raise ValueError("A plain HTTP LAN URL without embedded credentials is required")
    return url

def number(value, low, high):
    if isinstance(value,bool) or value is None:raise ValueError("invalid number")
    x=float(value)
    if not math.isfinite(x) or not low<=x<=high:raise ValueError("invalid numeric range")
    return x

def get_json(url, timeout):
    with urllib.request.urlopen(urllib.request.Request(url,method="GET"),
                                timeout=timeout) as response:
        if response.status!=200:raise RuntimeError("HTTP not OK")
        body=response.read(16384)
        return json.loads(body)

def shelly(base,timeout):
    root=base.rstrip("/")
    info=get_json(root+"/rpc/Shelly.GetDeviceInfo",timeout)
    if info.get("app")!="Plus1PM":raise ValueError("not a Shelly Plus 1PM")
    status=get_json(root+"/rpc/Switch.GetStatus?id=0",timeout)
    if status.get("id")!=0:raise ValueError("unexpected switch id")
    aenergy=status.get("aenergy")
    if not isinstance(aenergy,dict):raise ValueError("no energy total")
    return {"verified_model":"Shelly Plus 1PM","relay_on":status.get("output") is True,
        "total_cottage_w":number(status.get("apower"),0,20000),
        "total_energy_wh":number(aenergy.get("total"),0,1e12),
        "captured_unix":int(time.time()),
        "NOTE":"Whole cottage load, NOT compressor-only. NO relay commands."}

def cn105_sse(url,timeout):
    start=time.monotonic()
    mode=None;indoor=None;target=None;compressor=None
    with urllib.request.urlopen(urllib.request.Request(url,headers={
         "Accept":"text/event-stream"},method="GET"),timeout=timeout) as resp:
        if resp.status!=200:raise RuntimeError("SSE HTTP not OK")
        if "text/event-stream" not in resp.headers.get("Content-Type",""):
            raise ValueError("not SSE")
        event=None;data_lines=[]
        def consume(event,data_lines):
            nonlocal mode,indoor,target,compressor
            if event!="state" or not data_lines:return
            obj=json.loads("\n".join(data_lines))
            key=obj.get("id")
            if key=="climate/Mitsubishi Electric MSZ-FD25VA":
                mode=obj.get("mode")
                if mode not in ("HEAT","OFF","COOL","AUTO","HEAT_COOL","DRY","FAN_ONLY"):
                    raise ValueError("unknown climate mode")
                indoor=number(obj.get("current_temperature"),-10,50)
                target=number(obj.get("target_temperature"),0,35)
            elif key=="sensor/Compressor Frequency":
                compressor=number(obj.get("value"),0,200)
        while time.monotonic()-start<timeout:
            try:line=resp.readline(16384)
            except TimeoutError:break
            if not line:break
            line=line.decode("utf-8","strict").rstrip("\r\n")
            if line=="":
                consume(event,data_lines);event=None;data_lines=[]
                if indoor is not None and compressor is not None:break
            elif line.startswith("event: "):event=line[7:]
            elif line.startswith("data: "):data_lines.append(line[6:])
    if indoor is None or compressor is None:
        raise RuntimeError("No complete Mitsubishi climate/compressor snapshot")
    return {"verified_model":"Mitsubishi Electric MSZ-FD25VA",
        "mode":mode,"indoor_c_pump_sensor":indoor,
        "target_c":target,"compressor_hz":compressor,
        "captured_unix":int(time.time()),
        "NOTE":"Pump sensor only. Read-only SSE, commands/ACK unverified."}

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--cn105-url",required=True)
    ap.add_argument("--shelly-url",required=True)
    ap.add_argument("--timeout",type=float,default=6.)
    args=ap.parse_args()
    if not 2<=args.timeout<=15:ap.error("timeout must be 2..15 seconds")
    try:
        a=cn105_sse(require_http_local(args.cn105_url),args.timeout)
        b=shelly(require_http_local(args.shelly_url),args.timeout)
        print(json.dumps({"cn105":a,"shelly":b},ensure_ascii=False,indent=2))
    except Exception as exc:
        print(json.dumps({"ok":False,"error":str(exc)},ensure_ascii=False),file=sys.stderr)
        return 1
    return 0

if __name__=="__main__":raise SystemExit(main())
