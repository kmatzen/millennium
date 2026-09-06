import importlib.util
import json
import os
from pathlib import Path
import plistlib
import subprocess
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "write_recovery_media", ROOT / "tools/write_recovery_media.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class RecoveryMediaTests(unittest.TestCase):
    def make_signed_artifact(self, root):
        private = root / "private.pem"
        public = root / "public.pem"
        raw = root / "phone.img"
        image = root / "phone.img.zst"
        manifest = root / "recovery-manifest.json"
        signature = root / "recovery-manifest.json.sig"
        subprocess.run(["openssl", "genpkey", "-algorithm", "ED25519", "-out",
                        str(private)], check=True, stdout=subprocess.DEVNULL)
        subprocess.run(["openssl", "pkey", "-in", str(private), "-pubout", "-out",
                        str(public)], check=True, stdout=subprocess.DEVNULL)
        raw.write_bytes(b"recovery image" * 100)
        subprocess.run(["zstd", "--quiet", "--force", str(raw), "-o", str(image)],
                       check=True)
        compressed_size, compressed_digest = MODULE.sha256_file(image)
        value = {
            "schema": 1,
            "kind": "millennium-recovery-image",
            "image": {
                "filename": image.name,
                "compression": "zstd",
                "compressed_size": compressed_size,
                "compressed_sha256": compressed_digest,
                "expanded_size": raw.stat().st_size,
                "expanded_sha256": MODULE.hashlib.sha256(raw.read_bytes()).hexdigest(),
            },
        }
        manifest.write_bytes(MODULE.canonical(value))
        subprocess.run(["openssl", "pkeyutl", "-sign", "-rawin", "-inkey",
                        str(private), "-in", str(manifest), "-out", str(signature)],
                       check=True)
        return manifest, signature, public, image

    def test_verifies_signature_compressed_hash_and_zstd(self):
        with tempfile.TemporaryDirectory() as temporary:
            paths = self.make_signed_artifact(Path(temporary))
            value = MODULE.verified_manifest(*paths)
            self.assertEqual(value["image"]["expanded_size"], 1400)

    def test_rejects_changed_compressed_image(self):
        with tempfile.TemporaryDirectory() as temporary:
            manifest, signature, public, image = self.make_signed_artifact(Path(temporary))
            image.write_bytes(image.read_bytes() + b"changed")
            with self.assertRaisesRegex(SystemExit, "does not match"):
                MODULE.verified_manifest(manifest, signature, public, image)

    def test_darwin_accepts_only_removable_whole_disk(self):
        info = {
            "DeviceIdentifier": "disk9", "WholeDisk": True, "Internal": False,
            "RemovableMedia": True, "Ejectable": True, "TotalSize": 32_000_000_000,
            "BusProtocol": "USB", "VirtualOrPhysical": "Physical",
        }
        completed = subprocess.CompletedProcess([], 0, stdout=plistlib.dumps(info), stderr=b"")
        with mock.patch.object(MODULE, "run", return_value=completed):
            target = MODULE.darwin_target("/dev/disk9")
        self.assertEqual(target["raw_path"], "/dev/rdisk9")
        self.assertEqual(target["device_identifier"], "disk9")

    def test_darwin_rejects_internal_disk(self):
        info = {
            "DeviceIdentifier": "disk3", "WholeDisk": True, "Internal": True,
            "RemovableMedia": False, "Ejectable": False, "TotalSize": 1_000_000,
        }
        completed = subprocess.CompletedProcess([], 0, stdout=plistlib.dumps(info), stderr=b"")
        with mock.patch.object(MODULE, "run", return_value=completed):
            with self.assertRaisesRegex(SystemExit, "internal"):
                MODULE.darwin_target("/dev/disk3")

    def test_darwin_rejects_partition_path_before_inspection(self):
        with self.assertRaisesRegex(SystemExit, "whole disk"):
            MODULE.darwin_target("/dev/disk9s1")

    def test_darwin_resolves_containing_whole_disk(self):
        info = {"DeviceIdentifier": "disk12s1", "ParentWholeDisk": "disk12"}
        completed = subprocess.CompletedProcess([], 0, stdout=plistlib.dumps(info), stderr=b"")
        with mock.patch.object(MODULE, "run", return_value=completed), \
             mock.patch.object(MODULE.os.path, "ismount", return_value=True):
            self.assertEqual(MODULE.darwin_containing_disks(Path("/tmp/image.zst")),
                             {"disk12"})

    def test_rejects_disk_containing_source_image(self):
        target = {"device_identifier": "disk6"}
        with mock.patch.object(MODULE, "protected_disks", return_value={"disk1", "disk6"}):
            with self.assertRaisesRegex(SystemExit, "source-image disk"):
                MODULE.reject_protected_target(target, Path("/Volumes/UEBuild/image.zst"),
                                               "Darwin")

    def test_allows_distinct_recovery_disk(self):
        target = {"device_identifier": "disk12"}
        with mock.patch.object(MODULE, "protected_disks", return_value={"disk1", "disk6"}):
            MODULE.reject_protected_target(target, Path("/Volumes/UEBuild/image.zst"),
                                           "Darwin")

    def test_readback_hashes_exact_image_length(self):
        with tempfile.TemporaryDirectory() as temporary:
            target = Path(temporary) / "disk"
            target.write_bytes(b"signed image" + b"trailing media")
            expected = MODULE.hashlib.sha256(b"signed image").hexdigest()
            descriptor = os.open(target, os.O_RDONLY)
            try:
                self.assertEqual(MODULE.verify_readback(descriptor, 12, expected), expected)
            finally:
                os.close(descriptor)

    def test_darwin_target_is_opened_exclusively(self):
        with mock.patch.object(MODULE.os, "open", return_value=42) as opened:
            self.assertEqual(MODULE.open_target("/dev/rdisk9", "Darwin"), 42)
        flags = opened.call_args.args[1]
        self.assertEqual(flags & os.O_RDWR, os.O_RDWR)
        self.assertEqual(flags & os.O_EXCL, os.O_EXCL)

    def test_write_and_readback_share_one_descriptor(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest, signature, public, image = self.make_signed_artifact(root)
            value = MODULE.verified_manifest(manifest, signature, public, image)
            target = root / "target"
            target.write_bytes(b"\0" * 4096)
            descriptor = os.open(target, os.O_RDWR)
            try:
                metadata = value["image"]
                MODULE.stream_image(image, descriptor, metadata["expanded_size"],
                                    metadata["expanded_sha256"])
                self.assertEqual(
                    MODULE.verify_readback(descriptor, metadata["expanded_size"],
                                           metadata["expanded_sha256"]),
                    metadata["expanded_sha256"])
            finally:
                os.close(descriptor)

    def test_canonical_manifest_rejects_formatting_changes(self):
        value = {"schema": 1, "kind": "millennium-recovery-image"}
        self.assertEqual(MODULE.canonical(value),
                         b'{"kind":"millennium-recovery-image","schema":1}\n')


if __name__ == "__main__":
    unittest.main()
