#!/usr/bin/env python3
"""Boot and require both immutable system slots from one production image."""

import argparse
import json
from pathlib import Path
import subprocess
import sys


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--kernel", type=Path, required=True)
    parser.add_argument("--initrd", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    records = []
    for partition in (5, 6):
        output = args.output / ("system-" + str(partition))
        command = [
            sys.executable, str(Path(__file__).with_name("exact_image_test.py")),
            "--image", str(args.image), "--kernel", str(args.kernel),
            "--initrd", str(args.initrd), "--output", str(output),
            "--system-partition", str(partition),
        ]
        completed = subprocess.run(command, check=False)
        record = json.loads((output / "result.json").read_text())
        records.append(record)
        if completed.returncode:
            break
    passed = len(records) == 2 and all(item["result"] == "pass" for item in records)
    result = dict(records[0])
    result.update({
        "result": "pass" if passed else "fail",
        "system_partitions": [item["system_partition"] for item in records],
        "slot_results": records,
        "console_log": str((args.output / "console.log").resolve()),
    })
    result.pop("system_partition", None)
    (args.output / "console.log").write_bytes(b"\n".join(
        (args.output / ("system-" + str(item["system_partition"])) / "console.log").read_bytes()
        for item in records
    ))
    (args.output / "result.json").write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n")
    print(json.dumps(result, sort_keys=True))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
