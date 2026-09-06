#!/usr/bin/env python3
"""Production orchestration for signed Millennium Raspberry Pi OS updates."""

import argparse
from contextlib import contextmanager
import fcntl
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
import wave

import millennium_os_ota as ota


DEFAULT_CONFIG = "/etc/millennium/os-ota.conf"
MAX_MANIFEST = 128 * 1024
MAX_SIGNATURE = 4096


class AgentError(Exception):
    pass


DEFAULTS = {
    "automatic": "true",
    "channel": "stable",
    "device_group": "production",
    "manifest_url": "https://updates.kmatzen.com/millennium/os/stable/manifest.json",
    "signature_url": "https://updates.kmatzen.com/millennium/os/stable/manifest.json.sig",
    "trusted_keys": "release-2026-08:/etc/millennium/update-signing-key.pem",
    "state_dir": "/var/lib/millennium/os-ota",
    "application_lock": "/var/lib/millennium/ota/update.lock",
    "layout_id": "zero2w-ab-mbr-v1",
    "architecture": "arm64",
    "board_model_path": "/proc/device-tree/model",
    "selector_path": "/bootfs/autoboot.txt",
    "boot_partition_path": "/proc/device-tree/chosen/bootloader/partition",
    "tryboot_path": "/proc/device-tree/chosen/bootloader/tryboot",
    "active_boot": "/dev/disk/by-slot/active/boot",
    "active_root": "/dev/disk/by-slot/active/system",
    "inactive_boot": "/dev/disk/by-slot/other/boot",
    "inactive_root": "/dev/disk/by-slot/other/system",
    "install_window_start": "02:00",
    "install_window_end": "05:00",
    "health_timeout_seconds": "180",
    "maintenance_stable_seconds": "95",
    "failure_backoff_seconds": "3600",
    "max_failure_attempts": "3",
    "phone_state_url": "http://127.0.0.1:8081/api/state",
    "health_url": "http://127.0.0.1:8081/api/health",
    "metrics_url": "http://127.0.0.1:8081/api/metrics",
    "version_url": "http://127.0.0.1:8081/api/version",
    "keypad_device": "/dev/serial/by-id/usb-Arduino_LLC_Millennium_Alpha-if00",
    "display_device": "/dev/serial/by-id/usb-Arduino_LLC_Millennium_Beta-if00",
    "current_release": "/opt/millennium/current/release.json",
}


def log(message):
    print("millennium-os-ota: " + message, flush=True)


def load_config(path):
    result = dict(DEFAULTS)
    source = Path(path)
    if source.exists():
        for raw in source.read_text(encoding="utf-8").splitlines():
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            if "=" not in line:
                raise AgentError("invalid OS OTA config line")
            key, value = line.split("=", 1)
            key, value = key.strip(), value.strip()
            if key not in DEFAULTS or not value:
                raise AgentError("unknown or empty OS OTA config field: " + key)
            result[key] = value
    for key in ("health_timeout_seconds", "maintenance_stable_seconds",
                "failure_backoff_seconds", "max_failure_attempts"):
        try:
            result[key] = int(result[key])
        except ValueError as exc:
            raise AgentError("invalid numeric OS OTA config field: " + key) from exc
    automatic = result["automatic"].lower()
    if automatic not in ("0", "1", "false", "true", "no", "yes", "off", "on"):
        raise AgentError("invalid automatic OS OTA setting")
    result["automatic"] = automatic in ("1", "true", "yes", "on")
    if (result["health_timeout_seconds"] < 1
            or result["maintenance_stable_seconds"] < 1
            or result["failure_backoff_seconds"] < 1
            or result["max_failure_attempts"] < 1):
        raise AgentError("OS OTA numeric settings must be positive")
    ota.parse_clock(result["install_window_start"])
    ota.parse_clock(result["install_window_end"])
    return result


def trusted_keys(config):
    result = {}
    for item in config["trusted_keys"].split(","):
        key_id, separator, filename = item.partition(":")
        if not separator or not ota._identifier(key_id) or not filename.startswith("/"):
            raise AgentError("invalid trusted OS OTA key mapping")
        if key_id in result:
            raise AgentError("duplicate trusted OS OTA key ID")
        result[key_id] = Path(filename)
    return result


