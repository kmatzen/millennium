#!/usr/bin/env python3
"""Build and optionally sign an immutable Millennium OTA release."""

import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tarfile
import tempfile
from datetime import datetime, timezone


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def canonical(data):
    return (json.dumps(data, sort_keys=True, separators=(",", ":")) + "\n").encode()


def copy_payload(source, destination, mode):
    if not source.is_file():
        raise SystemExit("missing payload: %s" % source)
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)
    os.chmod(destination, mode)


def add_reproducible_tar(source, output):
    with output.open("wb") as raw:
        with gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0) as compressed:
            with tarfile.open(fileobj=compressed, mode="w", format=tarfile.PAX_FORMAT) as archive:
                for path in sorted(source.rglob("*")):
                    info = archive.gettarinfo(str(path), arcname=str(path.relative_to(source)))
                    info.uid = 0
                    info.gid = 0
                    info.uname = "root"
                    info.gname = "root"
                    info.mtime = 0
                    if path.is_file():
                        with path.open("rb") as stream:
                            archive.addfile(info, stream)
                    else:
                        archive.addfile(info)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--version", required=True)
    parser.add_argument("--sequence", required=True, type=int)
    parser.add_argument("--base-url", required=True)
    parser.add_argument("--daemon", type=Path, default=Path("host/daemon"))
    parser.add_argument("--portal", type=Path, default=Path("host/web_portal.html"))
    parser.add_argument("--keypad", type=Path, default=Path("Arduino/build/keypad/keypad.ino.hex"))
    parser.add_argument("--display", type=Path, default=Path("Arduino/build/display/display.ino.hex"))
    parser.add_argument("--flash-script", type=Path, default=Path("Arduino/pi_flash.sh"))
    parser.add_argument("--ota-worker", type=Path, default=Path("host/ota/millennium_ota.py"))
    parser.add_argument("--private-key", type=Path)
    parser.add_argument("--channel", default="stable")
    parser.add_argument("--architecture", default=platform.machine())
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()

    if args.sequence < 0 or not args.version or "/" in args.version:
        raise SystemExit("invalid version or sequence")
    args.output_dir.mkdir(parents=True, exist_ok=True)
    bundle_name = "millennium-%s.tar.gz" % args.version
    bundle_path = args.output_dir / bundle_name

    with tempfile.TemporaryDirectory(prefix="millennium-release-") as temp_name:
        root = Path(temp_name)
        payloads = {
            "host/millennium-daemon": (args.daemon, 0o755),
            "host/web_portal.html": (args.portal, 0o644),
            "arduino/keypad.hex": (args.keypad, 0o644),
            "arduino/display.hex": (args.display, 0o644),
            "arduino/pi_flash.sh": (args.flash_script, 0o755),
            "ota/millennium-ota": (args.ota_worker, 0o755),
        }
        for relative, (source, mode) in payloads.items():
            copy_payload(source, root / relative, mode)
        files = {}
        for relative in sorted(payloads):
            path = root / relative
            files[relative] = {"sha256": sha256(path), "size": path.stat().st_size}
        release = {
            "schema": 1,
            "version": args.version,
            "sequence": args.sequence,
            "architecture": args.architecture,
            "files": files,
        }
        (root / "release.json").write_bytes(canonical(release))
        add_reproducible_tar(root, bundle_path)

    base = args.base_url.rstrip("/")
    manifest = {
        "schema": 1,
        "channel": args.channel,
        "version": args.version,
        "sequence": args.sequence,
        "published_at": datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z"),
        "minimum_sequence": 0,
        "bundle": {
            "url": "%s/releases/%s/%s" % (base, args.version, bundle_name),
            "sha256": sha256(bundle_path),
            "size": bundle_path.stat().st_size,
        },
    }
    manifest_path = args.output_dir / "manifest.json"
    signature_path = args.output_dir / "manifest.json.sig"
    manifest_path.write_bytes(canonical(manifest))
    if args.private_key:
        subprocess.run([
            "openssl", "pkeyutl", "-sign", "-rawin", "-inkey", str(args.private_key),
            "-in", str(manifest_path), "-out", str(signature_path),
        ], check=True)
    print(json.dumps({
        "bundle": str(bundle_path),
        "manifest": str(manifest_path),
        "signature": str(signature_path) if args.private_key else None,
        "sha256": manifest["bundle"]["sha256"],
    }, sort_keys=True))


if __name__ == "__main__":
    main()
