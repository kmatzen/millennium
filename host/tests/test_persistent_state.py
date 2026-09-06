#!/usr/bin/env python3

import copy
import importlib.util
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
MODULE = ROOT / "host/os_ota/persistent_state.py"
CONTRACT = ROOT / "host/os_ota/persistent-state.json"
spec = importlib.util.spec_from_file_location("persistent_state", MODULE)
persistent = importlib.util.module_from_spec(spec)
spec.loader.exec_module(persistent)


class PersistentStateTests(unittest.TestCase):
    def setUp(self):
        self.value = persistent.load(CONTRACT)

    def test_production_contract_is_complete_and_renders_deterministically(self):
        persistent.validate(self.value)
        first = persistent.render_fstab(self.value)
        second = persistent.render_fstab(self.value)
        self.assertEqual(first, second)
        self.assertIn("LABEL=MILLENNIUM-DATA /persist ext4", first)
        self.assertIn("/persist/etc/NetworkManager/system-connections", first)
        tmpfiles = persistent.render_tmpfiles(self.value)
        self.assertIn("/etc/ssh/ssh_host_ed25519_key 0600", tmpfiles)
        self.assertIn("/home/matzen/.ssh 0700 1000 1000", tmpfiles)
        self.assertIn("/etc/millennium 0750 0 1000", tmpfiles)
        self.assertIn("/var/lib/millennium 0700 1000 1000", tmpfiles)

    def test_missing_identity_or_state_target_is_rejected(self):
        value = copy.deepcopy(self.value)
        value["entries"] = [entry for entry in value["entries"]
                            if entry["target"] != "/etc/machine-id"]
        with self.assertRaisesRegex(persistent.PersistentStateError, "mismatch"):
            persistent.validate(value)

    def test_path_traversal_and_duplicate_target_are_rejected(self):
        value = copy.deepcopy(self.value)
        value["entries"][0]["source"] = "../escape"
        with self.assertRaisesRegex(persistent.PersistentStateError, "unsafe"):
            persistent.validate(value)
        value = copy.deepcopy(self.value)
        value["entries"][1]["target"] = value["entries"][0]["target"]
        with self.assertRaisesRegex(persistent.PersistentStateError, "duplicate"):
            persistent.validate(value)

    def test_replaceable_software_paths_are_rejected(self):
        value = copy.deepcopy(self.value)
        value["entries"][0]["target"] = "/opt/millennium/current"
        with self.assertRaisesRegex(persistent.PersistentStateError, "replaceable"):
            persistent.validate(value)

    def test_permissive_secret_modes_are_rejected(self):
        for target in ("/etc/ssh/ssh_host_ed25519_key",
                       "/etc/NetworkManager/system-connections",
                       "/home/matzen/.ssh"):
            value = copy.deepcopy(self.value)
            next(entry for entry in value["entries"]
                 if entry["target"] == target)["mode"] = "0755"
            with self.assertRaisesRegex(persistent.PersistentStateError, "too broad"):
                persistent.validate(value)


if __name__ == "__main__":
    unittest.main()
