#!/usr/bin/env python3

import fcntl
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock
import urllib.error


ROOT = Path(__file__).resolve().parents[2]
OTA_SPEC = importlib.util.spec_from_file_location(
    "millennium_os_ota", ROOT / "host/os_ota/millennium_os_ota.py")
OTA = importlib.util.module_from_spec(OTA_SPEC)
sys.modules["millennium_os_ota"] = OTA
OTA_SPEC.loader.exec_module(OTA)
AGENT_SPEC = importlib.util.spec_from_file_location(
    "millennium_os_ota_agent", ROOT / "host/os_ota/millennium_os_ota_agent.py")
AGENT = importlib.util.module_from_spec(AGENT_SPEC)
AGENT_SPEC.loader.exec_module(AGENT)


class Response:
    def __init__(self, value):
        self.value = value
        self.headers = {"Content-Length": str(len(value))}

    def __enter__(self):
        return self

    def __exit__(self, *_args):
        return False

    def read(self, maximum=-1):
        return self.value if maximum < 0 else self.value[:maximum]


class OsOtaAgentTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)

    def tearDown(self):
        self.temporary.cleanup()

    def test_config_uses_image_runtime_mounts_and_other_slot(self):
        config = AGENT.load_config(self.root / "missing.conf")
        self.assertEqual(config["selector_path"], "/bootfs/autoboot.txt")
        self.assertEqual(config["inactive_boot"], "/dev/disk/by-slot/other/boot")
        self.assertEqual(config["inactive_root"], "/dev/disk/by-slot/other/system")
        self.assertEqual(config["layout_id"], "zero2w-ab-mbr-v1")

    def test_fetch_enforces_declared_and_actual_size(self):
        self.assertEqual(AGENT.fetch("https://example.test", 4,
                                    lambda *_args, **_kwargs: Response(b"data")), b"data")
        with self.assertRaisesRegex(AGENT.AgentError, "size limit"):
            AGENT.fetch("https://example.test", 3,
                        lambda *_args, **_kwargs: Response(b"data"))

    def test_health_http_error_body_can_be_parsed(self):
        error = urllib.error.HTTPError(
            "http://127.0.0.1/health", 503, "unhealthy", {},
            io.BytesIO(json.dumps({"overall_status": "CRITICAL"}).encode()))
        try:
            with mock.patch.object(AGENT.urllib.request, "urlopen", side_effect=error):
                self.assertEqual(
                    AGENT.read_json_url(
                        "http://127.0.0.1/health", allow_http_error=True),
                    {"overall_status": "CRITICAL"})
        finally:
            error.close()

    def test_existing_unlocked_application_lock_is_not_busy(self):
        path = self.root / "app.lock"
        path.touch()
        self.assertFalse(AGENT.lock_is_held(path))
        with path.open("a+") as stream:
            fcntl.flock(stream, fcntl.LOCK_EX | fcntl.LOCK_NB)
            self.assertTrue(AGENT.lock_is_held(path))

    def test_slot_aliases_are_resolved_before_low_level_writer(self):
        active = self.root / "active"
        other = self.root / "other"
        for directory in (active, other):
            directory.mkdir()
        devices = {}
        for name in ("active-boot", "active-root", "other-boot", "other-root"):
            devices[name] = self.root / name
            devices[name].touch()
        (active / "boot").symlink_to(devices["active-boot"])
        (active / "system").symlink_to(devices["active-root"])
        (other / "boot").symlink_to(devices["other-boot"])
        (other / "system").symlink_to(devices["other-root"])
        config = dict(AGENT.DEFAULTS)
        config.update({
            "active_boot": str(active / "boot"), "active_root": str(active / "system"),
            "inactive_boot": str(other / "boot"), "inactive_root": str(other / "system"),
        })
        targets, active_paths = AGENT.slot_paths(config)
        self.assertEqual(targets["boot"], devices["other-boot"].resolve())
        self.assertEqual(targets["root"], devices["other-root"].resolve())
        self.assertEqual(active_paths["boot"], active / "boot")

    def test_current_selector_drives_b_to_a_rotation(self):
        selector = self.root / "autoboot.txt"
        selector.write_text(OTA.render_autoboot(3, 2), encoding="ascii")
        config = dict(AGENT.DEFAULTS)
        config["selector_path"] = str(selector)
        with mock.patch.object(AGENT, "current_boot_state", return_value=(3, 0)):
            self.assertEqual(AGENT.boot_partitions(config), (3, 2))
        with mock.patch.object(AGENT, "current_boot_state", return_value=(2, 1)):
            with self.assertRaisesRegex(AGENT.AgentError, "stable normal boot"):
                AGENT.boot_partitions(config)

    def test_service_must_be_active_beyond_connection_window(self):
        active = mock.Mock(return_value=mock.Mock(returncode=0, stdout="50000000\n"))
        with mock.patch.object(AGENT.time, "monotonic", return_value=150):
            self.assertTrue(AGENT.service_stably_active("tunnel.service", 95, active))
        active.assert_any_call(
            ["systemctl", "is-active", "--quiet", "tunnel.service"],
            stdout=AGENT.subprocess.DEVNULL, stderr=AGENT.subprocess.DEVNULL)

    def test_audio_health_opens_and_writes_pcm_device(self):
        observed = {}
        real_mkstemp = tempfile.mkstemp

        def play(arguments, **_kwargs):
            observed["frames"] = Path(arguments[-1]).read_bytes()
            return mock.Mock(returncode=0)

        with mock.patch.object(AGENT.tempfile, "mkstemp",
                               side_effect=lambda **_kwargs:
                               real_mkstemp(dir=self.root, suffix=".wav")):
            self.assertTrue(AGENT.audio_health(play))
        self.assertGreater(len(observed["frames"]), 44)

    def health_payloads(self, keypad_drops=0, display_drops=0):
        return [
            {"current_state": 1, "sip_registered": 1},
            {"overall_status": "HEALTHY", "checks": {
                "serial_connection": {"status": "HEALTHY"},
                "sip_connection": {"status": "HEALTHY"},
            }},
            {"gauges": {
                "mcu_protocol_version": 2,
                "arduino_i2c_drops_keypad": keypad_drops,
                "arduino_i2c_drops_display": display_drops,
            }},
            {"version": "0.4.0"},
        ]

    def test_collect_health_requires_independent_mcu_liveness(self):
        config = dict(AGENT.DEFAULTS)
        config["maintenance_stable_seconds"] = 95
        with mock.patch.object(AGENT, "read_json_url",
                               side_effect=self.health_payloads()), \
                mock.patch.object(AGENT.Path, "exists", return_value=True), \
                mock.patch.object(AGENT, "filesystem_health", return_value=True), \
                mock.patch.object(AGENT, "service_active", return_value=True), \
                mock.patch.object(AGENT, "service_stably_active", return_value=True), \
                mock.patch.object(AGENT, "audio_health", return_value=True), \
                mock.patch.object(AGENT, "fetch", return_value=b"manifest"):
            self.assertTrue(all(AGENT.collect_health(config).values()))

        with mock.patch.object(AGENT, "read_json_url",
                               side_effect=self.health_payloads(keypad_drops=1)), \
                mock.patch.object(AGENT.Path, "exists", return_value=True), \
                mock.patch.object(AGENT, "filesystem_health", return_value=True), \
                mock.patch.object(AGENT, "service_active", return_value=True), \
                mock.patch.object(AGENT, "service_stably_active", return_value=True), \
                mock.patch.object(AGENT, "audio_health", return_value=True), \
                mock.patch.object(AGENT, "fetch", return_value=b"manifest"):
            checks = AGENT.collect_health(config)
        self.assertFalse(checks["keypad_mcu"])
        self.assertFalse(checks["local_controls"])


if __name__ == "__main__":
    unittest.main()