def atomic_bytes(path, value, mode=0o600):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=".os-agent-", dir=path.parent)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(value)
            stream.flush()
            os.fsync(stream.fileno())
        os.chmod(temporary, mode)
        os.replace(temporary, path)
        directory = os.open(path.parent, os.O_RDONLY)
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def fetch(url, maximum, opener=urllib.request.urlopen):
    request = urllib.request.Request(url, headers={"User-Agent": "Millennium-OS-OTA/1"})
    try:
        with opener(request, timeout=30) as response:
            length = response.headers.get("Content-Length")
            if length is not None and int(length) > maximum:
                raise AgentError("OS metadata exceeds size limit")
            value = response.read(maximum + 1)
    except (OSError, urllib.error.URLError, ValueError) as exc:
        raise AgentError("cannot download OS update metadata") from exc
    if not value or len(value) > maximum:
        raise AgentError("OS metadata is empty or exceeds size limit")
    return value


def installed_sequence(state_dir):
    try:
        value = int((Path(state_dir) / "installed-sequence").read_text().strip())
    except (OSError, ValueError):
        return 0
    return max(0, value)


def board_model(config):
    try:
        return Path(config["board_model_path"]).read_bytes().rstrip(b"\0\r\n").decode()
    except (OSError, UnicodeDecodeError) as exc:
        raise AgentError("cannot identify Raspberry Pi board model") from exc


def expected(config, sequence=None):
    return {
        "channel": config["channel"],
        "architecture": config["architecture"],
        "layout_id": config["layout_id"],
        "installed_sequence": installed_sequence(config["state_dir"]) if sequence is None else sequence,
        "board_model": board_model(config),
        "device_group": config["device_group"],
    }


def read_json_url(url, timeout=5, allow_http_error=False):
    try:
        with urllib.request.urlopen(url, timeout=timeout) as response:
            return json.loads(response.read(256 * 1024).decode("utf-8"))
    except urllib.error.HTTPError as exc:
        if allow_http_error:
            try:
                return json.loads(exc.read(256 * 1024).decode("utf-8"))
            except (OSError, UnicodeDecodeError, ValueError) as parse_exc:
                raise AgentError("local health endpoint returned invalid JSON") from parse_exc
        raise AgentError("local health endpoint unavailable") from exc
    except (OSError, ValueError, urllib.error.URLError) as exc:
        raise AgentError("local health endpoint unavailable") from exc


