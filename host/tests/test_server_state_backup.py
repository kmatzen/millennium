#!/usr/bin/env python3

import importlib.util
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "server_state_backup",
    ROOT / "host/monitoring/millennium_server_state_backup.py")
server_backup = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(server_backup)


class ServerStateBackupTests(unittest.TestCase):
    def test_paths_cannot_escape_home(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with self.assertRaises(SystemExit):
                server_backup.validate_paths(root, ["../secret"])
            with self.assertRaises(SystemExit):
                server_backup.validate_paths(root, ["/etc/shadow"])

    def test_every_required_path_must_exist(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "present").write_text("ok", encoding="utf-8")
            self.assertEqual(server_backup.validate_paths(root, ["present"]), ["present"])
            with self.assertRaises(SystemExit):
                server_backup.validate_paths(root, ["missing"])

    def test_restore_listing_must_cover_every_source(self):
        listing = ".config/doorman/config.yaml\nselfhosted/millennium-updates/manifest.json\n"
        server_backup.validate_listing(
            listing, [".config/doorman", "selfhosted/millennium-updates"])
        with self.assertRaises(SystemExit):
            server_backup.validate_listing(listing, [".ssh/authorized_keys"])


if __name__ == "__main__":
    unittest.main()
