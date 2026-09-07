#!/usr/bin/env python3
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

CONTENT = Path(__file__).parents[1]
sys.path.insert(0, str(CONTENT))
from catalogtool import build_catalog
from publish_catalog import PublishError, publish
from storytool import canonical, package, sign_ed25519

STORY = CONTENT / "stories" / "last_line" / "story.json"


class PublishCatalogTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.build = self.root / "build"
        self.build.mkdir()
        self.private = self.root / "private.pem"
        self.public = self.root / "public.pem"
        subprocess.run(["openssl", "genpkey", "-algorithm", "ED25519", "-out", str(self.private)], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        subprocess.run(["openssl", "pkey", "-in", str(self.private), "-pubout", "-out", str(self.public)], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        package(STORY, self.build, self.private, "test")
        manifest = next(self.build.glob("*.manifest.json"))
        catalog = build_catalog([manifest], "https://updates.example/experiences/", "stable", 1, "test")
        (self.build / "catalog.json").write_bytes(canonical(catalog))
        sign_ed25519(self.private, self.build / "catalog.json", self.build / "catalog.json.sig")
        self.web = self.root / "web"

    def tearDown(self):
        self.temporary.cleanup()

    def test_publishes_immutable_release_before_channel(self):
        result = publish(self.build, self.web, {"test": self.public}, {"test": self.public})
        self.assertEqual(result["sequence"], 1)
        release = self.web / result["releases"][0]
        self.assertTrue((release / "last-line-2.1.0.tar.gz").is_file())
        self.assertTrue((self.web / "stable/catalog.json.sig").is_file())
        with self.assertRaisesRegex(PublishError, "already exists|not newer"):
            publish(self.build, self.web, {"test": self.public}, {"test": self.public})

    def test_rejects_tamper_untrusted_keys_and_nonimmutable_url(self):
        catalog = json.loads((self.build / "catalog.json").read_text())
        catalog["packages"][0]["manifest_url"] = "https://updates.example/experiences/stable/package.json"
        catalog["packages"][0]["signature_url"] = catalog["packages"][0]["manifest_url"] + ".sig"
        (self.build / "catalog.json").write_bytes(canonical(catalog))
        sign_ed25519(self.private, self.build / "catalog.json", self.build / "catalog.json.sig")
        with self.assertRaisesRegex(PublishError, "not immutable"):
            publish(self.build, self.web, {"test": self.public}, {"test": self.public})
        with self.assertRaisesRegex(PublishError, "not trusted"):
            publish(self.build, self.web, {}, {"test": self.public})


if __name__ == "__main__":
    unittest.main()
