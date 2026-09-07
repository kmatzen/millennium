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
from install_content import (InstallError, install, rollback, safe_extract,
                             validate_manifest, verify_inventory)
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
            story["distribution"]["sequence"] = 2
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
        self.assertEqual(stat.S_IMODE((release / "story.mst").stat().st_mode), 0o444)
        second, second_sig = self.build(packages, "1.2.0")
        install(second, second_sig, {"test": self.public}, self.root / "installed")
        self.assertIn("1.2.0", os.readlink(self.root / "installed/current"))
        rollback(self.root / "installed")
        self.assertIn(BASE_VERSION, os.readlink(self.root / "installed/current"))

    def test_runtime_compatibility_and_sequence_rollback_are_rejected(self):
        packages = self.root / "packages"
        packages.mkdir()
        manifest, signature = self.build(packages)
        installed = self.root / "installed"
        with self.assertRaisesRegex(InstallError, "runtime schema"):
            install(manifest, signature, {"test": self.public}, installed,
                    runtime_schema=2, daemon_version="0.4.0")
        install(manifest, signature, {"test": self.public}, installed,
                runtime_schema=1, daemon_version="0.4.0")
        shutil.rmtree(installed / "releases" / f"last-line-{BASE_VERSION}")
        with self.assertRaisesRegex(InstallError, "sequence rollback"):
            install(manifest, signature, {"test": self.public}, installed,
                    runtime_schema=1, daemon_version="0.4.0")

    def test_daemon_version_bounds_are_enforced(self):
        packages = self.root / "packages"
        packages.mkdir()
        manifest, signature = self.build(packages)
        with self.assertRaisesRegex(InstallError, "newer daemon"):
            install(manifest, signature, {"test": self.public},
                    self.root / "installed", daemon_version="0.3.9")

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

    def test_duplicate_archive_members_are_rejected(self):
        archive = self.root / "duplicate.tar.gz"
        with tarfile.open(archive, "w:gz") as bundle:
            for value in (b"first", b"second"):
                info = tarfile.TarInfo("story.json")
                info.size = len(value)
                bundle.addfile(info, io.BytesIO(value))
        with self.assertRaisesRegex(InstallError, "duplicate archive member"):
            safe_extract(archive, self.root / "stage")

    def test_archive_member_count_is_bounded(self):
        archive = self.root / "many.tar.gz"
        with tarfile.open(archive, "w:gz") as bundle:
            for index in range(4097):
                info = tarfile.TarInfo("empty/%04d" % index)
                info.size = 0
                bundle.addfile(info, io.BytesIO())
        with self.assertRaisesRegex(InstallError, "too many members"):
            safe_extract(archive, self.root / "stage")

    def test_v2_manifest_rejects_undeclared_capabilities(self):
        packages = self.root / "packages"
        packages.mkdir()
        manifest_path, unused_signature = self.build(packages)
        manifest = json.loads(manifest_path.read_text())
        manifest["capabilities"].append("shell")
        with self.assertRaisesRegex(InstallError, "capabilities"):
            validate_manifest(manifest)

    def test_v2_inventory_rejects_added_or_changed_files(self):
        packages = self.root / "packages"
        packages.mkdir()
        manifest_path, unused_signature = self.build(packages)
        manifest = json.loads(manifest_path.read_text())
        archive = packages / manifest["bundle"]
        stage = self.root / "stage"
        stage.mkdir()
        safe_extract(archive, stage, manifest["quotas"]["storage_bytes"])
        verify_inventory(stage, manifest)
        (stage / "undeclared").write_text("no")
        with self.assertRaisesRegex(InstallError, "inventory"):
            verify_inventory(stage, manifest)

    def test_extraction_limit_rejects_expansion_bomb(self):
        archive = self.root / "large.tar.gz"
        with tarfile.open(archive, "w:gz") as bundle:
            info = tarfile.TarInfo("story.json")
            info.size = 33
            bundle.addfile(info, io.BytesIO(b"x" * 33))
        with self.assertRaisesRegex(InstallError, "extraction limit"):
            safe_extract(archive, self.root / "limited", maximum_bytes=32)


if __name__ == "__main__":
    unittest.main()
