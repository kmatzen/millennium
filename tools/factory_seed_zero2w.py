#!/usr/bin/env python3
"""Seed unique device state into a Millennium Zero 2 W A/B disk image."""

import argparse
from contextlib import contextmanager
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import tempfile
import time


HOST_KEYS = (
    "ssh_host_ed25519_key", "ssh_host_ed25519_key.pub",
    "ssh_host_rsa_key", "ssh_host_rsa_key.pub",
)
REQUIRED_CONFIG = (
    "admin-token", "daemon.conf", "device-id", "maintenance-known-hosts",
    "maintenance-tunnel-key", "maintenance-tunnel-key.pub",
    "maintenance-tunnel.conf", "ota.conf", "update-signing-key.pem",
    "wifi-setup-password",
)


def config_values(path):
    values = {}
    for raw in Path(path).read_text().splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        key, separator, value = line.partition("=")
        if not separator or not key.strip() or not value.strip():
            raise ValueError("invalid configuration line in " + str(path))
        values[key.strip()] = value.strip()
    return values


def validate_update_trust(config):
    """Reject factory state whose OTA key mappings cannot exist on target."""
    config = Path(config)
    values = config_values(config / "ota.conf")
    entries = values.get("trusted_keys", "").split(",")
    mappings = {}
    for entry in entries:
        key_id, separator, target = entry.partition(":")
        if not separator or not key_id or not target.startswith("/etc/millennium/"):
            raise ValueError("invalid OTA trusted key mapping")
        mappings[key_id] = target
    if "release-2026-08" not in mappings:
        raise ValueError("OTA configuration does not trust release-2026-08")
    paths = [values.get("public_key", "")] + list(mappings.values())
    for target in paths:
        if not target.startswith("/etc/millennium/"):
            raise ValueError("invalid OTA public key path")
        staged = config / Path(target).name
        if not staged.is_file() or staged.stat().st_size == 0:
            raise ValueError("missing configured OTA public key: " + target)


def run(arguments, **kwargs):
    return subprocess.run(arguments, check=True, **kwargs)


def ensure_block_device(path, sys_block=Path("/sys/class/block")):
    """Create a container-visible node for a kernel-known loop partition."""
    path = Path(path)
    if path.exists():
        if not stat.S_ISBLK(path.stat().st_mode):
            raise ValueError("partition path is not a block device: " + str(path))
        return
    identity = sys_block / path.name / "dev"
    for _ in range(50):
        try:
            value = identity.read_text().strip()
            break
        except FileNotFoundError:
            time.sleep(0.1)
    else:
        raise ValueError("kernel did not expose loop partition: " + path.name)
    match = re.fullmatch(r"([0-9]+):([0-9]+)", value)
    if not match:
        raise ValueError("invalid kernel block-device identity: " + value)
    major, minor = (int(part) for part in match.groups())
    os.mknod(path, stat.S_IFBLK | 0o600, os.makedev(major, minor))


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def validate_tree(path):
    path = Path(path)
    if not path.is_dir() or path.is_symlink():
        raise ValueError("staging root is missing or unsafe")
    for item in path.rglob("*"):
        info = item.lstat()
        if stat.S_ISLNK(info.st_mode) or not (stat.S_ISDIR(info.st_mode)
                                               or stat.S_ISREG(info.st_mode)):
            raise ValueError("staging tree contains a link or special file")


def validate_staging(staging):
    staging = Path(staging)
    validate_tree(staging)
    machine_id_raw = (staging / "etc/machine-id").read_bytes()
    if not re.fullmatch(rb"[0-9a-f]{32}\n", machine_id_raw):
        raise ValueError("invalid machine-id")
    ssh = staging / "etc/ssh"
    for name in HOST_KEYS:
        if not (ssh / name).is_file():
            raise ValueError("missing SSH host key: " + name)
    config = staging / "etc/millennium"
    for name in REQUIRED_CONFIG:
        if not (config / name).is_file() or (config / name).stat().st_size == 0:
            raise ValueError("missing device configuration: " + name)
    validate_update_trust(config)
    device_id = (config / "device-id").read_text().strip()
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_-]{0,31}", device_id):
        raise ValueError("invalid device ID")
    authorized = staging / "home/millennium/.ssh/authorized_keys"
    lines = [line for line in authorized.read_text().splitlines() if line.strip()]
    if not lines or any(not (line.startswith("ssh-ed25519 ") or
                             line.startswith("sk-ssh-ed25519@openssh.com "))
                        for line in lines):
        raise ValueError("authorized_keys must contain only Ed25519 keys")
    return device_id


def ownership(path, uid, gid, directory_mode=None, file_mode=None):
    path = Path(path)
    items = [path] + list(path.rglob("*")) if path.is_dir() else [path]
    for item in items:
        os.chown(item, uid, gid, follow_symlinks=False)
        if item.is_symlink():
            continue
        if item.is_dir() and directory_mode is not None:
            os.chmod(item, directory_mode)
        elif item.is_file() and file_mode is not None:
            os.chmod(item, file_mode(item))


def copy_file(source, destination, mode, uid=0, gid=0):
    destination = Path(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)
    os.chmod(destination, mode)
    os.chown(destination, uid, gid)


def overlay_config(source, destination):
    destination = Path(destination)
    destination.mkdir(parents=True, exist_ok=True)
    secret = {"maintenance-known-hosts", "maintenance-tunnel-key",
              "maintenance-tunnel.conf", "wifi-setup-password"}
    for item in Path(source).iterdir():
        if not item.is_file():
            raise ValueError("device configuration must contain files only")
        mode = 0o640 if item.name == "admin-token" else (
            0o600 if item.name in secret else 0o644)
        copy_file(item, destination / item.name, mode,
                  0, 1000)
    os.chown(destination, 0, 1000)
    os.chmod(destination, 0o750)


