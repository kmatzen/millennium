#!/usr/bin/env python3
import importlib.util
import json
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
TOOL_PATH = ROOT / "tools" / "generate_experience_schemas.py"
SPEC = importlib.util.spec_from_file_location("experience_schemas", TOOL_PATH)
TOOL = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
SPEC.loader.exec_module(TOOL)


class ExperienceSchemaTests(unittest.TestCase):
    def test_generated_files_are_canonical_and_current(self):
        for path, value in TOOL.outputs().items():
            self.assertEqual(path.read_text(), TOOL.render(value))
            self.assertEqual(json.loads(path.read_text()), value)

    def test_package_contract_is_closed_and_bounded(self):
        schema = TOOL.package_schema()
        self.assertFalse(schema["additionalProperties"])
        required = set(schema["required"])
        self.assertTrue({"sequence", "compatibility", "capabilities", "rating",
                         "locales", "quotas", "files", "state"} <= required)
        properties = schema["properties"]
        self.assertEqual(properties["schema"]["const"], 2)
        self.assertLessEqual(properties["size"]["maximum"], 256 * 1024 * 1024)
        self.assertLessEqual(
            properties["quotas"]["properties"]["state_bytes"]["maximum"],
            1024 * 1024,
        )
        self.assertFalse(properties["compatibility"]["additionalProperties"])
        self.assertFalse(properties["quotas"]["additionalProperties"])
        self.assertFalse(properties["state"]["additionalProperties"])

    def test_capabilities_are_an_allowlist_without_native_or_network(self):
        capabilities = set(
            TOOL.package_schema()["properties"]["capabilities"]["items"]["enum"]
        )
        self.assertTrue({"audio", "display", "handset", "keypad", "state"}
                        <= capabilities)
        self.assertFalse({"native", "shell", "network", "credentials"}
                         & capabilities)

    def test_catalog_requires_https_digest_rollout_and_revocation(self):
        schema = TOOL.catalog_schema()
        self.assertFalse(schema["additionalProperties"])
        self.assertTrue({"packages", "withdrawn", "denied"}
                        <= set(schema["required"]))
        entry = schema["properties"]["packages"]["items"]
        self.assertFalse(entry["additionalProperties"])
        self.assertEqual(entry["properties"]["manifest_url"]["pattern"],
                         r"^https://")
        self.assertIn("manifest_sha256", entry["required"])
        rollout = entry["properties"]["rollout"]
        self.assertEqual(rollout["properties"]["percentage"]["maximum"], 100)
        self.assertIn("hold", rollout["properties"])


if __name__ == "__main__":
    unittest.main()
