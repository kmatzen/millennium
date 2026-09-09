import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "coverage_ledger", ROOT / "tools/coverage_ledger.py")
coverage_ledger = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(coverage_ledger)


class CoverageLedgerTests(unittest.TestCase):
    def setUp(self):
        self.value = json.loads(
            (ROOT / "acceptance/coverage.json").read_text(encoding="utf-8"))

    def write(self, value):
        directory = tempfile.TemporaryDirectory()
        path = Path(directory.name) / "coverage.json"
        path.write_text(json.dumps(value), encoding="utf-8")
        return directory, path

    def test_production_ledger_has_bounded_physical_evidence_contracts(self):
        coverage_ledger.load(ROOT / "acceptance/coverage.json")
        for gate in self.value["physical_gates"].values():
            self.assertTrue(gate["evidence_command"])
            self.assertTrue(gate["pass_criteria"])
            self.assertTrue(gate["time_bound"])

    def test_rejects_physical_gate_without_pass_criteria(self):
        value = copy.deepcopy(self.value)
        del value["physical_gates"]["zero2w-boot"]["pass_criteria"]
        directory, path = self.write(value)
        self.addCleanup(directory.cleanup)
        with self.assertRaisesRegex(ValueError, "missing pass_criteria"):
            coverage_ledger.load(path)


if __name__ == "__main__":
    unittest.main()
