import importlib.util,unittest
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.exceptions import InvalidSignature
from pathlib import Path

SRC=Path(__file__).resolve().parents[1]/"tools/cottage_ota_publish.py"
spec=importlib.util.spec_from_file_location("ota_publish",SRC)
ota=importlib.util.module_from_spec(spec)
spec.loader.exec_module(ota)

class SignedReleaseTests(unittest.TestCase):
    def setUp(self):
        self.key=ec.generate_private_key(ec.SECP256R1())
        self.message=ota.canonical(2026111601,1600000,"01"*32)
        self.signature=self.key.sign(self.message,ec.ECDSA(hashes.SHA256()))
        self.key.public_key().verify(self.signature,self.message,ec.ECDSA(hashes.SHA256()))
    def test_signing_and_verification(self):
        self.key.public_key().verify(self.signature,self.message,ec.ECDSA(hashes.SHA256()))
    def test_tampered_version_is_rejected(self):
        tampered=self.message.replace(b"version=2026111601",b"version=2026111602")
        with self.assertRaises(InvalidSignature):
            self.key.public_key().verify(self.signature,tampered,ec.ECDSA(hashes.SHA256()))
    def test_tampered_hash_is_rejected(self):
        bad=self.message.replace(b"sha256=01",b"sha256=02")
        with self.assertRaises(InvalidSignature):
            self.key.public_key().verify(self.signature,bad,ec.ECDSA(hashes.SHA256()))
    def test_tampered_size_is_rejected(self):
        bad=self.message.replace(b"size=1600000",b"size=1600001")
        with self.assertRaises(InvalidSignature):
            self.key.public_key().verify(self.signature,bad,ec.ECDSA(hashes.SHA256()))
    def test_other_signer_is_rejected(self):
        other=ec.generate_private_key(ec.SECP256R1())
        with self.assertRaises(InvalidSignature):
            other.public_key().verify(self.signature,self.message,ec.ECDSA(hashes.SHA256()))
    def test_model_is_bound(self):
        self.assertIn(b"model=esp32s3-n16r8",self.message)

class ArduinoRollbackSourceGuardTests(unittest.TestCase):
    def test_arduino_early_confirmation_must_be_overridden(self):
        source=(Path(__file__).resolve().parents[1]/
                "firmware/cottage/src/main.cpp").read_text()
        self.assertIn('extern "C" bool verifyRollbackLater() { return true; }',source)
        self.assertIn('esp_ota_mark_app_valid_cancel_rollback()',source)
        self.assertIn('ESP_OTA_IMG_PENDING_VERIFY',source)

    def test_bootloader_rollback_compiled_in_build_recipe(self):
        source=(Path(__file__).resolve().parents[1]/
                "firmware/cottage/bootloader-rollback-idf55/sdkconfig.defaults").read_text()
        self.assertIn('CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y',source)

if __name__=="__main__":unittest.main()
