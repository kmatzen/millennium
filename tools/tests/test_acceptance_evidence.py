#!/usr/bin/env python3

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


as_built = load("as_built_record", ROOT / "tools/as_built_record.py")
playtest = load("playtest_record", ROOT / "content/playtest_record.py")


class AcceptanceEvidenceTests(unittest.TestCase):
    def test_blank_as_built_record_cannot_pass(self):
        value = {"schema": 1, "device_id": "phone-001", "captured_by": "operator",
                 "physical": {}, "installed_artifacts": {}, "photos": [], "tests": {}}
        missing = as_built.missing_evidence(value)
        self.assertIn("physical.pcb_revision", missing)
        self.assertIn("tests.controlled_brownout", missing)
        self.assertIn("installed_artifacts.keypad_hex", missing)

    def test_complete_playtest_requires_two_uncoached_callers_and_all_scenarios(self):
        participant = {"first_time_caller": True, "started_without_coaching": True,
                       "completed_primary_ending": True, "audio_clear": True,
                       "display_legible": True, "time_to_first_action_seconds": 4,
                       "total_duration_seconds": 300, "confusion_or_disengagement": []}
        value = {"schema": 1, "content_version": "story-1", "device_as_built_record": "phone-001",
                 "participants": [dict(participant), dict(participant)],
                 "physical_scenarios": {name: {"passed": True, "evidence": "observed"}
                                        for name in playtest.SCENARIOS},
                 "open_defects": [], "accepted_for_handoff": True}
        self.assertEqual(playtest.omissions(value), [])
        value["physical_scenarios"]["offline"]["evidence"] = None
        self.assertIn("physical_scenarios.offline", playtest.omissions(value))


if __name__ == "__main__":
    unittest.main()
