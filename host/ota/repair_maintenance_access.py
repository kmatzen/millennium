#!/usr/bin/env python3
"""Safely restore one explicitly fingerprinted maintenance SSH public key."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import pwd
import subprocess
import tempfile


KEY_TYPES = {"ssh-ed25519", "sk-ssh-ed25519@openssh.com"}


def read_key(path):
    raw = path.read_text(encoding="utf-8")
    if "\n" in raw.rstrip("\n") or raw.count("\n") > 1:
        raise ValueError("public key file must contain exactly one line")
    fields = raw.strip().split()
    if len(fields) < 2 or fields[0] not in KEY_TYPES:
        raise ValueError("maintenance key must be Ed25519 or FIDO Ed25519")
    return " ".join(fields)


def fingerprint(path):
    result = subprocess.run(["ssh-keygen", "-lf", str(path), "-E", "sha256"],
                            check=True, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE).stdout.split()
    if len(result) < 2 or not result[1].startswith("SHA256:"):
        raise ValueError("ssh-keygen returned no SHA-256 fingerprint")
    return result[1]


def atomic_authorize(path, key, uid, gid):
    path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    os.chmod(path.parent, 0o700)
    existing = path.read_text(encoding="utf-8").splitlines() if path.exists() else []
    changed = key not in existing
    lines = existing + ([key] if changed else [])
    descriptor, temporary = tempfile.mkstemp(prefix=".authorized_keys.", dir=path.parent)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
            for line in lines:
                stream.write(line + "\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.chmod(temporary, 0o600)
        os.chown(temporary, uid, gid)
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)
    os.chown(path.parent, uid, gid)
    return changed


def write_evidence(path, value):
    path.parent.mkdir(mode=0o755, parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=".maintenance-repair.", dir=path.parent)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
            json.dump(value, stream, indent=2, sort_keys=True)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.chmod(temporary, 0o600)
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--user", default="matzen")
    parser.add_argument("--key-file", type=Path, required=True)
    parser.add_argument("--fingerprint", required=True,
                        help="expected SHA256:... fingerprint, verified before install")
    parser.add_argument("--evidence", type=Path,
                        default=Path("/var/lib/millennium/maintenance-key-repair.json"))
    args = parser.parse_args()
    if os.geteuid() != 0:
        raise SystemExit("run as root from the phone's local console")
    try:
        account = pwd.getpwnam(args.user)
        key = read_key(args.key_file)
        actual = fingerprint(args.key_file)
    except (KeyError, ValueError, OSError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error))
    if actual != args.fingerprint:
        raise SystemExit("fingerprint mismatch: refusing to alter authorized_keys")
    authorized = Path(account.pw_dir) / ".ssh/authorized_keys"
    changed = atomic_authorize(authorized, key, account.pw_uid, account.pw_gid)
    subprocess.run(["sshd", "-t"], check=True)
    evidence = {
        "schema": 1,
        "completed_at": datetime.now(timezone.utc).replace(microsecond=0).isoformat(),
        "user": args.user,
        "key_fingerprint": actual,
        "key_type": key.split()[0],
        "key_added": changed,
        "authorized_keys_sha256": hashlib.sha256(authorized.read_bytes()).hexdigest(),
        "sshd_configuration_valid": True,
        "remote_login_verified": False,
    }
    write_evidence(args.evidence, evidence)
    print("Key installed atomically; remote login still must be verified.")
    print(args.evidence)


if __name__ == "__main__":
    main()
