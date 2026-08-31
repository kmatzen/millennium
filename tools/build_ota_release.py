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
import re
from datetime import datetime, timezone


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def canonical(data):
    return (json.dumps(data, sort_keys=True, separators=(",", ":")) + "\n").encode()


def sign_ed25519(private_key, message, signature):
    """Sign with OpenSSL 3, or cryptography on supported OpenSSL 1.1 hosts."""
    result = subprocess.run([
        "openssl", "pkeyutl", "-sign", "-rawin", "-inkey", str(private_key),
        "-in", str(message), "-out", str(signature),
    ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if result.returncode == 0:
        return
    try:
        from cryptography.hazmat.primitives import serialization
        key = serialization.load_pem_private_key(private_key.read_bytes(), password=None)
        signature.write_bytes(key.sign(message.read_bytes()))
    except Exception as exc:
        raise SystemExit(
            "Ed25519 signing requires OpenSSL 3 or python3-cryptography") from exc


def copy_payload(source, destination, mode):
    if not source.is_file():
        raise SystemExit("missing payload: %s" % source)
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)
    os.chmod(destination, mode)


def decode_intel_hex(path):
    memory = {}
    base = 0
    for number, raw in enumerate(path.read_text().splitlines(), 1):
        if not raw.startswith(":"):
            raise SystemExit("invalid Intel HEX line %d in %s" % (number, path))
        try:
            record = bytes.fromhex(raw[1:])
        except ValueError as exc:
            raise SystemExit("invalid Intel HEX encoding in %s: %s" % (path, exc))
        if len(record) < 5 or record[0] + 5 != len(record) or sum(record) & 0xff:
            raise SystemExit("invalid Intel HEX record in %s" % path)
        length = record[0]
        address = (record[1] << 8) | record[2]
        kind = record[3]
        data = record[4:4 + length]
        if kind == 0:
            for offset, value in enumerate(data):
                memory[base + address + offset] = value
        elif kind == 4 and length == 2:
            base = ((data[0] << 8) | data[1]) << 16
        elif kind == 2 and length == 2:
            base = ((data[0] << 8) | data[1]) << 4
        elif kind == 1:
            break
    if not memory:
        raise SystemExit("Intel HEX contains no firmware data: %s" % path)
    start, end = min(memory), max(memory)
    return bytes(memory.get(address, 0xff) for address in range(start, end + 1))


def firmware_identity(path, expected_role, expected_version):
    image = decode_intel_hex(path)
    pattern = re.compile(
        rb"MILLENNIUM role=(keypad|display) version=([^ ]+) protocol=([0-9]+) "
        rb"build=([^ ]+) selftest=ok")
    match = pattern.search(image)
    if not match:
        raise SystemExit("firmware identity not found in %s" % path)
    identity = {
        "role": match.group(1).decode(),
        "version": match.group(2).decode(),
        "protocol": int(match.group(3)),
        "build": match.group(4).decode(),
    }
    if identity["role"] != expected_role or identity["version"] != expected_version:
        raise SystemExit("firmware identity mismatch in %s: %r" % (path, identity))
    return identity


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
    parser.add_argument("--version")
    parser.add_argument("--sequence", required=True, type=int)
    parser.add_argument("--base-url", required=True)
    parser.add_argument("--daemon", type=Path, default=Path("host/daemon"))
    parser.add_argument("--portal", type=Path, default=Path("host/web_portal.html"))
    parser.add_argument("--keypad", type=Path, default=Path("Arduino/build/keypad/keypad.ino.hex"))
    parser.add_argument("--display", type=Path, default=Path("Arduino/build/display/display.ino.hex"))
    parser.add_argument("--flash-script", type=Path, default=Path("Arduino/pi_flash.sh"))
    parser.add_argument("--ota-worker", type=Path, default=Path("host/ota/millennium_ota.py"))
    parser.add_argument("--private-key", type=Path)
    parser.add_argument("--key-id", default="primary")
    parser.add_argument("--channel", default="stable")
    parser.add_argument("--device-groups", default="production")
    parser.add_argument("--hold", action="store_true")
    parser.add_argument("--withdrawn", action="store_true")
    parser.add_argument("--architecture", default=platform.machine())
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()

    source_version = (Path(__file__).resolve().parents[1] / "VERSION").read_text().strip()
    requested_version = args.version or source_version
    if requested_version != source_version:
        raise SystemExit("requested version %s does not match VERSION (%s)" %
                         (requested_version, source_version))
    args.version = requested_version

    try:
        output = subprocess.run([str(args.daemon.resolve()), "--version"], check=True,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                text=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError) as exc:
        raise SystemExit("cannot execute packaged daemon --version: %s" % exc)
    match = re.search(r"(?:^|\s)(\d+\.\d+\.\d+)(?:\s|$)", output)
    if not match or match.group(1) != args.version:
        raise SystemExit("packaged daemon version does not match release: %r" % output)

    if args.sequence < 0 or not args.version or "/" in args.version:
        raise SystemExit("invalid version or sequence")
    if not re.fullmatch(r"[A-Za-z0-9._-]{1,64}", args.key_id):
        raise SystemExit("invalid signing key ID")
    rollout_groups = [item.strip() for item in args.device_groups.split(",")
                      if item.strip()]
    if (not rollout_groups or any(not re.fullmatch(r"[A-Za-z0-9._-]{1,64}", item)
                                  for item in rollout_groups)):
        raise SystemExit("invalid rollout device groups")
    args.output_dir.mkdir(parents=True, exist_ok=True)
    identity = "%08d-%s" % (args.sequence, args.version)
    bundle_name = "millennium-%s.tar.gz" % identity
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
            "firmware": {
                "keypad": firmware_identity(args.keypad, "keypad", args.version),
                "display": firmware_identity(args.display, "display", args.version),
            },
        }
        (root / "release.json").write_bytes(canonical(release))
        add_reproducible_tar(root, bundle_path)

    base = args.base_url.rstrip("/")
    manifest = {
        "schema": 1,
        "channel": args.channel,
        "key_id": args.key_id,
        "version": args.version,
        "sequence": args.sequence,
        "published_at": datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z"),
        "minimum_sequence": 0,
        "rollout": {
            "groups": rollout_groups,
            "hold": args.hold,
            "withdrawn": args.withdrawn,
        },
        "bundle": {
            "url": "%s/releases/%s/%s" % (base, identity, bundle_name),
            "sha256": sha256(bundle_path),
            "size": bundle_path.stat().st_size,
        },
    }
    manifest_path = args.output_dir / "manifest.json"
    signature_path = args.output_dir / "manifest.json.sig"
    manifest_path.write_bytes(canonical(manifest))
    if args.private_key:
        sign_ed25519(args.private_key, manifest_path, signature_path)
    print(json.dumps({
        "bundle": str(bundle_path),
        "manifest": str(manifest_path),
        "signature": str(signature_path) if args.private_key else None,
        "sha256": manifest["bundle"]["sha256"],
    }, sort_keys=True))


if __name__ == "__main__":
    main()
