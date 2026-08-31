#!/usr/bin/env python3
"""Unit tests for the dependency-free appliance monitor helpers."""

import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock


MODULE_PATH = Path(__file__).parents[1] / "monitoring" / "millennium_monitor.py"
SPEC = importlib.util.spec_from_file_location("millennium_monitor", MODULE_PATH)
MONITOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MONITOR)
HIL_PATH = Path(__file__).parents[1] / "monitoring" / "millennium_hil_smoke.py"
HIL_SPEC = importlib.util.spec_from_file_location("millennium_hil_smoke", HIL_PATH)
HIL = importlib.util.module_from_spec(HIL_SPEC)
HIL_SPEC.loader.exec_module(HIL)


class MonitorTests(unittest.TestCase):
    def test_labels_escape_quotes_and_backslashes(self):
        rendered = MONITOR.metric("service", 1, {"name": 'a"b\\c'})
        self.assertEqual(rendered,
                         'millennium_service{name="a\\"b\\\\c"} 1')

    def test_numeric_metric_reads_counters_and_gauges(self):
        data = {"counters": {"serial_disconnects": 3},
                "gauges": {"mcu_resets_keypad": 2.0}}
        self.assertEqual(MONITOR.numeric_metric(data, "serial_disconnects"), 3.0)
        self.assertEqual(MONITOR.numeric_metric(data, "mcu_resets_keypad"), 2.0)
        self.assertEqual(MONITOR.numeric_metric(data, "missing"), 0.0)

    def test_atomic_write_replaces_complete_file(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "metrics.prom"
            MONITOR.atomic_write(target, "first\n")
            MONITOR.atomic_write(target, "second\n")
            self.assertEqual(target.read_text(), "second\n")

    def test_filesystem_errors_uses_portable_kernel_journal_flag(self):
        completed = subprocess.CompletedProcess([], 0, stdout="EXT4-fs error\nquiet\n")
        with mock.patch.object(MONITOR.subprocess, "run", return_value=completed) as run:
            self.assertEqual(MONITOR.recent_filesystem_errors(), 1)
        command = run.call_args.args[0]
        self.assertIn("-k", command)
        self.assertNotIn("--kernel", command)

    def test_hil_evaluate_requires_every_physical_gate(self):
        healthy = {"overall_status": "HEALTHY"}
        version = {"version": "0.4.0"}
        metrics = {"gauges": {"mcu_protocol_version": 2}}
        release = {"version": "0.4.0"}
        devices = {"keypad": True, "display": True}
        services = {"daemon.service": True,
                    "millennium-maintenance-tunnel.service": True}
        checks = HIL.evaluate(healthy, version, metrics, release, devices,
                              services, {"state": "committed"})
        self.assertTrue(all(checks.values()))
        devices["display"] = False
        checks = HIL.evaluate(healthy, version, metrics, release, devices,
                              services, {"state": "committed"})
        self.assertFalse(checks["display_present"])

    def test_hil_rejects_protocol_or_ota_failure(self):
        checks = HIL.evaluate(
            {"overall_status": "WARNING"}, {"version": "0.4.0"},
            {"gauges": {"mcu_protocol_version": 1}}, {"version": "0.4.0"},
            {"keypad": True, "display": True},
            {"daemon.service": True,
             "millennium-maintenance-tunnel.service": True},
            {"state": "rolled-back"})
        self.assertFalse(checks["mcu_protocol"])
        self.assertFalse(checks["ota_not_failed"])


if __name__ == "__main__":
    unittest.main()
