"""Generate the exact flash-backed HTML's deterministic gzip representation."""
import gzip
from pathlib import Path

def dashboard_bytes(source):
    raw=Path(source).read_bytes()
    start=b'R"ENEVINA_UI('
    end=b')ENEVINA_UI";'
    if raw.count(start)!=1 or raw.count(end)!=1:
        raise ValueError("Dashboard raw literal not unique")
    return raw.split(start,1)[1].split(end,1)[0]

def dashboard_gzip(source):
    data=bytearray(gzip.compress(dashboard_bytes(source),compresslevel=9,mtime=0))
    # Normalize the gzip OS byte across CPython/zlib versions.
    data[9]=255
    return bytes(data)

def write_header(source,destination):
    data=dashboard_gzip(source)
    rows=[",".join(f"0x{x:02x}" for x in data[i:i+24]) for i in range(0,len(data),24)]
    header="#pragma once\nstatic const unsigned char premium_dashboard_gzip[] PROGMEM={\n"+",\n".join(rows)+"\n};\n"
    destination=Path(destination)
    destination.parent.mkdir(parents=True,exist_ok=True)
    if not destination.exists() or destination.read_text()!=header:
        destination.write_text(header)

# SCons invokes this pre-script for shadow, release and bench environments.
# Importing the pure functions in host tests never requires PlatformIO.
if "Import" in globals():
    Import("env")
    project=Path(env.subst("$PROJECT_DIR"))
    generated=Path(env.subst("$BUILD_DIR"))/"generated"
    write_header(project/"include/cottage_premium_dashboard.hpp",generated/"cottage_dashboard_gzip.hpp")
    env.AppendUnique(CPPPATH=[str(generated)])
