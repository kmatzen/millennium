#!/usr/bin/env python3
"""Verify and write a signed Millennium recovery image to removable media."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import plistlib
import re
import subprocess


CHUNK_SIZE = 4 * 1024 * 1024
DISKUTIL = "/usr/sbin/diskutil"


def run(arguments, **kwargs):
    return subprocess.run(arguments, check=True, **kwargs)


def canonical(value):
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


def utc_timestamp():
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat()


def sha256_file(path):
    digest = hashlib.sha256()
    size = 0
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(CHUNK_SIZE), b""):
            size += len(chunk)
            digest.update(chunk)
    return size, digest.hexdigest()


def verified_manifest(manifest_path, signature_path, public_key, image_path):
    try:
        value = json.loads(manifest_path.read_bytes())
    except (OSError, json.JSONDecodeError) as exc:
        raise SystemExit("cannot read recovery manifest: %s" % exc)
    if manifest_path.read_bytes() != canonical(value):
        raise SystemExit("recovery manifest is not canonical JSON")
    if value.get("schema") != 1 or value.get("kind") != "millennium-recovery-image":
        raise SystemExit("unsupported recovery manifest")
    image = value.get("image")
    if not isinstance(image, dict) or image.get("compression") != "zstd":
        raise SystemExit("unsupported recovery image metadata")
    if image.get("filename") != image_path.name:
        raise SystemExit("recovery image filename does not match manifest")
    for field in ("compressed_size", "expanded_size"):
        if not isinstance(image.get(field), int) or image[field] <= 0:
            raise SystemExit("invalid %s" % field)
    for field in ("compressed_sha256", "expanded_sha256"):
        if not re.fullmatch(r"[0-9a-f]{64}", str(image.get(field, ""))):
            raise SystemExit("invalid %s" % field)
    verify = subprocess.run([
        "openssl", "pkeyutl", "-verify", "-rawin", "-pubin", "-inkey",
        str(public_key), "-in", str(manifest_path), "-sigfile", str(signature_path),
    ], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    if verify.returncode:
        raise SystemExit("recovery manifest signature verification failed")
    observed_size, observed_digest = sha256_file(image_path)
    if observed_size != image["compressed_size"] or observed_digest != image["compressed_sha256"]:
        raise SystemExit("compressed recovery image does not match signed manifest")
    test = subprocess.run(["zstd", "--quiet", "--test", str(image_path)],
                          stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    if test.returncode:
        raise SystemExit("recovery image failed Zstandard integrity check")
    return value


def darwin_target(target):
    if not re.fullmatch(r"/dev/disk[0-9]+", target):
        raise SystemExit("macOS target must be an explicit whole disk such as /dev/disk4")
    result = run([DISKUTIL, "info", "-plist", target],
                 stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    info = plistlib.loads(result.stdout)
    identifier = info.get("DeviceIdentifier")
    if identifier is None or target != "/dev/" + identifier or not info.get("WholeDisk"):
        raise SystemExit("target is not the OS-reported whole disk")
    if info.get("Internal") is True:
        raise SystemExit("refusing to write an internal disk")
    if not (info.get("RemovableMedia") or info.get("Ejectable")):
        raise SystemExit("target is not OS-reported removable media")
    if info.get("VirtualOrPhysical") not in (None, "Physical"):
        raise SystemExit("target is not a physical disk")
    size = info.get("TotalSize")
    if not isinstance(size, int) or size <= 0:
        raise SystemExit("cannot determine target size")
    return {
        "device_identifier": identifier,
        "path": target,
        "raw_path": "/dev/r" + identifier,
        "size_bytes": size,
        "transport": info.get("BusProtocol"),
    }


def linux_target(target):
    if not re.fullmatch(r"/dev/[A-Za-z0-9._+-]+", target):
        raise SystemExit("Linux target must be an explicit /dev whole-disk path")
    result = run(["lsblk", "--json", "--bytes", "--output",
                  "PATH,SIZE,TYPE,RM,HOTPLUG,RO,MOUNTPOINTS", target],
                 stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    devices = json.loads(result.stdout).get("blockdevices", [])
    if len(devices) != 1:
        raise SystemExit("cannot identify exactly one target disk")
    info = devices[0]
    if info.get("path") != target or info.get("type") != "disk":
        raise SystemExit("target is not the OS-reported whole disk")
    if not (info.get("rm") or info.get("hotplug")):
        raise SystemExit("target is not OS-reported removable media")
    if info.get("ro"):
        raise SystemExit("target is read-only")
    size = info.get("size")
    if not isinstance(size, int) or size <= 0:
        raise SystemExit("cannot determine target size")
    return {
        "device_identifier": Path(target).name,
        "path": target,
        "raw_path": target,
        "size_bytes": size,
        "transport": None,
    }


def identify_target(target, system=None):
    system = system or platform.system()
    if system == "Darwin":
        return darwin_target(target)
    if system == "Linux":
        return linux_target(target)
    raise SystemExit("recovery-media writing is unsupported on %s" % system)


def darwin_containing_disks(path):
    lookup = path if Path(path).is_dir() else Path(path).parent
    lookup = Path(lookup).resolve()
    while not os.path.ismount(lookup):
        parent = lookup.parent
        if parent == lookup:
            raise SystemExit("cannot identify mount containing %s" % path)
        lookup = parent
    result = run([DISKUTIL, "info", "-plist", str(lookup)],
                 stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    info = plistlib.loads(result.stdout)
    stores = info.get("APFSPhysicalStores") or []
    if stores:
        identifiers = set()
        for entry in stores:
            store = entry.get("APFSPhysicalStore")
            if not isinstance(store, str):
                raise SystemExit("cannot identify APFS physical store containing %s" % lookup)
            result = run([DISKUTIL, "info", "-plist", "/dev/" + store],
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            store_info = plistlib.loads(result.stdout)
            identifier = store_info.get("ParentWholeDisk")
            if not isinstance(identifier, str) or not re.fullmatch(r"disk[0-9]+", identifier):
                raise SystemExit("cannot identify APFS whole disk containing %s" % lookup)
            identifiers.add(identifier)
        return identifiers
    identifier = info.get("ParentWholeDisk")
    if not identifier and info.get("WholeDisk"):
        identifier = info.get("DeviceIdentifier")
    if not isinstance(identifier, str) or not re.fullmatch(r"disk[0-9]+", identifier):
        raise SystemExit("cannot identify disk containing %s" % lookup)
    return {identifier}


def linux_containing_disk(path):
    lookup = path if Path(path).is_dir() else Path(path).parent
    result = run(["findmnt", "--noheadings", "--output", "SOURCE", "--target",
                  str(lookup)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    source = result.stdout.strip()
    if not source.startswith("/dev/"):
        return None
    current = source
    seen = set()
    while current not in seen:
        seen.add(current)
        output = run(["lsblk", "--noheadings", "--output", "PKNAME", current],
                     stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True).stdout
        parents = {line.strip() for line in output.splitlines() if line.strip()}
        if not parents:
            return Path(current).name
        if len(parents) != 1:
            raise SystemExit("cannot identify one whole disk containing %s" % lookup)
        current = "/dev/" + parents.pop()
    raise SystemExit("cycle while resolving disk containing %s" % lookup)


def protected_disks(image_path, system):
    if system == "Darwin":
        return darwin_containing_disks(Path("/")) | darwin_containing_disks(image_path)
    if system == "Linux":
        return {disk for disk in
                (linux_containing_disk(Path("/")), linux_containing_disk(image_path))
                if disk}
    return set()


def reject_protected_target(target, image_path, system):
    if target["device_identifier"] in protected_disks(image_path, system):
        raise SystemExit("refusing to write the running system disk or source-image disk")


def unmount_target(target, system):
    if system == "Darwin":
        run([DISKUTIL, "unmountDisk", target])
    else:
        result = run(["lsblk", "--json", "--output", "PATH,MOUNTPOINTS", target],
                     stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        # util-linux refuses writes to mounted whole disks inconsistently across
        # distributions, so require the operator to unmount every partition.
        value = json.loads(result.stdout)
        stack = list(value.get("blockdevices", []))
        while stack:
            device = stack.pop()
            if any(device.get("mountpoints") or []):
                raise SystemExit("target has mounted filesystems; unmount them first")
            stack.extend(device.get("children") or [])


def open_target(destination, system):
    flags = os.O_RDWR
    # On macOS, retaining an exclusive raw-device descriptor prevents Disk
    # Arbitration/filesystem probes from changing FAT status bits between the
    # write and the mandatory byte-for-byte readback.
    if system == "Darwin":
        flags |= os.O_EXCL
    try:
        return os.open(destination, flags)
    except OSError as exc:
        raise SystemExit("cannot exclusively open recovery-media target: %s" % exc)


def stream_image(image_path, destination_fd, expected_size, expected_digest):
    process = subprocess.Popen(
        ["zstd", "--quiet", "--decompress", "--stdout", str(image_path)],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    digest = hashlib.sha256()
    size = 0
    try:
        os.lseek(destination_fd, 0, os.SEEK_SET)
        for chunk in iter(lambda: process.stdout.read(CHUNK_SIZE), b""):
            view = memoryview(chunk)
            while view:
                written = os.write(destination_fd, view)
                if written <= 0:
                    raise OSError("short recovery-media write")
                view = view[written:]
            digest.update(chunk)
            size += len(chunk)
        os.fsync(destination_fd)
    except BaseException:
        process.kill()
        process.wait()
        raise
    stderr = process.communicate()[1]
    if process.returncode:
        raise SystemExit("cannot decompress recovery image: %s" %
                         stderr.decode(errors="replace").strip())
    if size != expected_size or digest.hexdigest() != expected_digest:
        raise SystemExit("expanded recovery image does not match signed manifest")


def verify_readback(source_fd, size, expected_digest):
    digest = hashlib.sha256()
    remaining = size
    os.lseek(source_fd, 0, os.SEEK_SET)
    while remaining:
        chunk = os.read(source_fd, min(CHUNK_SIZE, remaining))
        if not chunk:
            raise SystemExit("short recovery-media readback")
        digest.update(chunk)
        remaining -= len(chunk)
    if digest.hexdigest() != expected_digest:
        raise SystemExit("recovery-media readback digest mismatch")
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--signature", type=Path, required=True)
    parser.add_argument("--public-key", type=Path, required=True)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--target", required=True,
                        help="explicit OS whole-disk path, never a partition")
    parser.add_argument("--write", action="store_true")
    parser.add_argument("--confirm-device",
                        help="exact OS device identifier required with --write")
    parser.add_argument("--evidence", type=Path)
    args = parser.parse_args()

    manifest = verified_manifest(args.manifest, args.signature,
                                 args.public_key, args.image)
    system = platform.system()
    target = identify_target(args.target, system)
    reject_protected_target(target, args.image, system)
    image = manifest["image"]
    if target["size_bytes"] < image["expanded_size"]:
        raise SystemExit("target is smaller than the signed recovery image")
    started_at = utc_timestamp()
    summary = {
        "schema": 1,
        "operation": "recovery-media-write" if args.write else "recovery-media-preflight",
        "passed": True,
        "started_at": started_at,
        "key_id": manifest["key_id"],
        "source_commit": manifest["source_commit"],
        "image": image,
        "target": {key: value for key, value in target.items() if key != "raw_path"},
        "signature_verified": True,
        "compressed_image_verified": True,
        "expanded_write_verified": False,
    }
    if args.write:
        if args.confirm_device != target["device_identifier"]:
            raise SystemExit("--confirm-device must exactly match %s" %
                             target["device_identifier"])
        if os.geteuid() != 0:
            raise SystemExit("recovery-media writing requires root")
        unmount_target(target["path"], system)
        target_fd = open_target(target["raw_path"], system)
        try:
            stream_image(args.image, target_fd, image["expanded_size"],
                         image["expanded_sha256"])
            summary["readback_sha256"] = verify_readback(
                target_fd, image["expanded_size"], image["expanded_sha256"])
        finally:
            os.close(target_fd)
        summary["expanded_write_verified"] = True
    summary["completed_at"] = utc_timestamp()
    if args.evidence:
        if args.evidence.exists():
            raise SystemExit("refusing to overwrite existing evidence")
        args.evidence.parent.mkdir(parents=True, exist_ok=True)
        args.evidence.write_bytes(json.dumps(summary, indent=2, sort_keys=True).encode() + b"\n")
    print(json.dumps(summary, sort_keys=True))


if __name__ == "__main__":
    main()
