#!/usr/bin/env python3
"""Validation primitives for fail-safe Millennium A/B operating-system OTA."""

import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import subprocess
import tempfile
from urllib.parse import urlparse


class OsOtaError(Exception):
    pass


HEX64 = re.compile(r"[0-9a-f]{64}")
HEX40 = re.compile(r"[0-9a-f]{40}")
IDENTIFIER = re.compile(r"[A-Za-z0-9._-]{1,64}")


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
