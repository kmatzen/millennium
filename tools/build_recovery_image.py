#!/usr/bin/env python3
"""Create and optionally sign a canonical whole-disk recovery-image manifest."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import subprocess


PARTITIONS = [
    {"number": 1, "role": "bootconfig", "type": "0x0c", "size_bytes": 32 * 1024**2},
    {"number": 2, "role": "boot_a", "type": "0x0c", "size_bytes": 256 * 1024**2},
    {"number": 3, "role": "boot_b", "type": "0x0c", "size_bytes": 256 * 1024**2},
    {"number": 5, "role": "system_a", "type": "0x83", "size_bytes": 3 * 1024**3},
    {"number": 6, "role": "system_b", "type": "0x83", "size_bytes": 3 * 1024**3},
    {"number": 7, "role": "persistent", "type": "0x83", "size_bytes": 8 * 1024**3},
]


def canonical(value):
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


def digest_stream(stream):
    digest = hashlib.sha256()
    size = 0
    for chunk in iter(lambda: stream.read(1024 * 1024), b""):
        size += len(chunk)
        digest.update(chunk)
    return size, digest.hexdigest()


def compressed_metadata(path):
    if not path.is_file() or path.stat().st_size == 0:
        raise SystemExit("missing or empty recovery image: %s" % path)
    with path.open("rb") as stream:
        compressed_size, compressed_sha256 = digest_stream(stream)
    process = subprocess.Popen(["zstd", "--quiet", "--decompress", "--stdout", str(path)],
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    expanded_size, expanded_sha256 = digest_stream(process.stdout)
    stderr = process.communicate()[1]
    if process.returncode:
        raise SystemExit("invalid Zstandard recovery image: %s" %
                         stderr.decode(errors="replace").strip())
    return {
        "filename": path.name,
        "compression": "zstd",
        "compressed_size": compressed_size,
        "compressed_sha256": compressed_sha256,
        "expanded_size": expanded_size,
        "expanded_sha256": expanded_sha256,
    }


def safe_identifier(value, label):
    if not re.fullmatch(r"[A-Za-z0-9._-]{1,64}", value):
        raise SystemExit("invalid %s" % label)
    return value


def source_commit(value):
    value = value.lower()
    if not re.fullmatch(r"[0-9a-f]{40}", value):
        raise SystemExit("source commit must be 40 lowercase hexadecimal characters")
    return value


def sign(private_key, manifest, signature):
    result = subprocess.run([
        "openssl", "pkeyutl", "-sign", "-rawin", "-inkey", str(private_key),
        "-in", str(manifest), "-out", str(signature),
    ], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode:
        raise SystemExit("cannot sign recovery manifest: %s" %
                         result.stderr.decode(errors="replace").strip())


def build_manifest(args, image):
    return {
        "schema": 1,
        "kind": "millennium-recovery-image",
        "version": safe_identifier(args.version, "version"),
        "published_at": datetime.now(timezone.utc).replace(
            microsecond=0).isoformat().replace("+00:00", "Z"),
        "key_id": safe_identifier(args.key_id, "key ID"),
        "source_commit": source_commit(args.source_commit),
        "board_models": sorted(set(args.board_model)),
        "architecture": safe_identifier(args.architecture, "architecture"),
        "layout": {
            "id": safe_identifier(args.layout_id, "layout ID"),
            "table": "mbr",
            "sector_size": 512,
            "slot_map": {
                "a": {"boot_partition": 2, "system_partition": 5},
                "b": {"boot_partition": 3, "system_partition": 6},
            },
            "partitions": PARTITIONS,
        },
        "image": image,
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--board-model", action="append", required=True)
    parser.add_argument("--architecture", default="arm64")
    parser.add_argument("--layout-id", default="zero2w-ab-mbr-v1")
    parser.add_argument("--key-id", default="release-2026-08")
    parser.add_argument("--private-key", type=Path)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    if any(not model.strip() or len(model.encode()) > 128 for model in args.board_model):
        raise SystemExit("invalid board model")
    image = compressed_metadata(args.image)
    args.output_dir.mkdir(parents=True, exist_ok=False)
    manifest = args.output_dir / "recovery-manifest.json"
    manifest.write_bytes(canonical(build_manifest(args, image)))
    signature = args.output_dir / "recovery-manifest.json.sig"
    if args.private_key:
        sign(args.private_key, manifest, signature)
    print(json.dumps({"manifest": str(manifest),
                      "signature": str(signature) if args.private_key else None},
                     sort_keys=True))


if __name__ == "__main__":
    main()
