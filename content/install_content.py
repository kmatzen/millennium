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


def safe_extract(archive, destination):
    with tarfile.open(archive, "r:gz") as bundle:
        members = bundle.getmembers()
        for member in members:
            path = Path(member.name)
            if path.is_absolute() or ".." in path.parts or member.issym() or member.islnk():
                raise InstallError(f"unsafe archive member: {member.name}")
            if not member.isfile() and not member.isdir():
                raise InstallError(f"unsupported archive member: {member.name}")
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


def atomic_link(root, name, target):
    temporary = root / ("." + name + ".new")
    temporary.unlink(missing_ok=True)
    temporary.symlink_to(target)
    os.replace(temporary, root / name)


def install(manifest_path, signature_path, keys, root):
    try:
        manifest = json.loads(manifest_path.read_text())
    except (OSError, json.JSONDecodeError) as exc:
        raise InstallError(f"invalid content manifest: {exc}") from exc
    required = {"schema", "id", "version", "key_id", "bundle", "sha256"}
    if set(manifest) != required or manifest["schema"] != 1:
        raise InstallError("content manifest schema is invalid")
    key = keys.get(manifest["key_id"])
    if key is None:
        raise InstallError("content manifest uses an untrusted key ID")
    verify_signature(manifest_path, signature_path, key)
    archive = manifest_path.parent / manifest["bundle"]
    if not archive.is_file() or digest(archive) != manifest["sha256"]:
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
        safe_extract(archive, stage)
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
    current = root / "current"
    if current.is_symlink():
        atomic_link(root, "previous", os.readlink(current))
    atomic_link(root, "current", Path("releases") / identity)
    status = {"id": manifest["id"], "version": manifest["version"],
              "identity": identity, "manifest_sha256": digest(manifest_path)}
    (root / "status.json").write_bytes(canonical(status))
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
    args = parser.parse_args()
    if args.command == "install":
        if not args.manifest or not args.signature or not args.trusted_key:
            parser.error("install requires manifest, signature, and trusted key")
        print(json.dumps(install(args.manifest, args.signature,
                                 trusted_keys(args.trusted_key), args.root), sort_keys=True))
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
