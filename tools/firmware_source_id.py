#!/usr/bin/env python3
"""Print a deterministic short ID for the Arduino firmware source inputs."""

import hashlib
from pathlib import Path


root = Path(__file__).resolve().parents[1]
inputs = [root / "VERSION", root / "Arduino/Makefile"]
for directory in (root / "Arduino/sketches", root / "Arduino/hardware",
                  root / "Arduino/libraries"):
    inputs.extend(path for path in directory.rglob("*") if path.is_file())

digest = hashlib.sha256()
for path in sorted(inputs):
    digest.update(str(path.relative_to(root)).encode() + b"\0")
    digest.update(path.read_bytes() + b"\0")
print(digest.hexdigest()[:12])
