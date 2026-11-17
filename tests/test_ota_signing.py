import importlib.util,unittest,contextlib,io,sys,tempfile
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

class SignedPullReleaseCapabilityTests(unittest.TestCase):
    def test_publish_rejects_firmware_without_compiled_ota(self):
        # A normal ESP image header alone must not make a release signable.
        image=b"\xE9"+b"\0"*4095
        self.assertFalse(ota.is_signed_pull_capable_image(image))
    def test_publisher_accepts_single_compiled_capability_marker(self):
        image=b"\xE9"+b"\0"*1024+ota.SIGNED_PULL_MARKER
        self.assertTrue(ota.is_signed_pull_capable_image(image))
    def test_publisher_rejects_ambiguous_marker(self):
        image=b"\xE9"+ota.SIGNED_PULL_MARKER*2
        self.assertFalse(ota.is_signed_pull_capable_image(image))
    def test_real_publisher_refuses_unsigned_pull_image_before_staging(self):
        from unittest.mock import patch
        with tempfile.TemporaryDirectory() as td:
            root=Path(td)
            fw=root/"unprovisioned.bin"
            fw.write_bytes(b"\xE9"+b"\x00"*4095)
            with patch.object(sys,"argv",["publish","--firmware",str(fw),
                                           "--version","1","--root",str(root)]):
                with contextlib.redirect_stderr(io.StringIO()):
                    with self.assertRaises(SystemExit) as caught:
                        ota.main()
                self.assertEqual(caught.exception.code,2)
            self.assertFalse((root/"releases").exists())
    def test_ota_boot_health_requires_signed_pull(self):
        root=Path(__file__).resolve().parents[1]
        source=(root/"firmware/cottage/src/main.cpp").read_text()
        self.assertIn("const bool healthy=cottage_ota_configured() &&",source)
        cpp=(root/"firmware/cottage/src/cottage_secure_ota.cpp").read_text()
        self.assertIn('ENEVINA_C24_SIGNED_PULL_OTA_CAPABLE_V1',cpp)
        self.assertIn("signed_pull_marker[0]=='E'",cpp)

class ArduinoRollbackSourceGuardTests(unittest.TestCase):
    def test_arduino_early_confirmation_must_be_overridden(self):
        source=(Path(__file__).resolve().parents[1]/
                "firmware/cottage/src/main.cpp").read_text()
        self.assertIn('extern "C" bool verifyRollbackLater() { return true; }',source)
        self.assertIn('esp_ota_mark_app_valid_cancel_rollback()',source)
        self.assertIn('ESP_OTA_IMG_PENDING_VERIFY',source)

    def test_signed_pull_worker_must_start_before_pending_image_commit(self):
        root=Path(__file__).resolve().parents[1]
        code=(root/"firmware/cottage/src/main.cpp").read_text()
        self.assertIn("task_optimizer!=nullptr && task_ota!=nullptr;",code)
        self.assertIn('xTaskCreatePinnedToCore(cottage_ota_worker,"cottage_ota"',code)
        self.assertIn('16384,nullptr,1,&task_ota,1)!=pdPASS',code)
        self.assertIn('task_ota=nullptr;',code)
    def test_regular_platformio_usb_upload_is_fail_closed(self):
        import subprocess
        root=Path(__file__).resolve().parents[1]
        ini=(root/"firmware/cottage/platformio.ini").read_text()
        self.assertIn("upload_protocol = custom",ini)
        self.assertIn("upload_command = python3 block_usb_upload.py",ini)
        block=root/"firmware/cottage/block_usb_upload.py"
        proc=subprocess.run([sys.executable,str(block)],capture_output=True,text=True)
        self.assertNotEqual(proc.returncode,0)
        self.assertIn("ENEVINA_LITE_USB_UPLOAD_BLOCKED",proc.stderr)
    def test_bootloader_rollback_compiled_in_build_recipe(self):
        source=(Path(__file__).resolve().parents[1]/
                "firmware/cottage/bootloader-rollback-idf55/sdkconfig.defaults").read_text()
        self.assertIn('CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y',source)

if __name__=="__main__":unittest.main()
