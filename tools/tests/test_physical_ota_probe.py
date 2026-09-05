#!/usr/bin/env python3

import importlib.util
from pathlib import Path
import socket
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "physical_ota_download_probe", ROOT / "tools/physical_ota_download_probe.py")
probe = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(probe)


class PhysicalOtaProbeTests(unittest.TestCase):
    def test_relay_url_preserves_https_hostname_and_path(self):
        self.assertEqual(
            probe.relay_url("https://updates.kmatzen.com/releases/a?x=1", 8444),
            "https://updates.kmatzen.com:8444/releases/a?x=1")

    def test_relay_url_rejects_non_https_source(self):
        with self.assertRaises(ValueError):
            probe.relay_url("http://updates.kmatzen.com/release", 8444)

    def test_resolution_override_is_scoped_to_ota_host_and_port(self):
        calls = []

        def resolved(host, port, *args, **kwargs):
            calls.append((host, port))
            return [(socket.AF_INET, socket.SOCK_STREAM, 6, "", (host, port))]

        with mock.patch.object(socket, "getaddrinfo", side_effect=resolved):
            with probe.RelayResolution("updates.kmatzen.com", "192.168.8.239", 8444):
                socket.getaddrinfo("updates.kmatzen.com", 8444)
                socket.getaddrinfo("maintenance.kmatzen.com", 8444)
                socket.getaddrinfo("updates.kmatzen.com", 443)
        self.assertEqual(calls, [
            ("192.168.8.239", 8444),
            ("maintenance.kmatzen.com", 8444),
            ("updates.kmatzen.com", 443),
        ])


if __name__ == "__main__":
    unittest.main()
