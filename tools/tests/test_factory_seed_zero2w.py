import importlib.util
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "factory_seed_zero2w", ROOT / "tools/factory_seed_zero2w.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class FactorySeedTests(unittest.TestCase):
    def make_staging(self, root):
        staging = Path(root)
        (staging / "etc/ssh").mkdir(parents=True)
        (staging / "etc/millennium").mkdir(parents=True)
        (staging / "home/millennium/.ssh").mkdir(parents=True)
        (staging / "etc/machine-id").write_text("a" * 32 + "\n")
        for name in MODULE.HOST_KEYS:
            (staging / "etc/ssh" / name).write_text(name)
        for name in MODULE.REQUIRED_CONFIG:
            value = "phone-001\n" if name == "device-id" else "value\n"
            (staging / "etc/millennium" / name).write_text(value)
        (staging / "home/millennium/.ssh/authorized_keys").write_text(
            "ssh-ed25519 AAAAC3Nza test\n")
        return staging

    def test_complete_staging_is_accepted(self):
        with tempfile.TemporaryDirectory() as directory:
            self.assertEqual(MODULE.validate_staging(self.make_staging(directory)),
                             "phone-001")

    def test_invalid_machine_id_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            staging = self.make_staging(directory)
            (staging / "etc/machine-id").write_text("short\n")
            with self.assertRaisesRegex(ValueError, "machine-id"):
                MODULE.validate_staging(staging)

    def test_links_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            staging = self.make_staging(directory)
            (staging / "escape").symlink_to("/etc/passwd")
            with self.assertRaisesRegex(ValueError, "link or special"):
                MODULE.validate_staging(staging)


if __name__ == "__main__":
    unittest.main()
