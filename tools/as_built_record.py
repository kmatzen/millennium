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
INSTALLED_ARTIFACTS = (
    "schematic", "pcb_layout", "bom", "gerbers", "keypad_hex",
    "display_hex", "host_binary", "content_manifest",
)
MEASURED_TESTS = {"cold_boot", "ringer_audio_peak", "coin_validator",
                  "controlled_brownout"}


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


def load_record(path):
    value = json.loads(path.read_text(encoding="utf-8"))
    if value.get("schema") != 1:
        raise SystemExit("unsupported as-built schema")
    return value


def update_record(path, change):
    value = load_record(path)
    change(value)
    value["status"] = "incomplete"
    value["updated_at"] = datetime.now(timezone.utc).replace(microsecond=0).isoformat()
    atomic_json(path, value)


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


def set_physical(args):
    def change(record):
        record.setdefault("physical", {})[args.field] = args.value
    update_record(args.record, change)


def add_photo(args):
    photo = args.path.resolve()
    if not photo.is_file():
        raise SystemExit("photo does not exist: %s" % photo)

    def change(record):
        item = {"path": str(photo), "sha256": digest(photo),
                "description": args.description}
        photos = record.setdefault("photos", [])
        if any(existing.get("sha256") == item["sha256"] for existing in photos):
            raise SystemExit("photo content is already recorded")
        photos.append(item)
    update_record(args.record, change)


def record_installed(args):
    artifact = args.path.resolve()
    if not artifact.is_file():
        raise SystemExit("installed artifact does not exist: %s" % artifact)

    def change(record):
        record.setdefault("installed_artifacts", {})[args.name] = {
            "path": str(artifact), "sha256": digest(artifact),
            "identity": args.identity,
        }
    update_record(args.record, change)


def record_test(args):
    evidence = args.evidence_file.resolve()
    if not evidence.is_file():
        raise SystemExit("evidence file does not exist: %s" % evidence)
    if args.name in MEASURED_TESTS:
        if not args.instrument_and_load or args.minimum_voltage is None:
            raise SystemExit("%s requires instrument/load and minimum voltage" % args.name)

    def change(record):
        record.setdefault("tests", {})[args.name] = {
            "passed": args.result == "pass",
            "evidence": {"path": str(evidence), "sha256": digest(evidence)},
            "date": args.date,
            "instrument_and_load": args.instrument_and_load,
            "minimum_voltage": args.minimum_voltage,
        }
    update_record(args.record, change)


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
        if name in MEASURED_TESTS and (not test.get("instrument_and_load") or
                                       test.get("minimum_voltage") is None):
            missing.append("tests.%s.measurement" % name)
    return missing


def validate(args):
    record = load_record(args.record)
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
    physical = commands.add_parser("set-physical")
    physical.add_argument("record", type=Path)
    physical.add_argument("--field", choices=PHYSICAL_FIELDS, required=True)
    physical.add_argument("--value", required=True)
    physical.set_defaults(function=set_physical)
    photo = commands.add_parser("add-photo")
    photo.add_argument("record", type=Path)
    photo.add_argument("--path", type=Path, required=True)
    photo.add_argument("--description", required=True)
    photo.set_defaults(function=add_photo)
    installed = commands.add_parser("record-installed")
    installed.add_argument("record", type=Path)
    installed.add_argument("--name", choices=INSTALLED_ARTIFACTS, required=True)
    installed.add_argument("--path", type=Path, required=True)
    installed.add_argument("--identity", required=True)
    installed.set_defaults(function=record_installed)
    test = commands.add_parser("record-test")
    test.add_argument("record", type=Path)
    test.add_argument("--name", choices=REQUIRED_TESTS, required=True)
    test.add_argument("--result", choices=("pass", "fail"), required=True)
    test.add_argument("--evidence-file", type=Path, required=True)
    test.add_argument("--date", required=True)
    test.add_argument("--instrument-and-load")
    test.add_argument("--minimum-voltage", type=float)
    test.set_defaults(function=record_test)
    check = commands.add_parser("validate")
    check.add_argument("record", type=Path)
    check.set_defaults(function=validate)
    args = parser.parse_args()
    args.function(args)


if __name__ == "__main__":
    main()
