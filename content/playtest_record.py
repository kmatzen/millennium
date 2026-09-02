#!/usr/bin/env python3
"""Create and validate privacy-preserving first-time-caller playtest records."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import tempfile


SCENARIOS = ("repeat", "invalid_input", "timeout", "interruption",
             "return_visit", "offline", "optional_input")


def now():
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat()


def atomic_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=".playtest-", dir=path.parent)
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
        raise SystemExit("unsupported playtest schema")
    return value


def file_evidence(path):
    path = path.resolve()
    if not path.is_file():
        raise SystemExit("evidence file does not exist: %s" % path)
    return {"path": str(path),
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


def valid_evidence(reference, record_path=None):
    if not isinstance(reference, dict) or not reference.get("path"):
        return False
    expected = reference.get("sha256", "")
    if len(expected) != 64 or any(c not in "0123456789abcdef" for c in expected):
        return False
    if record_path is None:
        return True
    path = Path(reference["path"])
    if not path.is_absolute():
        path = record_path.parent / path
    return path.is_file() and hashlib.sha256(path.read_bytes()).hexdigest() == expected


def update_record(path, change):
    value = load_record(path)
    change(value)
    value["updated_at"] = now()
    value["accepted_for_handoff"] = None
    atomic_json(path, value)


def template(args):
    value = {
        "schema": 1,
        "content_version": args.content_version,
        "device_as_built_record": args.as_built_record,
        "created_at": now(),
        "participants": [
            {"anonymous_id": "P1", "first_time_caller": True,
             "started_without_coaching": None, "completed_primary_ending": None,
             "time_to_first_action_seconds": None, "total_duration_seconds": None,
             "confusion_or_disengagement": [], "optional_interaction_discovered": None,
             "audio_clear": None, "display_legible": None,
             "observation_evidence": None},
            {"anonymous_id": "P2", "first_time_caller": True,
             "started_without_coaching": None, "completed_primary_ending": None,
             "time_to_first_action_seconds": None, "total_duration_seconds": None,
             "confusion_or_disengagement": [], "optional_interaction_discovered": None,
             "audio_clear": None, "display_legible": None,
             "observation_evidence": None},
        ],
        "physical_scenarios": {name: {"passed": None, "evidence": None}
                               for name in SCENARIOS},
        "open_defects": [], "accepted_for_handoff": None,
    }
    atomic_json(args.output, value)


def yes(value):
    return value == "yes"


def record_participant(args):
    def change(value):
        participants = value.setdefault("participants", [])
        matches = [item for item in participants if item.get("anonymous_id") == args.participant]
        if len(matches) != 1:
            raise SystemExit("participant ID must identify exactly one record")
        matches[0].update({
            "first_time_caller": True,
            "started_without_coaching": yes(args.started_without_coaching),
            "completed_primary_ending": yes(args.completed_primary_ending),
            "time_to_first_action_seconds": args.time_to_first_action_seconds,
            "total_duration_seconds": args.total_duration_seconds,
            "confusion_or_disengagement": args.confusion,
            "optional_interaction_discovered": yes(args.optional_interaction_discovered),
            "audio_clear": yes(args.audio_clear),
            "display_legible": yes(args.display_legible),
            "observation_evidence": file_evidence(args.evidence_file),
        })
    update_record(args.record, change)


def record_scenario(args):
    def change(value):
        value.setdefault("physical_scenarios", {})[args.scenario] = {
            "passed": args.result == "pass",
            "evidence": file_evidence(args.evidence_file),
        }
    update_record(args.record, change)


def decision(args):
    def change(value):
        value["open_defects"] = args.open_defect
        value["accepted_for_handoff"] = args.result == "accept"
        value["decision_evidence"] = file_evidence(args.evidence_file)
    value = load_record(args.record)
    change(value)
    value["updated_at"] = now()
    atomic_json(args.record, value)


def omissions(value, record_path=None):
    missing = []
    if not value.get("content_version") or not value.get("device_as_built_record"):
        missing.append("release identity")
    participants = value.get("participants", [])
    if len(participants) < 2:
        missing.append("at least two participants")
    for index, participant in enumerate(participants):
        prefix = "participants[%d]" % index
        if participant.get("first_time_caller") is not True:
            missing.append(prefix + ".first_time_caller")
        for field in ("started_without_coaching", "completed_primary_ending",
                      "audio_clear", "display_legible"):
            if participant.get(field) is not True:
                missing.append(prefix + "." + field)
        for field in ("time_to_first_action_seconds", "total_duration_seconds"):
            measurement = participant.get(field)
            if (isinstance(measurement, bool)
                    or not isinstance(measurement, (int, float)) or measurement < 0):
                missing.append(prefix + "." + field)
        first = participant.get("time_to_first_action_seconds")
        total = participant.get("total_duration_seconds")
        if (isinstance(first, (int, float)) and not isinstance(first, bool)
                and isinstance(total, (int, float)) and not isinstance(total, bool)
                and total < first):
            missing.append(prefix + ".duration_order")
        if "confusion_or_disengagement" not in participant:
            missing.append(prefix + ".confusion_or_disengagement")
        if not isinstance(participant.get("optional_interaction_discovered"), bool):
            missing.append(prefix + ".optional_interaction_discovered")
        if not valid_evidence(participant.get("observation_evidence"), record_path):
            missing.append(prefix + ".observation_evidence")
    scenarios = value.get("physical_scenarios", {})
    for name in SCENARIOS:
        result = scenarios.get(name, {})
        if (result.get("passed") is not True
                or not valid_evidence(result.get("evidence"), record_path)):
            missing.append("physical_scenarios." + name)
    if value.get("open_defects"):
        missing.append("open_defects")
    if value.get("accepted_for_handoff") is not True:
        missing.append("accepted_for_handoff")
    if not valid_evidence(value.get("decision_evidence"), record_path):
        missing.append("decision_evidence")
    return missing


def validate(args):
    value = load_record(args.record)
    missing = omissions(value, args.record)
    if missing:
        print("INCOMPLETE")
        for item in missing:
            print("- " + item)
        raise SystemExit(1)
    print("ACCEPTABLE: first-time and resilience playtest evidence passes")


def main():
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest="command", required=True)
    make = commands.add_parser("template")
    make.add_argument("--content-version", required=True)
    make.add_argument("--as-built-record", required=True)
    make.add_argument("--output", type=Path, required=True)
    make.set_defaults(function=template)
    participant = commands.add_parser("record-participant")
    participant.add_argument("record", type=Path)
    participant.add_argument("--participant", required=True)
    participant.add_argument("--started-without-coaching", choices=("yes", "no"), required=True)
    participant.add_argument("--completed-primary-ending", choices=("yes", "no"), required=True)
    participant.add_argument("--time-to-first-action-seconds", type=float, required=True)
    participant.add_argument("--total-duration-seconds", type=float, required=True)
    participant.add_argument("--audio-clear", choices=("yes", "no"), required=True)
    participant.add_argument("--display-legible", choices=("yes", "no"), required=True)
    participant.add_argument("--optional-interaction-discovered", choices=("yes", "no"), required=True)
    participant.add_argument("--confusion", action="append", default=[])
    participant.add_argument("--evidence-file", type=Path, required=True)
    participant.set_defaults(function=record_participant)
    scenario = commands.add_parser("record-scenario")
    scenario.add_argument("record", type=Path)
    scenario.add_argument("--scenario", choices=SCENARIOS, required=True)
    scenario.add_argument("--result", choices=("pass", "fail"), required=True)
    scenario.add_argument("--evidence-file", type=Path, required=True)
    scenario.set_defaults(function=record_scenario)
    release = commands.add_parser("decision")
    release.add_argument("record", type=Path)
    release.add_argument("--result", choices=("accept", "reject"), required=True)
    release.add_argument("--open-defect", action="append", default=[])
    release.add_argument("--evidence-file", type=Path, required=True)
    release.set_defaults(function=decision)
    check = commands.add_parser("validate")
    check.add_argument("record", type=Path)
    check.set_defaults(function=validate)
    args = parser.parse_args()
    args.function(args)


if __name__ == "__main__":
    main()
