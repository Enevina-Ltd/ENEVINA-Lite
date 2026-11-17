"""PlatformIO environments must use the same reviewed toolchain as the C2 bench."""
from pathlib import Path
import configparser
import unittest

ROOT=Path(__file__).resolve().parents[1]
class PioPinTests(unittest.TestCase):
    def test_cottage_and_bench_same_validated_versions(self):
        base=configparser.ConfigParser(interpolation=None)
        cottage=configparser.ConfigParser(interpolation=None)
        base.read(ROOT/"firmware/bench/platformio.ini")
        cottage.read(ROOT/"firmware/cottage/platformio.ini")
        a=base["env:esp32s3_generic_benchmark"]
        b=cottage["env:esp32s3_cottage_shadow"]
        self.assertEqual(b["platform"],"espressif32@7.0.1")
        self.assertEqual(b["platform"],a["platform"])
        self.assertEqual(b["platform_packages"].split(),a["platform_packages"].split())
        self.assertEqual(cottage["env:esp32s3_cottage_bench"]["extends"],"env:esp32s3_cottage_shadow")
if __name__=="__main__":
    unittest.main()
