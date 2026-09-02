import json
import os
import stat
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "wifi"))
import millennium_wifi as wifi
import millennium_wifi_helper as helper_module
import provision_wifi


class Result:
    def __init__(self, returncode=0, stdout=""):
        self.returncode = returncode
        self.stdout = stdout
        self.stderr = ""


class WifiTests(unittest.TestCase):
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


if __name__ == "__main__":
    unittest.main()
