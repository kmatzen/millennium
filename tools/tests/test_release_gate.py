import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("release_gate", ROOT / "tools/release_gate.py")
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
SPEC.loader.exec_module(MODULE)


class ReleaseGateTests(unittest.TestCase):
    def setUp(self):
        self.commit = "a" * 40
        self.policy = {
            "required_local_suites": ["host", "contracts"],
            "required_qemu_acceptance": ["wifi", "exact"],
            "require_exact_production_image": True,
            "forbid_physical_hardware_claim": True,
        }
        self.local = {"source_commit": self.commit, "results": [
            {"suite": "host", "passed": True, "returncode": 0},
            {"suite": "contracts", "passed": True, "returncode": 0},
        ]}
        self.qemu = {"source_commit": self.commit, "passed": True,
                     "exact_production_image_tested": True,
                     "physical_hardware_claimed": False,
                     "acceptance": ["wifi", "exact"]}
        self.exact = {"result": "pass", "exact_image_userspace": True,
                      "physical_hardware_claimed": False, "image_size": 1024}

    def errors(self):
        return MODULE.validate(self.policy, self.local, self.qemu,
                               self.exact, self.commit)

    def test_complete_coherent_evidence_passes(self):
        self.assertEqual(self.errors(), [])

    def test_stale_qemu_commit_is_rejected(self):
        self.qemu["source_commit"] = "b" * 40
        self.assertIn("different source commit", " ".join(self.errors()))

    def test_generic_guest_cannot_pass_release_gate(self):
        self.qemu["exact_production_image_tested"] = False
        self.assertIn("exact production image", " ".join(self.errors()))

    def test_missing_scenario_is_rejected(self):
        self.qemu["acceptance"].remove("wifi")
        self.assertIn("wifi", " ".join(self.errors()))

    def test_failed_local_suite_is_rejected(self):
        self.local["results"][1]["passed"] = False
        self.assertIn("contracts", " ".join(self.errors()))

    def test_false_physical_claim_is_rejected(self):
        self.qemu["physical_hardware_claimed"] = True
        self.assertIn("physical-hardware claim", " ".join(self.errors()))

    def test_missing_image_identity_is_rejected(self):
        del self.exact["image_size"]
        self.assertIn("non-empty image", " ".join(self.errors()))


if __name__ == "__main__":
    unittest.main()