def replace_tree(source, destination, uid, gid, directory_mode, file_mode,
                 preserve_symlinks=False):
    destination = Path(destination)
    if destination.exists():
        shutil.rmtree(destination)
    shutil.copytree(source, destination, symlinks=preserve_symlinks)
    ownership(destination, uid, gid, directory_mode, file_mode)


def seed_mounted(staging, system_a, system_b, persistent, source_commit):
    staging = Path(staging)
    device_id = validate_staging(staging)
    source_commit = source_commit.lower()
    if not re.fullmatch(r"[0-9a-f]{40}", source_commit):
        raise ValueError("source commit must be 40 hexadecimal characters")
    machine = staging / "etc/machine-id"
    for root in (Path(system_a), Path(system_b)):
        if not (root / "etc/os-release").is_file():
            raise ValueError("system slot is not a Linux root filesystem")
        slot_conf = root / "etc/rpi-image-gen/slot-shared.d/millennium.conf"
        declared = set(slot_conf.read_text().splitlines())
        required = {"Path=/etc/millennium", "Path=/etc/ssh",
                    "Path=/etc/NetworkManager/system-connections"}
        if not required.issubset(declared):
            raise ValueError("system slot lacks required shared-state declarations")
        copy_file(machine, root / "etc/machine-id", 0o444)
        for name in HOST_KEYS:
            copy_file(staging / "etc/ssh" / name, root / "etc/ssh" / name,
                      0o644 if name.endswith(".pub") else 0o600)
        overlay_config(staging / "etc/millennium", root / "etc/millennium")
        profiles = staging / "etc/NetworkManager/system-connections"
        if profiles.is_dir():
            replace_tree(profiles, root / "etc/NetworkManager/system-connections",
                         0, 0, 0o700, lambda unused: 0o600)

    persistent = Path(persistent)
    copy_file(machine, persistent / "common/etc/machine-id", 0o444)
    replace_tree(Path(system_a) / "etc/ssh", persistent / "shared/etc/ssh",
                 0, 0, 0o755,
                 lambda item: 0o600 if item.name.startswith("ssh_host_")
                 and not item.name.endswith(".pub") else 0o644,
                 preserve_symlinks=True)
    replace_tree(Path(system_a) / "etc/millennium",
                 persistent / "shared/etc/millennium", 0, 1000, 0o750,
                 lambda item: stat.S_IMODE(item.stat().st_mode))
    replace_tree(Path(system_a) / "etc/NetworkManager/system-connections",
                 persistent / "shared/etc/NetworkManager/system-connections",
                 0, 0, 0o700, lambda unused: 0o600)
    replace_tree(staging / "home/millennium/.ssh",
                 persistent / "home/millennium/.ssh", 1000, 1000, 0o700,
                 lambda unused: 0o600)
    record = {
        "schema": 1,
        "operation": "factory-provision",
        "device_id": device_id,
        "source_commit": source_commit,
        "completed_at": datetime.now(timezone.utc).replace(
            microsecond=0).isoformat().replace("+00:00", "Z"),
        "machine_id_sha256": sha256(machine),
        "ssh_host_ed25519_sha256": sha256(staging / "etc/ssh/ssh_host_ed25519_key.pub"),
        "maintenance_tunnel_key_sha256": sha256(
            staging / "etc/millennium/maintenance-tunnel-key.pub"),
        "authorized_keys_sha256": sha256(
            staging / "home/millennium/.ssh/authorized_keys"),
    }
    record_path = persistent / "factory-provision.json"
    record_path.write_text(json.dumps(record, indent=2, sort_keys=True) + "\n")
    os.chmod(record_path, 0o600)
    os.chown(record_path, 0, 0)
    return record


@contextmanager
def mounted_image(image):
    loop = run(["losetup", "--find", "--show", "--partscan", str(image)],
               stdout=subprocess.PIPE, text=True).stdout.strip()
    root = Path(tempfile.mkdtemp(prefix="millennium-factory-"))
    mounts = [root / "system-a", root / "system-b", root / "persistent"]
    try:
        for path, number in zip(mounts, (5, 6, 7)):
            path.mkdir()
            partition = loop + "p" + str(number)
            ensure_block_device(partition)
            run(["mount", "-o", "rw,nosuid,nodev,noexec", partition,
                 str(path)])
        yield mounts
        run(["sync"])
    finally:
        for path in reversed(mounts):
            subprocess.run(["umount", str(path)], check=False)
        subprocess.run(["losetup", "--detach", loop], check=False)
        shutil.rmtree(root, ignore_errors=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--staging", type=Path, required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--evidence", type=Path, required=True)
    args = parser.parse_args()
    if os.geteuid() != 0:
        raise SystemExit("factory seeding requires root")
    if not args.image.is_file():
        raise SystemExit("disk image does not exist")
    if args.evidence.exists():
        raise SystemExit("refusing to overwrite factory evidence")
    try:
        with mounted_image(args.image) as (system_a, system_b, persistent):
            record = seed_mounted(args.staging, system_a, system_b, persistent,
                                  args.source_commit)
    except (OSError, ValueError, subprocess.CalledProcessError) as exc:
        raise SystemExit("factory seeding failed: %s" % exc)
    args.evidence.parent.mkdir(parents=True, exist_ok=True)
    args.evidence.write_text(json.dumps(record, indent=2, sort_keys=True) + "\n")
    os.chmod(args.evidence, 0o600)
    print(json.dumps(record, sort_keys=True))


if __name__ == "__main__":
    main()
