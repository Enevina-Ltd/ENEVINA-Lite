#!/usr/bin/env python3
"""Assemble a first-install package from the rollback and application builds."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import zipfile

FLASH_BYTES = 16 * 1024 * 1024
SECTOR = 4096
ERASED = 0xFF
PART_ENTRY = struct.Struct("<HBBII16sI")
MD5_PREFIX = b"\xeb\xeb" + b"\xff" * 14
S3_CHIP = 9
PROJECT_ROOT = Path(__file__).resolve().parents[1]
PROJECT_NOTICES = (
    "LICENSE",
    "THIRD_PARTY_NOTICES.md",
    "third_party/midea-local/LICENSE",
    "third_party/msmart-ng/LICENSE.md",
    "third_party/pywfrac/LICENSE",
    "third_party/esphome-panasonic-ac/LICENSE",
)
KIND = {
    "bootloader.bin": "bootloader",
    "partition-table.bin": "partitions",
    "ota_data_initial.bin": "otadata",
}
NAMES = {
    "bootloader": "bootloader.bin",
    "partitions": "partitions.bin",
    "otadata": "ota_data_initial.bin",
    "application": "enevina-lite-app.bin",
}

def digest(data):
    return hashlib.sha256(data).hexdigest()

def partition_entries(binary):
    if not 0 < len(binary) <= SECTOR or len(binary) % PART_ENTRY.size:
        raise ValueError("Invalid partition table length")
    entries = {}
    for pos in range(0, len(binary), PART_ENTRY.size):
        blob = binary[pos : pos + PART_ENTRY.size]
        if blob.startswith(MD5_PREFIX):
            if blob[16:] != hashlib.md5(binary[:pos], usedforsecurity=False).digest():
                raise ValueError("Partition table checksum mismatch")
            if any(b != ERASED for b in binary[pos + PART_ENTRY.size:]):
                raise ValueError("Unexpected data after partition checksum")
            break
        magic, kind, subtype, offset, size, raw_name, flags = PART_ENTRY.unpack(blob)
        name = raw_name.split(b"\0", 1)[0].decode("ascii")
        if (magic != 0x50AA or not name or name in entries or flags or not size
                or offset % SECTOR or size % SECTOR or offset + size > FLASH_BYTES):
            raise ValueError("Invalid ESP32-S3 partition entry")
        entries[name] = {"type":kind, "subtype":subtype, "offset":offset, "size":size}
    else:
        raise ValueError("Partition checksum missing")
    ordered = sorted(entries.values(), key=lambda entry: entry["offset"])
    for first, second in zip(ordered, ordered[1:]):
        if first["offset"] + first["size"] > second["offset"]:
            raise ValueError("Overlapping flash partitions")
    for name, kind, subtype in (("nvs", 1, 2), ("otadata", 1, 0),
                                ("app0", 0, 16), ("app1", 0, 17)):
        entry = entries.get(name)
        if not entry or (entry["type"], entry["subtype"]) != (kind, subtype):
            raise ValueError("Required partition missing: " + name)
    if entries["app0"]["size"] != entries["app1"]["size"]:
        raise ValueError("A/B partitions have different sizes")
    return entries

def read_inside(build, relative):
    path = (build / relative).resolve()
    if not path.is_relative_to(build.resolve()):
        raise ValueError("Flash file is outside the supplied build directory")
    return path.read_bytes()

def validate_image(data, name):
    if (len(data) < 24 or data[0] != 0xE9 or (data[3] >> 4) != 4
            or int.from_bytes(data[12:14], "little") != S3_CHIP
            or not 1 <= data[1] <= 16 or data[23] != 1):
        raise ValueError(name + " is not a 16 MiB ESP32-S3 image with a digest")
    pos = 24
    checksum = 0xEF
    for _ in range(data[1]):
        if pos + 8 > len(data):
            raise ValueError(name + " has a truncated segment header")
        size = int.from_bytes(data[pos + 4:pos + 8], "little")
        start = pos + 8
        pos = start + size
        if pos > len(data):
            raise ValueError(name + " has a truncated segment")
        for byte in data[start:pos]:
            checksum ^= byte
    checksum_idx = pos | 15
    signed_size = checksum_idx + 1
    if (signed_size + 32 != len(data) or data[checksum_idx] != checksum
            or hashlib.sha256(data[:signed_size]).digest() != data[signed_size:]):
        raise ValueError(name + " segment checksum or SHA-256 does not match")

def collect(idf, pio):
    defaults = (idf / "config" / "sdkconfig.h").read_text(encoding="utf-8")
    for flag in ("CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE",
                 "CONFIG_ESPTOOLPY_FLASHSIZE_16MB", "CONFIG_IDF_TARGET_ESP32S3"):
        if not re.search(rf"^#define {flag} 1$", defaults, re.MULTILINE):
            raise ValueError("Required generated SDK flag missing: " + flag)
    metadata = json.loads((idf / "flasher_args.json").read_text(encoding="utf-8"))
    addresses = metadata["flash_files"]
    if metadata["flash_settings"].get("flash_size") != "16MB":
        raise ValueError("ESP-IDF flash build did not declare 16MB")
    assets = {}
    for address, relative in addresses.items():
        offset = int(address, 0)
        kind = KIND.get(Path(relative).name)
        if kind is None:
            continue # the IDF project builds a bootloader test app only
        if kind in assets or offset < 0 or offset % SECTOR:
            raise ValueError("Invalid repeated or unaligned flash artifact")
        assets[kind] = (offset, read_inside(idf, relative))
    if set(assets) != set(KIND.values()):
        raise ValueError("Expected bootloader, partition table and initial OTA data")
    table_offset, table_bytes = assets["partitions"]
    partitions = partition_entries(table_bytes)
    if partitions != partition_entries((pio / "partitions.bin").read_bytes()):
        raise ValueError("PlatformIO and rollback builds describe different flash layouts")
    boot, boot_image = assets["bootloader"]
    if boot != 0 or boot + len(boot_image) > table_offset or table_offset + SECTOR > min(
            entry["offset"] for entry in partitions.values()):
        raise ValueError("Bootloader or partition table overlaps flash partitions")
    ota, ota_blank = assets["otadata"]
    if (ota != partitions["otadata"]["offset"] or len(ota_blank) != partitions["otadata"]["size"]
            or len(ota_blank) != 2 * SECTOR or any(x != ERASED for x in ota_blank)):
        raise ValueError("Initial OTA data does not match the empty two-sector partition")
    app = (pio / "firmware.bin").read_bytes()
    for name, image in (("bootloader", boot_image), ("application", app)):
        validate_image(image, name)
    if len(app) > partitions["app0"]["size"]:
        raise ValueError("App does not fit the A/B partition layout")
    assets["application"] = (partitions["app0"]["offset"], app)
    return assets, partitions

def linked_archive_members(path):
    """Return archive members referenced in the active part of a GNU ld map."""
    contents = path.read_text(encoding="utf-8")
    _, marker, active = contents.partition("Linker script and memory map")
    if not marker:
        raise ValueError("Invalid GNU linker map: " + path.name)
    active = active.partition("Cross Reference Table")[0]
    archives = {}
    for filename, member in re.findall(r"([^\s()]+\.a)\(([^)\r\n]+)\)", active):
        name = Path(filename).name
        archives.setdefault(name, set()).add(member)
    if not archives:
        raise ValueError("GNU linker map has no archive members: " + path.name)
    return {archive: sorted(members) for archive, members in archives.items()}

def archive(idf, pio, source, out):
    if not re.fullmatch("[0-9a-f]{40}", source):
        raise ValueError("Use the exact 40-character Git source commit")
    assets, partitions = collect(idf, pio)
    ordered = sorted(assets.items(), key=lambda asset: asset[1][0])
    if any(offset + len(data) > FLASH_BYTES for _, (offset, data) in ordered):
        raise ValueError("Image extends beyond the flash")
    end = max(offset + len(data) for _, (offset, data) in ordered)
    merged = bytearray([ERASED]) * end
    files = {}
    regions = []
    for kind, (offset, data) in ordered:
        name = NAMES[kind]
        merged[offset:offset + len(data)] = data
        files[name] = data
        regions.append({"kind":kind, "file":name, "offset":hex(offset),
                        "size":len(data), "sha256":digest(data)})
    files["first-install.bin"] = bytes(merged)
    channels = set(re.findall(rb"ENEVINA_C24_CHANNEL_[0-9a-f]{64}", assets["application"][1]))
    if len(channels) > 1:
        raise ValueError("Application has more than one compiled OTA channel marker")
    channel = channels.pop().decode("ascii") if channels else None
    flash = {"chip":"esp32s3", "flash_bytes":FLASH_BYTES, "psram_bytes":8*1024*1024,
             "psram_type":"OPI", "source_commit":source, "ota_channel":channel,
             "partitions":partitions, "regions":regions,
             "first_install":{"file":"first-install.bin", "offset":"0x0",
                              "erase_settings":True, "sha256":digest(merged)}}
    files["flash-layout.json"] = (json.dumps(flash, sort_keys=True, indent=2) + "\n").encode()
    link_inventory = {
        "source_commit": source,
        "application": linked_archive_members(pio / "firmware.map"),
        "bootloader": linked_archive_members(idf / "bootloader" / "bootloader.map"),
    }
    files["linked-archives.json"] = (
        json.dumps(link_inventory, sort_keys=True, indent=2) + "\n").encode()
    for notice in PROJECT_NOTICES:
        files["notices/" + notice] = (PROJECT_ROOT / notice).read_bytes()
    commands = " ".join(region["offset"] + " " + region["file"] for region in regions)
    files["INSTALL.txt"] = (
        "Enevina Lite development installer for ESP32-S3, 16 MiB flash, 8 MiB OPI PSRAM\n"
        f"Source commit {source}\n\n"
        "Check SHA256SUMS before use. Read docs/release/N16R8_INSTALL.md at the source commit.\n"
        "The link inventory and repository notices support release review.\n"
        "Use these addressed components on a blank device, or when carrying out the documented recovery.\n"
        "Replace PORT with your device port.\n\n"
        "python -m esptool --chip esp32s3 --port PORT write_flash "
        "--flash_mode keep --flash_size keep --flash_freq keep " + commands + "\n\n"
        "first-install.bin at 0x0 overwrites flash gaps including NVS. Use it only for a blank device.\n"
        "For an installed device use its signed A/B update channel when available.\n"
    ).encode()
    files["SHA256SUMS"] = "".join(
        f"{digest(value)}  {name}\n" for name, value in sorted(files.items())).encode()
    with out.open("xb") as raw, zipfile.ZipFile(raw, "w", compression=zipfile.ZIP_STORED) as pkg:
        for name, data in sorted(files.items()):
            item = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
            item.external_attr = 0o100644 << 16
            item.create_system = 3
            pkg.writestr(item, data)
    return digest(out.read_bytes())

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--idf-build", required=True, type=Path)
    parser.add_argument("--pio-build", required=True, type=Path)
    parser.add_argument("--source", required=True)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        checksum = archive(args.idf_build, args.pio_build, args.source, args.output)
    except (OSError, ValueError, KeyError) as error:
        parser.error(str(error))
    print(checksum, args.output.name)

if __name__ == "__main__":
    main()