def dpkg_at_least(current, minimum, runner=subprocess.run):
    result = runner(["dpkg", "--compare-versions", current, "ge", minimum],
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return result.returncode == 0


def enforce_compatibility(config, manifest):
    current = read_json_url(config["version_url"]).get("version")
    try:
        release = json.loads(Path(config["current_release"]).read_text(encoding="utf-8"))
        mcu_versions = [release["firmware"][name]["version"]
                        for name in ("keypad", "display")]
    except (OSError, KeyError, TypeError, json.JSONDecodeError) as exc:
        raise AgentError("cannot determine installed MCU versions") from exc
    compatibility = manifest["compatibility"]
    if not isinstance(current, str) or not dpkg_at_least(
            current, compatibility["minimum_application_version"]):
        raise AgentError("installed application is too old for OS release")
    if any(not isinstance(value, str) or not dpkg_at_least(
            value, compatibility["minimum_mcu_version"]) for value in mcu_versions):
        raise AgentError("installed MCU firmware is too old for OS release")
    if compatibility["persistent_state_schema"] != 1:
        raise AgentError("unsupported persistent-state schema")


def manifest_paths(config):
    pending = Path(config["state_dir"]) / "pending"
    return pending / "manifest.json", pending / "manifest.json.sig"


def load_pending(config, allow_installed=False):
    manifest_path, signature_path = manifest_paths(config)
    try:
        untrusted = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise AgentError("no valid pending OS manifest") from exc
    key = trusted_keys(config).get(untrusted.get("key_id"))
    if key is None:
        raise AgentError("OS manifest uses an untrusted key ID")
    sequence = -1 if allow_installed else None
    return ota.load_verified_manifest(manifest_path, signature_path, key,
                                      expected(config, sequence))


def write_status(config, journal=None, failure=None, gate=None):
    value = ota.owner_safe_status(journal, failure, gate)
    value["updated_at"] = int(time.time())
    ota.atomic_json(Path(config["state_dir"]) / "status.json", value)
    os.chmod(Path(config["state_dir"]) / "status.json", 0o644)
    return value


@contextmanager
def update_lock(config):
    path = Path(config["state_dir"]) / "lock"
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a+") as stream:
        try:
            fcntl.flock(stream, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as exc:
            raise AgentError("another OS update operation is active") from exc
        yield


@contextmanager
def application_update_guard(config):
    """Exclude the application updater for the complete destructive phase."""
    path = Path(config["application_lock"])
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a+") as stream:
        try:
            fcntl.flock(stream, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            yield False
            return
        try:
            yield True
        finally:
            fcntl.flock(stream, fcntl.LOCK_UN)


def command_check(config, opener=urllib.request.urlopen):
    with update_lock(config):
        manifest_data = fetch(config["manifest_url"], MAX_MANIFEST, opener)
        signature_data = fetch(config["signature_url"], MAX_SIGNATURE, opener)
        try:
            untrusted = json.loads(manifest_data.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise AgentError("invalid OS manifest JSON") from exc
        key = trusted_keys(config).get(untrusted.get("key_id"))
        if key is None:
            raise AgentError("OS manifest uses an untrusted key ID")
        with tempfile.TemporaryDirectory(prefix="millennium-os-check-") as temporary:
            manifest = Path(temporary) / "manifest.json"
            signature = Path(temporary) / "manifest.json.sig"
            manifest.write_bytes(manifest_data)
            signature.write_bytes(signature_data)
            value = ota.load_verified_manifest(manifest, signature, key,
                                               expected(config, -1))
        if value["sequence"] <= installed_sequence(config["state_dir"]):
            write_status(config)
            log("no newer signed OS release")
            return False
        enforce_compatibility(config, value)
        manifest_path, signature_path = manifest_paths(config)
        atomic_bytes(manifest_path, manifest_data)
        atomic_bytes(signature_path, signature_data)
        write_status(config, {"phase": "pending", "sequence": value["sequence"],
                              "version": value["version"]})
        log("staged signed OS manifest sequence %d" % value["sequence"])
        return True


def lock_is_held(path):
    """True only while another process actually holds this advisory lock."""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a+") as stream:
        try:
            fcntl.flock(stream, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            return True
        fcntl.flock(stream, fcntl.LOCK_UN)
    return False


def busy_state(config, application_lock_held=False):
    state = read_json_url(config["phone_state_url"])
    idle = state.get("current_state") in (1, 2)
    return {
        "active_call": not idle,
        "coin_transaction": int(state.get("inserted_cents", 0)) > 0,
        "content_save": Path(config["state_dir"]).joinpath("content-save.lock").exists(),
        "maintenance_session": Path(config["state_dir"]).joinpath("maintenance.lock").exists(),
        "other_update": (False if application_lock_held else
                         lock_is_held(config["application_lock"])),
    }


def slot_paths(config):
    # The image's slot generator deliberately exposes stable symlink names.
    # Resolve them before entering the low-level writer, which rejects symlink
    # targets so a path cannot be retargeted between validation and writing.
    return ({"boot": Path(config["inactive_boot"]).resolve(strict=True),
             "root": Path(config["inactive_root"]).resolve(strict=True)},
            {"boot": Path(config["active_boot"]), "root": Path(config["active_root"])})


def boot_partitions(config):
    try:
        selector = Path(config["selector_path"]).read_text(encoding="ascii")
    except OSError as exc:
        raise AgentError("cannot read boot selector") from exc
    normal, candidate = ota.parse_autoboot(selector)
    actual, tryboot = current_boot_state(config)
    if tryboot != 0 or actual != normal:
        raise AgentError("refusing update outside a stable normal boot")
    return normal, candidate


def command_apply(config, reboot=True, override_window=False):
    if os.geteuid() != 0:
        raise AgentError("OS apply requires root")
    with update_lock(config):
        with application_update_guard(config) as application_locked:
            if not application_locked:
                gate = {"allowed": False, "reason": "device-busy",
                        "busy": ["other_update"]}
                write_status(config, gate=gate)
                log("OS release deferred: application update active")
                return False
            manifest = load_pending(config)
            enforce_compatibility(config, manifest)
            retry = ota.retry_status(config["state_dir"], manifest)
            if not retry["allowed"]:
                failure = ota.read_failure(config["state_dir"],
                                           ota.manifest_identity_digest(manifest))
                write_status(config, failure=failure)
                log("OS release deferred by retry policy")
                return False
            gate = ota.installation_gate(
                busy_state(config, application_lock_held=True),
                config["install_window_start"], config["install_window_end"],
                override_window=override_window)
            if not gate["allowed"]:
                write_status(config, gate=gate)
                log("OS release deferred: " + gate["reason"])
                return False
            # Prove this is a stable normal boot before downloading or writing
            # anything. On a one-shot candidate boot, "other" is the rollback
            # slot and must never be treated as an inactive update target.
            normal, candidate = boot_partitions(config)
            state = Path(config["state_dir"])
            downloads = ota.download_verified_images(manifest, state / "downloads")
            targets, active = slot_paths(config)
            journal_path = state / "transaction.json"
            journal = ota.write_inactive_images(manifest, downloads, targets, active,
                                                journal_path)
            journal = ota.arm_tryboot(config["selector_path"], journal_path,
                                      normal, candidate, reboot=False)
            write_status(config, journal)
            if reboot:
                result = subprocess.run(["reboot", "0 tryboot"], check=False)
                if result.returncode:
                    raise AgentError("cannot request one-shot tryboot")
            return True


def service_active(name, runner=subprocess.run):
    return runner(["systemctl", "is-active", "--quiet", name],
                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0


def service_stably_active(name, minimum_seconds, runner=subprocess.run):
    if not service_active(name, runner):
        return False
    result = runner([
        "systemctl", "show", "--property=ActiveEnterTimestampMonotonic",
        "--value", name,
    ], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    try:
        active_at = int(result.stdout.strip()) / 1_000_000
    except (AttributeError, ValueError):
        return False
    return result.returncode == 0 and active_at > 0 and time.monotonic() - active_at >= minimum_seconds


def named_health_is_healthy(health, name):
    return health.get("checks", {}).get(name, {}).get("status") == "HEALTHY"


def metric_value(metrics, name):
    value = metrics.get("gauges", {}).get(name)
    return value if isinstance(value, (int, float)) and not isinstance(value, bool) else None


def audio_health(runner=subprocess.run):
    descriptor, filename = tempfile.mkstemp(prefix="millennium-audio-health-",
                                             suffix=".wav", dir="/run")
    os.close(descriptor)
    try:
        with wave.open(filename, "wb") as sound:
            sound.setnchannels(1)
            sound.setsampwidth(2)
            sound.setframerate(8000)
            sound.writeframes(b"\0" * 1600)  # 100 ms of silence
        result = runner(["aplay", "--quiet", filename], timeout=5,
                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return result.returncode == 0
    except (OSError, subprocess.SubprocessError, wave.Error):
        return False
    finally:
        Path(filename).unlink(missing_ok=True)


def filesystem_health(config):
    return (os.path.ismount("/persistent") and os.path.ismount("/bootfs")
            and os.access("/persistent", os.W_OK) and os.access("/bootfs", os.W_OK))


def collect_health(config):
    try:
        state = read_json_url(config["phone_state_url"])
        health = read_json_url(config["health_url"], allow_http_error=True)
        metrics = read_json_url(config["metrics_url"])
        version = read_json_url(config["version_url"])
        protocol = metric_value(metrics, "mcu_protocol_version") == 2
        serial = named_health_is_healthy(health, "serial_connection")
        sip = named_health_is_healthy(health, "sip_connection")
        keypad = (Path(config["keypad_device"]).exists() and protocol
                  and metric_value(metrics, "arduino_i2c_drops_keypad") == 0)
        display = (Path(config["display_device"]).exists() and protocol
                   and metric_value(metrics, "arduino_i2c_drops_display") == 0)
        state_value = state.get("current_state")
        endpoint = fetch(config["manifest_url"], MAX_MANIFEST)
        return {
            "filesystems": filesystem_health(config),
            "daemon": (service_active("daemon.service")
                       and bool(version.get("version"))
                       and health.get("overall_status") in ("HEALTHY", "WARNING")
                       and serial and sip),
            "keypad_mcu": keypad,
            "display_mcu": display,
            "audio": audio_health(),
            "sip": sip and state.get("sip_registered") == 1,
            "local_controls": (keypad and display and serial
                               and isinstance(state_value, int)
                               and not isinstance(state_value, bool)
                               and 1 <= state_value <= 4),
            "update_endpoint": bool(endpoint),
            "maintenance_tunnel": service_stably_active(
                "millennium-maintenance-tunnel.service",
                config["maintenance_stable_seconds"]),
        }
    except Exception:
        return {name: False for name in ota.REQUIRED_BOOT_HEALTH}


def await_health(config):
    deadline = time.monotonic() + config["health_timeout_seconds"]
    checks = {name: False for name in ota.REQUIRED_BOOT_HEALTH}
    while time.monotonic() < deadline:
        checks = collect_health(config)
        if set(checks) == ota.REQUIRED_BOOT_HEALTH and all(checks.values()):
            return checks
        time.sleep(5)
    return checks


def current_boot_state(config):
    return (ota.read_bootloader_integer(config["boot_partition_path"]),
            ota.read_bootloader_integer(config["tryboot_path"]))


def command_boot_health(config, reboot_on_failure=True):
    if os.geteuid() != 0:
        raise AgentError("OS boot health requires root")
    with update_lock(config):
        journal_path = Path(config["state_dir"]) / "transaction.json"
        if not journal_path.exists():
            return False
        journal = ota.read_journal(journal_path)
        if journal.get("phase") != "tryboot-armed":
            return False
        boot_partition, tryboot = current_boot_state(config)
        manifest = load_pending(config, allow_installed=True)
        if (boot_partition, tryboot) != (journal["candidate_boot_partition"], 1):
            if (boot_partition, tryboot) != (journal["normal_boot_partition"], 0):
                raise AgentError("unexpected firmware state during OS recovery")
            checks = {name: False for name in ota.REQUIRED_BOOT_HEALTH}
            failure = ota.reject_boot_health(
                journal_path, checks, config["state_dir"], manifest,
                "firmware-fallback", base_delay=config["failure_backoff_seconds"],
                maximum_attempts=config["max_failure_attempts"])
            write_status(config, failure=failure["failure"])
            return False
        checks = await_health(config)
        if all(checks.values()):
            ota.record_boot_health(journal_path, checks)
            committed = ota.commit_tryboot_from_device_tree(
                config["selector_path"], journal_path,
                config["boot_partition_path"], config["tryboot_path"],
                Path(config["state_dir"]) / "installed-sequence")
            ota.clear_failure(config["state_dir"], manifest)
            write_status(config, committed)
            return True
        failed = ota.reject_boot_health(
            journal_path, checks, config["state_dir"], manifest,
            "candidate-health", base_delay=config["failure_backoff_seconds"],
            maximum_attempts=config["max_failure_attempts"])
        write_status(config, failure=failed["failure"])
        if reboot_on_failure:
            subprocess.run(["reboot"], check=False)
        return False


def command_recover(config):
    """Record pre-selection interruptions; firmware fallback is handled at boot."""
    if os.geteuid() != 0:
        raise AgentError("OS recovery requires root")
    with update_lock(config):
        journal_path = Path(config["state_dir"]) / "transaction.json"
        if not journal_path.exists():
            return False
        journal = ota.read_journal(journal_path)
        phase = journal.get("phase")
        if phase == "commit-intent":
            reconciled = ota.reconcile_commit_from_device_tree(
                config["selector_path"], journal_path,
                config["boot_partition_path"], config["tryboot_path"],
                Path(config["state_dir"]) / "installed-sequence")
            write_status(config, reconciled)
            log("reconciled interrupted OS commit")
            return True
        if phase in ("verified-downloads", "root-written-and-readback-verified",
                     "boot-written-and-readback-verified", "candidate-written",
                     "commit-interrupted"):
            write_status(config, journal)
            log("safe interrupted OS transaction retained for retry")
            return True
        return False


def command_clear(config):
    if os.geteuid() != 0:
        raise AgentError("clearing an OS failure requires root")
    manifest = load_pending(config, allow_installed=True)
    ota.clear_failure(config["state_dir"], manifest)
    write_status(config)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", default=DEFAULT_CONFIG)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("check")
    apply = sub.add_parser("apply")
    apply.add_argument("--no-reboot", action="store_true")
    apply.add_argument("--override-window", action="store_true")
    sub.add_parser("auto-apply")
    boot = sub.add_parser("boot-health")
    boot.add_argument("--no-reboot", action="store_true")
    sub.add_parser("recover")
    sub.add_parser("status")
    sub.add_parser("clear-failure")
    args = parser.parse_args()
    try:
        config = load_config(args.config)
        if args.command == "check":
            command_check(config)
        elif args.command == "apply":
            command_apply(config, not args.no_reboot, args.override_window)
        elif args.command == "auto-apply":
            if config["automatic"]:
                command_apply(config)
            else:
                log("automatic OS updates are disabled")
        elif args.command == "boot-health":
            command_boot_health(config, not args.no_reboot)
        elif args.command == "recover":
            command_recover(config)
        elif args.command == "status":
            path = Path(config["state_dir"]) / "status.json"
            print(path.read_text().strip() if path.exists() else '{"schema":1,"state":"idle"}')
        elif args.command == "clear-failure":
            command_clear(config)
    except (AgentError, ota.OsOtaError, OSError, ValueError) as exc:
        log(str(exc))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
