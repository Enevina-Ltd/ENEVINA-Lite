import hashlib
import json
import struct
import tempfile
import unittest
import zipfile
from pathlib import Path

from tools.cottage_install_package import archive, collect, MD5_PREFIX, PART_ENTRY

def table(app_bank_size=0x640000):
    parts = [
        (1, 2, 0x9000, 0x5000, b"nvs"),
        (1, 0, 0xE000, 0x2000, b"otadata"),
        (0, 16, 0x10000, app_bank_size, b"app0"),
        (0, 17, 0x650000, app_bank_size, b"app1"),
    ]
    data = b"".join(PART_ENTRY.pack(0x50AA, *args, 0) for args in parts)
    return (data + MD5_PREFIX + hashlib.md5(data, usedforsecurity=False).digest()).ljust(3072,b"\xff")

def image():
    header = bytearray(24)
    header[:4] = b"\xe9\x01\x02\x4f"
    header[12], header[23] = 9, 1
    payload = b"\x01\x02\x03\x04"
    data = header + struct.pack("<II", 0x40374000, len(payload)) + payload
    checksum_pos = len(data) | 15
    data = data.ljust(checksum_pos, b"\x00") + bytes([0xEF ^ 1 ^ 2 ^ 3 ^ 4])
    return bytes(data) + hashlib.sha256(data).digest()

class InstallPackage(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.directory = Path(temp.name)
        self.idf = self.directory / "idf"
        self.pio = self.directory / "pio"
        for p in (self.idf / "config", self.idf / "bootloader", self.idf / "partition_table", self.pio):
            p.mkdir(parents=True)
        self.header = self.idf / "config" / "sdkconfig.h"
        self.header.write_text(
            "#define CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE 1\n"
            "#define CONFIG_IDF_TARGET_ESP32S3 1\n"
            "#define CONFIG_ESPTOOLPY_FLASHSIZE_16MB 1\n", encoding="utf-8")
        self.args = self.idf / "flasher_args.json"
        self.metadata = {
            "flash_settings": {"flash_size": "16MB"},
            "flash_files": {
                "0x0": "bootloader/bootloader.bin",
                "0x8000": "partition_table/partition-table.bin",
                "0xe000": "ota_data_initial.bin",
                "0x10000": "idf-test-app.bin",
            },
        }
        self.args.write_text(json.dumps(self.metadata), encoding="utf-8")
        (self.idf/"bootloader"/"bootloader.bin").write_bytes(image())
        (self.idf/"partition_table"/"partition-table.bin").write_bytes(table())
        (self.idf/"ota_data_initial.bin").write_bytes(b"\xff"*8192)
        (self.pio/"partitions.bin").write_bytes(table())
        (self.pio/"firmware.bin").write_bytes(image())
        for map_file, library in (
                (self.pio/"firmware.map", "libFrameworkArduino.a"),
                (self.idf/"bootloader"/"bootloader.map", "libbootloader_support.a"),
        ):
            map_file.write_text(
                "Discarded input sections\n/tmp/project/libUnused.a(ignored.o)\n"
                "Linker script and memory map\n"
                f" .text /build/root/{library}(active.o)\n"
                "Cross Reference Table\n/tmp/project/libCrossref.a(ignored.o)\n",
                encoding="utf-8")

    def test_reproducible_installer_and_addressed_components(self):
        one, two = self.directory/"one.zip", self.directory/"two.zip"
        self.assertEqual(archive(self.idf,self.pio,"a"*40,one), archive(self.idf,self.pio,"a"*40,two))
        self.assertEqual(one.read_bytes(),two.read_bytes())
        with zipfile.ZipFile(one) as z:
            layout = json.loads(z.read("flash-layout.json"))
            self.assertEqual(layout["source_commit"],"a"*40)
            self.assertEqual(layout["partitions"]["app1"]["offset"],0x650000)
            self.assertTrue(layout["first_install"]["erase_settings"])
            combined = z.read("first-install.bin")
            for region in layout["regions"]:
                offset = int(region["offset"],0)
                data = z.read(region["file"])
                self.assertEqual(combined[offset:offset+len(data)],data)
                self.assertEqual(hashlib.sha256(data).hexdigest(),region["sha256"])
            self.assertNotIn("idf-test-app.bin",z.namelist())
            self.assertIn("notices/LICENSE", z.namelist())
            inventory = json.loads(z.read("linked-archives.json"))
            self.assertEqual(inventory["source_commit"], "a"*40)
            self.assertEqual(inventory["application"], {"libFrameworkArduino.a": ["active.o"]})
            self.assertEqual(inventory["bootloader"], {"libbootloader_support.a": ["active.o"]})
            sha256sums = z.read("SHA256SUMS").decode()
            self.assertIn("  linked-archives.json\n", sha256sums)
            self.assertIn("  notices/LICENSE\n", sha256sums)
            self.assertNotIn("nvs", [p["kind"] for p in layout["regions"]])

    def test_rejects_arduino_bootloader_or_mismatched_partition_table(self):
        flags = self.header.read_text(encoding="utf-8")
        self.header.write_text(flags.replace("CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE 1",
                                             "CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE 0"),encoding="utf-8")
        with self.assertRaisesRegex(ValueError,"APP_ROLLBACK_ENABLE"):
            collect(self.idf,self.pio)
        self.header.write_text(flags,encoding="utf-8")
        (self.pio/"partitions.bin").write_bytes(table(0x630000))
        with self.assertRaisesRegex(ValueError,"different flash layouts"):
            collect(self.idf,self.pio)

    def test_rejects_wrong_initial_ota_or_corrupt_firmware(self):
        ota = self.idf/"ota_data_initial.bin"
        ota.write_bytes(b"\0"*8192)
        with self.assertRaisesRegex(ValueError,"Initial OTA"):
            collect(self.idf,self.pio)
        ota.write_bytes(b"\xff"*8192)
        (self.pio/"firmware.bin").write_bytes(image()[:-1]+b"\0")
        with self.assertRaisesRegex(ValueError,"checksum or SHA-256"):
            collect(self.idf,self.pio)

    def test_rejects_inventory_when_build_map_is_unavailable(self):
        (self.pio/"firmware.map").unlink()
        out = self.directory/"missing-map.zip"
        with self.assertRaises(FileNotFoundError):
            archive(self.idf, self.pio, "a"*40, out)
        self.assertFalse(out.exists())

    def test_rejects_file_outside_idf_build(self):
        self.metadata["flash_files"]["0x0"]="../bootloader.bin"
        self.args.write_text(json.dumps(self.metadata),encoding="utf-8")
        with self.assertRaisesRegex(ValueError,"outside"):
            collect(self.idf,self.pio)

if __name__ == "__main__":
    unittest.main()
