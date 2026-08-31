#!/usr/bin/env python3
import json
import io
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tarfile
import tempfile
import unittest


CONTENT = Path(__file__).parents[1]
sys.path.insert(0, str(CONTENT))
from install_content import InstallError, install, rollback, safe_extract
from storytool import package

STORY = CONTENT / "stories" / "last_line" / "story.json"
BASE_VERSION = json.loads(STORY.read_text())["version"]


class ContentInstallTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.private = self.root / "private.pem"
        self.public = self.root / "public.pem"
        subprocess.run(["openssl", "genpkey", "-algorithm", "ED25519", "-out", str(self.private)],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        subprocess.run(["openssl", "pkey", "-in", str(self.private), "-pubout", "-out", str(self.public)],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    def tearDown(self):
        self.temporary.cleanup()

    def build(self, output, version=None):
        story_path = STORY
        if version:
            story = json.loads(STORY.read_text())
            story["version"] = version
            source_root = output / f"story-{version}"
            source_root.mkdir()
            shutil.copytree(STORY.parent / "media", source_root / "media")
            story_path = source_root / "story.json"
            story_path.write_text(json.dumps(story))
        package(story_path, output, self.private, "test")
        manifest = next(output.glob(f"*{version or BASE_VERSION}.manifest.json"))
        return manifest, Path(str(manifest) + ".sig")

    def test_signed_install_and_rollback(self):
        packages = self.root / "packages"
        packages.mkdir()
        first, first_sig = self.build(packages)
        install(first, first_sig, {"test": self.public}, self.root / "installed")
        self.assertTrue((self.root / "installed/current/story.mst").is_file())
        release = (self.root / "installed/current").resolve()
        self.assertEqual(stat.S_IMODE(release.stat().st_mode), 0o755)
        second, second_sig = self.build(packages, "1.2.0")
        install(second, second_sig, {"test": self.public}, self.root / "installed")
        self.assertIn("1.2.0", os.readlink(self.root / "installed/current"))
        rollback(self.root / "installed")
        self.assertIn(BASE_VERSION, os.readlink(self.root / "installed/current"))

    def test_tampered_bundle_is_rejected(self):
        packages = self.root / "packages"
        packages.mkdir()
        manifest, signature = self.build(packages)
        archive = packages / json.loads(manifest.read_text())["bundle"]
        with archive.open("ab") as stream:
            stream.write(b"tamper")
        with self.assertRaisesRegex(InstallError, "digest"):
            install(manifest, signature, {"test": self.public}, self.root / "installed")

    def test_archive_path_traversal_is_rejected(self):
        archive = self.root / "traversal.tar.gz"
        with tarfile.open(archive, "w:gz") as bundle:
            info = tarfile.TarInfo("../escaped")
            info.size = 1
            bundle.addfile(info, io.BytesIO(b"x"))
        with self.assertRaisesRegex(InstallError, "unsafe archive member"):
            safe_extract(archive, self.root / "stage")
        self.assertFalse((self.root / "escaped").exists())

    def test_archive_links_are_rejected(self):
        archive = self.root / "link.tar.gz"
        with tarfile.open(archive, "w:gz") as bundle:
            info = tarfile.TarInfo("story.mst")
            info.type = tarfile.SYMTYPE
            info.linkname = "/etc/passwd"
            bundle.addfile(info)
        with self.assertRaisesRegex(InstallError, "unsafe archive member"):
            safe_extract(archive, self.root / "stage")


if __name__ == "__main__":
    unittest.main()
