import importlib.util,unittest,contextlib,io,sys,tempfile
from cryptography.hazmat.primitives import hashes,serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.exceptions import InvalidSignature
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))
from cottage_ota_channel import channel_tag

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
        self.assertFalse(ota.is_signed_pull_capable_image(image,"ENEVINA_C24_CHANNEL_test"))
    def test_publisher_accepts_single_compiled_capability_marker(self):
        marker="ENEVINA_C24_CHANNEL_"+"a"*64
        image=b"\xE9"+b"\0"*1024+ota.SIGNED_PULL_MARKER+marker.encode()
        self.assertTrue(ota.is_signed_pull_capable_image(image,marker))
    def test_publisher_rejects_ambiguous_marker(self):
        image=b"\xE9"+ota.SIGNED_PULL_MARKER*2
        self.assertFalse(ota.is_signed_pull_capable_image(image,"ENEVINA_C24_CHANNEL_test"))
    def test_different_publisher_or_ca_or_endpoint_never_matches(self):
        manifest="https://ota.test:8443/manifest.txt"
        firmware="https://ota.test:8443/firmware.bin"
        ca="dummy-ca\n"
        key="dummy-publisher\n"
        tag=channel_tag(manifest,firmware,ca,key)
        image=b"\xE9"+ota.SIGNED_PULL_MARKER+tag.encode()
        self.assertTrue(ota.is_signed_pull_capable_image(image,tag))
        for changed in (
            channel_tag(manifest,firmware,ca,"other-publisher"),
            channel_tag(manifest,firmware,"other-ca",key),
            channel_tag("https://other.test:8443/manifest.txt",
                        "https://other.test:8443/firmware.bin",ca,key),
        ):
            self.assertNotEqual(tag,changed)
            self.assertFalse(ota.is_signed_pull_capable_image(image,changed))

    def test_static_project_directory_is_part_of_the_channel(self):
        root = "https://ota.example"
        dist = root + "/ENEVINA-Lite/ota/field"
        lab = root + "/ENEVINA-Lite/ota/lab"
        ca, key = "ca", "public-key"
        def tag(directory):
            return channel_tag(directory + "/manifest.txt", directory + "/firmware.bin", ca, key)
        self.assertNotEqual(tag(root), tag(dist))
        self.assertNotEqual(tag(lab), tag(dist))
        with self.assertRaises(ValueError):
            channel_tag(dist + "/manifest.txt", lab + "/firmware.bin", ca, key)
        for path in ("/ota/../", "/ota//", "/%2e/", "/./", "/with%20space/", "/a\\\\b/", "/a\t/"):
            with self.subTest(path=path), self.assertRaises(ValueError):
                channel_tag(root + path + "manifest.txt", root + path + "firmware.bin", ca, key)
        for suffix in ("?latest", "?", "#"):
            with self.subTest(suffix=suffix), self.assertRaises(ValueError):
                channel_tag(dist + "/manifest.txt" + suffix, dist + "/firmware.bin", ca, key)

    def test_channel_digest_in_generator_and_build(self):
        root=Path(__file__).resolve().parents[1]
        generator=(root/"tools/cottage_ota_generate_header.py").read_text()
        cpp=(root/"firmware/cottage/src/cottage_secure_ota.cpp").read_text()
        self.assertIn('COTTAGE_OTA_CHANNEL_MARKER',generator)
        self.assertIn('static const volatile char signed_channel_marker[]',cpp)
        self.assertIn("signed_channel_marker[0]=='E'",cpp)

    def test_real_publisher_accepts_matching_channel_and_refuses_wrong_channel(self):
        from unittest.mock import patch
        manifest="https://ota.test/ENEVINA-Lite/field/manifest.txt"
        firmware_url="https://ota.test/ENEVINA-Lite/field/firmware.bin"
        with tempfile.TemporaryDirectory() as td:
            root=Path(td)
            publisher=ec.generate_private_key(ec.SECP256R1())
            (root/"publisher-private.pem").write_bytes(publisher.private_bytes(
                serialization.Encoding.PEM,
                serialization.PrivateFormat.PKCS8,
                serialization.NoEncryption()))
            pub=publisher.public_key().public_bytes(
                serialization.Encoding.PEM,
                serialization.PublicFormat.SubjectPublicKeyInfo).decode("ascii")
            ca="test-only-public-ca-text\n"
            (root/"ca-public.pem").write_text(ca)
            (root/"publisher-public.pem").write_text(pub)
            marker=channel_tag(manifest,firmware_url,ca,pub)
            image=root/"firmware.bin"
            image.write_bytes(b"\xE9"+b"\x00"*4096+ota.SIGNED_PULL_MARKER+marker.encode())
            args=["publish","--firmware",str(image),"--version","1",
                  "--root",str(root),"--manifest-url",manifest,
                  "--firmware-url",firmware_url]
            with patch.object(sys,"argv",args[:-1]+["https://wrong.test/firmware.bin"]):
                with contextlib.redirect_stderr(io.StringIO()):
                    with self.assertRaises(SystemExit) as rejected:
                        ota.main()
                self.assertEqual(rejected.exception.code,2)
            self.assertFalse((root/"releases").exists())
            with patch.object(sys,"argv",args):
                with contextlib.redirect_stdout(io.StringIO()):
                    ota.main()
            self.assertTrue((root/"releases/manifest.txt").exists())
            self.assertEqual((root/"releases/firmware.bin").read_bytes(),
                             image.read_bytes())
    def test_real_publisher_refuses_unsigned_pull_image_before_staging(self):
        from unittest.mock import patch
        with tempfile.TemporaryDirectory() as td:
            root=Path(td)
            fw=root/"unprovisioned.bin"
            fw.write_bytes(b"\xE9"+b"\x00"*4095)
            (root/"ca-public.pem").write_text("dummy-ca\n")
            (root/"publisher-public.pem").write_text("dummy-publisher\n")
            with patch.object(sys,"argv",["publish","--firmware",str(fw),
                                            "--version","1","--root",str(root),
                                           "--manifest-url","https://ota.test/manifest.txt",
                                           "--firmware-url","https://ota.test/firmware.bin"]):
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
        self.assertRegex(
            code,r'xTaskCreatePinnedToCore\(cottage_ota_worker,"cottage_ota",\s*'
                 r'\d+,nullptr,1,&task_ota,1\)!=pdPASS'
        )
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
