#!/usr/bin/env python3

import copy
import importlib.util
from pathlib import Path
import os
import tempfile
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
        self.assertIn("LABEL=PERSISTENT /persistent ext4", first)
        self.assertIn("/persistent/etc/NetworkManager/system-connections", first)
        tmpfiles = persistent.render_tmpfiles(self.value)
        self.assertIn("/etc/ssh/ssh_host_ed25519_key 0600", tmpfiles)
        self.assertIn("/home/millennium/.ssh 0700 1000 1000", tmpfiles)
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
                       "/home/millennium/.ssh"):
            value = copy.deepcopy(self.value)
            next(entry for entry in value["entries"]
                 if entry["target"] == target)["mode"] = "0755"
            with self.assertRaisesRegex(persistent.PersistentStateError, "too broad"):
                persistent.validate(value)

    def make_staging(self, root):
        staging = root / "staging"
        destination = root / "persist"
        staging.mkdir()
        destination.mkdir()
        for entry in self.value["entries"]:
            source = staging / entry["target"].lstrip("/")
            source.parent.mkdir(parents=True, exist_ok=True)
            if entry["kind"] == "directory":
                source.mkdir(exist_ok=True)
                (source / "sample").write_text(entry["target"], encoding="utf-8")
            else:
                source.write_text(entry["target"], encoding="utf-8")
        return staging, destination

    def test_seed_and_verify_copies_only_allowlisted_state(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            staging, destination = self.make_staging(root)
            unrelated = staging / "opt/millennium/current/daemon"
            unrelated.parent.mkdir(parents=True)
            unrelated.write_text("replaceable", encoding="utf-8")
            record = persistent.seed_persistent_state(
                self.value, staging, destination, apply_ownership=False)
            self.assertEqual(len(record["entries"]), len(self.value["entries"]))
            self.assertFalse((destination / "opt").exists())
            persistent.verify_persistent_state(
                self.value, destination, check_ownership=False)

    def test_seed_refuses_nonempty_destination(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            staging, destination = self.make_staging(root)
            (destination / "unexpected").write_text("data")
            with self.assertRaisesRegex(persistent.PersistentStateError, "not empty"):
                persistent.seed_persistent_state(
                    self.value, staging, destination, apply_ownership=False)

    def test_seed_rejects_escaping_symlink(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            staging, destination = self.make_staging(root)
            source = staging / "home/millennium/.ssh"
            (source / "escape").symlink_to("/etc/passwd")
            with self.assertRaisesRegex(persistent.PersistentStateError, "escapes"):
                persistent.seed_persistent_state(
                    self.value, staging, destination, apply_ownership=False)

    def test_verify_detects_content_and_mode_changes(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            staging, destination = self.make_staging(root)
            persistent.seed_persistent_state(
                self.value, staging, destination, apply_ownership=False)
            target = destination / "identity/machine-id"
            os.chmod(target, 0o644)
            target.write_text("changed", encoding="utf-8")
            os.chmod(target, 0o444)
            with self.assertRaisesRegex(persistent.PersistentStateError, "content"):
                persistent.verify_persistent_state(
                    self.value, destination, check_ownership=False)
            # Reseed a clean destination to test the declared top-level mode.
            clean = root / "clean"
            clean.mkdir()
            persistent.seed_persistent_state(
                self.value, staging, clean, apply_ownership=False)
            os.chmod(clean / "identity/machine-id", 0o600)
            with self.assertRaisesRegex(persistent.PersistentStateError, "mode"):
                persistent.verify_persistent_state(
                    self.value, clean, check_ownership=False)


if __name__ == "__main__":
    unittest.main()
