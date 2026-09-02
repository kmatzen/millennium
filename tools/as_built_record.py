#!/usr/bin/env python3
"""Capture and validate auditable per-phone as-built evidence.

Capture deliberately leaves physical observations blank.  Those fields cannot be
inferred from a repository or a running host and validation refuses handoff until
an operator has supplied them and every required recovery drill has passed.
"""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import socket
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
PHYSICAL_FIELDS = (
    "asset_serial", "owner_inventory_reference", "pcb_revision",
    "wiring_deviations", "power_supply_and_rating",
)
REQUIRED_TESTS = (
    "cold_boot", "ringer_audio_peak", "coin_validator", "controlled_brownout",
    "idle_power_loss", "active_call_power_loss", "content_save_power_loss",
    "ota_download_interruption", "mcu_flash_interruption",
    "host_activation_interruption",
)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def text_command(arguments):
    try:
        return subprocess.run(arguments, check=True, text=True,
                              stdout=subprocess.PIPE,
                              stderr=subprocess.DEVNULL).stdout.strip()
    except (FileNotFoundError, subprocess.CalledProcessError):
        return None


def atomic_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=".as-built-", dir=path.parent)
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


def capture(args):
    repo = args.repo.resolve()
    tracked = {}
    candidates = {
        "schematic": repo / "pcb/phonev6.kicad_sch",
        "pcb_layout": repo / "pcb/phonev6.kicad_pcb",
        "bom": repo / "pcb/phonev6.csv",
        "gerbers": repo / "pcb/production/phonev6-gerbers.zip",
        "keypad_hex": repo / "Arduino/build/keypad/keypad.ino.hex",
        "display_hex": repo / "Arduino/build/display/display.ino.hex",
    }
    for label, path in candidates.items():
        if path.is_file():
            tracked[label] = {"path": str(path.relative_to(repo)), "sha256": digest(path),
                              "installed_confirmed": False}
    record = {
        "schema": 1,
        "status": "incomplete",
        "device_id": args.device_id,
        "captured_at": datetime.now(timezone.utc).replace(microsecond=0).isoformat(),
        "captured_by": args.operator,
        "host": {
            "hostname": socket.gethostname(),
            "architecture": platform.machine(),
            "machine_id_sha256": None,
            "source_commit": text_command(["git", "-C", str(repo), "rev-parse", "HEAD"]),
        },
        "physical": {field: None for field in PHYSICAL_FIELDS},
        "candidate_artifacts": tracked,
        "installed_artifacts": {},
        "photos": [],
        "tests": {name: {"passed": None, "evidence": None, "date": None,
                         "instrument_and_load": None, "minimum_voltage": None}
                  for name in REQUIRED_TESTS},
    }
    machine_id = args.system_root / "etc/machine-id"
    if machine_id.is_file():
        record["host"]["machine_id_sha256"] = digest(machine_id)
    atomic_json(args.output, record)
    print(args.output)


def missing_evidence(record):
    missing = []
    for field in ("device_id", "captured_by"):
        if not record.get(field):
            missing.append(field)
    physical = record.get("physical", {})
    for field in PHYSICAL_FIELDS:
        if physical.get(field) in (None, "", "REQUIRED"):
            missing.append("physical." + field)
    installed = record.get("installed_artifacts", {})
    for field in ("schematic", "pcb_layout", "bom", "gerbers", "keypad_hex",
                  "display_hex", "host_binary", "content_manifest"):
        item = installed.get(field, {})
        if not item.get("sha256") or not item.get("identity"):
            missing.append("installed_artifacts." + field)
    if not record.get("photos"):
        missing.append("photos")
    for index, photo in enumerate(record.get("photos", [])):
        if not photo.get("sha256") or not photo.get("description"):
            missing.append("photos[%d]" % index)
    tests = record.get("tests", {})
    for name in REQUIRED_TESTS:
        test = tests.get(name, {})
        if test.get("passed") is not True or not test.get("evidence") or not test.get("date"):
            missing.append("tests." + name)
        if name in {"cold_boot", "ringer_audio_peak", "coin_validator",
                    "controlled_brownout"} and (not test.get("instrument_and_load") or
                                                 test.get("minimum_voltage") is None):
            missing.append("tests.%s.measurement" % name)
    return missing


def validate(args):
    record = json.loads(args.record.read_text(encoding="utf-8"))
    if record.get("schema") != 1:
        raise SystemExit("unsupported as-built schema")
    missing = missing_evidence(record)
    if missing:
        print("INCOMPLETE")
        for item in missing:
            print("- " + item)
        raise SystemExit(1)
    print("ACCEPTABLE: all required as-built evidence is present")


def main():
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest="command", required=True)
    make = commands.add_parser("capture")
    make.add_argument("--device-id", required=True)
    make.add_argument("--operator", required=True)
    make.add_argument("--output", type=Path, required=True)
    make.add_argument("--repo", type=Path, default=ROOT)
    make.add_argument("--system-root", type=Path, default=Path("/"))
    make.set_defaults(function=capture)
    check = commands.add_parser("validate")
    check.add_argument("record", type=Path)
    check.set_defaults(function=validate)
    args = parser.parse_args()
    args.function(args)


if __name__ == "__main__":
    main()
