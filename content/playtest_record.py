#!/usr/bin/env python3
"""Create and validate privacy-preserving first-time-caller playtest records."""

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path


SCENARIOS = ("repeat", "invalid_input", "timeout", "interruption",
             "return_visit", "offline", "optional_input")


def template(args):
    value = {
        "schema": 1,
        "content_version": args.content_version,
        "device_as_built_record": args.as_built_record,
        "created_at": datetime.now(timezone.utc).replace(microsecond=0).isoformat(),
        "participants": [
            {"anonymous_id": "P1", "first_time_caller": True,
             "started_without_coaching": None, "completed_primary_ending": None,
             "time_to_first_action_seconds": None, "total_duration_seconds": None,
             "confusion_or_disengagement": [], "optional_interaction_discovered": None,
             "audio_clear": None, "display_legible": None},
            {"anonymous_id": "P2", "first_time_caller": True,
             "started_without_coaching": None, "completed_primary_ending": None,
             "time_to_first_action_seconds": None, "total_duration_seconds": None,
             "confusion_or_disengagement": [], "optional_interaction_discovered": None,
             "audio_clear": None, "display_legible": None},
        ],
        "physical_scenarios": {name: {"passed": None, "evidence": None}
                               for name in SCENARIOS},
        "open_defects": [], "accepted_for_handoff": None,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n",
                           encoding="utf-8")


def omissions(value):
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
            if not isinstance(participant.get(field), (int, float)):
                missing.append(prefix + "." + field)
        if "confusion_or_disengagement" not in participant:
            missing.append(prefix + ".confusion_or_disengagement")
    scenarios = value.get("physical_scenarios", {})
    for name in SCENARIOS:
        result = scenarios.get(name, {})
        if result.get("passed") is not True or not result.get("evidence"):
            missing.append("physical_scenarios." + name)
    if value.get("open_defects"):
        missing.append("open_defects")
    if value.get("accepted_for_handoff") is not True:
        missing.append("accepted_for_handoff")
    return missing


def validate(args):
    value = json.loads(args.record.read_text(encoding="utf-8"))
    if value.get("schema") != 1:
        raise SystemExit("unsupported playtest schema")
    missing = omissions(value)
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
    check = commands.add_parser("validate")
    check.add_argument("record", type=Path)
    check.set_defaults(function=validate)
    args = parser.parse_args()
    args.function(args)


if __name__ == "__main__":
    main()
