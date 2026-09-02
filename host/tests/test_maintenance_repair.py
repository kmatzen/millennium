#!/usr/bin/env python3

import importlib.util
import os
from pathlib import Path
import tempfile
import unittest


MODULE = Path(__file__).resolve().parents[1] / "ota/repair_maintenance_access.py"
spec = importlib.util.spec_from_file_location("maintenance_repair", MODULE)
repair = importlib.util.module_from_spec(spec)
spec.loader.exec_module(repair)


class MaintenanceRepairTests(unittest.TestCase):
    def test_key_parser_rejects_multiple_lines_and_non_ed25519(self):
        with tempfile.TemporaryDirectory() as name:
            path = Path(name) / "key.pub"
            path.write_text("ssh-rsa AAAA bad\n")
            with self.assertRaisesRegex(ValueError, "Ed25519"):
                repair.read_key(path)
            path.write_text("ssh-ed25519 AAAA one\nssh-ed25519 BBBB two\n")
            with self.assertRaisesRegex(ValueError, "one line"):
                repair.read_key(path)

    def test_authorization_is_atomic_additive_and_idempotent(self):
        with tempfile.TemporaryDirectory() as name:
            path = Path(name) / ".ssh/authorized_keys"
            path.parent.mkdir()
            original = "ssh-ed25519 AAAA existing"
            candidate = "sk-ssh-ed25519@openssh.com BBBB hardware"
            path.write_text(original + "\n")
            uid, gid = os.getuid(), os.getgid()
            self.assertTrue(repair.atomic_authorize(path, candidate, uid, gid))
            self.assertFalse(repair.atomic_authorize(path, candidate, uid, gid))
            self.assertEqual(path.read_text().splitlines(), [original, candidate])
            self.assertEqual(path.stat().st_mode & 0o777, 0o600)


if __name__ == "__main__":
    unittest.main()
