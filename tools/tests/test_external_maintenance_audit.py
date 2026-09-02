#!/usr/bin/env python3

import importlib.util
import json
import os
from argparse import Namespace
from pathlib import Path
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "external_maintenance_audit", ROOT / "tools/external_maintenance_audit.py")
audit = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(audit)


class ExternalMaintenanceAuditTests(unittest.TestCase):
    def arguments(self, root):
        return Namespace(
            ssh=Path("ssh"), identity=None, jump_host="anima",
            jump_user=None, jump_port=None, server_id="anima",
            secret=root / "secret", baseline=root / "baseline.json",
            output=root / "audit.json", device_id="phone-001",
            phone_user="matzen", phone_port=22022,
            network_description="cellular hotspot")

    def make_secret(self, root):
        path = root / "secret"
        path.write_bytes(b"s" * 32)
        os.chmod(path, 0o600)
        return path

    def test_secret_permissions_are_enforced(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "secret"
            path.write_bytes(b"s" * 32)
            os.chmod(path, 0o644)
            with self.assertRaises(SystemExit):
                audit.secret(path)

    def test_same_home_source_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            secret = self.make_secret(root)
            value = audit.secret(secret)
            args = self.arguments(root)
            args.baseline.write_text(json.dumps({
                "schema": 1, "operation": "home-network-baseline",
                "server_id": "anima", "secret_sha256": audit.hashlib.sha256(value).hexdigest(),
                "source_address_hmac_sha256": audit.peer_hmac(value, "198.51.100.1"),
            }), encoding="utf-8")
            with mock.patch.object(audit, "observed_peer", return_value="198.51.100.1"):
                with self.assertRaises(SystemExit):
                    audit.audit(args)

    def test_external_hardware_backed_probe_is_recorded_without_raw_ip(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            secret = self.make_secret(root)
            value = audit.secret(secret)
            args = self.arguments(root)
            args.baseline.write_text(json.dumps({
                "schema": 1, "operation": "home-network-baseline",
                "server_id": "anima", "secret_sha256": audit.hashlib.sha256(value).hexdigest(),
                "source_address_hmac_sha256": audit.peer_hmac(value, "198.51.100.1"),
            }), encoding="utf-8")
            output = "\n".join((
                "raspberrypi", "/opt/millennium/releases/00000009-0.4.0",
                "/var/lib/millennium/content/releases/last-line-2.1.0", "active",
                "health_overall_status 0.00",
                "health_check_serial_connection_status 0.00",
                "health_check_sip_connection_status 0.00",
            ))
            result = mock.Mock(stdout=output, stderr="Offering public key: ED25519-SK")
            with mock.patch.object(audit, "observed_peer", return_value="203.0.113.9"), \
                    mock.patch.object(audit, "ssh_run", return_value=result):
                audit.audit(args)
            record = json.loads(args.output.read_text(encoding="utf-8"))
            self.assertTrue(record["passed"])
            self.assertNotIn("203.0.113.9", args.output.read_text(encoding="utf-8"))
            self.assertTrue(record["hardware_backed_authentication"])


if __name__ == "__main__":
    unittest.main()
