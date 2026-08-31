#!/usr/bin/env python3
"""Signed, transactional OTA client for an unattended Millennium phone."""

import argparse
import contextlib
import fcntl
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import select
import shutil
import subprocess
import sys
import tarfile
import tempfile
import termios
import time
import urllib.request

DEFAULT_CONFIG = "/etc/millennium/ota.conf"
DEFAULT_STATE = "/var/lib/millennium/ota"
REQUIRED_FILES = (
    "host/millennium-daemon",
    "host/web_portal.html",
    "arduino/keypad.hex",
    "arduino/display.hex",
    "arduino/pi_flash.sh",
    "ota/millennium-ota",
    "content/millennium-content",
    "content/storytool.py",
)


class OtaError(Exception):
    pass


def log(message):
    print("millennium-ota: %s" % message, flush=True)


def load_config(path):
    config = {
        "channel": "stable",
        "device_group": "production",
        "manifest_url": "https://updates.kmatzen.com/millennium/stable/manifest.json",
        "public_key": "/etc/millennium/update-signing-key.pem",
        "trusted_keys": "primary:/etc/millennium/update-signing-key.pem",
        "state_dir": DEFAULT_STATE,
        "release_dir": "/opt/millennium/releases",
        "current_link": "/opt/millennium/current",
        "previous_link": "/opt/millennium/previous",
        "service": "daemon.service",
        "health_url": "http://127.0.0.1:8081/api/health",
        "metrics_url": "http://127.0.0.1:8081/api/metrics",
        "version_url": "http://127.0.0.1:8081/api/version",
        "phone_state_url": "http://127.0.0.1:8081/api/state",
        "keypad_device": "/dev/serial/by-id/usb-Arduino_LLC_Millennium_Alpha-if00",
        "display_device": "/dev/serial/by-id/usb-Arduino_LLC_Millennium_Beta-if00",
        "health_timeout_seconds": "150",
        "max_failure_attempts": "3",
        "failure_backoff_seconds": "3600",
        "automatic": "true",
        "architecture": "auto",
        "install_window_start": "02:00",
        "install_window_end": "05:00",
    }
    config_path = Path(path)
    if config_path.exists():
        for raw in config_path.read_text().splitlines():
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            if "=" not in line:
                raise OtaError("invalid config line: %s" % raw)
            key, value = line.split("=", 1)
            config[key.strip()] = value.strip()
    return config


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def atomic_write(path, data, mode=0o644):
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temp_name = tempfile.mkstemp(prefix=".%s." % path.name, dir=str(path.parent))
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.chmod(temp_name, mode)
        os.replace(temp_name, path)
    finally:
        if os.path.exists(temp_name):
            os.unlink(temp_name)


def write_status(state_dir, state, message, **extra):
    payload = {"state": state, "message": message, "updated_at": int(time.time())}
    payload.update(extra)
    atomic_write(state_dir / "status.json", (json.dumps(payload, sort_keys=True) + "\n").encode())
    log("%s: %s" % (state, message))


def download(url, destination, maximum=256 * 1024 * 1024):
    request = urllib.request.Request(url, headers={"User-Agent": "Millennium-OTA/1"})
    with urllib.request.urlopen(request, timeout=30) as response:
        length = response.headers.get("Content-Length")
        if length and int(length) > maximum:
            raise OtaError("download exceeds size limit")
        with destination.open("wb") as stream:
            total = 0
            while True:
                chunk = response.read(1024 * 1024)
                if not chunk:
                    break
                total += len(chunk)
                if total > maximum:
                    raise OtaError("download exceeds size limit")
                stream.write(chunk)


