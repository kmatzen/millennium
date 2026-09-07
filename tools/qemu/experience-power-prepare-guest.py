#!/usr/bin/env python3
"""Stage a durable in-flight experience transition for a host power cut."""

import json
import os
from pathlib import Path
import sys


if len(sys.argv) != 2 or sys.argv[1] not in {"activating", "selecting"}:
    raise SystemExit("usage: experience-power-prepare-guest.py {activating|selecting}")

phase = sys.argv[1]
root = Path("/var/lib/millennium/content")
previous = Path("releases/last-line-2.1.0")
candidate = Path("releases/last-line-2.1.1")
if not (root / previous).is_dir() or not (root / candidate).is_dir():
    raise SystemExit("run experience-lifecycle-test before the power test")

digest = ("a" if phase == "activating" else "b") * 64
journal = {
    "phase": phase,
    "previous": str(previous),
    "digest": digest,
    "state_path": "state/last-line/current.json",
    "state_before": None,
}
temporary_journal = root / ".activation-journal.json.power-test"
with temporary_journal.open("wb") as output:
    output.write(json.dumps(journal, separators=(",", ":"), sort_keys=True).encode())
    output.flush()
    os.fsync(output.fileno())
os.replace(temporary_journal, root / "activation-journal.json")

temporary_link = root / ".current.power-test"
temporary_link.unlink(missing_ok=True)
temporary_link.symlink_to(candidate)
os.replace(temporary_link, root / "current")
directory_fd = os.open(root, os.O_RDONLY | os.O_DIRECTORY)
try:
    os.fsync(directory_fd)
finally:
    os.close(directory_fd)
print(digest)
