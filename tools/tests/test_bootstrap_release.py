import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "write_bootstrap_release", ROOT / "tools/write_bootstrap_release.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class BootstrapReleaseTests(unittest.TestCase):
    def test_checked_firmware_contains_both_distinct_identities(self):
        firmware = dict(MODULE.identity(path) for path in (
            ROOT / "Arduino/build/keypad/keypad.ino.hex",
            ROOT / "Arduino/build/display/display.ino.hex",
        ))
        self.assertEqual(set(firmware), {"keypad", "display"})
        self.assertEqual(firmware["keypad"]["protocol"], 2)
        self.assertEqual(firmware["display"]["protocol"], 2)

    def test_missing_identity_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "empty.hex"
            path.write_text(":00000001FF\n", encoding="ascii")
            with self.assertRaisesRegex(ValueError, "identity missing"):
                MODULE.identity(path)

    def test_cli_writes_canonical_release(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "release.json"
            old = MODULE.sys.argv
            MODULE.sys.argv = ["write_bootstrap_release.py",
                               str(ROOT / "Arduino/build/keypad/keypad.ino.hex"),
                               str(ROOT / "Arduino/build/display/display.ino.hex"),
                               "0.4.0", "a" * 40,
                               str(output)]
            try:
                MODULE.main()
            finally:
                MODULE.sys.argv = old
            value = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(set(value["firmware"]), {"keypad", "display"})
            self.assertEqual(value["version"], "0.4.0")
            self.assertEqual(value["sequence"], 0)
            self.assertEqual(value["source_commit"], "a" * 40)
            self.assertTrue(output.read_bytes().endswith(b"\n"))


if __name__ == "__main__":
    unittest.main()
