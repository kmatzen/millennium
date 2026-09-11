#!/usr/bin/env python3
"""Extract attested identities from Intel HEX firmware into release.json."""

import json
from pathlib import Path
import re
import sys


PATTERN = re.compile(
    rb"MILLENNIUM role=(keypad|display) version=([^ ]+) protocol=([0-9]+) "
    rb"build=([^ ]+) selftest=ok")


def identity(path):
    raw = bytearray()
    for line in Path(path).read_text(encoding="ascii").splitlines():
        record = bytes.fromhex(line[1:])
        if record[3] == 0:
            raw.extend(record[4:4 + record[0]])
    match = PATTERN.search(raw)
    if not match:
        raise ValueError("firmware identity missing from %s" % path)
    role = match.group(1).decode()
    return role, {
        "role": role,
        "version": match.group(2).decode(),
        "protocol": int(match.group(3)),
        "build": match.group(4).decode(),
    }


def main():
    if len(sys.argv) != 6:
        raise SystemExit(
            "usage: write_bootstrap_release.py KEYPAD DISPLAY VERSION SOURCE_COMMIT OUTPUT")
    firmware = dict(identity(path) for path in sys.argv[1:3])
    if set(firmware) != {"keypad", "display"}:
        raise SystemExit("bootstrap firmware identities are incomplete")
    version, source_commit = sys.argv[3:5]
    if not re.fullmatch(r"[A-Za-z0-9._-]{1,64}", version):
        raise SystemExit("invalid bootstrap version")
    if not re.fullmatch(r"[0-9a-f]{40}", source_commit):
        raise SystemExit("invalid bootstrap source commit")
    Path(sys.argv[5]).write_text(
        json.dumps({"firmware": firmware, "sequence": 0,
                    "source_commit": source_commit, "version": version},
                   sort_keys=True) + "\n",
        encoding="utf-8")


if __name__ == "__main__":
    main()
