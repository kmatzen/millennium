#!/usr/bin/env python3

import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
MODULE = ROOT / "host/os_ota/millennium_os_ota.py"
BUILDER = ROOT / "tools/build_os_release.py"
spec = importlib.util.spec_from_file_location("millennium_os_ota", MODULE)
os_ota = importlib.util.module_from_spec(spec)
spec.loader.exec_module(os_ota)


class OsOtaTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.private = self.root / "private.pem"
        self.public = self.root / "public.pem"
        subprocess.run(["openssl", "genpkey", "-algorithm", "ED25519", "-out",
                        str(self.private)], check=True, stdout=subprocess.DEVNULL)
        subprocess.run(["openssl", "pkey", "-in", str(self.private), "-pubout",
                        "-out", str(self.public)], check=True,
                       stdout=subprocess.DEVNULL)
        self.boot = self.root / "boot.img"
        self.rootfs = self.root / "root.img"
        self.boot.write_bytes((b"boot-image\0" * 257) + b"end")
        self.rootfs.write_bytes((b"root-image\0" * 521) + b"end")

    def tearDown(self):
        self.temporary.cleanup()

    def build(self):
        output = self.root / "release"
        subprocess.run([
            sys.executable, str(BUILDER), "--sequence", "12", "--version",
            "2026.09.0", "--base-url", "https://updates.example/millennium/os",
            "--boot-image", str(self.boot), "--root-image", str(self.rootfs),
            "--layout-id", "zero2w-ab-v1", "--board-model",
            "Raspberry Pi Zero 2 W Rev 1.0", "--architecture", "armv7l",
            "--key-id", "release-2026-08", "--device-groups", "phone-001",
            "--minimum-application-version", "0.4.0", "--minimum-mcu-version",
            "0.4.0", "--persistent-state-schema", "1", "--source-commit",
            "a" * 40, "--private-key", str(self.private), "--output-dir",
            str(output),
        ], check=True, stdout=subprocess.PIPE)
        return output

    @staticmethod
    def expected(**changes):
        value = {
            "channel": "stable", "architecture": "armv7l",
            "layout_id": "zero2w-ab-v1", "installed_sequence": 11,
            "board_model": "Raspberry Pi Zero 2 W Rev 1.0",
            "device_group": "phone-001",
        }
        value.update(changes)
        return value

    def test_signed_manifest_and_both_images_verify(self):
        output = self.build()
        value = os_ota.load_verified_manifest(
            output / "manifest.json", output / "manifest.json.sig", self.public,
            self.expected())
        for name in ("boot", "root"):
            path = next(output.glob(name + "-*.img.gz"))
            os_ota.verify_image(path, value["images"][name])
        self.assertEqual(value["source_commit"], "a" * 40)
        self.assertEqual(value["sequence"], 12)

    def test_manifest_tampering_breaks_signature(self):
        output = self.build()
        with (output / "manifest.json").open("ab") as stream:
            stream.write(b" ")
        with self.assertRaisesRegex(os_ota.OsOtaError, "signature"):
            os_ota.load_verified_manifest(
                output / "manifest.json", output / "manifest.json.sig",
                self.public, self.expected())

    def test_incompatible_board_and_layout_are_rejected(self):
        output = self.build()
        value = json.loads((output / "manifest.json").read_text())
        with self.assertRaisesRegex(os_ota.OsOtaError, "board"):
            os_ota.validate_manifest(value, self.expected(board_model="other"))
        with self.assertRaisesRegex(os_ota.OsOtaError, "layout"):
            os_ota.validate_manifest(value, self.expected(layout_id="other"))

    def test_old_held_and_unselected_releases_are_rejected(self):
        output = self.build()
        value = json.loads((output / "manifest.json").read_text())
        with self.assertRaisesRegex(os_ota.OsOtaError, "not newer"):
            os_ota.validate_manifest(value, self.expected(installed_sequence=12))
        value["rollout"]["hold"] = True
        with self.assertRaisesRegex(os_ota.OsOtaError, "held"):
            os_ota.validate_manifest(value, self.expected())
        value["rollout"]["hold"] = False
        with self.assertRaisesRegex(os_ota.OsOtaError, "group"):
            os_ota.validate_manifest(value, self.expected(device_group="other"))

    def test_compressed_and_expanded_tampering_are_rejected(self):
        output = self.build()
        value = json.loads((output / "manifest.json").read_text())
        boot = next(output.glob("boot-*.img.gz"))
        original = boot.read_bytes()
        boot.write_bytes(original + b"tamper")
        with self.assertRaisesRegex(os_ota.OsOtaError, "compressed"):
            os_ota.verify_image(boot, value["images"]["boot"])
        boot.write_bytes(original)
        value["images"]["boot"]["expanded_sha256"] = "0" * 64
        with self.assertRaisesRegex(os_ota.OsOtaError, "expanded"):
            os_ota.verify_image(boot, value["images"]["boot"])

    def test_https_downloads_stage_atomically_and_verify(self):
        output = self.build()
        manifest = json.loads((output / "manifest.json").read_text())
        payloads = {
            manifest["images"][name]["url"]: next(output.glob(name + "-*.img.gz")).read_bytes()
            for name in ("boot", "root")
        }

        class Response:
            def __init__(self, data):
                self.data = data
                self.offset = 0

            def __enter__(self):
                return self

            def __exit__(self, *unused):
                return False

            def read(self, size):
                value = self.data[self.offset:self.offset + size]
                self.offset += len(value)
                return value

        staged = self.root / "staged"
        result = os_ota.download_verified_images(
            manifest, staged, opener=lambda url, timeout: Response(payloads[url]))
        self.assertEqual(set(result), {"boot", "root"})
        self.assertFalse(list(staged.glob("*.part")))
        for name, path in result.items():
            os_ota.verify_image(path, manifest["images"][name])

    def test_interrupted_or_truncated_download_never_becomes_staged(self):
        output = self.build()
        manifest = json.loads((output / "manifest.json").read_text())
        root_url = manifest["images"]["root"]["url"]
        root_data = next(output.glob("root-*.img.gz")).read_bytes()

        class Interrupted:
            def __enter__(self):
                return self

            def __exit__(self, *unused):
                return False

            def read(self, size):
                raise ConnectionError("simulated network removal")

        staged = self.root / "staged"
        with self.assertRaisesRegex(os_ota.OsOtaError, "download failed"):
            os_ota.download_verified_images(
                manifest, staged, opener=lambda url, timeout: Interrupted())
        self.assertFalse(list(staged.iterdir()))

        class Truncated(Interrupted):
            def __init__(self):
                self.once = True

            def read(self, size):
                if self.once:
                    self.once = False
                    return root_data[:max(1, len(root_data) // 2)]
                return b""

        with self.assertRaisesRegex(os_ota.OsOtaError, "verification failed"):
            os_ota.download_verified_images(
                manifest, staged, opener=lambda url, timeout: Truncated())
        self.assertFalse(list(staged.iterdir()))

    def test_inactive_slots_are_written_synced_and_read_back(self):
        output = self.build()
        value = json.loads((output / "manifest.json").read_text())
        downloads = {name: next(output.glob(name + "-*.img.gz"))
                     for name in ("boot", "root")}
        targets = {name: self.root / ("inactive-" + name)
                   for name in ("boot", "root")}
        active = {name: self.root / ("active-" + name)
                  for name in ("boot", "root")}
        for path in list(targets.values()) + list(active.values()):
            path.write_bytes(b"unused" * 4096)
        journal = self.root / "journal.json"
        transaction = os_ota.write_inactive_images(
            value, downloads, targets, active, journal, allow_regular=True)
        self.assertEqual(transaction["phase"], "candidate-written")
        self.assertEqual(json.loads(journal.read_text())["phase"],
                         "candidate-written")
        self.assertEqual(targets["boot"].read_bytes()[:self.boot.stat().st_size],
                         self.boot.read_bytes())
        self.assertEqual(targets["root"].read_bytes()[:self.rootfs.stat().st_size],
                         self.rootfs.read_bytes())

    def test_active_target_overlap_is_rejected_before_writing(self):
        output = self.build()
        value = json.loads((output / "manifest.json").read_text())
        downloads = {name: next(output.glob(name + "-*.img.gz"))
                     for name in ("boot", "root")}
        shared = self.root / "shared"
        inactive_root = self.root / "inactive-root"
        shared.write_bytes(b"active" * 4096)
        inactive_root.write_bytes(b"inactive" * 4096)
        before = shared.read_bytes()
        with self.assertRaisesRegex(os_ota.OsOtaError, "overlaps active"):
            os_ota.write_inactive_images(
                value, downloads, {"boot": shared, "root": inactive_root},
                {"boot": shared, "root": self.rootfs}, self.root / "journal",
                allow_regular=True)
        self.assertEqual(shared.read_bytes(), before)

    def test_all_downloads_verify_before_first_target_write(self):
        output = self.build()
        value = json.loads((output / "manifest.json").read_text())
        downloads = {name: next(output.glob(name + "-*.img.gz"))
                     for name in ("boot", "root")}
        downloads["boot"].write_bytes(downloads["boot"].read_bytes() + b"bad")
        targets = {name: self.root / ("inactive-" + name)
                   for name in ("boot", "root")}
        active = {name: self.root / ("active-" + name)
                  for name in ("boot", "root")}
        for path in list(targets.values()) + list(active.values()):
            path.write_bytes((str(path) + " sentinel").encode() * 1024)
        before = {name: path.read_bytes() for name, path in targets.items()}
        with self.assertRaisesRegex(os_ota.OsOtaError, "compressed"):
            os_ota.write_inactive_images(
                value, downloads, targets, active, self.root / "journal",
                allow_regular=True)
        self.assertEqual({name: path.read_bytes() for name, path in targets.items()},
                         before)
        self.assertFalse((self.root / "journal").exists())

    def test_production_writer_rejects_regular_files(self):
        target = self.root / "target"
        active = self.root / "active"
        target.write_bytes(b"target")
        active.write_bytes(b"active")
        with self.assertRaisesRegex(os_ota.OsOtaError, "block device"):
            os_ota.validate_inactive_targets(
                {"boot": target, "root": self.rootfs},
                {"boot": active, "root": self.boot})

    def test_target_capacity_is_checked_before_any_write(self):
        output = self.build()
        value = json.loads((output / "manifest.json").read_text())
        downloads = {name: next(output.glob(name + "-*.img.gz"))
                     for name in ("boot", "root")}
        targets = {"boot": self.root / "tiny", "root": self.root / "large"}
        active = {"boot": self.root / "active-boot",
                  "root": self.root / "active-root"}
        targets["boot"].write_bytes(b"tiny")
        targets["root"].write_bytes(b"root sentinel" * 4096)
        active["boot"].write_bytes(b"active boot")
        active["root"].write_bytes(b"active root")
        before = {name: path.read_bytes() for name, path in targets.items()}
        with self.assertRaisesRegex(os_ota.OsOtaError, "smaller"):
            os_ota.write_inactive_images(
                value, downloads, targets, active, self.root / "journal",
                allow_regular=True)
        self.assertEqual({name: path.read_bytes() for name, path in targets.items()},
                         before)

    def tryboot_journal(self):
        journal = self.root / "journal.json"
        os_ota.atomic_json(journal, {
            "schema": 1, "operation": "os-slot-write", "sequence": 12,
            "version": "2026.09.0", "source_commit": "a" * 40,
            "layout_id": "zero2w-ab-v1", "phase": "candidate-written",
        })
        return journal

    def test_device_tree_boot_state_accepts_binary_and_ascii(self):
        binary = self.root / "binary"
        ascii_value = self.root / "ascii"
        binary.write_bytes(bytes.fromhex("00000003"))
        ascii_value.write_bytes(b"1\0")
        self.assertEqual(os_ota.read_bootloader_integer(binary), 3)
        self.assertEqual(os_ota.read_bootloader_integer(ascii_value), 1)
        ascii_value.write_bytes(b"not-a-number")
        with self.assertRaisesRegex(os_ota.OsOtaError, "encoding"):
            os_ota.read_bootloader_integer(ascii_value)

    def test_autoboot_parser_rejects_extra_directives(self):
        value = os_ota.render_autoboot(2, 3)
        self.assertEqual(os_ota.parse_autoboot(value), (2, 3))
        with self.assertRaisesRegex(os_ota.OsOtaError, "unexpected"):
            os_ota.parse_autoboot(value + "gpu_mem=16\n")
        with self.assertRaisesRegex(os_ota.OsOtaError, "overlapping"):
            os_ota.render_autoboot(2, 2)

    def test_tryboot_arms_only_after_durable_candidate(self):
        journal = self.tryboot_journal()
        selector = self.root / "autoboot.txt"
        calls = []

        class Result:
            returncode = 0

        value = os_ota.arm_tryboot(
            selector, journal, 2, 3, reboot=True,
            runner=lambda arguments, check: calls.append(arguments) or Result())
        self.assertEqual(value["phase"], "tryboot-armed")
        self.assertEqual(os_ota.parse_autoboot(selector.read_text()), (2, 3))
        self.assertEqual(calls, [["reboot", "0 tryboot"]])

    def test_tryboot_commit_requires_every_health_check(self):
        journal = self.tryboot_journal()
        selector = self.root / "autoboot.txt"
        os_ota.arm_tryboot(selector, journal, 2, 3)
        checks = {name: True for name in os_ota.REQUIRED_BOOT_HEALTH}
        checks.pop("audio")
        with self.assertRaisesRegex(os_ota.OsOtaError, "incomplete"):
            os_ota.record_boot_health(journal, checks)
        checks["audio"] = False
        with self.assertRaisesRegex(os_ota.OsOtaError, "failed"):
            os_ota.record_boot_health(journal, checks)

    def test_tryboot_commit_swaps_normal_and_candidate(self):
        journal = self.tryboot_journal()
        selector = self.root / "autoboot.txt"
        os_ota.arm_tryboot(selector, journal, 2, 3)
        checks = {name: True for name in os_ota.REQUIRED_BOOT_HEALTH}
        os_ota.record_boot_health(journal, checks)
        with self.assertRaisesRegex(os_ota.OsOtaError, "expected one-shot"):
            os_ota.commit_tryboot(selector, journal, 3, 0)
        value = os_ota.commit_tryboot(selector, journal, 3, 1)
        self.assertEqual(value["phase"], "committed")
        self.assertEqual(os_ota.parse_autoboot(selector.read_text()), (3, 2))

    def test_tryboot_commit_rejects_wrong_partition_or_changed_selector(self):
        journal = self.tryboot_journal()
        selector = self.root / "autoboot.txt"
        os_ota.arm_tryboot(selector, journal, 2, 3)
        checks = {name: True for name in os_ota.REQUIRED_BOOT_HEALTH}
        os_ota.record_boot_health(journal, checks)
        with self.assertRaisesRegex(os_ota.OsOtaError, "expected one-shot"):
            os_ota.commit_tryboot(selector, journal, 2, 1)
        selector.write_text(os_ota.render_autoboot(4, 3))
        with self.assertRaisesRegex(os_ota.OsOtaError, "changed"):
            os_ota.commit_tryboot(selector, journal, 3, 1)

    def test_tryboot_commit_reads_firmware_device_tree_state(self):
        journal = self.tryboot_journal()
        selector = self.root / "autoboot.txt"
        partition = self.root / "partition"
        tryboot = self.root / "tryboot"
        partition.write_bytes(bytes.fromhex("00000003"))
        tryboot.write_bytes(bytes.fromhex("00000001"))
        os_ota.arm_tryboot(selector, journal, 2, 3)
        os_ota.record_boot_health(
            journal, {name: True for name in os_ota.REQUIRED_BOOT_HEALTH})
        value = os_ota.commit_tryboot_from_device_tree(
            selector, journal, partition, tryboot)
        self.assertEqual(value["active_boot_partition"], 3)

    def test_installation_gate_respects_busy_work_and_window(self):
        at_one_am = time.struct_time((2026, 9, 6, 1, 0, 0, 6, 249, -1))
        ready = {name: False for name in os_ota.BUSY_REASONS}
        self.assertTrue(os_ota.installation_gate(
            ready, "00:30", "02:00", at_one_am)["allowed"])
        ready["active_call"] = True
        blocked = os_ota.installation_gate(
            ready, "00:30", "02:00", at_one_am)
        self.assertEqual(blocked["reason"], "device-busy")
        ready["active_call"] = False
        outside = os_ota.installation_gate(
            ready, "02:00", "03:00", at_one_am)
        self.assertEqual(outside["reason"], "outside-maintenance-window")
        self.assertTrue(os_ota.installation_gate(
            ready, "23:00", "02:00", at_one_am)["allowed"])

    def test_failed_os_release_backs_off_quarantines_and_clears(self):
        manifest = json.loads((self.build() / "manifest.json").read_text())
        first = os_ota.record_failure(
            self.root / "state", manifest, "health-failed", now=100,
            base_delay=10, maximum_attempts=2)
        self.assertFalse(first["quarantined"])
        self.assertEqual(os_ota.retry_status(
            self.root / "state", manifest, now=105)["reason"], "backoff")
        self.assertTrue(os_ota.retry_status(
            self.root / "state", manifest, now=110)["allowed"])
        second = os_ota.record_failure(
            self.root / "state", manifest, "health-failed", now=111,
            base_delay=10, maximum_attempts=2)
        self.assertTrue(second["quarantined"])
        self.assertEqual(os_ota.retry_status(
            self.root / "state", manifest, now=999)["reason"], "quarantined")
        os_ota.clear_failure(self.root / "state", manifest)
        self.assertTrue(os_ota.retry_status(
            self.root / "state", manifest, now=999)["allowed"])

    def test_failed_candidate_records_checks_and_never_commits(self):
        output = self.build()
        manifest = json.loads((output / "manifest.json").read_text())
        journal = self.tryboot_journal()
        selector = self.root / "autoboot.txt"
        os_ota.arm_tryboot(selector, journal, 2, 3)
        checks = {name: True for name in os_ota.REQUIRED_BOOT_HEALTH}
        checks["display_mcu"] = False
        failed = os_ota.reject_boot_health(
            journal, checks, self.root / "state", manifest,
            "health-failed", now=100)
        self.assertEqual(failed["phase"], "health-failed")
        self.assertEqual(failed["failed_checks"], ["display_mcu"])
        with self.assertRaisesRegex(os_ota.OsOtaError, "health has not passed"):
            os_ota.commit_tryboot(selector, journal, 3, 1)
        self.assertEqual(os_ota.parse_autoboot(selector.read_text()), (2, 3))

    def test_successful_commit_persists_anti_rollback_sequence(self):
        journal = self.tryboot_journal()
        selector = self.root / "autoboot.txt"
        installed = self.root / "installed-sequence"
        os_ota.arm_tryboot(selector, journal, 2, 3)
        os_ota.record_boot_health(
            journal, {name: True for name in os_ota.REQUIRED_BOOT_HEALTH})
        os_ota.commit_tryboot(selector, journal, 3, 1, installed)
        self.assertEqual(installed.read_text(), "12\n")

    def test_power_loss_before_selector_swap_restores_prior_sequence(self):
        journal = self.tryboot_journal()
        selector = self.root / "autoboot.txt"
        installed = self.root / "installed-sequence"
        installed.write_text("7\n")
        os_ota.arm_tryboot(selector, journal, 2, 3)
        os_ota.record_boot_health(
            journal, {name: True for name in os_ota.REQUIRED_BOOT_HEALTH})
        original = os_ota.atomic_text

        def interrupt(path, value):
            if Path(path) == selector:
                raise OSError("simulated power loss")
            return original(path, value)

        with mock.patch.object(os_ota, "atomic_text", side_effect=interrupt):
            with self.assertRaisesRegex(OSError, "power loss"):
                os_ota.commit_tryboot(selector, journal, 3, 1, installed)
        self.assertEqual(os_ota.read_journal(journal)["phase"], "commit-intent")
        self.assertEqual(installed.read_text(), "12\n")
        value = os_ota.reconcile_commit(selector, journal, 2, 0, installed)
        self.assertEqual(value["phase"], "commit-interrupted")
        self.assertEqual(installed.read_text(), "7\n")
        self.assertEqual(os_ota.parse_autoboot(selector.read_text()), (2, 3))

    def test_power_loss_after_selector_swap_finalizes_commit(self):
        journal = self.tryboot_journal()
        selector = self.root / "autoboot.txt"
        installed = self.root / "installed-sequence"
        installed.write_text("7\n")
        os_ota.arm_tryboot(selector, journal, 2, 3)
        os_ota.record_boot_health(
            journal, {name: True for name in os_ota.REQUIRED_BOOT_HEALTH})
        original = os_ota.atomic_json
        writes = 0

        def interrupt(path, value):
            nonlocal writes
            writes += 1
            if writes == 2:
                raise OSError("simulated power loss")
            return original(path, value)

        with mock.patch.object(os_ota, "atomic_json", side_effect=interrupt):
            with self.assertRaisesRegex(OSError, "power loss"):
                os_ota.commit_tryboot(selector, journal, 3, 1, installed)
        self.assertEqual(os_ota.read_journal(journal)["phase"], "commit-intent")
        self.assertEqual(os_ota.parse_autoboot(selector.read_text()), (3, 2))
        value = os_ota.reconcile_commit(selector, journal, 3, 0, installed)
        self.assertEqual(value["phase"], "committed")
        self.assertTrue(value["reconciled"])
        self.assertEqual(installed.read_text(), "12\n")

    def test_owner_status_does_not_expose_internal_failure_detail(self):
        status = os_ota.owner_safe_status(
            {"phase": "tryboot-armed", "sequence": 12,
             "version": "2026.09.0", "inactive_targets": {"root": "/dev/mmc"}},
            {"attempts": 3, "retry_after": 200, "quarantined": True,
             "error_code": "secret-path-/etc"})
        self.assertEqual(status["state"], "quarantined")
        self.assertTrue(status["action_required"])
        self.assertNotIn("inactive_targets", status)
        self.assertNotIn("error_code", status)

    def test_end_to_end_failed_candidate_falls_back_then_healthy_retry_commits(self):
        output = self.build()
        manifest = json.loads((output / "manifest.json").read_text())
        payloads = {
            manifest["images"][name]["url"]: next(output.glob(name + "-*.img.gz")).read_bytes()
            for name in ("boot", "root")
        }

        class Response:
            def __init__(self, data):
                self.data = data

            def __enter__(self):
                return self

            def __exit__(self, *unused):
                return False

            def read(self, size):
                value, self.data = self.data[:size], self.data[size:]
                return value

        downloads = os_ota.download_verified_images(
            manifest, self.root / "staged",
            opener=lambda url, timeout: Response(payloads[url]))
        targets = {name: self.root / ("inactive-" + name) for name in ("boot", "root")}
        active = {name: self.root / ("active-" + name) for name in ("boot", "root")}
        for path in list(targets.values()) + list(active.values()):
            path.write_bytes(b"old-slot-sentinel" * 4096)
        old = {name: path.read_bytes() for name, path in active.items()}
        journal = self.root / "journal.json"
        selector = self.root / "autoboot.txt"
        os_ota.write_inactive_images(
            manifest, downloads, targets, active, journal, allow_regular=True)
        os_ota.arm_tryboot(selector, journal, 2, 3)
        checks = {name: True for name in os_ota.REQUIRED_BOOT_HEALTH}
        checks["sip"] = False
        os_ota.reject_boot_health(
            journal, checks, self.root / "state", manifest,
            "health-failed", now=100, base_delay=1)
        # Firmware's one-shot flag is gone after the candidate boot; the normal
        # selector and both old active payloads remain unchanged.
        self.assertEqual(os_ota.parse_autoboot(selector.read_text()), (2, 3))
        self.assertEqual({name: path.read_bytes() for name, path in active.items()}, old)

        os_ota.clear_failure(self.root / "state", manifest)
        os_ota.write_inactive_images(
            manifest, downloads, targets, active, journal, allow_regular=True)
        os_ota.arm_tryboot(selector, journal, 2, 3)
        os_ota.record_boot_health(
            journal, {name: True for name in os_ota.REQUIRED_BOOT_HEALTH})
        installed = self.root / "installed-sequence"
        os_ota.commit_tryboot(selector, journal, 3, 1, installed)
        self.assertEqual(os_ota.parse_autoboot(selector.read_text()), (3, 2))
        self.assertEqual(installed.read_text(), "12\n")


if __name__ == "__main__":
    unittest.main()
