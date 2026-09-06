import importlib.util
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "audit_zero2w_rootfs", ROOT / "tools/audit_zero2w_rootfs.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class RootfsAuditTests(unittest.TestCase):
    def fixture(self, package_lines="base-files\t1\n"):
        temporary = tempfile.TemporaryDirectory()
        root = Path(temporary.name) / "root"
        (root / "tmp").mkdir(parents=True)
        (root / "opt/millennium").mkdir(parents=True)
        manifest = Path(temporary.name) / "manifest"
        manifest.write_text(package_lines, encoding="utf-8")
        return temporary, root, manifest

    def test_clean_runtime_root_passes(self):
        temporary, root, manifest = self.fixture()
        try:
            (root / "var/cache/apt/archives").mkdir(parents=True)
            (root / "var/cache/apt/archives/lock").touch()
            MODULE.audit(root, manifest)
        finally:
            temporary.cleanup()

    def test_cached_package_is_rejected(self):
        temporary, root, manifest = self.fixture()
        try:
            (root / "var/cache/apt/archives").mkdir(parents=True)
            (root / "var/cache/apt/archives/package.deb").write_bytes(b"deb")
            with self.assertRaisesRegex(ValueError, "apt package cache"):
                MODULE.audit(root, manifest)
        finally:
            temporary.cleanup()

    def test_tool_package_is_rejected(self):
        temporary, root, manifest = self.fixture("base-files\t1\nbinutils\t2\n")
        try:
            with self.assertRaisesRegex(ValueError, "forbidden packages: binutils"):
                MODULE.audit(root, manifest)
        finally:
            temporary.cleanup()

    def test_source_and_temporary_residue_are_rejected(self):
        temporary, root, manifest = self.fixture()
        try:
            (root / "tmp/leak").write_text("x")
            (root / "opt/millennium/main.cpp").write_text("int main(){}")
            with self.assertRaisesRegex(ValueError, "target /tmp.*source/build residue"):
                MODULE.audit(root, manifest)
        finally:
            temporary.cleanup()


if __name__ == "__main__":
    unittest.main()