def verify_signature(public_key, manifest, signature):
    result = subprocess.run([
        "openssl", "pkeyutl", "-verify", "-rawin", "-pubin",
        "-inkey", str(public_key), "-in", str(manifest), "-sigfile", str(signature),
    ], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode == 0:
        return

    # Debian 11's OpenSSL 1.1.1 pkeyutl CLI cannot verify Ed25519. Its system
    # Python cryptography package can, using the same PEM key and raw signature.
    try:
        from cryptography.hazmat.primitives import serialization
        key = serialization.load_pem_public_key(public_key.read_bytes())
        key.verify(signature.read_bytes(), manifest.read_bytes())
    except Exception as exc:
        raise OtaError("manifest signature verification failed") from exc


def trusted_public_key(config, manifest):
    key_id = manifest.get("key_id")
    entries = config.get("trusted_keys", "")
    keys = {}
    for entry in entries.split(","):
        if not entry.strip():
            continue
        if ":" not in entry:
            raise OtaError("invalid trusted_keys entry")
        name, path = entry.split(":", 1)
        keys[name.strip()] = path.strip()
    if not keys and config.get("public_key"):
        keys["primary"] = config["public_key"]
    if not isinstance(key_id, str) or key_id not in keys:
        raise OtaError("manifest signing key is not trusted: %s" % key_id)
    return Path(keys[key_id])


def validate_manifest(data, expected_channel):
    if data.get("schema") != 1 or data.get("channel") != expected_channel:
        raise OtaError("unsupported manifest schema or channel")
    if not isinstance(data.get("sequence"), int) or data["sequence"] < 0:
        raise OtaError("invalid manifest sequence")
    if not isinstance(data.get("minimum_sequence"), int) or data["minimum_sequence"] < 0:
        raise OtaError("invalid minimum sequence")
    if data["minimum_sequence"] > data["sequence"]:
        raise OtaError("minimum sequence exceeds release sequence")
    if not isinstance(data.get("key_id"), str) or not re.fullmatch(
            r"[A-Za-z0-9._-]{1,64}", data["key_id"]):
        raise OtaError("invalid signing key ID")
    version = data.get("version")
    if not isinstance(version, str) or not version or "/" in version or version in (".", ".."):
        raise OtaError("invalid manifest version")
    bundle = data.get("bundle")
    if not isinstance(bundle, dict):
        raise OtaError("missing bundle")
    if not bundle.get("url", "").startswith("https://"):
        raise OtaError("bundle URL must use HTTPS")
    digest = bundle.get("sha256", "")
    if len(digest) != 64 or any(char not in "0123456789abcdef" for char in digest):
        raise OtaError("invalid bundle digest")
    if not isinstance(bundle.get("size"), int) or bundle["size"] <= 0:
        raise OtaError("invalid bundle size")
    rollout = data.get("rollout", {})
    if not isinstance(rollout, dict):
        raise OtaError("invalid rollout policy")
    groups = rollout.get("groups", ["production"])
    if (not isinstance(groups, list) or not groups or
            any(not isinstance(group, str) or not re.fullmatch(
                r"[A-Za-z0-9._-]{1,64}", group) for group in groups)):
        raise OtaError("invalid rollout device groups")
    if not isinstance(rollout.get("hold", False), bool):
        raise OtaError("invalid rollout hold")
    if not isinstance(rollout.get("withdrawn", False), bool):
        raise OtaError("invalid release withdrawal")


def rollout_block_reason(config, manifest):
    rollout = manifest.get("rollout", {})
    if rollout.get("withdrawn", False):
        return "release was withdrawn by its signer"
    if rollout.get("hold", False):
        return "release rollout is on hold"
    groups = rollout.get("groups", ["production"])
    if config.get("device_group", "production") not in groups:
        return "device group is not selected for this rollout"
    return None


def installed_sequence(state_dir):
    try:
        return int((state_dir / "installed-sequence").read_text().strip())
    except (FileNotFoundError, ValueError):
        return -1


def release_identity(manifest):
    return "%08d-%s" % (manifest["sequence"], manifest["version"])


def manifest_failure_id(manifest_path, manifest):
    return "%s-%s" % (release_identity(manifest), sha256(manifest_path)[:16])


def failure_path(state_dir, manifest_path, manifest):
    return state_dir / "failures" / (manifest_failure_id(manifest_path, manifest) + ".json")


def read_failure(state_dir, manifest_path, manifest):
    path = failure_path(state_dir, manifest_path, manifest)
    try:
        data = json.loads(path.read_text())
        if data.get("schema") != 1:
            return None
        return data
    except (FileNotFoundError, ValueError, TypeError):
        return None


def record_failure(config, manifest_path, manifest, error):
    state_dir = Path(config["state_dir"])
    prior = read_failure(state_dir, manifest_path, manifest) or {}
    attempts = int(prior.get("attempts", 0)) + 1
    base = max(1, int(config.get("failure_backoff_seconds", "3600")))
    maximum = max(1, int(config.get("max_failure_attempts", "3")))
    delay = base * (2 ** min(attempts - 1, 8))
    payload = {
        "schema": 1,
        "release": release_identity(manifest),
        "manifest_sha256": sha256(manifest_path),
        "attempts": attempts,
        "maximum_attempts": maximum,
        "last_failure": int(time.time()),
        "retry_after": int(time.time()) + delay,
        "error": str(error)[:512],
    }
    atomic_write(failure_path(state_dir, manifest_path, manifest),
                 (json.dumps(payload, sort_keys=True) + "\n").encode(), mode=0o600)
    return payload


def clear_failure(state_dir, manifest_path, manifest):
    try:
        failure_path(state_dir, manifest_path, manifest).unlink()
    except FileNotFoundError:
        pass


@contextlib.contextmanager
def update_lock(state_dir):
    state_dir.mkdir(parents=True, exist_ok=True)
    with (state_dir / "update.lock").open("a+") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise OtaError("another update operation is running")
        yield


def command_check(config):
    state_dir = Path(config["state_dir"])
    with update_lock(state_dir):
        pending = state_dir / "pending"
        pending.mkdir(parents=True, exist_ok=True)
        manifest = pending / "manifest.json"
        signature = pending / "manifest.json.sig"
        write_status(state_dir, "checking", "downloading signed manifest")
        with tempfile.TemporaryDirectory(prefix="ota-check-", dir=str(state_dir)) as temp_name:
            temp = Path(temp_name)
            fetched_manifest = temp / "manifest.json"
            fetched_signature = temp / "manifest.json.sig"
            url = config["manifest_url"]
            download(url, fetched_manifest, maximum=1024 * 1024)
            download(url + ".sig", fetched_signature, maximum=4096)
            data = json.loads(fetched_manifest.read_text())
            validate_manifest(data, config["channel"])
            verify_signature(trusted_public_key(config, data), fetched_manifest,
                             fetched_signature)
            current = installed_sequence(state_dir)
            if data["sequence"] < current:
                raise OtaError("signed manifest attempts a sequence rollback")
            if current >= 0 and current < data["minimum_sequence"]:
                raise OtaError("installed release is below manifest minimum sequence")
            block_reason = rollout_block_reason(config, data)
            if block_reason:
                for obsolete in (manifest, signature):
                    try:
                        obsolete.unlink()
                    except FileNotFoundError:
                        pass
                write_status(state_dir, "held", block_reason,
                             version=data["version"], sequence=data["sequence"],
                             available=False)
                print(json.dumps({"available": False, "version": data["version"],
                                  "sequence": data["sequence"], "reason": block_reason}))
                return
            atomic_write(manifest, fetched_manifest.read_bytes())
            atomic_write(signature, fetched_signature.read_bytes())
        available = data["sequence"] > current
        write_status(state_dir, "available" if available else "current",
                     "release %s is %s" % (data["version"], "available" if available else "installed"),
                     version=data["version"], sequence=data["sequence"], available=available)
        print(json.dumps({"available": available, "version": data["version"], "sequence": data["sequence"]}))


def safe_extract(bundle, destination):
    destination.mkdir(parents=True, exist_ok=True)
    with tarfile.open(bundle, "r:gz") as archive:
        members = archive.getmembers()
        if len(members) > 64 or sum(member.size for member in members) > 512 * 1024 * 1024:
            raise OtaError("release archive exceeds extraction limits")
        for member in members:
            target = (destination / member.name).resolve()
            if destination.resolve() not in target.parents and target != destination.resolve():
                raise OtaError("unsafe bundle path")
            if member.issym() or member.islnk() or member.isdev():
                raise OtaError("unsupported bundle entry")
        archive.extractall(destination, members=members)


def verify_release(release_dir, manifest, architecture=None):
    release_file = release_dir / "release.json"
    data = json.loads(release_file.read_text())
    if data.get("schema") != 1 or data.get("version") != manifest["version"] or data.get("sequence") != manifest["sequence"]:
        raise OtaError("release metadata does not match manifest")
    expected_architecture = platform.machine() if architecture in (None, "auto") else architecture
    if data.get("architecture") != expected_architecture:
        raise OtaError("release architecture %s does not match device %s" %
                       (data.get("architecture"), expected_architecture))
    files = data.get("files", {})
    if set(files) != set(REQUIRED_FILES):
        raise OtaError("release payload set is incomplete or unexpected")
    for relative in REQUIRED_FILES:
        path = release_dir / relative
        metadata = files[relative]
        if not path.is_file() or path.stat().st_size != metadata.get("size") or sha256(path) != metadata.get("sha256"):
            raise OtaError("payload verification failed: %s" % relative)
    firmware = data.get("firmware")
    if not isinstance(firmware, dict) or set(firmware) != {"keypad", "display"}:
        raise OtaError("release firmware identity set is incomplete")
    for role in ("keypad", "display"):
        identity = firmware[role]
        if (not isinstance(identity, dict) or identity.get("role") != role or
                identity.get("version") != manifest["version"] or
                not isinstance(identity.get("protocol"), int) or
                identity["protocol"] < 1 or not identity.get("build")):
            raise OtaError("invalid %s firmware identity metadata" % role)
    os.chmod(release_dir / "host/millennium-daemon", 0o755)
    os.chmod(release_dir / "arduino/pi_flash.sh", 0o755)
    os.chmod(release_dir / "ota/millennium-ota", 0o755)
    os.chmod(release_dir / "content/millennium-content", 0o755)


def get_json(url, timeout=5, parse_http_error=False):
    try:
        with urllib.request.urlopen(url, timeout=timeout) as response:
            return json.loads(response.read().decode())
    except urllib.error.HTTPError as error:
        if parse_http_error:
            return json.loads(error.read().decode())
        raise


def phone_is_idle(config):
    try:
        state = get_json(config["phone_state_url"])
        return state.get("current_state") in (1, 2)
    except Exception as error:
        raise OtaError("cannot verify that phone is idle: %s" % error)


def atomic_symlink(link, target):
    link.parent.mkdir(parents=True, exist_ok=True)
    temporary = link.parent / (".%s.new" % link.name)
    try:
        temporary.unlink()
    except FileNotFoundError:
        pass
    temporary.symlink_to(target)
    os.replace(temporary, link)


def activation_path(state_dir):
    return state_dir / "activation.json"


def write_activation(state_dir, old_release, new_release, firmware_attempted):
    data = {
        "schema": 1,
        "old_release": str(old_release.resolve()),
        "new_release": str(new_release.resolve()),
        "firmware_attempted": list(firmware_attempted),
    }
    atomic_write(activation_path(state_dir), (json.dumps(data, sort_keys=True) + "\n").encode())


def validated_release_path(config, value):
    releases = Path(config["release_dir"]).resolve()
    candidate = Path(value).resolve()
    if candidate.parent != releases or not (candidate / "host/millennium-daemon").is_file():
        raise OtaError("activation journal contains an invalid release path")
    return candidate


def run_checked(arguments, env=None):
    log("running %s" % " ".join(str(item) for item in arguments))
    subprocess.run([str(item) for item in arguments], check=True, env=env)


def firmware_digest_path(state_dir, target):
    return state_dir / "firmware" / (target + ".sha256")


IDENTITY_PATTERN = re.compile(
    r"MILLENNIUM role=(keypad|display) version=([^ ]+) protocol=([0-9]+) "
    r"build=([^ ]+) selftest=(ok|fail)")


def read_firmware_identity(device, target, timeout=5):
    descriptor = os.open(device, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    try:
        attributes = termios.tcgetattr(descriptor)
        attributes[0] = 0
        attributes[1] = 0
        attributes[2] = termios.CS8 | termios.CLOCAL | termios.CREAD
        attributes[3] = 0
        attributes[4] = termios.B9600
        attributes[5] = termios.B9600
        termios.tcsetattr(descriptor, termios.TCSANOW, attributes)
        termios.tcflush(descriptor, termios.TCIFLUSH)
        query = b"I" if target == "keypad" else b"\x07"
        os.write(descriptor, query)
        deadline = time.monotonic() + timeout
        next_query = time.monotonic() + 1
        received = bytearray()
        while time.monotonic() < deadline:
            if time.monotonic() >= next_query:
                os.write(descriptor, query)
                next_query = time.monotonic() + 1
            readable, _, _ = select.select([descriptor], [], [], 0.25)
            if not readable:
                continue
            try:
                chunk = os.read(descriptor, 512)
            except BlockingIOError:
                continue
            if chunk:
                received.extend(chunk)
                match = IDENTITY_PATTERN.search(received.decode(errors="ignore"))
                if match:
                    return {
                        "role": match.group(1), "version": match.group(2),
                        "protocol": int(match.group(3)), "build": match.group(4),
                        "selftest": match.group(5),
                    }
        raise OtaError("%s firmware did not answer identity query" % target)
    finally:
        os.close(descriptor)


def attest_firmware(config, release_dir, target):
    release = json.loads((release_dir / "release.json").read_text())
    expected = release["firmware"][target]
    device = config["keypad_device" if target == "keypad" else "display_device"]
    actual = read_firmware_identity(device, target)
    comparable = {key: actual.get(key) for key in ("role", "version", "protocol", "build")}
    if comparable != expected or actual.get("selftest") != "ok":
        raise OtaError("%s firmware attestation mismatch: expected %r, got %r" %
                       (target, expected, actual))
    return actual


def flash_image(config, state_dir, release_dir, target):
    image = release_dir / "arduino" / (target + ".hex")
    digest = sha256(image)
    digest_path = firmware_digest_path(state_dir, target)
    env = dict(os.environ)
    env["MANAGE_DAEMON"] = "0"
    run_checked([release_dir / "arduino/pi_flash.sh", target, image], env=env)
    expected = config["keypad_device" if target == "keypad" else "display_device"]
    for _ in range(100):
        if Path(expected).exists():
            attest_firmware(config, release_dir, target)
            atomic_write(digest_path, (digest + "\n").encode())
            return True
        time.sleep(0.1)
    raise OtaError("%s firmware flashed but application USB device did not return" % target)


def flash_if_changed(config, state_dir, release_dir, target):
    image = release_dir / "arduino" / (target + ".hex")
    digest_path = firmware_digest_path(state_dir, target)
    if digest_path.exists() and digest_path.read_text().strip() == sha256(image):
        return False
    return flash_image(config, state_dir, release_dir, target)


def health_check(config, expected_version, release_dir):
    deadline = time.monotonic() + int(config["health_timeout_seconds"])
    last_error = "service unavailable"
    while time.monotonic() < deadline:
        try:
            version = get_json(config["version_url"])
            health = get_json(config["health_url"], parse_http_error=True)
            metrics = get_json(config["metrics_url"])
            keypad = Path(config["keypad_device"]).exists()
            beta = Path(config["display_device"]).exists()
            if (version.get("version") == expected_version and
                    health.get("overall_status") in ("HEALTHY", "WARNING") and
                    metrics.get("gauges", {}).get("mcu_protocol_version") == 2 and
                    keypad and beta):
                # flash_image() already attested each MCU while the daemon was
                # stopped.  Once it is running it exclusively owns both serial
                # devices, so a second direct identity query would race it.
                # The negotiated protocol gauge proves that the daemon has
                # completed a valid v2 handshake with the running firmware.
                return
            last_error = "version, health, MCU protocol, or device presence did not match"
        except Exception as error:
            last_error = str(error)
        # The appliance API is deliberately rate limited.  Three probes every
        # second can lock the updater out of the very health endpoint it needs
        # to observe; five seconds remains responsive while staying below the
        # local request budget.
        time.sleep(5)
    raise OtaError("post-update health check failed: %s" % last_error)


def prune_releases(releases, keep):
    resolved_keep = {path.resolve() for path in keep if path is not None}
    for candidate in releases.iterdir():
        if candidate.is_dir() and not candidate.is_symlink() and candidate.resolve() not in resolved_keep:
            shutil.rmtree(candidate)


def command_apply(config):
    if os.geteuid() != 0:
        raise OtaError("apply must run as root")
    state_dir = Path(config["state_dir"])
    with update_lock(state_dir):
        if not phone_is_idle(config):
            raise OtaError("phone is in a call; update deferred")
        pending = state_dir / "pending"
        manifest_path = pending / "manifest.json"
        signature_path = pending / "manifest.json.sig"
        manifest = json.loads(manifest_path.read_text())
        validate_manifest(manifest, config["channel"])
        block_reason = rollout_block_reason(config, manifest)
        if block_reason:
            raise OtaError(block_reason)
        verify_signature(trusted_public_key(config, manifest), manifest_path,
                         signature_path)
        if manifest["sequence"] <= installed_sequence(state_dir):
            write_status(state_dir, "current", "no newer release is pending")
            return
        identity = release_identity(manifest)
        bundle = pending / ("millennium-%s.tar.gz" % identity)
        write_status(state_dir, "downloading", "downloading release bundle", version=manifest["version"])
        download(manifest["bundle"]["url"], bundle, maximum=manifest["bundle"]["size"])
        if bundle.stat().st_size != manifest["bundle"]["size"] or sha256(bundle) != manifest["bundle"]["sha256"]:
            raise OtaError("bundle digest or size mismatch")
        releases = Path(config["release_dir"])
        release_dir = releases / identity
        staging = releases / (".%s.staging" % identity)
        current_link = Path(config["current_link"])
        previous_link = Path(config["previous_link"])
        old_release = current_link.resolve() if current_link.is_symlink() else None
        if old_release is None or not (old_release / "host/millennium-daemon").is_file():
            raise OtaError("no valid current release is available for rollback")
        referenced = {old_release.resolve()}
        if previous_link.is_symlink():
            referenced.add(previous_link.resolve())
        if release_dir.resolve() in referenced:
            raise OtaError("new release identity conflicts with an active release")
        if staging.exists():
            shutil.rmtree(staging)
        safe_extract(bundle, staging)
        verify_release(staging, manifest, config.get("architecture", "auto"))
        if release_dir.exists():
            verify_release(release_dir, manifest, config.get("architecture", "auto"))
            shutil.rmtree(staging)
        else:
            os.replace(staging, release_dir)
        if not phone_is_idle(config):
            raise OtaError("phone became busy while staging; update deferred")
        write_status(state_dir, "installing", "installing host and firmware", version=manifest["version"])
        atomic_symlink(previous_link, old_release)
        write_activation(state_dir, old_release, release_dir, [])
        run_checked(["systemctl", "stop", config["service"]])
        switched = False
        flashed = []
        try:
            for target in ("keypad", "display"):
                flashed.append(target)
                write_activation(state_dir, old_release, release_dir, flashed)
                if not flash_if_changed(config, state_dir, release_dir, target):
                    flashed.pop()
                    write_activation(state_dir, old_release, release_dir, flashed)
            atomic_symlink(current_link, release_dir)
            switched = True
            run_checked(["systemctl", "start", config["service"]])
            health_check(config, manifest["version"], release_dir)
        except Exception:
            rollback_complete = True
            if switched:
                atomic_symlink(current_link, old_release)
            for target in reversed(flashed):
                previous_image = old_release / "arduino" / (target + ".hex")
                if previous_image.is_file():
                    try:
                        flash_image(config, state_dir, old_release, target)
                    except Exception as rollback_error:
                        rollback_complete = False
                        log("WARNING: %s firmware rollback failed: %s" % (target, rollback_error))
            subprocess.run(["systemctl", "restart", config["service"]], check=False)
            if rollback_complete:
                try:
                    activation_path(state_dir).unlink()
                except FileNotFoundError:
                    pass
            raise
        activation_path(state_dir).unlink()
        atomic_write(state_dir / "installed-sequence", (str(manifest["sequence"]) + "\n").encode())
        clear_failure(state_dir, manifest_path, manifest)
        prune_releases(releases, (release_dir, old_release))
        write_status(state_dir, "committed", "release passed health checks",
                     version=manifest["version"], sequence=manifest["sequence"])


def command_status(config):
    path = Path(config["state_dir"]) / "status.json"
    print(path.read_text().strip() if path.exists() else '{"state":"never-run"}')


def command_recover(config):
    if os.geteuid() != 0:
        raise OtaError("recover must run as root")
    state_dir = Path(config["state_dir"])
    journal = activation_path(state_dir)
    if not journal.exists():
        return
    with update_lock(state_dir):
        data = json.loads(journal.read_text())
        if data.get("schema") != 1:
            raise OtaError("unsupported activation journal")
        old_release = validated_release_path(config, data.get("old_release", ""))
        validated_release_path(config, data.get("new_release", ""))
        attempted = data.get("firmware_attempted", [])
        if not isinstance(attempted, list) or any(target not in ("keypad", "display") for target in attempted):
            raise OtaError("invalid firmware recovery list")
        write_status(state_dir, "recovering", "rolling back interrupted activation")
        atomic_symlink(Path(config["current_link"]), old_release)
        for target in reversed(attempted):
            if (old_release / "arduino" / (target + ".hex")).is_file():
                flash_image(config, state_dir, old_release, target)
        journal.unlink()
        write_status(state_dir, "rolled-back", "interrupted activation was rolled back")


def parse_clock(value):
    try:
        hour, minute = (int(part) for part in value.split(":"))
    except (ValueError, TypeError):
        raise OtaError("invalid install window time: %s" % value)
    if hour not in range(24) or minute not in range(60):
        raise OtaError("invalid install window time: %s" % value)
    return hour * 60 + minute


def within_install_window(config, now=None):
    local = now or time.localtime()
    current = local.tm_hour * 60 + local.tm_min
    start = parse_clock(config["install_window_start"])
    end = parse_clock(config["install_window_end"])
    if start == end:
        return True
    if start < end:
        return start <= current < end
    return current >= start or current < end


def command_auto_apply(config):
    if config.get("automatic", "false").lower() not in ("1", "true", "yes", "on"):
        log("automatic installation is disabled")
        return
    if not within_install_window(config):
        log("outside automatic installation window")
        return
    manifest_path = Path(config["state_dir"]) / "pending/manifest.json"
    if not manifest_path.exists():
        log("no verified manifest is pending")
        return
    manifest = json.loads(manifest_path.read_text())
    if manifest.get("sequence", -1) <= installed_sequence(Path(config["state_dir"])):
        log("pending manifest is not newer than the installed release")
        return
    failure = read_failure(Path(config["state_dir"]), manifest_path, manifest)
    if failure:
        if failure.get("attempts", 0) >= int(config.get("max_failure_attempts", "3")):
            log("release is quarantined after repeated failures")
            return
        if int(time.time()) < failure.get("retry_after", 0):
            log("release retry is in exponential backoff")
            return
    try:
        command_apply(config)
    except Exception as error:
        record_failure(config, manifest_path, manifest, error)
        raise


def command_clear_failure(config):
    if os.geteuid() != 0:
        raise OtaError("clear-failure must run as root")
    state_dir = Path(config["state_dir"])
    manifest_path = state_dir / "pending/manifest.json"
    if not manifest_path.exists():
        raise OtaError("no pending manifest")
    manifest = json.loads(manifest_path.read_text())
    clear_failure(state_dir, manifest_path, manifest)
    write_status(state_dir, "retry-enabled",
                 "administrator cleared the release failure quarantine",
                 version=manifest.get("version"), sequence=manifest.get("sequence"))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", default=DEFAULT_CONFIG)
    parser.add_argument("command", choices=("check", "apply", "auto-apply", "recover",
                                             "clear-failure", "status"))
    args = parser.parse_args()
    try:
        config = load_config(args.config)
        if args.command == "check":
            command_check(config)
        elif args.command == "apply":
            command_apply(config)
        elif args.command == "auto-apply":
            command_auto_apply(config)
        elif args.command == "recover":
            command_recover(config)
        elif args.command == "clear-failure":
            command_clear_failure(config)
        else:
            command_status(config)
    except Exception as error:
        try:
            config = load_config(args.config)
            write_status(Path(config["state_dir"]), "error", str(error))
        except Exception:
            pass
        print("millennium-ota: error: %s" % error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
