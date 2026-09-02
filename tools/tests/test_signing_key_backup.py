#!/usr/bin/env python3

import importlib.util
import json
from argparse import Namespace
from pathlib import Path
import plistlib
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "signing_key_backup", ROOT / "tools/signing_key_backup.py")
backup = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(backup)


class SigningKeyOfflineCopyTests(unittest.TestCase):
    def test_internal_macos_volume_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            volume = Path(directory)
            info = {"MountPoint": str(volume), "Internal": True,
                    "Ejectable": False, "RemovableMedia": False}
            result = mock.Mock(stdout=plistlib.dumps(info))
            with mock.patch.object(backup.platform, "system", return_value="Darwin"), \
                    mock.patch.object(backup.os.path, "ismount", return_value=True), \
                    mock.patch.object(backup, "run", return_value=result):
                with self.assertRaises(SystemExit):
                    backup.removable_volume_info(volume)

    def test_copy_is_digest_verified_and_recorded(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "key.aes256"
            source.write_bytes(b"encrypted key material")
            volume = root / "volume"
            volume.mkdir()
            evidence = root / "copy.json"
            args = Namespace(backup=source, volume=volume, evidence=evidence,
                             key_id="release-2026-08", media_id="usb-a")
            info = {"mount_point": str(volume), "device_identifier": "disk9s1"}
            with mock.patch.object(backup, "removable_volume_info", return_value=info):
                backup.command_copy_offline(args)
            destination = volume / "millennium-signing/release-2026-08/usb-a/key.aes256"
            self.assertEqual(destination.read_bytes(), source.read_bytes())
            record = json.loads(evidence.read_text(encoding="utf-8"))
            self.assertTrue(record["passed"])
            self.assertEqual(record["ciphertext_sha256"],
                             backup.hashlib.sha256(source.read_bytes()).hexdigest())

    def test_existing_copy_is_never_overwritten(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "key.aes256"
            source.write_bytes(b"new")
            volume = root / "volume"
            destination = volume / "millennium-signing/release-2026-08/usb-a/key.aes256"
            destination.parent.mkdir(parents=True)
            destination.write_bytes(b"existing")
            args = Namespace(backup=source, volume=volume,
                             evidence=root / "copy.json",
                             key_id="release-2026-08", media_id="usb-a")
            with mock.patch.object(backup, "removable_volume_info", return_value={}):
                with self.assertRaises(SystemExit):
                    backup.command_copy_offline(args)
            self.assertEqual(destination.read_bytes(), b"existing")

    def test_plaintext_private_key_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "private.pem"
            source.write_text("-----BEGIN PRIVATE KEY-----\nsecret\n", encoding="utf-8")
            volume = root / "volume"
            volume.mkdir()
            args = Namespace(backup=source, volume=volume,
                             evidence=root / "copy.json",
                             key_id="release-2026-08", media_id="usb-a")
            with mock.patch.object(backup, "removable_volume_info", return_value={}):
                with self.assertRaises(SystemExit):
                    backup.command_copy_offline(args)
            self.assertFalse((volume / "millennium-signing").exists())


if __name__ == "__main__":
    unittest.main()
