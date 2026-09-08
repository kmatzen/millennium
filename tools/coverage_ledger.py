#!/usr/bin/env python3
"""Validate, report, or execute the repository's acceptance coverage ledger."""
import argparse
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
DEFAULT = ROOT / "acceptance/coverage.json"


def load(path):
    value = json.loads(path.read_text(encoding="utf-8"))
    if value.get("schema") != 1:
        raise ValueError("unsupported coverage schema")
    suites = value.get("automated_suites")
    functions = value.get("functions")
    gates = value.get("physical_gates")
    if not isinstance(suites, dict) or not suites:
        raise ValueError("automated_suites must be a non-empty object")
    if not isinstance(functions, list) or not functions:
        raise ValueError("functions must be a non-empty array")
    if not isinstance(gates, dict):
        raise ValueError("physical_gates must be an object")
    seen = set()
    for name, suite in suites.items():
        command = suite.get("command")
        if not isinstance(command, list) or not command or not all(
                isinstance(item, str) and item for item in command):
            raise ValueError(f"suite {name}: command must be a non-empty argv array")
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
    for name, gate in gates.items():
        if not isinstance(gate.get("issue"), int) or gate["issue"] < 1:
            raise ValueError(f"physical gate {name}: missing GitHub issue")
        procedure = gate.get("procedure", "")
        file_name = procedure.split("#", 1)[0]
        if not file_name or not (ROOT / file_name).is_file():
            raise ValueError(f"physical gate {name}: procedure does not exist")
    return value


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=("validate", "report", "physical-plan",
                                           "run-automated"))
    parser.add_argument("--ledger", type=Path, default=DEFAULT)
    parser.add_argument("--suite", action="append", default=[])
    args = parser.parse_args()
    try:
        value = load(args.ledger)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        raise SystemExit(f"invalid coverage ledger: {exc}")
    if args.action == "validate":
        print(f"VALID: {len(value['functions'])} functions, "
              f"{len(value['automated_suites'])} automated suites, "
              f"{len(value['physical_gates'])} physical/human gates")
        return
    if args.action == "report":
        for item in value["functions"]:
            auto = ",".join(item.get("automated", [])) or "-"
            physical = ",".join(item.get("physical", [])) or "-"
            print(f"{item['id']}\tautomated={auto}\tphysical={physical}")
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
    for name in selected:
        suite = value["automated_suites"][name]
        if suite.get("runner") and suite["runner"] != "local":
            raise SystemExit(f"suite {name} requires runner {suite['runner']}; run it there")
        print(f"RUN {name}: {' '.join(suite['command'])}", flush=True)
        subprocess.run(suite["command"], cwd=ROOT / suite.get("cwd", "."), check=True)


if __name__ == "__main__":
    main()
