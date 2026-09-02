#!/usr/bin/env python3

import importlib.util
import json
from argparse import Namespace
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
handoff = load("handoff_acceptance", ROOT / "tools/handoff_acceptance.py")


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
                       "total_duration_seconds": 300, "confusion_or_disengagement": [],
                       "optional_interaction_discovered": False,
                       "observation_evidence": {"path": "notes", "sha256": "a" * 64}}
        value = {"schema": 1, "content_version": "story-1", "device_as_built_record": "phone-001",
                 "participants": [dict(participant), dict(participant)],
                 "physical_scenarios": {name: {"passed": True,
                                                "evidence": {"path": "notes", "sha256": "b" * 64}}
                                        for name in playtest.SCENARIOS},
                 "open_defects": [], "accepted_for_handoff": True,
                 "decision_evidence": {"path": "decision", "sha256": "c" * 64}}
        self.assertEqual(playtest.omissions(value), [])
        value["physical_scenarios"]["offline"]["evidence"] = None
        self.assertIn("physical_scenarios.offline", playtest.omissions(value))

    def test_playtest_evidence_digest_detects_tampering(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "notes.txt"
            path.write_text("observed", encoding="utf-8")
            reference = playtest.file_evidence(path)
            self.assertTrue(playtest.valid_evidence(reference, Path(directory) / "record.json"))
            path.write_text("changed", encoding="utf-8")
            self.assertFalse(playtest.valid_evidence(reference, Path(directory) / "record.json"))

    def test_handoff_requires_every_physical_gate(self):
        value = {
            "schema": 1,
            "device_id": "phone-001",
            "as_built_record": "as-built.json",
            "playtest_record": "playtest.json",
            "wifi_clients": {},
            "offline_signing_key_copies": [],
            "external_maintenance": {},
        }
        missing = handoff.omissions(value, Path("handoff.json"), check_linked=False)
        self.assertIn("wifi_clients.ios", missing)
        self.assertIn("wifi_clients.windows", missing)
        self.assertIn("offline_signing_key_copies.two_distinct_media", missing)
        self.assertIn("external_maintenance", missing)

    def test_handoff_rejects_one_media_label_even_with_two_entries(self):
        key = {"media_label": "USB-A", "ciphertext_sha256": "a" * 64,
               "physically_distinct": True, "verified_at": "2026-09-02",
               "recovery_tested_at": "2026-09-02",
               "evidence": {"path": "log", "sha256": "b" * 64}}
        value = {
            "schema": 1, "device_id": "phone-001",
            "as_built_record": "as-built.json", "playtest_record": "playtest.json",
            "wifi_clients": {name: {"passed": True, "date": "2026-09-02",
                                     "evidence": {"path": "notes", "sha256": "a" * 64}}
                             for name in handoff.WIFI_PLATFORMS},
            "offline_signing_key_copies": [dict(key), dict(key)],
            "external_maintenance": {"passed": True, "date": "2026-09-02",
                                     "network_description": "cellular",
                                     "evidence": {"path": "audit", "sha256": "c" * 64}},
        }
        missing = handoff.omissions(value, Path("handoff.json"), check_linked=False)
        self.assertIn("offline_signing_key_copies.two_distinct_media", missing)

    def test_measured_test_command_requires_measurement(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            record = root / "record.json"
            evidence = root / "evidence.txt"
            evidence.write_text("observed", encoding="utf-8")
            record.write_text(json.dumps({"schema": 1, "status": "incomplete"}),
                              encoding="utf-8")
            args = Namespace(record=record, name="controlled_brownout",
                             result="pass", evidence_file=evidence,
                             date="2026-09-02", instrument_and_load=None,
                             minimum_voltage=None)
            with self.assertRaises(SystemExit):
                as_built.record_test(args)

    def test_recorded_test_hashes_its_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            record = root / "record.json"
            evidence = root / "evidence.txt"
            evidence.write_text("observed", encoding="utf-8")
            record.write_text(json.dumps({"schema": 1, "status": "incomplete"}),
                              encoding="utf-8")
            args = Namespace(record=record, name="idle_power_loss", result="pass",
                             evidence_file=evidence, date="2026-09-02",
                             instrument_and_load=None, minimum_voltage=None)
            as_built.record_test(args)
            saved = json.loads(record.read_text(encoding="utf-8"))
            self.assertEqual(saved["tests"]["idle_power_loss"]["evidence"]["sha256"],
                             as_built.digest(evidence))


if __name__ == "__main__":
    unittest.main()
