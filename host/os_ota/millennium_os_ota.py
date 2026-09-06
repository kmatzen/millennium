#!/usr/bin/env python3
"""Validation primitives for fail-safe Millennium A/B operating-system OTA."""

import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import struct
import subprocess
import tempfile
import time
from urllib.parse import urlparse
from urllib.request import urlopen


class OsOtaError(Exception):
    pass


HEX64 = re.compile(r"[0-9a-f]{64}")
HEX40 = re.compile(r"[0-9a-f]{40}")
IDENTIFIER = re.compile(r"[A-Za-z0-9._-]{1,64}")
REQUIRED_BOOT_HEALTH = frozenset({
    "filesystems", "daemon", "keypad_mcu", "display_mcu", "audio", "sip",
    "local_controls", "update_endpoint", "maintenance_tunnel",
})
BUSY_REASONS = frozenset({"active_call", "coin_transaction", "content_save",
                          "maintenance_session", "other_update"})


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def verify_signature(public_key, manifest, signature):
    result = subprocess.run([
        "openssl", "pkeyutl", "-verify", "-rawin", "-pubin", "-inkey",
        str(public_key), "-in", str(manifest), "-sigfile", str(signature),
    ], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode == 0:
        return
    try:
        from cryptography.hazmat.primitives import serialization
        key = serialization.load_pem_public_key(Path(public_key).read_bytes())
        key.verify(Path(signature).read_bytes(), Path(manifest).read_bytes())
    except Exception as exc:
        raise OsOtaError("OS manifest signature verification failed") from exc


def _identifier(value):
    return isinstance(value, str) and IDENTIFIER.fullmatch(value) is not None


def validate_manifest(value, expected):
    if value.get("schema") != 1 or value.get("kind") != "millennium-os-release":
        raise OsOtaError("unsupported OS manifest")
    for field in ("channel", "key_id", "version", "architecture", "layout_id"):
        if not _identifier(value.get(field)):
            raise OsOtaError("invalid OS manifest field: " + field)
    if value["channel"] != expected["channel"]:
        raise OsOtaError("OS release channel mismatch")
    if value["architecture"] != expected["architecture"]:
        raise OsOtaError("OS release architecture mismatch")
    if value["layout_id"] != expected["layout_id"]:
        raise OsOtaError("OS release partition layout mismatch")
    if not isinstance(value.get("sequence"), int) or value["sequence"] < 0:
        raise OsOtaError("invalid OS release sequence")
    if value["sequence"] <= expected["installed_sequence"]:
        raise OsOtaError("OS release sequence is not newer")
    if not isinstance(value.get("source_commit"), str) or not HEX40.fullmatch(
            value["source_commit"]):
        raise OsOtaError("invalid OS source commit")
    models = value.get("board_models")
    if not isinstance(models, list) or expected["board_model"] not in models:
        raise OsOtaError("OS release does not support this board")
    rollout = value.get("rollout", {})
    if rollout.get("hold") is not False or rollout.get("withdrawn") is not False:
        raise OsOtaError("OS release is held or withdrawn")
    if expected["device_group"] not in rollout.get("groups", []):
        raise OsOtaError("device group is not selected for OS rollout")
    compatibility = value.get("compatibility", {})
    if (not _identifier(compatibility.get("minimum_application_version"))
            or not _identifier(compatibility.get("minimum_mcu_version"))
            or not isinstance(compatibility.get("persistent_state_schema"), int)
            or compatibility["persistent_state_schema"] < 1):
        raise OsOtaError("invalid OS compatibility metadata")
    images = value.get("images")
    if not isinstance(images, dict) or set(images) != {"boot", "root"}:
        raise OsOtaError("OS image set is incomplete")
    for name, image in images.items():
        parsed = urlparse(image.get("url", ""))
        if parsed.scheme != "https" or not parsed.netloc:
            raise OsOtaError("insecure OS image URL: " + name)
        if image.get("compression") != "gzip":
            raise OsOtaError("unsupported OS image compression")
        for size in ("compressed_size", "expanded_size"):
            if not isinstance(image.get(size), int) or image[size] < 1:
                raise OsOtaError("invalid OS image size: " + name)
        for digest in ("compressed_sha256", "expanded_sha256"):
            if not isinstance(image.get(digest), str) or not HEX64.fullmatch(
                    image[digest]):
                raise OsOtaError("invalid OS image digest: " + name)


def verify_image(path, metadata):
    path = Path(path)
    if path.stat().st_size != metadata["compressed_size"] or sha256(path) != metadata[
            "compressed_sha256"]:
        raise OsOtaError("compressed OS image verification failed")
    digest = hashlib.sha256()
    size = 0
    try:
        with gzip.open(path, "rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                size += len(chunk)
                if size > metadata["expanded_size"]:
                    raise OsOtaError("expanded OS image exceeds signed size")
                digest.update(chunk)
    except (OSError, EOFError) as exc:
        raise OsOtaError("invalid compressed OS image") from exc
    if size != metadata["expanded_size"] or digest.hexdigest() != metadata[
            "expanded_sha256"]:
        raise OsOtaError("expanded OS image verification failed")


def download_verified_images(manifest, staging_dir, opener=urlopen):
    """Atomically stage signed-size-bounded HTTPS payloads before slot writes."""
    staging_dir = Path(staging_dir)
    staging_dir.mkdir(parents=True, exist_ok=True)
    completed = {}
    for name in ("root", "boot"):
        metadata = manifest["images"][name]
        parsed = urlparse(metadata["url"])
        if parsed.scheme != "https" or not parsed.netloc:
            raise OsOtaError("insecure OS image URL: " + name)
        destination = staging_dir / (name + "-" + metadata["compressed_sha256"] + ".img.gz")
        temporary = staging_dir / (destination.name + ".part")
        if destination.exists():
            verify_image(destination, metadata)
            completed[name] = destination
            continue
        temporary.unlink(missing_ok=True)
        digest = hashlib.sha256()
        size = 0
        try:
            with opener(metadata["url"], timeout=60) as response, temporary.open("xb") as output:
                while True:
                    chunk = response.read(1024 * 1024)
                    if not chunk:
                        break
                    size += len(chunk)
                    if size > metadata["compressed_size"]:
                        raise OsOtaError("compressed OS image exceeds signed size")
                    output.write(chunk)
                    digest.update(chunk)
                output.flush()
                os.fsync(output.fileno())
            if (size != metadata["compressed_size"]
                    or digest.hexdigest() != metadata["compressed_sha256"]):
                raise OsOtaError("downloaded OS image verification failed")
            os.replace(temporary, destination)
            directory = os.open(staging_dir, os.O_RDONLY)
            try:
                os.fsync(directory)
            finally:
                os.close(directory)
            verify_image(destination, metadata)
            completed[name] = destination
        except Exception as exc:
            temporary.unlink(missing_ok=True)
            if isinstance(exc, OsOtaError):
                raise
            raise OsOtaError("OS image download failed: " + name) from exc
    return completed


def load_verified_manifest(manifest_path, signature_path, public_key, expected):
    verify_signature(public_key, manifest_path, signature_path)
    try:
        value = json.loads(Path(manifest_path).read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise OsOtaError("invalid OS manifest JSON") from exc
    validate_manifest(value, expected)
    return value


def atomic_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=".os-ota-", dir=path.parent)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
            json.dump(value, stream, sort_keys=True)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
        directory = os.open(path.parent, os.O_RDONLY)
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def manifest_identity_digest(manifest):
    """Stable release identity used for failure quarantine and status."""
    encoded = json.dumps(manifest, sort_keys=True, separators=(",", ":")).encode()
    return hashlib.sha256(encoded).hexdigest()


def parse_clock(value):
    try:
        hour, minute = (int(part) for part in value.split(":"))
    except (AttributeError, TypeError, ValueError):
        raise OsOtaError("invalid OS maintenance-window time")
    if hour not in range(24) or minute not in range(60):
        raise OsOtaError("invalid OS maintenance-window time")
    return hour * 60 + minute


def within_maintenance_window(start, end, now=None):
    current_time = now or time.localtime()
    current = current_time.tm_hour * 60 + current_time.tm_min
    beginning, ending = parse_clock(start), parse_clock(end)
    if beginning == ending:
        return True
    if beginning < ending:
        return beginning <= current < ending
    return current >= beginning or current < ending


def installation_gate(busy, window_start, window_end, now=None,
                      override_window=False):
    unknown = set(busy) - BUSY_REASONS
    if unknown:
        raise OsOtaError("unknown OS installation busy reason")
    active = sorted(reason for reason, value in busy.items() if value is True)
    if any(value not in (True, False) for value in busy.values()):
        raise OsOtaError("invalid OS installation busy state")
    if active:
        return {"allowed": False, "reason": "device-busy", "busy": active}
    if not override_window and not within_maintenance_window(
            window_start, window_end, now):
        return {"allowed": False, "reason": "outside-maintenance-window",
                "busy": []}
    return {"allowed": True, "reason": "ready", "busy": []}


def failure_path(state_dir, digest):
    if not isinstance(digest, str) or not HEX64.fullmatch(digest):
        raise OsOtaError("invalid OS manifest failure identity")
    return Path(state_dir) / "failures" / (digest + ".json")


def read_failure(state_dir, digest):
    try:
        value = json.loads(failure_path(state_dir, digest).read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None
    if value.get("schema") != 1 or value.get("manifest_sha256") != digest:
        return None
    return value


def record_failure(state_dir, manifest, error_code, now=None,
                   base_delay=3600, maximum_attempts=3):
    if not _identifier(error_code):
        raise OsOtaError("invalid OS failure code")
    if base_delay < 1 or maximum_attempts < 1:
        raise OsOtaError("invalid OS retry policy")
    digest = manifest_identity_digest(manifest)
    prior = read_failure(state_dir, digest) or {}
    attempts = min(int(prior.get("attempts", 0)) + 1, maximum_attempts)
    timestamp = int(time.time() if now is None else now)
    delay = base_delay * (2 ** min(attempts - 1, 8))
    value = {
        "schema": 1, "manifest_sha256": digest,
        "sequence": manifest["sequence"], "version": manifest["version"],
        "attempts": attempts, "maximum_attempts": maximum_attempts,
        "last_failure": timestamp, "retry_after": timestamp + delay,
        "quarantined": attempts >= maximum_attempts,
        "error_code": error_code,
    }
    atomic_json(failure_path(state_dir, digest), value)
    return value


def retry_status(state_dir, manifest, now=None):
    failure = read_failure(state_dir, manifest_identity_digest(manifest))
    if not failure:
        return {"allowed": True, "reason": "not-failed"}
    if failure["quarantined"]:
        return {"allowed": False, "reason": "quarantined",
                "attempts": failure["attempts"]}
    timestamp = int(time.time() if now is None else now)
    if timestamp < failure["retry_after"]:
        return {"allowed": False, "reason": "backoff",
                "retry_after": failure["retry_after"],
                "attempts": failure["attempts"]}
    return {"allowed": True, "reason": "retry",
            "attempts": failure["attempts"]}


def clear_failure(state_dir, manifest):
    path = failure_path(state_dir, manifest_identity_digest(manifest))
    try:
        path.unlink()
    except FileNotFoundError:
        pass


def owner_safe_status(journal=None, failure=None, gate=None):
    """Return status that deliberately excludes paths, URLs and error details."""
    value = {"schema": 1, "state": "idle", "action_required": False}
    if journal:
        value.update({"state": journal.get("phase", "unknown"),
                      "sequence": journal.get("sequence"),
                      "version": journal.get("version")})
    if gate and not gate.get("allowed", False):
        value.update({"state": "deferred", "reason": gate.get("reason")})
    if failure:
        value.update({"state": "quarantined" if failure.get("quarantined") else "backoff",
                      "attempts": failure.get("attempts"),
                      "retry_after": failure.get("retry_after"),
                      "action_required": bool(failure.get("quarantined"))})
    return {key: item for key, item in value.items() if item is not None}


def _same_device(first, second):
    try:
        return os.path.samefile(first, second)
    except FileNotFoundError:
        return False


def validate_inactive_targets(targets, active_paths, allow_regular=False):
    if set(targets) != {"boot", "root"} or set(active_paths) != {"boot", "root"}:
        raise OsOtaError("complete active and inactive boot/root paths are required")
    resolved = {}
    for name, value in targets.items():
        path = Path(value)
        if path.is_symlink() or not path.exists():
            raise OsOtaError("inactive %s target is missing or a symlink" % name)
        mode = path.stat().st_mode
        if not stat.S_ISBLK(mode) and not (allow_regular and stat.S_ISREG(mode)):
            raise OsOtaError("inactive %s target is not a block device" % name)
        resolved[name] = path.resolve()
    if _same_device(resolved["boot"], resolved["root"]):
        raise OsOtaError("inactive boot and root targets overlap")
    for target_name, target in resolved.items():
        for active_name, active in active_paths.items():
            if _same_device(target, Path(active)):
                raise OsOtaError("inactive %s target overlaps active %s" %
                                 (target_name, active_name))
    return resolved


def target_capacity(path):
    try:
        with Path(path).open("rb", buffering=0) as stream:
            return os.lseek(stream.fileno(), 0, os.SEEK_END)
    except OSError as exc:
        raise OsOtaError("cannot determine inactive target capacity") from exc


def write_verified_image(source, metadata, target, allow_regular=False):
    source = Path(source)
    target = Path(target)
    verify_image(source, metadata)
    mode = target.stat().st_mode
    if stat.S_ISREG(mode) and not allow_regular:
        raise OsOtaError("regular-file OS target is test-only")
    if target_capacity(target) < metadata["expanded_size"]:
        raise OsOtaError("inactive target is smaller than signed image")
    digest = hashlib.sha256()
    written = 0
    try:
        with gzip.open(source, "rb") as encoded, target.open("r+b", buffering=0) as output:
            while True:
                chunk = encoded.read(1024 * 1024)
                if not chunk:
                    break
                written += len(chunk)
                if written > metadata["expanded_size"]:
                    raise OsOtaError("expanded OS image exceeds signed size")
                output.write(chunk)
                digest.update(chunk)
            output.flush()
            os.fsync(output.fileno())
    except (OSError, EOFError) as exc:
        raise OsOtaError("inactive-slot write failed") from exc
    if (written != metadata["expanded_size"]
            or digest.hexdigest() != metadata["expanded_sha256"]):
        raise OsOtaError("written OS image does not match signed metadata")
    readback = hashlib.sha256()
    remaining = metadata["expanded_size"]
    with target.open("rb", buffering=0) as stream:
        while remaining:
            chunk = stream.read(min(1024 * 1024, remaining))
            if not chunk:
                raise OsOtaError("short read while verifying inactive slot")
            remaining -= len(chunk)
            readback.update(chunk)
    if readback.hexdigest() != metadata["expanded_sha256"]:
        raise OsOtaError("inactive-slot readback verification failed")


def write_inactive_images(manifest, downloads, targets, active_paths, journal,
                          allow_regular=False):
    if set(downloads) != {"boot", "root"}:
        raise OsOtaError("both downloaded OS images are required")
    resolved = validate_inactive_targets(targets, active_paths, allow_regular)
    # Validate both sources before changing either inactive target.
    for name in ("root", "boot"):
        verify_image(downloads[name], manifest["images"][name])
        if target_capacity(resolved[name]) < manifest["images"][name]["expanded_size"]:
            raise OsOtaError("inactive %s target is smaller than signed image" % name)
    transaction = {
        "schema": 1, "operation": "os-slot-write",
        "sequence": manifest["sequence"], "version": manifest["version"],
        "source_commit": manifest["source_commit"], "layout_id": manifest["layout_id"],
        "manifest_sha256": manifest_identity_digest(manifest),
        "phase": "verified-downloads", "inactive_targets": {
            name: str(path) for name, path in resolved.items()
        },
    }
    atomic_json(journal, transaction)
    for name in ("root", "boot"):
        write_verified_image(downloads[name], manifest["images"][name],
                             resolved[name], allow_regular)
        transaction["phase"] = name + "-written-and-readback-verified"
        atomic_json(journal, transaction)
    transaction["phase"] = "candidate-written"
    atomic_json(journal, transaction)
    return transaction


def read_bootloader_integer(path):
    try:
        value = Path(path).read_bytes()
    except OSError as exc:
        raise OsOtaError("cannot read firmware boot state") from exc
    if len(value) == 4:
        return struct.unpack(">I", value)[0]
    try:
        text = value.rstrip(b"\0\r\n").decode("ascii")
        if text and text.isdigit():
            return int(text)
    except UnicodeDecodeError:
        pass
    raise OsOtaError("invalid firmware boot-state encoding")


def render_autoboot(normal_partition, try_partition):
    if (not isinstance(normal_partition, int) or not isinstance(try_partition, int)
            or not 1 <= normal_partition <= 63 or not 1 <= try_partition <= 63
            or normal_partition == try_partition):
        raise OsOtaError("invalid or overlapping boot partitions")
    value = ("[all]\ntryboot_a_b=1\nboot_partition=%d\n"
             "[tryboot]\nboot_partition=%d\n" %
             (normal_partition, try_partition))
    if len(value.encode("ascii")) > 512:
        raise OsOtaError("autoboot selector exceeds firmware limit")
    return value


def parse_autoboot(value):
    match = re.fullmatch(
        r"\[all\]\ntryboot_a_b=1\nboot_partition=([0-9]{1,2})\n"
        r"\[tryboot\]\nboot_partition=([0-9]{1,2})\n", value)
    if not match:
        raise OsOtaError("autoboot selector has unexpected directives")
    normal, candidate = (int(match.group(1)), int(match.group(2)))
    render_autoboot(normal, candidate)
    return normal, candidate


def atomic_text(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=".autoboot-", dir=path.parent)
    try:
        with os.fdopen(descriptor, "w", encoding="ascii", newline="\n") as stream:
            stream.write(value)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
        directory = os.open(path.parent, os.O_RDONLY)
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def read_journal(path):
    try:
        value = json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise OsOtaError("missing or invalid OS transaction journal") from exc
    if value.get("schema") != 1 or value.get("operation") != "os-slot-write":
        raise OsOtaError("unexpected OS transaction journal")
    return value


def arm_tryboot(selector_path, journal_path, normal_partition,
                candidate_partition, reboot=False, runner=subprocess.run):
    journal = read_journal(journal_path)
    if journal.get("phase") != "candidate-written":
        raise OsOtaError("inactive images are not durably verified")
    selector = render_autoboot(normal_partition, candidate_partition)
    atomic_text(selector_path, selector)
    if parse_autoboot(Path(selector_path).read_text(encoding="ascii")) != (
            normal_partition, candidate_partition):
        raise OsOtaError("autoboot selector readback failed")
    journal["normal_boot_partition"] = normal_partition
    journal["candidate_boot_partition"] = candidate_partition
    journal["phase"] = "tryboot-armed"
    atomic_json(journal_path, journal)
    if reboot:
        result = runner(["reboot", "0 tryboot"], check=False)
        if result.returncode:
            raise OsOtaError("cannot request one-shot tryboot")
    return journal


def record_boot_health(journal_path, checks):
    journal = read_journal(journal_path)
    if journal.get("phase") != "tryboot-armed":
        raise OsOtaError("candidate boot is not armed")
    if set(checks) != REQUIRED_BOOT_HEALTH or not all(
            value is True for value in checks.values()):
        raise OsOtaError("candidate boot health gate is incomplete or failed")
    journal["health_checks"] = dict(sorted(checks.items()))
    journal["phase"] = "health-passed"
    atomic_json(journal_path, journal)
    return journal


def reject_boot_health(journal_path, checks, state_dir, manifest, error_code,
                       now=None, base_delay=3600, maximum_attempts=3):
    journal = read_journal(journal_path)
    if journal.get("phase") != "tryboot-armed":
        raise OsOtaError("candidate boot is not armed")
    if set(checks) != REQUIRED_BOOT_HEALTH or any(
            value not in (True, False) for value in checks.values()):
        raise OsOtaError("candidate boot health result is incomplete")
    digest = manifest_identity_digest(manifest)
    recorded = journal.get("manifest_sha256")
    if recorded is not None and recorded != digest:
        raise OsOtaError("candidate manifest does not match transaction")
    failure = record_failure(state_dir, manifest, error_code, now,
                             base_delay, maximum_attempts)
    journal["health_checks"] = dict(sorted(checks.items()))
    journal["failed_checks"] = sorted(name for name, passed in checks.items()
                                      if not passed)
    journal["failure"] = {
        "manifest_sha256": digest,
        "attempts": failure["attempts"],
        "retry_after": failure["retry_after"],
        "quarantined": failure["quarantined"],
        "error_code": failure["error_code"],
    }
    journal["phase"] = "health-failed"
    atomic_json(journal_path, journal)
    return journal


def commit_tryboot(selector_path, journal_path, boot_partition, tryboot,
                   installed_sequence_path=None):
    journal = read_journal(journal_path)
    if journal.get("phase") != "health-passed":
        raise OsOtaError("candidate health has not passed")
    normal = journal.get("normal_boot_partition")
    candidate = journal.get("candidate_boot_partition")
    if tryboot != 1 or boot_partition != candidate:
        raise OsOtaError("refusing commit outside the expected one-shot candidate boot")
    current = parse_autoboot(Path(selector_path).read_text(encoding="ascii"))
    if current != (normal, candidate):
        raise OsOtaError("autoboot selector changed during candidate transaction")
    atomic_text(selector_path, render_autoboot(candidate, normal))
    if parse_autoboot(Path(selector_path).read_text(encoding="ascii")) != (
            candidate, normal):
        raise OsOtaError("committed autoboot selector readback failed")
    journal["phase"] = "committed"
    journal["active_boot_partition"] = candidate
    if installed_sequence_path is not None:
        atomic_text(installed_sequence_path, "%d\n" % journal["sequence"])
    atomic_json(journal_path, journal)
    return journal


def commit_tryboot_from_device_tree(selector_path, journal_path,
                                    partition_path, tryboot_path,
                                    installed_sequence_path=None):
    return commit_tryboot(
        selector_path, journal_path,
        read_bootloader_integer(partition_path),
        read_bootloader_integer(tryboot_path),
        installed_sequence_path,
    )
