#!/usr/bin/env python3
"""Stream anima's Millennium operations state into Restic and verify restore."""

import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path, PurePosixPath
import subprocess
import tempfile


DEFAULT_PATHS = (
    "selfhosted/millennium-updates",
    ".config/doorman",
    ".config/systemd/user/doormand.service",
    ".config/systemd/user/millennium-backup-pull.service",
    ".config/systemd/user/millennium-backup-pull.timer",
    ".config/systemd/user/millennium-metrics-pull.service",
    ".config/systemd/user/millennium-metrics-pull.timer",
    ".local/bin/doormand",
    ".local/bin/millennium-backup-pull",
    ".local/bin/millennium-metrics-pull",
    ".ssh/authorized_keys",
    ".local/share/millennium-recovery",
)


def now():
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat()


def atomic_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=".server-backup-", dir=path.parent)
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


def validate_paths(home, values):
    paths = []
    for value in values:
        pure = PurePosixPath(value)
        if pure.is_absolute() or ".." in pure.parts or not pure.parts:
            raise SystemExit("server backup path must be relative and contained in home")
        path = home.joinpath(*pure.parts)
        if not path.exists():
            raise SystemExit("required server backup path is missing: %s" % value)
        paths.append(value)
    return paths


def validate_listing(listing, required):
    members = set(line.strip().rstrip("/") for line in listing.splitlines() if line.strip())
    missing = [path for path in required
               if path not in members and not any(item.startswith(path + "/") for item in members)]
    if missing:
        raise SystemExit("restore stream is missing required paths: %s" % ", ".join(missing))


def restic(args, *arguments, **kwargs):
    command = [str(args.restic), "-r", str(args.repository)] + list(arguments)
    environment = os.environ.copy()
    environment["RESTIC_PASSWORD_FILE"] = str(args.password_file)
    return subprocess.run(command, check=True, env=environment, **kwargs)


def backup(args):
    home = args.home.resolve()
    paths = validate_paths(home, args.path or DEFAULT_PATHS)
    environment = os.environ.copy()
    environment["RESTIC_PASSWORD_FILE"] = str(args.password_file)
    archive = subprocess.Popen(
        [str(args.tar), "--format=pax", "-C", str(home), "-cf", "-"] + paths,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    store = subprocess.run(
        [str(args.restic), "-r", str(args.repository), "backup", "--stdin",
         "--stdin-filename", "millennium-server-state.tar",
         "--tag", "millennium-server", "--tag", args.server_id],
        stdin=archive.stdout, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        env=environment)
    archive.stdout.close()
    archive_error = archive.stderr.read().decode("utf-8", "replace")
    archive_status = archive.wait()
    if archive_status or store.returncode:
        raise SystemExit("server-state backup stream failed: tar=%d restic=%d %s" %
                         (archive_status, store.returncode, archive_error.strip()))
    snapshots = restic(args, "snapshots", "--latest", "1", "--json",
                       "--tag", "millennium-server", stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE, text=True)
    values = json.loads(snapshots.stdout)
    if len(values) != 1 or not values[0].get("id"):
        raise SystemExit("cannot identify saved server-state snapshot")
    snapshot_id = values[0]["id"]
    dump = subprocess.Popen(
        [str(args.restic), "-r", str(args.repository), "dump", snapshot_id,
         "millennium-server-state.tar"], stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, env=environment)
    listing = subprocess.run([str(args.tar), "-tf", "-"], stdin=dump.stdout,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                             text=True)
    dump.stdout.close()
    dump_error = dump.stderr.read().decode("utf-8", "replace")
    dump_status = dump.wait()
    if dump_status or listing.returncode:
        raise SystemExit("server-state restore stream failed: restic=%d tar=%d %s" %
                         (dump_status, listing.returncode, dump_error.strip()))
    validate_listing(listing.stdout, paths)
    restic(args, "forget", "--tag", "millennium-server", "--keep-daily", "14",
           "--keep-weekly", "8", "--keep-monthly", "12",
           stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    record = {
        "schema": 1, "operation": "millennium-server-state-backup",
        "passed": True, "completed_at": now(), "server_id": args.server_id,
        "snapshot_id": snapshot_id[:8], "repository": "restricted-restic",
        "paths": paths, "plaintext_archive_staged": False,
        "restore_stream_verified": True,
    }
    atomic_json(args.evidence, record)
    print(json.dumps(record, sort_keys=True))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--home", type=Path, default=Path.home())
    parser.add_argument("--repository", type=Path, required=True)
    parser.add_argument("--password-file", type=Path, required=True)
    parser.add_argument("--evidence", type=Path, required=True)
    parser.add_argument("--server-id", required=True)
    parser.add_argument("--path", action="append")
    parser.add_argument("--restic", type=Path, default=Path("restic"))
    parser.add_argument("--tar", type=Path, default=Path("tar"))
    args = parser.parse_args()
    backup(args)


if __name__ == "__main__":
    main()
