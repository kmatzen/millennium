#!/usr/bin/env python3
"""Reject a release unless all automated evidence is complete and coherent."""
import argparse
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
HEX40 = re.compile(r"[0-9a-f]{40}")


def read_json(path):
    value = json.loads(path.read_text(encoding="utf-8"))
    if value.get("schema") != 1:
        raise ValueError(f"{path}: unsupported schema")
    return value


def validate(policy, local, qemu, exact, expected_commit):
    errors = []
    if not HEX40.fullmatch(expected_commit):
        errors.append("expected source commit is not a full lowercase SHA-1")
    if local.get("source_commit") != expected_commit:
        errors.append("local evidence belongs to a different source commit")
    results = local.get("results", [])
    by_suite = {item.get("suite"): item for item in results}
    for suite in policy["required_local_suites"]:
        item = by_suite.get(suite)
        if not item or item.get("passed") is not True or item.get("returncode") != 0:
            errors.append(f"local suite did not pass: {suite}")
    if qemu.get("source_commit") != expected_commit:
        errors.append("QEMU evidence belongs to a different source commit")
    if qemu.get("passed") is not True:
        errors.append("QEMU full test did not pass")
    if policy.get("require_exact_production_image") and \
            qemu.get("exact_production_image_tested") is not True:
        errors.append("QEMU run did not boot the exact production image")
    missing = sorted(set(policy["required_qemu_acceptance"]) -
                     set(qemu.get("acceptance", [])))
    if missing:
        errors.append("QEMU acceptance is incomplete: " + ", ".join(missing))
    if policy.get("forbid_physical_hardware_claim") and \
            qemu.get("physical_hardware_claimed") is not False:
        errors.append("QEMU evidence makes an invalid physical-hardware claim")
    if exact.get("result") != "pass" or exact.get("exact_image_userspace") is not True:
        errors.append("exact production-image boot did not pass")
    if exact.get("physical_hardware_claimed") is not False:
        errors.append("exact-image evidence makes an invalid hardware claim")
    if not isinstance(exact.get("image_size"), int) or exact["image_size"] <= 0:
        errors.append("exact-image evidence does not identify a non-empty image")
    return errors


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--local", type=Path, required=True)
    parser.add_argument("--qemu", type=Path, required=True)
    parser.add_argument("--exact", type=Path, required=True)
    parser.add_argument("--source-commit")
    parser.add_argument("--policy", type=Path,
                        default=ROOT / "acceptance/release-gate.json")
    args = parser.parse_args()
    commit = args.source_commit or subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    try:
        errors = validate(read_json(args.policy), read_json(args.local),
                          read_json(args.qemu), read_json(args.exact), commit)
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as exc:
        raise SystemExit(f"REJECTED: invalid release evidence: {exc}")
    if errors:
        raise SystemExit("REJECTED:\n- " + "\n- ".join(errors))
    print(f"ACCEPTED: all automated release gates passed for {commit}")
    print("Physical-only acceptance remains separately required; no hardware claim was made.")


if __name__ == "__main__":
    main()
