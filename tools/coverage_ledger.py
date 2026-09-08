#!/usr/bin/env python3
"""Validate, report, or execute the repository's acceptance coverage ledger."""
import argparse
import datetime
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
DEFAULT = ROOT / "acceptance/coverage.json"
INVENTORY = ROOT / "acceptance/inventory.json"
FIDELITY = {"unit", "process", "guest-integration", "outside-in",
            "exact-image", "physical-only"}
FAULTS = {"authentication", "malformed-input", "interruption", "retry",
          "rollback", "resource-exhaustion", "restart"}
INVENTORY_CATEGORIES = {"production-executable", "systemd-unit", "api-route",
                        "cli-action", "updater-state", "wifi-route",
                        "maintenance-action", "plugin", "experience-event",
                        "mcu-message", "recovery-tool"}


def load(path):
    value = json.loads(path.read_text(encoding="utf-8"))
    if value.get("schema") != 1:
        raise ValueError("unsupported coverage schema")
    suites = value.get("automated_suites")
    functions = value.get("functions")
    gates = value.get("physical_gates")
    profiles = value.get("profiles")
    if not isinstance(suites, dict) or not suites:
        raise ValueError("automated_suites must be a non-empty object")
    if not isinstance(functions, list) or not functions:
        raise ValueError("functions must be a non-empty array")
    if not isinstance(gates, dict):
        raise ValueError("physical_gates must be an object")
    if not isinstance(profiles, dict):
        raise ValueError("profiles must be an object")
    open_defects = value.get("open_defects")
    if not isinstance(open_defects, list) or any(
            not isinstance(number, int) or number <= 0 for number in open_defects):
        raise ValueError("open_defects must be a list of positive issue numbers")
    if len(open_defects) != len(set(open_defects)):
        raise ValueError("open_defects must not contain duplicates")
    seen = set()
    for name, suite in suites.items():
        command = suite.get("command")
        if not isinstance(command, list) or not command or not all(
                isinstance(item, str) and item for item in command):
            raise ValueError(f"suite {name}: command must be a non-empty argv array")
        if not suite.get("evidence"):
            raise ValueError(f"suite {name}: retained evidence location is missing")
        cwd = ROOT / suite.get("cwd", ".")
        if not cwd.is_dir():
            raise ValueError(f"suite {name}: cwd does not exist: {cwd}")
        executable = command[0]
        if "/" in executable and not (cwd / executable).is_file():
            raise ValueError(f"suite {name}: executable does not exist: {executable}")
    for function in functions:
        identity = function.get("id")
        if not isinstance(identity, str) or not identity or identity in seen:
            raise ValueError(f"invalid or duplicate function id: {identity!r}")
        seen.add(identity)
        automated = function.get("automated", [])
        physical = function.get("physical", [])
        if not automated and not physical:
            raise ValueError(f"function {identity}: no acceptance coverage")
        unknown = sorted(set(automated) - set(suites))
        if unknown:
            raise ValueError(f"function {identity}: unknown suites: {', '.join(unknown)}")
        unknown = sorted(set(physical) - set(gates))
        if unknown:
            raise ValueError(f"function {identity}: unknown physical gates: {', '.join(unknown)}")
        profile = profiles.get(identity)
        if not isinstance(profile, dict):
            raise ValueError(f"function {identity}: missing fidelity/fault profile")
        fidelity = profile.get("fidelity", [])
        faults = profile.get("faults", [])
        if not fidelity or set(fidelity) - FIDELITY:
            raise ValueError(f"function {identity}: invalid fidelity profile")
        if not faults or set(faults) - FAULTS:
            raise ValueError(f"function {identity}: invalid fault profile")
        if "physical-only" in fidelity and not profile.get("mock_limitation"):
            raise ValueError(f"function {identity}: physical mock limitation is not explicit")
    if set(profiles) != seen:
        raise ValueError("profiles and functions must have identical identities")
    for name, gate in gates.items():
        if not isinstance(gate.get("issue"), int) or gate["issue"] < 1:
            raise ValueError(f"physical gate {name}: missing GitHub issue")
        procedure = gate.get("procedure", "")
        file_name = procedure.split("#", 1)[0]
        if not file_name or not (ROOT / file_name).is_file():
            raise ValueError(f"physical gate {name}: procedure does not exist")
    inventory = json.loads(INVENTORY.read_text(encoding="utf-8"))
    if inventory.get("schema") != 1 or not isinstance(inventory.get("items"), list):
        raise ValueError("invalid generated item inventory")
    item_ids = set()
    categories = set()
    for item in inventory["items"]:
        identity = item.get("id")
        if not identity or identity in item_ids:
            raise ValueError(f"invalid or duplicate inventory id: {identity!r}")
        item_ids.add(identity)
        categories.add(item.get("category"))
        if item.get("function") not in seen:
            raise ValueError(f"inventory item {identity}: unknown function owner")
        source = item.get("source", "")
        if not source or not (ROOT / source).is_file():
            raise ValueError(f"inventory item {identity}: source does not exist")
    if categories != INVENTORY_CATEGORIES:
        raise ValueError("generated inventory categories are incomplete")
    return value


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=("validate", "report", "inventory",
                                           "physical-plan", "run-automated"))
    parser.add_argument("--ledger", type=Path, default=DEFAULT)
    parser.add_argument("--suite", action="append", default=[])
    parser.add_argument("--evidence", type=Path)
    args = parser.parse_args()
    try:
        value = load(args.ledger)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        raise SystemExit(f"invalid coverage ledger: {exc}")
    if args.action == "validate":
        print(f"VALID: {len(value['functions'])} functions, "
              f"{len(value['automated_suites'])} automated suites, "
              f"{len(value['physical_gates'])} physical/human gates, "
              f"{len(json.loads(INVENTORY.read_text())['items'])} inventory items")
        return
    if args.action == "report":
        for item in value["functions"]:
            auto = ",".join(item.get("automated", [])) or "-"
            physical = ",".join(item.get("physical", [])) or "-"
            print(f"{item['id']}\tautomated={auto}\tphysical={physical}")
        return
    if args.action == "inventory":
        for item in json.loads(INVENTORY.read_text(encoding="utf-8"))["items"]:
            profile = value["profiles"][item["function"]]
            print(f"{item['id']}\towner={item['function']}\t"
                  f"fidelity={','.join(profile['fidelity'])}\t"
                  f"faults={','.join(profile['faults'])}\t{item['source']}")
        return
    if args.action == "physical-plan":
        consumers = {name: [] for name in value["physical_gates"]}
        for item in value["functions"]:
            for gate in item.get("physical", []):
                consumers[gate].append(item["id"])
        for name, gate in value["physical_gates"].items():
            print(f"#{gate['issue']}\t{name}\t{gate['procedure']}\t" +
                  ",".join(consumers[name]))
        return
    selected = args.suite or list(value["automated_suites"])
    unknown = sorted(set(selected) - set(value["automated_suites"]))
    if unknown:
        raise SystemExit("unknown suite: " + ", ".join(unknown))
    if args.evidence:
        args.evidence.parent.mkdir(parents=True, exist_ok=True)
    results = []
    for name in selected:
        suite = value["automated_suites"][name]
        if suite.get("runner") and suite["runner"] != "local":
            raise SystemExit(f"suite {name} requires runner {suite['runner']}; run it there")
        print(f"RUN {name}: {' '.join(suite['command'])}", flush=True)
        started = datetime.datetime.now(datetime.timezone.utc)
        completed = subprocess.run(suite["command"], cwd=ROOT / suite.get("cwd", "."))
        results.append({"suite": name, "command": suite["command"],
                        "started_at": started.replace(microsecond=0).isoformat(),
                        "completed_at": datetime.datetime.now(
                            datetime.timezone.utc).replace(microsecond=0).isoformat(),
                        "passed": completed.returncode == 0,
                        "returncode": completed.returncode})
        if completed.returncode:
            if args.evidence:
                args.evidence.write_text(json.dumps(
                    {"schema": 1, "source_commit": subprocess.check_output(
                        ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
                     "results": results}, indent=2, sort_keys=True) + "\n")
            raise SystemExit(completed.returncode)
    if args.evidence:
        args.evidence.write_text(json.dumps(
            {"schema": 1, "source_commit": subprocess.check_output(
                ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
             "results": results}, indent=2, sort_keys=True) + "\n")


if __name__ == "__main__":
    main()
