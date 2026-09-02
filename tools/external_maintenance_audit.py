#!/usr/bin/env python3
"""Prove maintenance access from a network distinct from the home baseline."""

import argparse
from datetime import datetime, timezone
import hashlib
import hmac
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile


PHONE_PROBE = r'''set -eu
hostname
readlink -f /opt/millennium/current
readlink -f /var/lib/millennium/content/current
systemctl is-active millennium-maintenance-tunnel.service
curl -fsS http://127.0.0.1:8080/api/health
'''


def now():
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat()


def atomic_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=".external-audit-", dir=path.parent)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
            json.dump(value, stream, indent=2, sort_keys=True)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def secret(path):
    mode = path.stat().st_mode & 0o777
    if mode & 0o077:
        raise SystemExit("network-audit secret must not be group/world accessible")
    value = path.read_bytes()
    if len(value) < 32:
        raise SystemExit("network-audit secret must contain at least 32 bytes")
    return value


def peer_hmac(secret_value, address):
    return hmac.new(secret_value, address.encode("utf-8"), hashlib.sha256).hexdigest()


def jump_destination(args):
    return "%s@%s" % (args.jump_user, args.jump_host) if args.jump_user else args.jump_host


def jump_spec(args):
    value = jump_destination(args)
    return "%s:%d" % (value, args.jump_port) if args.jump_port else value


def ssh_run(args, destination, command, jump=None, port=None, verbose=False):
    invocation = [str(args.ssh), "-o", "BatchMode=yes", "-o", "ConnectTimeout=20"]
    if verbose:
        invocation.append("-vv")
    if args.identity:
        invocation += ["-i", str(args.identity)]
    if jump:
        invocation += ["-J", jump]
    if port:
        invocation += ["-p", str(port)]
    invocation += [destination, command]
    return subprocess.run(invocation, check=True, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          env=os.environ.copy())


def observed_peer(args):
    result = ssh_run(args, jump_destination(args),
                     "printf '%s\\n' \"$SSH_CONNECTION\"", port=args.jump_port)
    fields = result.stdout.strip().split()
    if len(fields) != 4 or not fields[0]:
        raise SystemExit("jump host returned an invalid SSH_CONNECTION")
    return fields[0]


def init_secret(args):
    if args.output.exists():
        raise SystemExit("refusing to overwrite network-audit secret")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    descriptor = os.open(args.output, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(descriptor, "wb") as stream:
        stream.write(os.urandom(32))
        stream.flush()
        os.fsync(stream.fileno())
    print(args.output)


def baseline(args):
    value = secret(args.secret)
    peer = observed_peer(args)
    record = {
        "schema": 1, "operation": "home-network-baseline",
        "captured_at": now(), "server_id": args.server_id,
        "secret_sha256": hashlib.sha256(value).hexdigest(),
        "source_address_hmac_sha256": peer_hmac(value, peer),
        "raw_source_address_stored": False,
    }
    atomic_json(args.output, record)
    print(json.dumps(record, sort_keys=True))


def metric_is_healthy(output, name):
    match = re.search(r"^%s\s+([0-9.]+)$" % re.escape(name), output, re.MULTILINE)
    return bool(match and float(match.group(1)) == 0.0)


def audit(args):
    value = secret(args.secret)
    baseline_record = json.loads(args.baseline.read_text(encoding="utf-8"))
    if baseline_record.get("schema") != 1 or baseline_record.get("operation") != "home-network-baseline":
        raise SystemExit("invalid home-network baseline")
    if baseline_record.get("server_id") != args.server_id:
        raise SystemExit("baseline maintenance server does not match this audit")
    if not hmac.compare_digest(baseline_record.get("secret_sha256", ""),
                               hashlib.sha256(value).hexdigest()):
        raise SystemExit("baseline was created with a different audit secret")
    peer = observed_peer(args)
    current_hmac = peer_hmac(value, peer)
    if hmac.compare_digest(current_hmac,
                           baseline_record.get("source_address_hmac_sha256", "")):
        raise SystemExit("current source network matches the home baseline")
    destination = "%s@127.0.0.1" % args.phone_user
    probe_args = argparse.Namespace(**vars(args))
    result = ssh_run(probe_args, destination, PHONE_PROBE,
                     jump=jump_spec(args), port=args.phone_port, verbose=True)
    hardware_key = bool(re.search(r"(sk-ssh-ed25519|ED25519-SK)", result.stderr,
                                  re.IGNORECASE))
    if not hardware_key:
        raise SystemExit("SSH trace did not prove hardware-backed key authentication")
    lines = result.stdout.splitlines()
    if len(lines) < 5 or lines[3] != "active":
        raise SystemExit("phone maintenance probe returned incomplete evidence")
    health = "\n".join(lines[4:])
    checks = {
        "maintenance_tunnel": lines[3] == "active",
        "overall_health": metric_is_healthy(health, "health_overall_status"),
        "serial_health": metric_is_healthy(health, "health_check_serial_connection_status"),
        "sip_health": metric_is_healthy(health, "health_check_sip_connection_status"),
    }
    if not all(checks.values()):
        raise SystemExit("phone failed one or more external maintenance health checks")
    record = {
        "schema": 1, "operation": "external-maintenance-audit", "passed": True,
        "completed_at": now(), "device_id": args.device_id,
        "network_description": args.network_description,
        "source_network_differs_from_home": True,
        "source_address_hmac_sha256": current_hmac,
        "raw_source_address_stored": False,
        "server_id": args.server_id, "hardware_backed_authentication": True,
        "target_hostname": lines[0], "active_release": lines[1],
        "active_content": lines[2], "checks": checks,
    }
    atomic_json(args.output, record)
    print(json.dumps(record, sort_keys=True))


def add_connection_args(parser):
    parser.add_argument("--ssh", type=Path, default=Path("ssh"))
    parser.add_argument("--identity", type=Path)
    parser.add_argument("--jump-host", required=True)
    parser.add_argument("--jump-user")
    parser.add_argument("--jump-port", type=int)
    parser.add_argument("--server-id", required=True,
                        help="stable identity shared by LAN and public aliases")


def main():
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest="command", required=True)
    initialize = commands.add_parser("init-secret")
    initialize.add_argument("--output", type=Path, required=True)
    initialize.set_defaults(function=init_secret)
    home = commands.add_parser("baseline")
    home.add_argument("--secret", type=Path, required=True)
    home.add_argument("--output", type=Path, required=True)
    add_connection_args(home)
    home.set_defaults(function=baseline)
    external = commands.add_parser("audit")
    external.add_argument("--secret", type=Path, required=True)
    external.add_argument("--baseline", type=Path, required=True)
    external.add_argument("--output", type=Path, required=True)
    external.add_argument("--device-id", required=True)
    external.add_argument("--phone-user", required=True)
    external.add_argument("--network-description", required=True)
    external.add_argument("--phone-port", type=int, default=22022)
    add_connection_args(external)
    external.set_defaults(function=audit)
    args = parser.parse_args()
    args.function(args)


if __name__ == "__main__":
    main()
