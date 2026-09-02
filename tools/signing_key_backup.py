#!/usr/bin/env python3
"""Create and recovery-test a hardware-recipient-encrypted OTA key backup."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile


def run(arguments, **kwargs):
    return subprocess.run(arguments, check=True, **kwargs)


def public_der(path, private=False):
    command = ["openssl", "pkey", "-in", str(path)]
    if not private:
        command.insert(2, "-pubin")
    command += ["-pubout", "-outform", "DER"]
    return run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout


def fingerprint(path, private=False):
    return hashlib.sha256(public_der(path, private)).hexdigest()


def atomic_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=".key-record-", dir=path.parent)
    try:
        with os.fdopen(descriptor, "w") as stream:
            json.dump(value, stream, indent=2, sort_keys=True)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.chmod(temporary, 0o600)
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def sign_and_verify(private_key, public_key, directory):
    challenge = directory / "challenge"
    signature = directory / "challenge.sig"
    challenge.write_bytes(os.urandom(64))
    result = subprocess.run(
        ["openssl", "pkeyutl", "-sign", "-rawin", "-inkey", str(private_key),
         "-in", str(challenge), "-out", str(signature)],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if result.returncode != 0:
        from cryptography.hazmat.primitives import serialization
        key = serialization.load_pem_private_key(private_key.read_bytes(), password=None)
        signature.write_bytes(key.sign(challenge.read_bytes()))
    verify = subprocess.run(
        ["openssl", "pkeyutl", "-verify", "-rawin", "-pubin", "-inkey",
         str(public_key), "-in", str(challenge), "-sigfile", str(signature)],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if verify.returncode != 0:
        from cryptography.hazmat.primitives import serialization
        key = serialization.load_pem_public_key(public_key.read_bytes())
        key.verify(signature.read_bytes(), challenge.read_bytes())
    return hashlib.sha256(signature.read_bytes()).hexdigest()


def command_backup(args):
    if args.output.exists():
        raise SystemExit("refusing to overwrite encrypted backup: %s" % args.output)
    if fingerprint(args.private_key, True) != fingerprint(args.public_key):
        raise SystemExit("private key does not match expected public key")
    recipient = args.recipient.replace(" ", "").upper()
    if not re.fullmatch(r"[0-9A-F]{40}", recipient):
        raise SystemExit("recipient must be a full 40-hex OpenPGP fingerprint")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    run(["gpg", "--batch", "--yes", "--trust-model", "always",
         "--recipient", recipient, "--output", str(args.output), "--encrypt",
         str(args.private_key)])
    os.chmod(args.output, 0o600)
    record = {
        "schema": 1, "operation": "backup",
        "created_at": datetime.now(timezone.utc).replace(microsecond=0).isoformat(),
        "key_id": args.key_id,
        "public_key_sha256": fingerprint(args.public_key),
        "recipient_fingerprint": recipient,
        "ciphertext_sha256": hashlib.sha256(args.output.read_bytes()).hexdigest(),
        "media_id": args.media_id,
    }
    atomic_json(Path(str(args.output) + ".json"), record)
    print(json.dumps(record, sort_keys=True))


def command_recover(args):
    args.scratch_dir.mkdir(parents=True, exist_ok=True)
    expected_ciphertext = hashlib.sha256(args.backup.read_bytes()).hexdigest()
    with tempfile.TemporaryDirectory(prefix="millennium-key-drill-",
                                     dir=args.scratch_dir) as temporary:
        root = Path(temporary)
        private_key = root / "restored-private.pem"
        try:
            run(["gpg", "--output", str(private_key), "--decrypt", str(args.backup)])
            os.chmod(private_key, 0o600)
            if fingerprint(private_key, True) != fingerprint(args.public_key):
                raise SystemExit("restored private key does not match public key")
            signature_digest = sign_and_verify(private_key, args.public_key, root)
        finally:
            if private_key.exists():
                private_key.unlink()
    record = {
        "schema": 1, "operation": "recovery-drill", "passed": True,
        "completed_at": datetime.now(timezone.utc).replace(microsecond=0).isoformat(),
        "key_id": args.key_id, "media_id": args.media_id,
        "operator": args.operator,
        "public_key_sha256": fingerprint(args.public_key),
        "ciphertext_sha256": expected_ciphertext,
        "disposable_signature_sha256": signature_digest,
        "scratch_directory": str(args.scratch_dir),
    }
    atomic_json(args.evidence, record)
    print(json.dumps(record, sort_keys=True))


def main():
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest="command", required=True)
    backup = commands.add_parser("backup")
    backup.add_argument("--private-key", type=Path, required=True)
    backup.add_argument("--public-key", type=Path, required=True)
    backup.add_argument("--recipient", required=True)
    backup.add_argument("--output", type=Path, required=True)
    backup.add_argument("--key-id", required=True)
    backup.add_argument("--media-id", required=True)
    backup.set_defaults(function=command_backup)
    recover = commands.add_parser("recover")
    recover.add_argument("--backup", type=Path, required=True)
    recover.add_argument("--public-key", type=Path, required=True)
    recover.add_argument("--scratch-dir", type=Path, required=True,
                         help="operator-provided memory-backed directory")
    recover.add_argument("--evidence", type=Path, required=True)
    recover.add_argument("--key-id", required=True)
    recover.add_argument("--media-id", required=True)
    recover.add_argument("--operator", required=True)
    recover.set_defaults(function=command_recover)
    args = parser.parse_args()
    args.function(args)


if __name__ == "__main__":
    main()
