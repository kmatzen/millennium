#!/usr/bin/env python3
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
from urllib.parse import urlparse

CONTENT = Path(__file__).parents[1]
sys.path.insert(0, str(CONTENT))
from catalogtool import build_catalog
from experience_agent import ExperienceAgent, ExperienceError
from install_content import atomic_json
from storytool import canonical, package, sign_ed25519

STORY = CONTENT / "stories" / "last_line" / "story.json"


class ExperienceAgentTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.temp = Path(self.temporary.name)
        self.server = self.temp / "server"
        self.server.mkdir()
        self.root = self.temp / "state"
        self.private = self.temp / "private.pem"
        self.public = self.temp / "public.pem"
        subprocess.run(["openssl", "genpkey", "-algorithm", "ED25519", "-out", str(self.private)], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        subprocess.run(["openssl", "pkey", "-in", str(self.private), "-pubout", "-out", str(self.public)], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.manifest = self._package("1.1.0", 1)
        catalog = build_catalog([self.manifest], "https://updates.example/stable/", "stable", 1, "test")
        (self.server / "catalog.json").write_bytes(canonical(catalog))
        sign_ed25519(self.private, self.server / "catalog.json", self.server / "catalog.json.sig")

    def tearDown(self):
        self.temporary.cleanup()

    def _package(self, version, sequence):
        source = self.temp / ("source-" + version)
        source.mkdir()
        shutil.copytree(STORY.parent / "media", source / "media")
        value = json.loads(STORY.read_text())
        value["version"] = version
        value["distribution"]["sequence"] = sequence
        (source / "story.json").write_text(json.dumps(value))
        package(source / "story.json", self.server, self.private, "test")
        return next(self.server.glob("*%s.manifest.json" % version))

    def _fetch(self, url, destination, maximum):
        source = self.server / Path(urlparse(url).path).name
        if source.stat().st_size > maximum:
            raise ExperienceError("too large")
        shutil.copy2(source, destination)
        return source.stat().st_size

    def _agent(self, **kwargs):
        defaults = dict(root=self.root, catalog_url="https://updates.example/catalog.json",
                        catalog_keys={"test": self.public}, package_keys={"test": self.public},
                        device_id="phone-001", daemon_version="0.4.0", fetcher=self._fetch)
        defaults.update(kwargs)
        return ExperienceAgent(**defaults)

    def test_signed_catalog_download_install_and_health_commit(self):
        activated = self._agent().check()
        self.assertEqual(activated, ["last-line"])
        self.assertIn("1.1.0", os.readlink(self.root / "current"))
        self.assertFalse((self.root / "activation-journal.json").exists())
        self.assertEqual(self._agent().status()["last_result"], "healthy")
        self.assertTrue((self.root / "current/package.manifest.json").is_file())

    def test_multiple_catalog_entries_install_without_hijack_and_can_be_selected(self):
        source = self.temp / "source-other"
        source.mkdir()
        shutil.copytree(STORY.parent / "media", source / "media")
        value = json.loads(STORY.read_text())
        value["id"] = "other-story"
        value["version"] = "1.0.0"
        value["distribution"]["sequence"] = 1
        (source / "story.json").write_text(json.dumps(value))
        package(source / "story.json", self.server, self.private, "test")
        other = next(self.server.glob("other-story-*.manifest.json"))
        catalog = build_catalog([self.manifest, other],
                                "https://updates.example/experiences/",
                                "stable", 2, "test")
        (self.server / "catalog.json").write_bytes(canonical(catalog))
        sign_ed25519(self.private, self.server / "catalog.json", self.server / "catalog.json.sig")
        agent = self._agent()
        self.assertEqual(agent.check(), ["last-line", "other-story"])
        self.assertIn("last-line-1.1.0", os.readlink(self.root / "current"))
        self.assertTrue((self.root / "releases/other-story-1.0.0").is_dir())
        agent.select_package("other-story")
        self.assertIn("other-story-1.0.0", os.readlink(self.root / "current"))
        self.assertIn("last-line-1.1.0", os.readlink(self.root / "previous"))

    def test_bad_health_rolls_back_and_quarantines_digest(self):
        self._agent().check()
        previous = os.readlink(self.root / "current")
        newer = self._package("1.2.0", 2)
        catalog = build_catalog([newer], "https://updates.example/stable/", "stable", 2, "test")
        (self.server / "catalog.json").write_bytes(canonical(catalog))
        sign_ed25519(self.private, self.server / "catalog.json", self.server / "catalog.json.sig")
        with self.assertRaisesRegex(ExperienceError, "health gate"):
            self._agent(activation_healthy=lambda: False).check()
        self.assertEqual(os.readlink(self.root / "current"), previous)
        quarantine = json.loads((self.root / "quarantine.json").read_text())
        self.assertIn(catalog["packages"][0]["manifest_sha256"], quarantine)
        self.assertEqual(self._agent().check(), [])

    def test_interrupted_activation_recovers_previous_and_quarantines(self):
        releases = self.root / "releases"
        (releases / "fallback-1.0.0").mkdir(parents=True)
        (releases / "bad-2.0.0").mkdir()
        (releases / "bad-2.0.0" / "package.manifest.json").write_text("{torn")
        (self.root / "current").symlink_to("releases/bad-2.0.0")
        atomic_json(self.root / "activation-journal.json", {"previous": "releases/fallback-1.0.0", "digest": "a" * 64})
        self.assertTrue(self._agent().recover())
        self.assertEqual(os.readlink(self.root / "current"), "releases/fallback-1.0.0")
        self.assertEqual(json.loads((self.root / "experience-status.json").read_text())["last_result"],
                         "rolled_back_interrupted_activation")

    def test_interrupted_migration_restores_namespaced_state(self):
        releases = self.root / "releases"
        (releases / "fallback-1.0.0").mkdir(parents=True)
        (self.root / "current").symlink_to("releases/fallback-1.0.0")
        state_path = self.root / "state" / "last-line" / "current.json"
        before = {"schema": 1, "data": {"visits": 7}}
        atomic_json(state_path, {"schema": 2, "data": {"visits": 7}})
        atomic_json(self.root / "activation-journal.json",
                    {"previous": "releases/fallback-1.0.0", "digest": "b" * 64,
                     "state_path": "state/last-line/current.json", "state_before": before})
        self._agent().recover()
        self.assertEqual(json.loads(state_path.read_text()), before)

    def test_recovery_removes_only_unselectable_staging_directories(self):
        (self.root / ".catalog-abandoned").mkdir(parents=True)
        (self.root / "releases" / ".install-abandoned").mkdir(parents=True)
        (self.root / "releases" / "safe-1.0.0").mkdir()
        self.assertTrue(self._agent().recover())
        self.assertFalse((self.root / ".catalog-abandoned").exists())
        self.assertFalse((self.root / "releases" / ".install-abandoned").exists())
        self.assertTrue((self.root / "releases" / "safe-1.0.0").exists())

    def test_disable_active_package_selects_fallback_and_blocks_updates(self):
        releases = self.root / "releases"
        (releases / "last-line-0.9.0").mkdir(parents=True)
        (releases / "safe-1.0.0").mkdir()
        (self.root / "current").symlink_to("releases/last-line-0.9.0")
        agent = self._agent(fallback="safe-1.0.0")
        result = agent.set_enabled("last-line", False)
        self.assertEqual(os.readlink(self.root / "current"), "releases/safe-1.0.0")
        self.assertEqual(result["disabled"], ["last-line"])
        self.assertEqual(agent.check(), [])

    def test_owner_request_is_consumed_without_network_and_metrics_are_aggregate(self):
        requests = self.root / "owner-requests"
        requests.mkdir(parents=True)
        (requests / "request.json").write_text('{"action":"disable","id":"last-line"}')
        agent = self._agent()
        self.assertTrue(agent.process_owner_request())
        self.assertFalse((requests / "request.json").exists())
        self.assertEqual(json.loads((self.root / "disabled.json").read_text()), ["last-line"])
        self.assertEqual(json.loads((self.root / "metrics.json").read_text()),
                         {"owner_disable": 1})
        published = json.loads((self.root / "owner-status.json").read_text())
        self.assertNotIn("device_id", json.dumps(published))

    def test_owner_request_symlink_is_rejected(self):
        requests = self.root / "owner-requests"
        requests.mkdir(parents=True)
        target = self.root / "untrusted.json"
        target.write_text('{"action":"disable","id":"last-line"}')
        (requests / "request.json").symlink_to(target)
        with self.assertRaises((ExperienceError, OSError)):
            self._agent().process_owner_request()

    def test_denylist_uses_permanent_fallback(self):
        releases = self.root / "releases"
        (releases / "last-line-1.0.0").mkdir(parents=True)
        (releases / "safe-1.0.0").mkdir()
        (self.root / "current").symlink_to("releases/last-line-1.0.0")
        catalog = json.loads((self.server / "catalog.json").read_text())
        catalog["denied"] = ["last-line"]
        agent = self._agent(fallback="safe-1.0.0")
        agent._apply_denylist(catalog)
        self.assertEqual(os.readlink(self.root / "current"), "releases/safe-1.0.0")

    def test_denylist_and_disable_match_exact_hyphenated_id(self):
        releases = self.root / "releases"
        release = releases / "last-line-1.0.0"
        fallback = releases / "safe-1.0.0"
        release.mkdir(parents=True)
        fallback.mkdir()
        (self.root / "current").symlink_to("releases/last-line-1.0.0")
        agent = self._agent(fallback="last-line-1.0.0")
        self.assertEqual(agent._active_package_id(), "last-line")
        agent.fallback = "safe-1.0.0"
        agent._apply_denylist({"denied": ["last"]})
        self.assertEqual(os.readlink(self.root / "current"), "releases/last-line-1.0.0")
        agent.set_enabled("last", False)
        self.assertEqual(os.readlink(self.root / "current"), "releases/last-line-1.0.0")

    def test_busy_or_unhealthy_phone_defers_activation(self):
        self.assertEqual(self._agent(idle=lambda: False).check(), [])
        self.assertFalse((self.root / "current").exists())
        shutil.rmtree(self.root)
        self.assertEqual(self._agent(hardware_healthy=lambda: False).check(), [])
        shutil.rmtree(self.root)
        self.assertEqual(self._agent(allowed_time=lambda: False).check(), [])
        self.assertEqual(self._agent(allowed_time=lambda: False).status()["last_result"],
                         "deferred_maintenance_window")

    def test_catalog_rollback_and_manifest_substitution_are_rejected(self):
        atomic_json(self.root / "catalog-state.json", {"sequence": 2})
        with self.assertRaisesRegex(ExperienceError, "catalog sequence rollback"):
            self._agent().check()
        shutil.rmtree(self.root)
        catalog = json.loads((self.server / "catalog.json").read_text())
        catalog["packages"][0]["manifest_sha256"] = "0" * 64
        (self.server / "catalog.json").write_bytes(canonical(catalog))
        sign_ed25519(self.private, self.server / "catalog.json", self.server / "catalog.json.sig")
        with self.assertRaisesRegex(ExperienceError, "manifest digest"):
            self._agent().check()

    def test_same_catalog_sequence_cannot_change_identity(self):
        self._agent().check()
        catalog = json.loads((self.server / "catalog.json").read_text())
        catalog["packages"][0]["rollout"]["hold"] = True
        (self.server / "catalog.json").write_bytes(canonical(catalog))
        sign_ed25519(self.private, self.server / "catalog.json", self.server / "catalog.json.sig")
        with self.assertRaisesRegex(ExperienceError, "identity changed"):
            self._agent().check()

    def test_gc_never_removes_current_previous_or_fallback(self):
        releases = self.root / "releases"
        for name in ("current-3.0.0", "previous-2.0.0", "safe-1.0.0", "old-1.0.0"):
            (releases / name).mkdir(parents=True)
        (self.root / "current").symlink_to("releases/current-3.0.0")
        (self.root / "previous").symlink_to("releases/previous-2.0.0")
        removed = self._agent(fallback="safe-1.0.0", max_releases=3).collect_garbage()
        self.assertEqual(removed, ["old-1.0.0"])

    def test_network_loss_during_staging_preserves_known_good(self):
        releases = self.root / "releases"
        (releases / "safe-1.0.0").mkdir(parents=True)
        (self.root / "current").symlink_to("releases/safe-1.0.0")
        calls = 0
        def interrupted(url, destination, maximum):
            nonlocal calls
            calls += 1
            if calls == 3:
                destination.write_bytes(b"partial")
                raise OSError("network removed")
            return self._fetch(url, destination, maximum)
        with self.assertRaisesRegex(OSError, "network removed"):
            self._agent(fetcher=interrupted, fallback="safe-1.0.0").check()
        self.assertEqual(os.readlink(self.root / "current"), "releases/safe-1.0.0")
        self.assertTrue((releases / "safe-1.0.0").is_dir())

    def test_package_signature_failure_preserves_known_good(self):
        releases = self.root / "releases"
        (releases / "safe-1.0.0").mkdir(parents=True)
        (self.root / "current").symlink_to("releases/safe-1.0.0")
        Path(str(self.manifest) + ".sig").write_bytes(b"invalid")
        with self.assertRaisesRegex(Exception, "signature"):
            self._agent(fallback="safe-1.0.0").check()
        self.assertEqual(os.readlink(self.root / "current"), "releases/safe-1.0.0")

    def test_withdrawal_prevents_install_and_cleanup_failure_preserves_current(self):
        catalog = json.loads((self.server / "catalog.json").read_text())
        catalog["withdrawn"] = [catalog["packages"][0]["manifest_sha256"]]
        (self.server / "catalog.json").write_bytes(canonical(catalog))
        sign_ed25519(self.private, self.server / "catalog.json", self.server / "catalog.json.sig")
        self.assertEqual(self._agent().check(), [])
        releases = self.root / "releases"
        for name in ("safe-1.0.0", "previous-0.9.0", "old-0.1.0"):
            (releases / name).mkdir(parents=True, exist_ok=True)
        (self.root / "current").symlink_to("releases/safe-1.0.0")
        (self.root / "previous").symlink_to("releases/previous-0.9.0")
        with mock.patch("experience_agent.shutil.rmtree", side_effect=OSError("power loss")):
            with self.assertRaisesRegex(OSError, "power loss"):
                self._agent(fallback="safe-1.0.0", max_releases=2).collect_garbage()
        self.assertEqual(os.readlink(self.root / "current"), "releases/safe-1.0.0")

    def test_failed_first_activation_leaves_no_bad_current(self):
        with self.assertRaisesRegex(ExperienceError, "health gate"):
            self._agent(activation_healthy=lambda: False).check()
        self.assertFalse((self.root / "current").exists())

    def test_disk_full_install_preserves_known_good_and_quarantines_candidate(self):
        releases = self.root / "releases"
        (releases / "safe-1.0.0").mkdir(parents=True)
        (self.root / "current").symlink_to("releases/safe-1.0.0")
        with mock.patch("experience_agent.install", side_effect=OSError(28, "No space left on device")):
            with self.assertRaisesRegex(OSError, "No space"):
                self._agent(fallback="safe-1.0.0").check()
        self.assertEqual(os.readlink(self.root / "current"), "releases/safe-1.0.0")
        self.assertEqual(len(json.loads((self.root / "quarantine.json").read_text())), 1)


if __name__ == "__main__":
    unittest.main()
