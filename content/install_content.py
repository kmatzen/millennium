#!/usr/bin/env python3
"""Verify and atomically activate a signed Millennium story package."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile

from storytool import canonical, compile_runtime, validate


class InstallError(RuntimeError):
    pass


CAPABILITIES = {"audio", "display", "handset", "keypad", "coin",
                "credential", "timers", "state"}
SEMVER_RE = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+$")
ID_RE = re.compile(r"^[a-z0-9][a-z0-9._-]{0,63}$")
DIGEST_RE = re.compile(r"^[0-9a-f]{64}$")


def digest(path):
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def trusted_keys(values):
    keys = {}
    for value in values:
        try:
            key_id, filename = value.split(":", 1)
        except ValueError as exc:
            raise InstallError("trusted key must be ID:PATH") from exc
        if not re.fullmatch(r"[A-Za-z0-9._-]{1,64}", key_id):
            raise InstallError("invalid trusted key ID")
        keys[key_id] = Path(filename)
    return keys


def verify_signature(manifest_path, signature_path, key_path):
    result = subprocess.run(
        ["openssl", "pkeyutl", "-verify", "-rawin", "-pubin", "-inkey",
         str(key_path), "-in", str(manifest_path), "-sigfile", str(signature_path)],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
    if result.returncode == 0:
        return
    try:
        from cryptography.hazmat.primitives import serialization
        key = serialization.load_pem_public_key(key_path.read_bytes())
        key.verify(signature_path.read_bytes(), manifest_path.read_bytes())
    except Exception as exc:
        raise InstallError("content manifest signature is invalid") from exc


def safe_extract(archive, destination, maximum_bytes=256 * 1024 * 1024):
    with tarfile.open(archive, "r:gz") as bundle:
        members = bundle.getmembers()
        total = 0
        for member in members:
            path = Path(member.name)
            if path.is_absolute() or ".." in path.parts or member.issym() or member.islnk():
                raise InstallError(f"unsafe archive member: {member.name}")
            if not member.isfile() and not member.isdir():
                raise InstallError(f"unsupported archive member: {member.name}")
            if member.isfile():
                total += member.size
                if member.size < 0 or total > maximum_bytes:
                    raise InstallError("content archive exceeds its extraction limit")
        # Avoid tarfile.extractall(filter=...), which is unavailable on the
        # older Python shipped by some supported Raspberry Pi OS images.  The
        # member validation above deliberately permits only plain files and
        # directories, and each destination is resolved again before writing.
        destination = destination.resolve()
        for member in members:
            target = (destination / member.name).resolve()
            try:
                target.relative_to(destination)
            except ValueError as exc:
                raise InstallError(f"unsafe archive member: {member.name}") from exc
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True)
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            source = bundle.extractfile(member)
            if source is None:
                raise InstallError(f"cannot read archive member: {member.name}")
            with source, target.open("wb") as output:
                shutil.copyfileobj(source, output)
            os.chmod(target, member.mode & 0o777)


def validate_manifest(manifest):
    legacy = {"schema", "id", "version", "key_id", "bundle", "sha256"}
    required = legacy | {"sequence", "size", "compatibility", "capabilities",
                         "rating", "locales", "quotas", "files", "state"}
    schema = manifest.get("schema") if isinstance(manifest, dict) else None
    if schema == 1 and set(manifest) == legacy:
        return
    if schema != 2 or set(manifest) != required:
        raise InstallError("content manifest schema is invalid")
    if (not ID_RE.fullmatch(str(manifest["id"]))
            or not SEMVER_RE.fullmatch(str(manifest["version"]))
            or not re.fullmatch(r"[A-Za-z0-9._-]{1,64}", str(manifest["key_id"]))
            or not re.fullmatch(r"[a-z0-9._-]+\.tar\.gz", str(manifest["bundle"]))
            or not DIGEST_RE.fullmatch(str(manifest["sha256"]))):
        raise InstallError("content manifest identity is invalid")
    if (not isinstance(manifest["sequence"], int)
            or isinstance(manifest["sequence"], bool) or manifest["sequence"] < 1):
        raise InstallError("content manifest sequence is invalid")
    if (not isinstance(manifest["size"], int) or isinstance(manifest["size"], bool)
            or not 1 <= manifest["size"] <= 256 * 1024 * 1024):
        raise InstallError("content manifest size is invalid")
    compatibility = manifest["compatibility"]
    if (not isinstance(compatibility, dict)
            or not {"runtime_schema_min", "runtime_schema_max"} <= set(compatibility)
            or set(compatibility) - {"runtime_schema_min", "runtime_schema_max",
                                     "daemon_min", "daemon_max"}):
        raise InstallError("content compatibility is invalid")
    minimum = compatibility.get("runtime_schema_min")
    maximum = compatibility.get("runtime_schema_max")
    if (not isinstance(minimum, int) or isinstance(minimum, bool)
            or not isinstance(maximum, int) or isinstance(maximum, bool)
            or minimum < 1 or maximum < minimum):
        raise InstallError("content runtime schema range is invalid")
    for field in ("daemon_min", "daemon_max"):
        if field in compatibility and not SEMVER_RE.fullmatch(str(compatibility[field])):
            raise InstallError("content daemon compatibility is invalid")
    capabilities = manifest["capabilities"]
    if (not isinstance(capabilities, list) or len(capabilities) != len(set(capabilities))
            or any(item not in CAPABILITIES for item in capabilities)):
        raise InstallError("content capabilities are invalid")
    if manifest["rating"] not in {"everyone", "teen", "mature"}:
        raise InstallError("content rating is invalid")
    locales = manifest["locales"]
    if (not isinstance(locales, list) or not locales or len(locales) != len(set(locales))
            or any(not isinstance(item, str) or
                   not re.fullmatch(r"[a-z]{2,3}(-[A-Z]{2})?", item)
                   for item in locales)):
        raise InstallError("content locales are invalid")
    quotas = manifest["quotas"]
    limits = {"storage_bytes": (1, 256 * 1024 * 1024),
              "state_bytes": (0, 1024 * 1024), "session_seconds": (1, 86400)}
    if not isinstance(quotas, dict) or set(quotas) != set(limits):
        raise InstallError("content quotas are invalid")
    for field, (low, high) in limits.items():
        value = quotas[field]
        if not isinstance(value, int) or isinstance(value, bool) or not low <= value <= high:
            raise InstallError("content quotas are invalid")
    state = manifest["state"]
    if (not isinstance(state, dict)
            or set(state) - {"schema", "rollback_compatible", "migrate_from"}
            or not {"schema", "rollback_compatible"} <= set(state)
            or not isinstance(state["schema"], int) or isinstance(state["schema"], bool)
            or state["schema"] < 1 or not isinstance(state["rollback_compatible"], bool)):
        raise InstallError("content state contract is invalid")
    migrate = state.get("migrate_from", [])
    if (not isinstance(migrate, list) or len(migrate) != len(set(migrate))
            or any(not isinstance(item, int) or isinstance(item, bool) or item < 1
                   for item in migrate)):
        raise InstallError("content state migration contract is invalid")
    files = manifest["files"]
    if not isinstance(files, list) or len(files) < 2:
        raise InstallError("content file inventory is invalid")
    seen = set()
    for item in files:
        if (not isinstance(item, dict) or set(item) != {"path", "sha256", "size"}
                or not isinstance(item["path"], str)
                or not re.fullmatch(r"[A-Za-z0-9_.-]+(/[A-Za-z0-9_.-]+)*", item["path"])
                or item["path"] in seen or not DIGEST_RE.fullmatch(str(item["sha256"]))
                or not isinstance(item["size"], int) or isinstance(item["size"], bool)
                or item["size"] < 0):
            raise InstallError("content file inventory is invalid")
        seen.add(item["path"])
    if {"story.json", "story.mst"} - seen:
        raise InstallError("content file inventory omits required files")


def verify_inventory(stage, manifest):
    if manifest["schema"] == 1:
        return
    expected = {item["path"]: item for item in manifest["files"]}
    actual = {path.relative_to(stage).as_posix(): path
              for path in stage.rglob("*") if path.is_file()}
    if set(actual) != set(expected):
        raise InstallError("content file inventory does not match archive")
    total = 0
    for name, path in actual.items():
        total += path.stat().st_size
        if (path.stat().st_size != expected[name]["size"]
                or digest(path) != expected[name]["sha256"]):
            raise InstallError("content file digest does not match inventory")
    if total > manifest["quotas"]["storage_bytes"]:
        raise InstallError("content package exceeds its storage quota")


def atomic_link(root, name, target):
    temporary = root / ("." + name + ".new")
    temporary.unlink(missing_ok=True)
    temporary.symlink_to(target)
    os.replace(temporary, root / name)


def atomic_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name("." + path.name + ".new")
    with temporary.open("wb") as output:
        output.write(canonical(value))
        output.flush()
        os.fsync(output.fileno())
    os.replace(temporary, path)


def semver(value):
    return tuple(int(item) for item in value.split("."))


def install(manifest_path, signature_path, keys, root, runtime_schema=1,
            daemon_version=None):
    try:
        manifest = json.loads(manifest_path.read_text())
    except (OSError, json.JSONDecodeError) as exc:
        raise InstallError(f"invalid content manifest: {exc}") from exc
    validate_manifest(manifest)
    key = keys.get(manifest["key_id"])
    if key is None:
        raise InstallError("content manifest uses an untrusted key ID")
    verify_signature(manifest_path, signature_path, key)
    if manifest["schema"] == 2:
        compatibility = manifest["compatibility"]
        if not (compatibility["runtime_schema_min"] <= runtime_schema
                <= compatibility["runtime_schema_max"]):
            raise InstallError("content package is incompatible with this runtime schema")
        if daemon_version is not None:
            if not SEMVER_RE.fullmatch(daemon_version):
                raise InstallError("installed daemon version is invalid")
            current = semver(daemon_version)
            if ("daemon_min" in compatibility
                    and current < semver(compatibility["daemon_min"])):
                raise InstallError("content package requires a newer daemon")
            if ("daemon_max" in compatibility
                    and current > semver(compatibility["daemon_max"])):
                raise InstallError("content package requires an older daemon")
        sequences_path = root / "sequences.json"
        try:
            sequences = json.loads(sequences_path.read_text())
        except FileNotFoundError:
            sequences = {}
        except (OSError, json.JSONDecodeError) as exc:
            raise InstallError("content anti-rollback state is invalid") from exc
        installed_sequence = sequences.get(manifest["id"], 0)
        if (not isinstance(installed_sequence, int) or installed_sequence < 0
                or manifest["sequence"] <= installed_sequence):
            raise InstallError("content manifest attempts a sequence rollback")
    archive = manifest_path.parent / manifest["bundle"]
    if (not archive.is_file() or digest(archive) != manifest["sha256"]
            or (manifest["schema"] == 2 and archive.stat().st_size != manifest["size"])):
        raise InstallError("content bundle digest does not match manifest")
    identity = f"{manifest['id']}-{manifest['version']}"
    if not re.fullmatch(r"[a-z0-9][a-z0-9._-]{0,63}-[0-9]+\.[0-9]+\.[0-9]+", identity):
        raise InstallError("content identity is invalid")
    releases = root / "releases"
    releases.mkdir(parents=True, exist_ok=True)
    target = releases / identity
    if target.exists():
        raise InstallError("content release already exists; immutable releases cannot be overwritten")
    with tempfile.TemporaryDirectory(dir=releases, prefix=".install-") as temporary:
        stage = Path(temporary)
        maximum = (manifest["quotas"]["storage_bytes"]
                   if manifest["schema"] == 2 else 256 * 1024 * 1024)
        safe_extract(archive, stage, maximum)
        verify_inventory(stage, manifest)
        story_path = stage / "story.json"
        runtime_path = stage / "story.mst"
        try:
            story = json.loads(story_path.read_text())
        except (OSError, json.JSONDecodeError) as exc:
            raise InstallError(f"packaged story is invalid: {exc}") from exc
        errors = validate(story, stage)
        if errors:
            raise InstallError("packaged story validation failed: " + "; ".join(errors))
        if runtime_path.read_bytes() != compile_runtime(story):
            raise InstallError("compiled story does not match reviewed story JSON")
        shutil.copytree(stage, target)
        # TemporaryDirectory is deliberately 0700. copytree preserves that
        # mode on the destination root, which would make an otherwise valid
        # release unreadable by the unprivileged daemon. Story payloads are
        # immutable public appliance data; the archive retains file/subdir
        # modes, while the release root must always be traversable.
        os.chmod(target, 0o755)
    current = root / "current"
    if current.is_symlink():
        atomic_link(root, "previous", os.readlink(current))
    atomic_link(root, "current", Path("releases") / identity)
    status = {"id": manifest["id"], "version": manifest["version"],
              "schema": manifest["schema"],
              "sequence": manifest.get("sequence", 0),
              "identity": identity, "manifest_sha256": digest(manifest_path)}
    atomic_json(root / "status.json", status)
    if manifest["schema"] == 2:
        sequences[manifest["id"]] = manifest["sequence"]
        atomic_json(root / "sequences.json", sequences)
    return status


def rollback(root):
    previous = root / "previous"
    current = root / "current"
    if not previous.is_symlink():
        raise InstallError("no previous content release is available")
    previous_target = os.readlink(previous)
    current_target = os.readlink(current) if current.is_symlink() else None
    atomic_link(root, "current", previous_target)
    if current_target:
        atomic_link(root, "previous", current_target)
    return previous_target


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=("install", "rollback", "status"))
    parser.add_argument("--root", type=Path, default=Path("/var/lib/millennium/content"))
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--signature", type=Path)
    parser.add_argument("--trusted-key", action="append", default=[])
    parser.add_argument("--runtime-schema", type=int, default=1)
    parser.add_argument("--daemon-version")
    args = parser.parse_args()
    if args.command == "install":
        if not args.manifest or not args.signature or not args.trusted_key:
            parser.error("install requires manifest, signature, and trusted key")
        print(json.dumps(install(args.manifest, args.signature,
                                 trusted_keys(args.trusted_key), args.root,
                                 args.runtime_schema, args.daemon_version), sort_keys=True))
    elif args.command == "rollback":
        print(rollback(args.root))
    else:
        status = args.root / "status.json"
        print(status.read_text().strip() if status.exists() else "{}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except InstallError as exc:
        print(f"ERROR: {exc}")
        raise SystemExit(1)
