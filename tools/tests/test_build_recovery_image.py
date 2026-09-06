import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "build_recovery_image", ROOT / "tools/build_recovery_image.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class RecoveryImageTests(unittest.TestCase):
    def test_digest_stream_binds_size_and_content(self):
        size, digest = MODULE.digest_stream(io.BytesIO(b"millennium"))
        self.assertEqual(size, 10)
        self.assertEqual(digest, "4b1a741d678c3304cec0d943cac51ce75f878b43f844f5a0b5cbc7a4d5a13233")

    def test_manifest_binds_board_layout_and_both_image_forms(self):
        class Args:
            version = "1.0.0"
            key_id = "release-2026-08"
            source_commit = "a" * 40
            board_model = ["Raspberry Pi Zero 2 W Rev 1.0"]
            architecture = "arm64"
            layout_id = "zero2w-ab-mbr-v1"
        image = {"filename": "phone.img.zst", "compression": "zstd",
                 "compressed_size": 12, "compressed_sha256": "b" * 64,
                 "expanded_size": 34, "expanded_sha256": "c" * 64}
        value = MODULE.build_manifest(Args, image)
        self.assertEqual(value["kind"], "millennium-recovery-image")
        self.assertEqual(value["image"], image)
        self.assertEqual(value["layout"]["slot_map"]["b"]["system_partition"], 6)
        self.assertEqual(value["layout"]["partitions"][-1]["size_bytes"], 8 * 1024**3)

    def test_canonical_json_has_stable_order_and_newline(self):
        self.assertEqual(MODULE.canonical({"z": 1, "a": 2}), b'{"a":2,"z":1}\n')

    def test_short_source_commit_is_rejected(self):
        with self.assertRaisesRegex(SystemExit, "40 lowercase"):
            MODULE.source_commit("00ac4d5")


if __name__ == "__main__":
    unittest.main()
