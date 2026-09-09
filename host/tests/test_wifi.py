import json
import io
import os
import stat
import sys
import tempfile
import urllib.error
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "wifi"))
import millennium_wifi as wifi
import millennium_wifi_helper as helper_module
import provision_wifi
import millennium_wifi_portal as portal_module


class Result:
    def __init__(self, returncode=0, stdout=""):
        self.returncode = returncode
        self.stdout = stdout
        self.stderr = ""


class WifiTests(unittest.TestCase):
    def test_setup_restart_cycles_stale_active_ap(self):
        commands = []

        def run(arguments, **unused):
            commands.append(arguments)
            return Result()

        with tempfile.TemporaryDirectory() as directory:
            manager = wifi.NetworkManager(run=run, profile_dir=directory)
            manager.start_setup("phone-001", "password1")

        self.assertEqual(commands[-2][-3:],
                         ["connection", "down", wifi.SETUP_CONNECTION])
        self.assertEqual(commands[-1][-3:],
                         ["connection", "up", wifi.SETUP_CONNECTION])

    def test_validate_hostile_ssids_without_interpolation(self):
        for ssid in ("Home Wi-Fi", "café", 'quote"semi;colon', "$(touch /tmp/nope)"):
            request = {"ssid": ssid, "security": "wpa-psk",
                       "passphrase": "correct horse", "hidden": False}
            profile = wifi.owner_keyfile(request)
            self.assertIn("ssid=" + wifi.ssid_bytes(ssid), profile)
            self.assertNotIn("ssid=" + ssid, profile)

    def test_rejects_control_oversize_and_bad_password(self):
        base = {"ssid": "home", "security": "wpa-psk", "passphrase": "password1", "hidden": False}
        for field, value in (("ssid", "bad\nname"), ("ssid", "x" * 33),
                             ("passphrase", "short"), ("hidden", 1)):
            request = dict(base); request[field] = value
            with self.assertRaises(wifi.WifiError):
                wifi.validate_request(request)

    def test_open_network_requires_explicit_empty_password(self):
        request = {"ssid": "guest", "security": "open", "passphrase": "", "hidden": False}
        profile = wifi.owner_keyfile(request)
        self.assertNotIn("[wifi-security]", profile)
        request["passphrase"] = "secret123"
        with self.assertRaises(wifi.WifiError):
            wifi.owner_keyfile(request)

    def test_nmcli_never_receives_owner_credentials(self):
        calls = []
        def run(arguments, **unused):
            calls.append(arguments)
            return Result()
        with tempfile.TemporaryDirectory() as directory:
            manager = wifi.NetworkManager(run=run, profile_dir=directory)
            request = {"ssid": "private network", "security": "wpa-psk",
                       "passphrase": "not-in-argv", "hidden": False}
            self.assertTrue(manager.apply_owner(request))
            flattened = " ".join(item for call in calls for item in call)
            self.assertNotIn("private network", flattened)
            self.assertNotIn("not-in-argv", flattened)
            mode = stat.S_IMODE(os.stat(Path(directory) / "millennium-owner.nmconnection").st_mode)
            self.assertEqual(mode, 0o600)

    def test_failed_candidate_restores_last_good_profile(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "millennium-owner.nmconnection"
            path.write_text("old-profile", encoding="utf-8")
            manager = wifi.NetworkManager(run=lambda *args, **kwargs: Result(), profile_dir=directory)
            request = {"ssid": "new", "security": "wpa-psk", "passphrase": "password1", "hidden": False}
            manager.apply_owner(request)
            self.assertNotEqual(path.read_text(), "old-profile")
            manager.restore_owner()
            self.assertEqual(path.read_text(), "old-profile")

    def test_owner_activation_retries_ap_to_station_transition(self):
        attempts = []

        def run(arguments, **unused):
            if arguments[-3:] == ["connection", "up", wifi.OWNER_CONNECTION]:
                attempts.append(arguments)
                return Result(returncode=10 if len(attempts) < 3 else 0)
            return Result()

        sleeps = []
        with tempfile.TemporaryDirectory() as directory:
            manager = wifi.NetworkManager(run=run, profile_dir=directory,
                                          sleep=sleeps.append)
            connected = manager.apply_owner({
                "ssid": "home", "security": "wpa-psk",
                "passphrase": "password1", "hidden": False,
            })

        self.assertTrue(connected)
        self.assertEqual(len(attempts), 3)
        self.assertEqual(sleeps, [1, 1])

    def test_owner_activation_retry_is_bounded(self):
        attempts = []

        def run(arguments, **unused):
            if arguments[-3:] == ["connection", "up", wifi.OWNER_CONNECTION]:
                attempts.append(arguments)
                return Result(returncode=10)
            return Result()

        with tempfile.TemporaryDirectory() as directory:
            manager = wifi.NetworkManager(run=run, profile_dir=directory,
                                          sleep=lambda unused: None)
            connected = manager.apply_owner({
                "ssid": "home", "security": "wpa-psk",
                "passphrase": "password1", "hidden": False,
            })

        self.assertFalse(connected)
        self.assertEqual(len(attempts), 20)

    def test_owner_activation_covers_measured_zero2w_ap_handoff(self):
        attempts = []

        def run(arguments, **unused):
            if arguments[-3:] == ["connection", "up", wifi.OWNER_CONNECTION]:
                attempts.append(arguments)
                return Result(returncode=10 if len(attempts) < 9 else 0)
            return Result()

        sleeps = []
        with tempfile.TemporaryDirectory() as directory:
            manager = wifi.NetworkManager(run=run, profile_dir=directory,
                                          sleep=sleeps.append)
            connected = manager.apply_owner({
                "ssid": "home", "security": "wpa-psk",
                "passphrase": "password1", "hidden": False,
            })

        self.assertTrue(connected)
        self.assertEqual(len(attempts), 9)
        self.assertEqual(sleeps, [1] * 8)

    def test_scan_deduplicates_and_sorts(self):
        output = "weak:WPA2:20\nstrong:WPA2:90\nstrong:WPA2:80\n:--:100\n"
        manager = wifi.NetworkManager(run=lambda *args, **kwargs: Result(stdout=output))
        self.assertEqual([item["ssid"] for item in manager.scan()], ["strong", "weak"])

    def test_factory_handoff_qr_escapes_fields(self):
        qr = provision_wifi.wifi_qr("semi;colon", "pass:word")
        self.assertEqual(qr, r"WIFI:T:WPA;S:semi\;colon;P:pass\:word;;")

    def test_helper_rejects_unknown_shape(self):
        instance = helper_module.Helper(mock.Mock(), tempfile.mkdtemp())
        with self.assertRaises(wifi.WifiError):
            instance.dispatch({"action": "scan", "extra": True})

    def test_json_line_has_hard_limit(self):
        import io
        with self.assertRaises(wifi.WifiError):
            wifi.read_json_line(io.BytesIO(b"x" * (wifi.MAX_REQUEST + 1) + b"\n"))

    def test_setup_password_is_readable_and_random_shaped(self):
        first = wifi.generate_setup_password()
        second = wifi.generate_setup_password()
        self.assertRegex(first, r"^[A-Z2-9]{4}(?:-[A-Z2-9]{4}){3}$")
        self.assertNotEqual(first, second)

    def test_hidden_network_profile_is_explicit(self):
        profile = wifi.owner_keyfile({"ssid": "not broadcast", "security": "wpa-psk",
                                      "passphrase": "hidden-secret", "hidden": True})
        self.assertIn("hidden=true", profile)
        self.assertIn("ssid=110;111;116;32;98;114;111;97;100;99;97;115;116;", profile)

    def test_atomic_profile_save_preserves_old_file_if_replace_is_interrupted(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "owner.nmconnection"
            path.write_text("known-good", encoding="utf-8")
            with mock.patch.object(wifi.os, "replace", side_effect=OSError("simulated power loss")):
                with self.assertRaisesRegex(OSError, "simulated power loss"):
                    wifi.write_secret(path, "candidate")
            self.assertEqual(path.read_text(encoding="utf-8"), "known-good")
            self.assertEqual(list(Path(directory).glob("owner.nmconnection.*")), [])

    def test_wrong_password_rolls_back_and_restores_setup_ap(self):
        manager = mock.Mock()
        manager.apply_owner.return_value = False
        with tempfile.TemporaryDirectory() as directory:
            helper = helper_module.Helper(manager, directory)
            helper.lock.acquire()
            helper._apply({"ssid": "home", "security": "wpa-psk",
                           "passphrase": "wrong-password", "hidden": False})
            status = json.loads((Path(directory) / "status.json").read_text())
        self.assertEqual(status["state"], "failed")
        self.assertIn("rejected", status["message"])
        manager.restore_owner.assert_called_once_with()
        manager.restore_setup.assert_called_once_with()
        self.assertFalse(helper.lock.locked())

    def test_connectivity_probe_is_bounded_including_dns(self):
        calls = []

        def run(arguments, **kwargs):
            calls.append((arguments, kwargs))
            return Result(stdout="200 1234")

        self.assertTrue(wifi.connectivity_ok(run=run))
        arguments, kwargs = calls[0]
        self.assertIn("--connect-timeout", arguments)
        self.assertIn("--max-time", arguments)
        self.assertEqual(kwargs["timeout"], 12)

        def hangs(*unused_args, **unused_kwargs):
            raise wifi.subprocess.TimeoutExpired("curl", 12)

        self.assertFalse(wifi.connectivity_ok(run=hangs))

    def test_success_commits_owner_and_stops_setup_services(self):
        manager = mock.Mock()
        manager.apply_owner.return_value = True
        with tempfile.TemporaryDirectory() as directory:
            helper = helper_module.Helper(manager, directory)
            helper.lock.acquire()
            with mock.patch.object(helper_module, "connectivity_ok", return_value=True), \
                    mock.patch.object(helper_module.subprocess, "run") as run, \
                    mock.patch.object(helper_module.Path, "unlink"):
                helper._apply({"ssid": "home", "security": "wpa-psk",
                               "passphrase": "correct-password", "hidden": False})
            status = json.loads((Path(directory) / "status.json").read_text())
            self.assertTrue((Path(directory) / "owner-network-configured").exists())
        self.assertEqual(status["state"], "connected")
        self.assertIn("--no-block", run.call_args_list[-1].args[0])
        self.assertIn("millennium-wifi-portal.service", run.call_args_list[-1].args[0])
        manager.restore_owner.assert_not_called()
        manager.restore_setup.assert_not_called()
        self.assertFalse(helper.lock.locked())

    def test_radio_failure_is_reported_without_credentials(self):
        def failed_radio(arguments, **unused):
            raise wifi.subprocess.CalledProcessError(10, arguments, stderr="radio unavailable")
        manager = wifi.NetworkManager(run=failed_radio)
        with self.assertRaises(wifi.subprocess.CalledProcessError) as raised:
            manager.scan()
        self.assertNotIn("password", str(raised.exception).lower())

    def test_captive_portal_probe_routes_cover_major_platforms(self):
        for path in ("/generate_204", "/gen_204", "/ncsi.txt", "/hotspot-detect.html"):
            handler = object.__new__(portal_module.Portal)
            handler.path = path
            handler.headers = {"Host": "10.42.0.1"}
            handler.client_address = ("10.42.0.2", 12345)
            handler.send_response = mock.Mock()
            handler.send_header = mock.Mock()
            handler.end_headers = mock.Mock()
            handler.send_page = mock.Mock()
            portal_module.Portal.do_GET(handler)
            if path == "/hotspot-detect.html":
                handler.send_page.assert_called_once_with()
            else:
                handler.send_response.assert_called_once_with(302)
                handler.send_header.assert_any_call("Location", "/")

    def test_portal_page_renders_real_template(self):
        handler = object.__new__(portal_module.Portal)
        handler.session = mock.Mock(return_value=("csrf-token", True))
        handler.send_response = mock.Mock()
        handler.send_header = mock.Mock()
        handler.end_headers = mock.Mock()
        handler.wfile = io.BytesIO()
        with mock.patch.object(portal_module, "helper_request",
                               return_value={"networks": [{"ssid": "Home & Work"}]}):
            handler.send_page()
        page = handler.wfile.getvalue().decode()
        self.assertIn('name=csrf value="csrf-token"', page)
        self.assertIn('value="Home &amp; Work"', page)
        self.assertIn("body{font:17px system-ui", page)

    def test_physical_client_evidence_requires_matching_browser_and_probe(self):
        portal_module.PROBE_OBSERVATIONS.clear()
        portal_module.record_probe("10.42.0.2", "/generate_204")
        handler = object.__new__(portal_module.Portal)
        handler.path = "/acceptance.json?platform=android"
        handler.headers = {"Host": "10.42.0.1", "User-Agent": "Mozilla/5.0 Android 14"}
        handler.client_address = ("10.42.0.2", 12345)
        handler.wfile = io.BytesIO()
        handler.send_response = mock.Mock()
        handler.send_header = mock.Mock()
        handler.end_headers = mock.Mock()
        handler.send_error = mock.Mock()
        portal_module.Portal.do_GET(handler)
        value = json.loads(handler.wfile.getvalue())
        self.assertEqual(value["platform"], "android")
        self.assertEqual(value["captive_probe_path"], "/generate_204")
        self.assertFalse(value["client_address_stored"])
        self.assertFalse(value["user_agent_stored"])

    def test_physical_client_evidence_rejects_claimed_platform_mismatch(self):
        portal_module.PROBE_OBSERVATIONS.clear()
        portal_module.record_probe("10.42.0.2", "/generate_204")
        handler = object.__new__(portal_module.Portal)
        handler.path = "/acceptance.json?platform=ios"
        handler.headers = {"Host": "10.42.0.1", "User-Agent": "Mozilla/5.0 Android 14"}
        handler.client_address = ("10.42.0.2", 12345)
        handler.send_error = mock.Mock()
        portal_module.Portal.do_GET(handler)
        handler.send_error.assert_called_once()


if __name__ == "__main__":
    unittest.main()
