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


class OtaTests(unittest.TestCase):
    def test_install_window_handles_midnight(self):
        config = {"install_window_start": "23:00", "install_window_end": "02:00"}
        self.assertTrue(ota.within_install_window(config, time.struct_time((2026, 1, 1, 1, 0, 0, 0, 1, -1))))
        self.assertFalse(ota.within_install_window(config, time.struct_time((2026, 1, 1, 12, 0, 0, 0, 1, -1))))

    def test_manifest_validation_rejects_insecure_bundle(self):
        manifest = {
            "schema": 1, "channel": "stable", "version": "1.0.0", "sequence": 1,
            "minimum_sequence": 0,
            "bundle": {"url": "http://example/bundle", "sha256": "a" * 64, "size": 1},
        }
        with self.assertRaises(ota.OtaError):
            ota.validate_manifest(manifest, "stable")

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

    def test_builder_signs_verifiable_reproducible_bundle(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            inputs = root / "inputs"
            inputs.mkdir()
            paths = {}
            for filename in ("daemon", "portal", "keypad", "display", "flash"):
                paths[filename] = inputs / filename
                paths[filename].write_bytes((filename + "\n").encode())
            private = root / "private.pem"
            public = root / "public.pem"
            subprocess.run(["openssl", "genpkey", "-algorithm", "ED25519", "-out", str(private)], check=True)
            subprocess.run(["openssl", "pkey", "-in", str(private), "-pubout", "-out", str(public)], check=True)
            output_a = root / "a"
            output_b = root / "b"
            base = [
                sys.executable, str(BUILDER), "--version", "1.2.3", "--sequence", "7",
                "--base-url", "https://updates.example/millennium", "--daemon", str(paths["daemon"]),
                "--portal", str(paths["portal"]), "--keypad", str(paths["keypad"]),
                "--display", str(paths["display"]), "--flash-script", str(paths["flash"]),
                "--ota-worker", str(WORKER_PATH),
                "--private-key", str(private),
            ]
            subprocess.run(base + ["--output-dir", str(output_a)], check=True, stdout=subprocess.PIPE)
            subprocess.run(base + ["--output-dir", str(output_b)], check=True, stdout=subprocess.PIPE)
            self.assertEqual((output_a / "millennium-1.2.3.tar.gz").read_bytes(),
                             (output_b / "millennium-1.2.3.tar.gz").read_bytes())
            ota.verify_signature(public, output_a / "manifest.json", output_a / "manifest.json.sig")
            web_root = root / "web"
            subprocess.run([str(PUBLISHER), str(output_a), str(web_root)], check=True, stdout=subprocess.PIPE)
            self.assertEqual((web_root / "stable/manifest.json").read_bytes(),
                             (output_a / "manifest.json").read_bytes())
            self.assertTrue((web_root / "releases/1.2.3/millennium-1.2.3.tar.gz").is_file())
            data = json.loads((output_a / "manifest.json").read_text())
            ota.validate_manifest(data, "stable")
            extracted = root / "extracted"
            ota.safe_extract(output_a / "millennium-1.2.3.tar.gz", extracted)
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
                shutil.copyfile(output_a / "millennium-1.2.3.tar.gz", destination)

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

            interrupted = releases / "1.2.3"
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
            self.assertEqual(current.resolve(), (releases / "1.2.3").resolve())
            self.assertEqual((state / "installed-sequence").read_text().strip(), "7")

            with (output_a / "manifest.json").open("ab") as stream:
                stream.write(b" ")
            with self.assertRaises(ota.OtaError):
                ota.verify_signature(public, output_a / "manifest.json", output_a / "manifest.json.sig")


if __name__ == "__main__":
    unittest.main()
