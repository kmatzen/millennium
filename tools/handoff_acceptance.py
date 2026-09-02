#!/usr/bin/env python3
"""Create, update, and validate the final physical-phone handoff record."""

import argparse
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import tempfile


ROOT = Path(__file__).resolve().parents[1]
WIFI_PLATFORMS = ("ios", "android", "macos", "windows")


def now():
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat()


def atomic_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=".handoff-", dir=path.parent)
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


def read(path):
    return json.loads(path.read_text(encoding="utf-8"))


def file_evidence(path):
    path = path.resolve()
    if not path.is_file():
        raise SystemExit("evidence file does not exist: %s" % path)
    return {"path": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def linked_path(record_path, value):
    path = Path(value)
    return path if path.is_absolute() else record_path.parent / path


def valid_evidence(reference, record_path, check_file):
    if not isinstance(reference, dict) or not reference.get("path"):
        return False
    expected = reference.get("sha256", "")
    if len(expected) != 64 or any(c not in "0123456789abcdef" for c in expected):
        return False
    if not check_file:
        return True
    path = linked_path(record_path, reference["path"])
    return path.is_file() and hashlib.sha256(path.read_bytes()).hexdigest() == expected


def template(args):
    value = {
        "schema": 1,
        "status": "incomplete",
        "device_id": args.device_id,
        "created_at": now(),
        "as_built_record": args.as_built_record,
        "playtest_record": args.playtest_record,
        "wifi_clients": {
            platform: {"passed": None, "date": None, "evidence": None}
            for platform in WIFI_PLATFORMS
        },
        "offline_signing_key_copies": [],
        "external_maintenance": {
            "passed": None,
            "date": None,
            "network_description": None,
            "evidence": None,
        },
    }
    atomic_json(args.output, value)
    print(args.output)


def update_record(args, change):
    value = read(args.record)
    if value.get("schema") != 1:
        raise SystemExit("unsupported handoff schema")
    change(value)
    value["updated_at"] = now()
    value["status"] = "incomplete"
    atomic_json(args.record, value)


def record_wifi(args):
    def change(value):
        value.setdefault("wifi_clients", {})[args.platform] = {
            "passed": args.result == "pass",
            "date": args.date,
            "evidence": file_evidence(args.evidence_file),
        }
    update_record(args, change)


def add_key_copy(args):
    ciphertext = args.ciphertext.resolve()
    if not ciphertext.is_file():
        raise SystemExit("ciphertext does not exist: %s" % ciphertext)
    digest = hashlib.sha256(ciphertext.read_bytes()).hexdigest()

    def change(value):
        copies = value.setdefault("offline_signing_key_copies", [])
        if any(item.get("media_label") == args.media_label for item in copies):
            raise SystemExit("media label already recorded")
        copies.append({
            "media_label": args.media_label,
            "ciphertext_sha256": digest,
            "physically_distinct": True,
            "verified_at": args.verified_at,
            "recovery_tested_at": args.recovery_tested_at,
            "evidence": file_evidence(args.evidence_file),
        })
    update_record(args, change)


def record_external(args):
    def change(value):
        value["external_maintenance"] = {
            "passed": args.result == "pass",
            "date": args.date,
            "network_description": args.network_description,
            "evidence": file_evidence(args.evidence_file),
        }
    update_record(args, change)


def omissions(value, record_path, check_linked=True):
    missing = []
    if not value.get("device_id"):
        missing.append("device_id")
    wifi = value.get("wifi_clients", {})
    for platform in WIFI_PLATFORMS:
        item = wifi.get(platform, {})
        if (item.get("passed") is not True or not item.get("date")
                or not valid_evidence(item.get("evidence"), record_path, check_linked)):
            missing.append("wifi_clients." + platform)
    copies = value.get("offline_signing_key_copies", [])
    usable = [item for item in copies
              if item.get("media_label") and item.get("ciphertext_sha256")
              and item.get("physically_distinct") is True and item.get("verified_at")
              and item.get("recovery_tested_at")
              and valid_evidence(item.get("evidence"), record_path, check_linked)]
    if len({item["media_label"] for item in usable}) < 2:
        missing.append("offline_signing_key_copies.two_distinct_media")
    external = value.get("external_maintenance", {})
    if (external.get("passed") is not True or not external.get("date")
            or not external.get("network_description")
            or not valid_evidence(external.get("evidence"), record_path, check_linked)):
        missing.append("external_maintenance")
    for field, module_path, function_name in (
        ("as_built_record", ROOT / "tools/as_built_record.py", "missing_evidence"),
        ("playtest_record", ROOT / "content/playtest_record.py", "omissions"),
    ):
        reference = value.get(field)
        if not reference:
            missing.append(field)
            continue
        if not check_linked:
            continue
        path = linked_path(record_path, reference)
        if not path.is_file():
            missing.append(field + ".missing")
            continue
        linked = read(path)
        module = load_module("handoff_" + field, module_path)
        if getattr(module, function_name)(linked):
            missing.append(field + ".incomplete")
    return missing


def validate(args):
    value = read(args.record)
    if value.get("schema") != 1:
        raise SystemExit("unsupported handoff schema")
    missing = omissions(value, args.record)
    if missing:
        print("INCOMPLETE")
        for item in missing:
            print("- " + item)
        raise SystemExit(1)
    value["status"] = "acceptable"
    value["validated_at"] = now()
    atomic_json(args.record, value)
    print("ACCEPTABLE: all physical handoff gates have auditable evidence")


def main():
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest="command", required=True)
    make = commands.add_parser("template")
    make.add_argument("--device-id", required=True)
    make.add_argument("--as-built-record", required=True)
    make.add_argument("--playtest-record", required=True)
    make.add_argument("--output", type=Path, required=True)
    make.set_defaults(function=template)
    wifi = commands.add_parser("record-wifi")
    wifi.add_argument("record", type=Path)
    wifi.add_argument("--platform", choices=WIFI_PLATFORMS, required=True)
    wifi.add_argument("--result", choices=("pass", "fail"), required=True)
    wifi.add_argument("--date", required=True)
    wifi.add_argument("--evidence-file", type=Path, required=True)
    wifi.set_defaults(function=record_wifi)
    key = commands.add_parser("add-key-copy")
    key.add_argument("record", type=Path)
    key.add_argument("--media-label", required=True)
    key.add_argument("--ciphertext", type=Path, required=True)
    key.add_argument("--verified-at", required=True)
    key.add_argument("--recovery-tested-at", required=True)
    key.add_argument("--evidence-file", type=Path, required=True)
    key.set_defaults(function=add_key_copy)
    external = commands.add_parser("record-external")
    external.add_argument("record", type=Path)
    external.add_argument("--result", choices=("pass", "fail"), required=True)
    external.add_argument("--date", required=True)
    external.add_argument("--network-description", required=True)
    external.add_argument("--evidence-file", type=Path, required=True)
    external.set_defaults(function=record_external)
    check = commands.add_parser("validate")
    check.add_argument("record", type=Path)
    check.set_defaults(function=validate)
    args = parser.parse_args()
    args.function(args)


if __name__ == "__main__":
    main()
