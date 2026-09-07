#!/usr/bin/env python3
"""Verify boot recovery after a host-driven experience transition power cut."""

import json
import os
from pathlib import Path
import sys


if len(sys.argv) != 2 or sys.argv[1] not in {"activating", "selecting"}:
    raise SystemExit("usage: experience-power-verify-guest.py {activating|selecting}")

phase = sys.argv[1]
root = Path("/var/lib/millennium/content")
digest = ("a" if phase == "activating" else "b") * 64
if (root / "activation-journal.json").exists():
    raise SystemExit("activation journal survived recovery")
if not (root / "current").is_symlink() or os.readlink(root / "current") != "releases/last-line-2.1.0":
    raise SystemExit("known-good fallback was not restored")
quarantine = json.loads((root / "quarantine.json").read_text())
if quarantine.get(digest, {}).get("reason") != "interrupted_activation":
    raise SystemExit("interrupted digest was not quarantined")
print(digest)
