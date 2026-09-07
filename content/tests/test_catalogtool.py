#!/usr/bin/env python3
import copy
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


CONTENT = Path(__file__).parents[1]
sys.path.insert(0, str(CONTENT))
from catalogtool import (CatalogError, build_catalog, eligible_packages,
                         rollout_bucket, validate_catalog)
from storytool import package


STORY = CONTENT / "stories" / "last_line" / "story.json"


class CatalogToolTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        package(STORY, self.root)
        self.manifest = next(self.root.glob("*.manifest.json"))
        self.catalog = build_catalog(
            [self.manifest], "https://updates.kmatzen.com/experiences/stable/",
            "stable", 7, "catalog-2026", 100)

    def tearDown(self):
        self.temporary.cleanup()

    def test_catalog_is_canonical_and_reproducible(self):
        second = build_catalog(
            [self.manifest], "https://updates.kmatzen.com/experiences/stable",
            "stable", 7, "catalog-2026", 100)
        self.assertEqual(self.catalog, second)
        entry = self.catalog["packages"][0]
        self.assertTrue(entry["manifest_url"].startswith("https://"))
        self.assertIn("/releases/last-line/00000001-2.1.0/", entry["manifest_url"])
        self.assertEqual(entry["signature_url"], entry["manifest_url"] + ".sig")

    def test_transport_must_be_https_and_package_schema_two(self):
        with self.assertRaisesRegex(CatalogError, "HTTPS"):
            build_catalog([self.manifest], "http://updates.example/", "stable",
                          1, "key")
        legacy = json.loads(self.manifest.read_text())
        legacy["schema"] = 1
        self.manifest.write_text(json.dumps(legacy))
        with self.assertRaisesRegex(CatalogError, "schema-2"):
            build_catalog([self.manifest], "https://updates.example/", "stable",
                          1, "key")

    def test_hold_group_withdrawal_denylist_and_sequence_filter(self):
        entry = self.catalog["packages"][0]
        self.assertEqual(len(eligible_packages(self.catalog, "phone-001")), 1)
        held = copy.deepcopy(self.catalog)
        held["packages"][0]["rollout"]["hold"] = True
        self.assertEqual(eligible_packages(held, "phone-001"), [])
        grouped = copy.deepcopy(self.catalog)
        grouped["packages"][0]["rollout"]["groups"] = ["museum"]
        self.assertEqual(eligible_packages(grouped, "phone-001"), [])
        self.assertEqual(len(eligible_packages(grouped, "phone-001", ["museum"])), 1)
        withdrawn = copy.deepcopy(self.catalog)
        withdrawn["withdrawn"] = [entry["manifest_sha256"]]
        self.assertEqual(eligible_packages(withdrawn, "phone-001"), [])
        denied = copy.deepcopy(self.catalog)
        denied["denied"] = [entry["id"]]
        self.assertEqual(eligible_packages(denied, "phone-001"), [])
        self.assertEqual(eligible_packages(
            self.catalog, "phone-001", installed={entry["id"]: entry["sequence"]}), [])

    def test_percentage_bucket_is_stable_and_exclusive(self):
        bucket = rollout_bucket("phone-001", "last-line")
        self.assertEqual(bucket, rollout_bucket("phone-001", "last-line"))
        catalog = copy.deepcopy(self.catalog)
        catalog["packages"][0]["rollout"]["percentage"] = bucket
        self.assertEqual(eligible_packages(catalog, "phone-001"), [])
        catalog["packages"][0]["rollout"]["percentage"] = bucket + 1
        self.assertEqual(len(eligible_packages(catalog, "phone-001")), 1)

    def test_malformed_or_duplicate_entries_are_rejected(self):
        bad = copy.deepcopy(self.catalog)
        bad["packages"].append(copy.deepcopy(bad["packages"][0]))
        with self.assertRaisesRegex(CatalogError, "identity"):
            validate_catalog(bad)
        bad = copy.deepcopy(self.catalog)
        bad["packages"][0]["rollout"]["percentage"] = 101
        with self.assertRaisesRegex(CatalogError, "rollout"):
            validate_catalog(bad)


if __name__ == "__main__":
    unittest.main()
