#!/usr/bin/env python3

import importlib.util
import json
from argparse import Namespace
from pathlib import Path
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "physical_interruption",
    ROOT / "host/monitoring/millennium_physical_interruption.py")
physical = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(physical)


def snapshot(boot_id="boot-a", ota_state="committed", release="release-9",
             firmware=None):
    checks = {"daemon_health": True, "host_version": True, "mcu_protocol": True,
              "keypad_present": True, "display_present": True,
              "daemon_service": True, "maintenance_tunnel": True,
              "ota_not_failed": ota_state != "rolled-back"}
    return {
        "boot_id": boot_id, "active_release": release,
        "active_content": "story-2.1.0", "installed_sequence": "9",
        "ota_state": ota_state, "ota_sequence": 9,
        "firmware_digests": firmware or {"keypad": "k", "display": "d"},
        "services": {"daemon": True, "maintenance_tunnel": True},
        "hil": {"passed": ota_state != "rolled-back", "checks": checks},
        "hil_exit_status": 0 if ota_state != "rolled-back" else 1,
    }


class PhysicalInterruptionTests(unittest.TestCase):
    def arguments(self, root, scenario="idle_power_loss"):
        return Namespace(state_dir=root, scenario=scenario)

    def test_arm_requires_healthy_baseline_and_is_durable(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            args = self.arguments(root)
            with mock.patch.object(physical, "capture_snapshot", return_value=snapshot()):
                physical.arm(args)
            armed = json.loads((root / "armed.json").read_text(encoding="utf-8"))
            self.assertEqual(armed["scenario"], "idle_power_loss")
            self.assertTrue(armed["requires_boot_change"])

    def test_reconcile_waits_for_required_boot_change(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            args = self.arguments(root)
            with mock.patch.object(physical, "capture_snapshot", return_value=snapshot()):
                physical.arm(args)
                self.assertEqual(physical.reconcile(args), 2)
            self.assertTrue((root / "armed.json").exists())

    def test_reconcile_records_recovery_and_clears_arm(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            args = self.arguments(root)
            with mock.patch.object(physical, "capture_snapshot", return_value=snapshot()):
                physical.arm(args)
            with mock.patch.object(physical, "capture_snapshot",
                                   return_value=snapshot(boot_id="boot-b")):
                self.assertEqual(physical.reconcile(args), 0)
            self.assertFalse((root / "armed.json").exists())
            evidence = list((root / "evidence").glob("idle_power_loss-*.json"))
            self.assertEqual(len(evidence), 1)
            record = json.loads(evidence[0].read_text(encoding="utf-8"))
            self.assertEqual(record["status"], "system-recovered")
            self.assertTrue(record["physical_observation_required"])
            self.assertFalse(record["automatic_acceptance_claimed"])

    def test_rollback_requires_prior_release_and_firmware(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            args = self.arguments(root, "mcu_flash_interruption")
            with mock.patch.object(physical, "capture_snapshot", return_value=snapshot()):
                physical.arm(args)
            after = snapshot(boot_id="boot-b", ota_state="rolled-back",
                             release="wrong-release",
                             firmware={"keypad": "wrong", "display": "d"})
            with mock.patch.object(physical, "capture_snapshot", return_value=after):
                self.assertEqual(physical.reconcile(args), 1)
            self.assertTrue((root / "armed.json").exists())

    def test_evidence_attempts_never_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first = physical.unique_evidence_path(root, "idle_power_loss", "stamp")
            first.write_text("first", encoding="utf-8")
            second = physical.unique_evidence_path(root, "idle_power_loss", "stamp")
            self.assertNotEqual(first, second)
            self.assertEqual(first.read_text(encoding="utf-8"), "first")


if __name__ == "__main__":
    unittest.main()
