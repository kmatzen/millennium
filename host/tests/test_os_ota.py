#!/usr/bin/env python3

import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
MODULE = ROOT / "host/os_ota/millennium_os_ota.py"
BUILDER = ROOT / "tools/build_os_release.py"
spec = importlib.util.spec_from_file_location("millennium_os_ota", MODULE)
os_ota = importlib.util.module_from_spec(spec)
spec.loader.exec_module(os_ota)


class OsOtaTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.private = self.root / "private.pem"
        self.public = self.root / "public.pem"
        subprocess.run(["openssl", "genpkey", "-algorithm", "ED25519", "-out",
                        str(self.private)], check=True, stdout=subprocess.DEVNULL)
        subprocess.run(["openssl", "pkey", "-in", str(self.private), "-pubout",
                        "-out", str(self.public)], check=True,
                       stdout=subprocess.DEVNULL)
        self.boot = self.root / "boot.img"
        self.rootfs = self.root / "root.img"
        self.boot.write_bytes((b"boot-image\0" * 257) + b"end")
        self.rootfs.write_bytes((b"root-image\0" * 521) + b"end")

    def tearDown(self):
        self.temporary.cleanup()

    def build(self):
        output = self.root / "release"
        subprocess.run([
            sys.executable, str(BUILDER), "--sequence", "12", "--version",
            "2026.09.0", "--base-url", "https://updates.example/millennium/os",
            "--boot-image", str(self.boot), "--root-image", str(self.rootfs),
            "--layout-id", "zero2w-ab-v1", "--board-model",
            "Raspberry Pi Zero 2 W Rev 1.0", "--architecture", "armv7l",
            "--key-id", "release-2026-08", "--device-groups", "phone-001",
            "--minimum-application-version", "0.4.0", "--minimum-mcu-version",
            "0.4.0", "--persistent-state-schema", "1", "--source-commit",
            "a" * 40, "--private-key", str(self.private), "--output-dir",
            str(output),
        ], check=True, stdout=subprocess.PIPE)
        return output

    @staticmethod
    def expected(**changes):
        value = {
            "channel": "stable", "architecture": "armv7l",
            "layout_id": "zero2w-ab-v1", "installed_sequence": 11,
            "board_model": "Raspberry Pi Zero 2 W Rev 1.0",
            "device_group": "phone-001",
        }
        value.update(changes)
        return value

    def test_signed_manifest_and_both_images_verify(self):
        output = self.build()
        value = os_ota.load_verified_manifest(
            output / "manifest.json", output / "manifest.json.sig", self.public,
            self.expected())
        for name in ("boot", "root"):
            path = next(output.glob(name + "-*.img.gz"))
            os_ota.verify_image(path, value["images"][name])
        self.assertEqual(value["source_commit"], "a" * 40)
        self.assertEqual(value["sequence"], 12)

    def test_manifest_tampering_breaks_signature(self):
        output = self.build()
        with (output / "manifest.json").open("ab") as stream:
            stream.write(b" ")
        with self.assertRaisesRegex(os_ota.OsOtaError, "signature"):
            os_ota.load_verified_manifest(
                output / "manifest.json", output / "manifest.json.sig",
                self.public, self.expected())

    def test_incompatible_board_and_layout_are_rejected(self):
        output = self.build()
        value = json.loads((output / "manifest.json").read_text())
        with self.assertRaisesRegex(os_ota.OsOtaError, "board"):
            os_ota.validate_manifest(value, self.expected(board_model="other"))
        with self.assertRaisesRegex(os_ota.OsOtaError, "layout"):
            os_ota.validate_manifest(value, self.expected(layout_id="other"))

    def test_old_held_and_unselected_releases_are_rejected(self):
        output = self.build()
        value = json.loads((output / "manifest.json").read_text())
        with self.assertRaisesRegex(os_ota.OsOtaError, "not newer"):
            os_ota.validate_manifest(value, self.expected(installed_sequence=12))
        value["rollout"]["hold"] = True
        with self.assertRaisesRegex(os_ota.OsOtaError, "held"):
            os_ota.validate_manifest(value, self.expected())
        value["rollout"]["hold"] = False
        with self.assertRaisesRegex(os_ota.OsOtaError, "group"):
            os_ota.validate_manifest(value, self.expected(device_group="other"))

    def test_compressed_and_expanded_tampering_are_rejected(self):
        output = self.build()
        value = json.loads((output / "manifest.json").read_text())
        boot = next(output.glob("boot-*.img.gz"))
        original = boot.read_bytes()
        boot.write_bytes(original + b"tamper")
        with self.assertRaisesRegex(os_ota.OsOtaError, "compressed"):
            os_ota.verify_image(boot, value["images"]["boot"])
        boot.write_bytes(original)
        value["images"]["boot"]["expanded_sha256"] = "0" * 64
        with self.assertRaisesRegex(os_ota.OsOtaError, "expanded"):
            os_ota.verify_image(boot, value["images"]["boot"])


if __name__ == "__main__":
    unittest.main()
