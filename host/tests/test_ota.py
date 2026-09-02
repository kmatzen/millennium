#!/usr/bin/env python3

import importlib.util
import io
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
WORKER_PATH = ROOT / "host/ota/millennium_ota.py"
BUILDER = ROOT / "tools/build_ota_release.py"
PUBLISHER = ROOT / "tools/publish_ota_release.sh"
spec = importlib.util.spec_from_file_location("millennium_ota", WORKER_PATH)
ota = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ota)


def write_identity_hex(path, role, version="0.4.0", build="test-build"):
    payload = ("MILLENNIUM role=%s version=%s protocol=1 build=%s selftest=ok" %
               (role, version, build)).encode()
    lines = []
    for address in range(0, len(payload), 16):
        chunk = payload[address:address + 16]
        record = bytes([len(chunk), address >> 8, address & 0xff, 0]) + chunk
        checksum = (-sum(record)) & 0xff
        lines.append(":" + (record + bytes([checksum])).hex().upper())
    lines.append(":00000001FF")
    path.write_text("\n".join(lines) + "\n")


class OtaTests(unittest.TestCase):
    def test_network_loss_during_manifest_download_preserves_pending_release(self):
        with tempfile.TemporaryDirectory() as name:
            state = Path(name)
            pending = state / "pending"
            pending.mkdir()
            (pending / "manifest.json").write_text('{"previous":true}\n')
            (pending / "manifest.json.sig").write_bytes(b"previous-signature")
            config = {"state_dir": str(state), "manifest_url": "https://updates.example/manifest.json",
                      "channel": "stable"}

            def interrupted_download(unused_url, destination, maximum):
                destination.write_bytes(b"partial")
                raise OSError("simulated network loss")

            with mock.patch.object(ota, "download", side_effect=interrupted_download):
                with self.assertRaisesRegex(OSError, "network loss"):
                    ota.command_check(config)
            self.assertEqual((pending / "manifest.json").read_text(), '{"previous":true}\n')
            self.assertEqual((pending / "manifest.json.sig").read_bytes(), b"previous-signature")
            self.assertEqual(list(state.glob("ota-check-*")), [])

    def test_network_loss_during_bundle_download_never_activates_candidate(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            state = root / "state"
            pending = state / "pending"
            pending.mkdir(parents=True)
            manifest = {"schema": 1, "channel": "stable", "version": "0.4.0",
                        "sequence": 12, "key_id": "test", "minimum_sequence": 0,
                        "bundle": {"url": "https://updates.example/release.tar.gz",
                                   "sha256": "a" * 64, "size": 100}}
            (pending / "manifest.json").write_text(json.dumps(manifest))
            (pending / "manifest.json.sig").write_bytes(b"signature")
            old = root / "releases/bootstrap"
            (old / "host").mkdir(parents=True)
            (old / "host/millennium-daemon").write_text("known good")
            current = root / "current"
            current.symlink_to(old)
            config = {"state_dir": str(state), "channel": "stable",
                      "release_dir": str(root / "releases"), "current_link": str(current),
                      "previous_link": str(root / "previous"), "service": "daemon.service"}

            def interrupted_download(unused_url, destination, maximum):
                destination.write_bytes(b"half a bundle")
                raise OSError("simulated network loss")

            with mock.patch.object(ota.os, "geteuid", return_value=0), \
                    mock.patch.object(ota, "phone_is_idle", return_value=True), \
                    mock.patch.object(ota, "validate_manifest"), \
                    mock.patch.object(ota, "trusted_public_key"), \
                    mock.patch.object(ota, "verify_signature"), \
                    mock.patch.object(ota, "download", side_effect=interrupted_download):
                with self.assertRaisesRegex(OSError, "network loss"):
                    ota.command_apply(config)
            self.assertEqual(current.resolve(), old.resolve())
            self.assertFalse(ota.activation_path(state).exists())
            self.assertFalse((root / "previous").exists())

    def test_recovery_after_power_loss_mid_flash_restores_only_attempted_mcu(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            state = root / "state"
            releases = root / "releases"
            old = releases / "old"
            new = releases / "new"
            for release in (old, new):
                (release / "arduino").mkdir(parents=True)
                (release / "host").mkdir()
                (release / "host/millennium-daemon").write_text("daemon")
                (release / "arduino/keypad.hex").write_text("keypad")
                (release / "arduino/display.hex").write_text("display")
            current = root / "current"
            current.symlink_to(new)
            ota.write_activation(state, old, new, ["keypad"])
            config = {"state_dir": str(state), "release_dir": str(releases),
                      "current_link": str(current)}
            flashed = mock.Mock(return_value=True)
            with mock.patch.object(ota.os, "geteuid", return_value=0), \
                    mock.patch.object(ota, "flash_image", flashed):
                ota.command_recover(config)
            self.assertEqual(current.resolve(), old.resolve())
            self.assertEqual([call.args[-1] for call in flashed.call_args_list], ["keypad"])
            self.assertFalse(ota.activation_path(state).exists())

    def test_release_identity_includes_sequence(self):
        self.assertEqual(ota.release_identity({"sequence": 7, "version": "0.4.0"}),
                         "00000007-0.4.0")
        self.assertNotEqual(ota.release_identity({"sequence": 7, "version": "0.4.0"}),
                            ota.release_identity({"sequence": 8, "version": "0.4.0"}))

    def test_install_window_handles_midnight(self):
        config = {"install_window_start": "23:00", "install_window_end": "02:00"}
        self.assertTrue(ota.within_install_window(config, time.struct_time((2026, 1, 1, 1, 0, 0, 0, 1, -1))))
        self.assertFalse(ota.within_install_window(config, time.struct_time((2026, 1, 1, 12, 0, 0, 0, 1, -1))))

    def test_manifest_validation_rejects_insecure_bundle(self):
        manifest = {
            "schema": 1, "channel": "stable", "version": "1.0.0", "sequence": 1,
            "key_id": "primary",
            "minimum_sequence": 0,
            "bundle": {"url": "http://example/bundle", "sha256": "a" * 64, "size": 1},
        }
        with self.assertRaises(ota.OtaError):
            ota.validate_manifest(manifest, "stable")

    def test_rollout_group_hold_and_withdrawal(self):
        config = {"device_group": "canary"}
        manifest = {"rollout": {"groups": ["canary"], "hold": False,
                                 "withdrawn": False}}
        self.assertIsNone(ota.rollout_block_reason(config, manifest))
        manifest["rollout"]["hold"] = True
        self.assertIn("hold", ota.rollout_block_reason(config, manifest))
        manifest["rollout"]["hold"] = False
        manifest["rollout"]["withdrawn"] = True
        self.assertIn("withdrawn", ota.rollout_block_reason(config, manifest))
        manifest["rollout"] = {"groups": ["production"], "hold": False,
                               "withdrawn": False}
        self.assertIn("group", ota.rollout_block_reason(config, manifest))

    def test_firmware_attestation_rejects_swapped_or_stale_role(self):
        with tempfile.TemporaryDirectory() as name:
            release = Path(name)
            expected = {"keypad": {"role": "keypad", "version": "0.4.0",
                                    "protocol": 1, "build": "expected"},
                        "display": {"role": "display", "version": "0.4.0",
                                    "protocol": 1, "build": "expected"}}
            (release / "release.json").write_text(json.dumps({"firmware": expected}))
            config = {"keypad_device": "/dev/keypad", "display_device": "/dev/display"}
            stale = {"role": "display", "version": "0.3.0", "protocol": 1,
                     "build": "stale", "selftest": "ok"}
            with mock.patch.object(ota, "read_firmware_identity", return_value=stale):
                with self.assertRaisesRegex(ota.OtaError, "attestation mismatch"):
                    ota.attest_firmware(config, release, "keypad")

    def test_firmware_attestation_accepts_exact_signed_identity(self):
        with tempfile.TemporaryDirectory() as name:
            release = Path(name)
            identity = {"role": "display", "version": "0.4.0", "protocol": 1,
                        "build": "exact"}
            (release / "release.json").write_text(json.dumps(
                {"firmware": {"display": identity}}))
            actual = dict(identity, selftest="ok")
            config = {"display_device": "/dev/display"}
            with mock.patch.object(ota, "read_firmware_identity", return_value=actual):
                self.assertEqual(actual, ota.attest_firmware(config, release, "display"))

    def test_health_check_uses_daemon_protocol_after_prestart_attestation(self):
        config = {
            "version_url": "version", "health_url": "health",
            "metrics_url": "metrics", "health_timeout_seconds": "1",
            "keypad_device": "/dev/keypad", "display_device": "/dev/display",
        }
        replies = [
            {"version": "0.4.0"}, {"overall_status": "WARNING"},
            {"gauges": {"mcu_protocol_version": 2}},
        ]
        with mock.patch.object(ota, "get_json", side_effect=replies), \
                mock.patch.object(ota.Path, "exists", return_value=True), \
                mock.patch.object(ota, "attest_firmware") as attest:
            ota.health_check(config, "0.4.0", Path("/release"))
        attest.assert_not_called()

    def test_safe_extract_rejects_traversal(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            bundle = root / "bad.tar.gz"
            with tarfile.open(bundle, "w:gz") as archive:
                info = tarfile.TarInfo("../escape")
                info.size = 1
                archive.addfile(info, io.BytesIO(b"x"))
            with self.assertRaises(ota.OtaError):
                ota.safe_extract(bundle, root / "out")

    def test_builder_rejects_version_mismatch(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            daemon = root / "daemon"
            daemon.write_text("#!/bin/sh\necho 'millennium-daemon 9.9.9 (test)'\n")
            daemon.chmod(0o755)
            result = subprocess.run([
                sys.executable, str(BUILDER), "--version", "0.4.0", "--sequence", "8",
                "--base-url", "https://updates.example/millennium",
                "--daemon", str(daemon), "--output-dir", str(root / "out"),
            ], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(b"daemon version does not match", result.stderr)

    def test_failed_release_is_backed_off_and_quarantined(self):
        with tempfile.TemporaryDirectory() as name:
            state = Path(name)
            manifest_path = state / "pending/manifest.json"
            manifest_path.parent.mkdir(parents=True)
            manifest = {"sequence": 9, "version": "0.4.0"}
            manifest_path.write_text(json.dumps(manifest))
            config = {
                "state_dir": str(state), "automatic": "true",
                "install_window_start": "00:00", "install_window_end": "00:00",
                "max_failure_attempts": "3", "failure_backoff_seconds": "60",
            }
            first = ota.record_failure(config, manifest_path, manifest, "failed once")
            self.assertEqual(first["attempts"], 1)
            apply = mock.Mock()
            with mock.patch.object(ota, "command_apply", apply):
                ota.command_auto_apply(config)
            apply.assert_not_called()

            with mock.patch.object(ota.os, "geteuid", return_value=0):
                ota.command_clear_failure(config)
            self.assertIsNone(ota.read_failure(state, manifest_path, manifest))

            ota.record_failure(config, manifest_path, manifest, "failed twice")
            ota.record_failure(config, manifest_path, manifest, "failed three times")
            third = ota.record_failure(config, manifest_path, manifest, "failed four times")
            self.assertEqual(third["attempts"], 3)
            with mock.patch.object(ota, "command_apply", apply):
                ota.command_auto_apply(config)
            apply.assert_not_called()

    def test_builder_signs_verifiable_reproducible_bundle(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            inputs = root / "inputs"
            inputs.mkdir()
            paths = {}
            for filename in ("daemon", "portal", "keypad", "display", "flash",
                             "content_installer", "storytool"):
                paths[filename] = inputs / filename
                paths[filename].write_bytes((filename + "\n").encode())
            paths["daemon"].write_text("#!/bin/sh\necho 'millennium-daemon 0.4.0 (test)'\n")
            paths["daemon"].chmod(0o755)
            write_identity_hex(paths["keypad"], "keypad")
            write_identity_hex(paths["display"], "display")
            private = root / "private.pem"
            public = root / "public.pem"
            subprocess.run(["openssl", "genpkey", "-algorithm", "ED25519", "-out", str(private)], check=True)
            subprocess.run(["openssl", "pkey", "-in", str(private), "-pubout", "-out", str(public)], check=True)
            output_a = root / "a"
            output_b = root / "b"
            base = [
                sys.executable, str(BUILDER), "--version", "0.4.0", "--sequence", "7",
                "--base-url", "https://updates.example/millennium", "--daemon", str(paths["daemon"]),
                "--portal", str(paths["portal"]), "--keypad", str(paths["keypad"]),
                "--display", str(paths["display"]), "--flash-script", str(paths["flash"]),
                "--ota-worker", str(WORKER_PATH),
                "--content-installer", str(paths["content_installer"]),
                "--storytool", str(paths["storytool"]),
                "--private-key", str(private),
            ]
            subprocess.run(base + ["--output-dir", str(output_a)], check=True, stdout=subprocess.PIPE)
            subprocess.run(base + ["--output-dir", str(output_b)], check=True, stdout=subprocess.PIPE)
            bundle_name = "millennium-00000007-0.4.0.tar.gz"
            self.assertEqual((output_a / bundle_name).read_bytes(),
                             (output_b / bundle_name).read_bytes())
            ota.verify_signature(public, output_a / "manifest.json", output_a / "manifest.json.sig")
            web_root = root / "web"
            subprocess.run([str(PUBLISHER), str(output_a), str(web_root)], check=True, stdout=subprocess.PIPE)
            self.assertEqual((web_root / "stable/manifest.json").read_bytes(),
                             (output_a / "manifest.json").read_bytes())
            self.assertTrue((web_root / "releases/00000007-0.4.0" / bundle_name).is_file())
            data = json.loads((output_a / "manifest.json").read_text())
            ota.validate_manifest(data, "stable")
            extracted = root / "extracted"
            ota.safe_extract(output_a / bundle_name, extracted)
            ota.verify_release(extracted, data)

            state = root / "state"
            releases = root / "releases"
            old = releases / "bootstrap"
            old.mkdir(parents=True)
            (old / "host").mkdir()
            (old / "host/millennium-daemon").write_text("old daemon\n")
            (old / "arduino").mkdir()
            (old / "arduino/keypad.hex").write_text("old keypad\n")
            (old / "arduino/display.hex").write_text("old display\n")
            current = root / "current"
            current.symlink_to(old)
            config = {
                "channel": "stable", "manifest_url": "https://updates.example/stable/manifest.json",
                "public_key": str(public), "state_dir": str(state), "release_dir": str(releases),
                "current_link": str(current), "previous_link": str(root / "previous"),
                "service": "daemon.service", "phone_state_url": "unused", "health_url": "unused",
                "version_url": "unused", "health_timeout_seconds": "1", "keypad_device": "unused",
                "display_device": "unused", "automatic": "true", "install_window_start": "00:00",
                "install_window_end": "00:00",
            }

            def fake_check_download(url, destination, maximum):
                source = output_a / ("manifest.json.sig" if url.endswith(".sig") else "manifest.json")
                shutil.copyfile(source, destination)

            with mock.patch.object(ota, "download", side_effect=fake_check_download):
                ota.command_check(config)
            self.assertTrue((state / "pending/manifest.json").is_file())

            def fake_apply_download(url, destination, maximum):
                shutil.copyfile(output_a / bundle_name, destination)

            real_subprocess_run = subprocess.run
            def fake_subprocess_run(arguments, *args, **kwargs):
                if arguments[0] == "systemctl":
                    return subprocess.CompletedProcess(arguments, 0)
                return real_subprocess_run(arguments, *args, **kwargs)

            rollback_flash = mock.Mock(return_value=True)
            with mock.patch.object(ota.os, "geteuid", return_value=0), \
                 mock.patch.object(ota, "phone_is_idle", return_value=True), \
                 mock.patch.object(ota, "download", side_effect=fake_apply_download), \
                 mock.patch.object(ota, "run_checked"), \
                 mock.patch.object(ota, "flash_if_changed", return_value=True), \
                 mock.patch.object(ota, "flash_image", rollback_flash), \
                 mock.patch.object(ota, "health_check", side_effect=ota.OtaError("unhealthy")), \
                 mock.patch.object(ota.subprocess, "run", side_effect=fake_subprocess_run):
                with self.assertRaises(ota.OtaError):
                    ota.command_apply(config)
            self.assertEqual(current.resolve(), old.resolve())
            self.assertFalse((state / "installed-sequence").exists())
            self.assertEqual([call.args[-1] for call in rollback_flash.call_args_list], ["display", "keypad"])

            interrupted = releases / "00000007-0.4.0"
            ota.atomic_symlink(current, interrupted)
            ota.write_activation(state, old, interrupted, ["keypad", "display"])
            boot_recovery_flash = mock.Mock(return_value=True)
            with mock.patch.object(ota.os, "geteuid", return_value=0), \
                 mock.patch.object(ota, "flash_image", boot_recovery_flash):
                ota.command_recover(config)
            self.assertEqual(current.resolve(), old.resolve())
            self.assertFalse(ota.activation_path(state).exists())
            self.assertEqual([call.args[-1] for call in boot_recovery_flash.call_args_list], ["display", "keypad"])

            with mock.patch.object(ota.os, "geteuid", return_value=0), \
                 mock.patch.object(ota, "phone_is_idle", return_value=True), \
                 mock.patch.object(ota, "download", side_effect=fake_apply_download), \
                 mock.patch.object(ota, "run_checked"), \
                 mock.patch.object(ota, "flash_if_changed", return_value=False), \
                 mock.patch.object(ota, "health_check"):
                ota.command_apply(config)
            self.assertEqual(current.resolve(), (releases / "00000007-0.4.0").resolve())
            self.assertEqual((state / "installed-sequence").read_text().strip(), "7")

            with (output_a / "manifest.json").open("ab") as stream:
                stream.write(b" ")
            with self.assertRaises(ota.OtaError):
                ota.verify_signature(public, output_a / "manifest.json", output_a / "manifest.json.sig")


if __name__ == "__main__":
    unittest.main()
