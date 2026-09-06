#!/usr/bin/env python3
"""Build and optionally sign a canonical Millennium A/B OS release manifest."""

import argparse
from datetime import datetime, timezone
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess


ROOT = Path(__file__).resolve().parents[1]


def canonical(value):
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def checked_commit(explicit):
    if explicit:
        commit = explicit.lower()
    else:
        try:
            commit = subprocess.run(
                ["git", "-C", str(ROOT), "rev-parse", "HEAD"], check=True,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
            ).stdout.strip().lower()
            status = subprocess.run(
                ["git", "-C", str(ROOT), "status", "--porcelain",
                 "--untracked-files=normal"], check=True, stdout=subprocess.PIPE,
                stderr=subprocess.PIPE, text=True,
            ).stdout
        except (OSError, subprocess.CalledProcessError) as exc:
            raise SystemExit("cannot determine OS release source commit: %s" % exc)
        if status:
            raise SystemExit("refusing to build an OS release from a dirty worktree")
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise SystemExit("source commit must be 40 lowercase hexadecimal characters")
    return commit


def compress_image(source, destination):
    if not source.is_file() or source.stat().st_size == 0:
        raise SystemExit("missing or empty image: %s" % source)
    with source.open("rb") as raw, destination.open("wb") as output:
        with gzip.GzipFile(filename="", mode="wb", fileobj=output, mtime=0) as encoded:
            shutil.copyfileobj(raw, encoded, length=1024 * 1024)


def image_metadata(source, compressed, url):
    return {
        "url": url,
        "compression": "gzip",
        "compressed_size": compressed.stat().st_size,
        "compressed_sha256": sha256(compressed),
        "expanded_size": source.stat().st_size,
        "expanded_sha256": sha256(source),
    }


def sign(private_key, manifest, signature):
    result = subprocess.run([
        "openssl", "pkeyutl", "-sign", "-rawin", "-inkey", str(private_key),
        "-in", str(manifest), "-out", str(signature),
    ], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode:
        raise SystemExit("cannot sign OS manifest: %s" %
                         result.stderr.decode(errors="replace").strip())


def identifier(value, label):
    if not re.fullmatch(r"[A-Za-z0-9._-]{1,64}", value):
        raise SystemExit("invalid %s" % label)
    return value


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sequence", type=int, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--base-url", required=True)
    parser.add_argument("--boot-image", type=Path, required=True)
    parser.add_argument("--root-image", type=Path, required=True)
    parser.add_argument("--layout-id", required=True)
    parser.add_argument("--board-model", action="append", required=True)
    parser.add_argument("--architecture", default="armv7l")
    parser.add_argument("--key-id", default="release-2026-08")
    parser.add_argument("--channel", default="stable")
    parser.add_argument("--device-groups", default="production")
    parser.add_argument("--minimum-application-version", required=True)
    parser.add_argument("--minimum-mcu-version", required=True)
    parser.add_argument("--persistent-state-schema", type=int, required=True)
    parser.add_argument("--source-commit")
    parser.add_argument("--hold", action="store_true")
    parser.add_argument("--withdrawn", action="store_true")
    parser.add_argument("--private-key", type=Path)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()

    if args.sequence < 0 or args.persistent_state_schema < 1:
        raise SystemExit("sequence and state schema must be positive")
    if not args.base_url.startswith("https://"):
        raise SystemExit("OS release base URL must use HTTPS")
    commit = checked_commit(args.source_commit)
    for value, label in ((args.version, "version"), (args.layout_id, "layout ID"),
                         (args.architecture, "architecture"),
                         (args.key_id, "key ID"), (args.channel, "channel")):
        identifier(value, label)
    models = sorted(set(args.board_model))
    if any(not model.strip() or len(model.encode()) > 128 for model in models):
        raise SystemExit("invalid board model")
    groups = sorted(set(item.strip() for item in args.device_groups.split(",")
                        if item.strip()))
    if not groups or any(not re.fullmatch(r"[A-Za-z0-9._-]{1,64}", item)
                         for item in groups):
        raise SystemExit("invalid rollout groups")

    args.output_dir.mkdir(parents=True, exist_ok=False)
    release_id = "%08d-%s" % (args.sequence, args.version)
    base = args.base_url.rstrip("/") + "/releases/" + release_id
    images = {}
    for name, source in (("boot", args.boot_image), ("root", args.root_image)):
        filename = "%s-%s.img.gz" % (name, release_id)
        destination = args.output_dir / filename
        compress_image(source, destination)
        images[name] = image_metadata(source, destination, base + "/" + filename)
    manifest = {
        "schema": 1,
        "kind": "millennium-os-release",
        "channel": args.channel,
        "key_id": args.key_id,
        "version": args.version,
        "sequence": args.sequence,
        "published_at": datetime.now(timezone.utc).replace(
            microsecond=0).isoformat().replace("+00:00", "Z"),
        "source_commit": commit,
        "architecture": args.architecture,
        "board_models": models,
        "layout_id": args.layout_id,
        "compatibility": {
            "minimum_application_version": args.minimum_application_version,
            "minimum_mcu_version": args.minimum_mcu_version,
            "persistent_state_schema": args.persistent_state_schema,
        },
        "rollout": {"groups": groups, "hold": args.hold,
                    "withdrawn": args.withdrawn},
        "images": images,
    }
    manifest_path = args.output_dir / "manifest.json"
    manifest_path.write_bytes(canonical(manifest))
    signature = args.output_dir / "manifest.json.sig"
    if args.private_key:
        sign(args.private_key, manifest_path, signature)
    print(json.dumps({"release_id": release_id, "manifest": str(manifest_path),
                      "signature": str(signature) if args.private_key else None},
                     sort_keys=True))


if __name__ == "__main__":
    main()
