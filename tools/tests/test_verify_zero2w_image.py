import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "verify_zero2w_image", ROOT / "tools/verify_zero2w_image.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class VerifyZero2WImageTests(unittest.TestCase):
    def test_region_comparison_detects_match_and_difference(self):
        with tempfile.NamedTemporaryFile() as image:
            image.write(b"boot-a" + b"boot-a" + b"changed")
            image.flush()
            path = Path(image.name)
            self.assertTrue(MODULE.regions_equal(path, 0, 6, 6, chunk_size=2))
            self.assertFalse(MODULE.regions_equal(path, 0, 12, 6, chunk_size=2))

    def test_regular_file_size(self):
        with tempfile.NamedTemporaryFile() as image:
            image.truncate(4096)
            self.assertEqual(MODULE.media_size(Path(image.name)), 4096)

    def test_darwin_block_device_size(self):
        path = mock.MagicMock()
        path.stat.return_value.st_size = 0
        stream = mock.MagicMock()
        stream.fileno.return_value = 7
        path.open.return_value.__enter__.return_value = stream
        replies = [struct.pack("I", 512), struct.pack("Q", 124735488)]
        with mock.patch.object(MODULE.sys, "platform", "darwin"), \
                mock.patch.object(MODULE.fcntl, "ioctl", side_effect=replies):
            self.assertEqual(MODULE.media_size(path), 63864569856)


if __name__ == "__main__":
    unittest.main()
