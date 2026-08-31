#!/usr/bin/env python3
"""Emit release checksums, build provenance, and an SPDX 2.3 source SBOM."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]


def run(arguments):
    return subprocess.run(arguments, cwd=ROOT, check=True, text=True,
                          stdout=subprocess.PIPE).stdout.strip()


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--daemon", type=Path)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    commit = run(["git", "rev-parse", "HEAD"])
    version = (ROOT / "VERSION").read_text().strip()
    tracked = [ROOT / item for item in run(["git", "ls-files"]).splitlines()]
    tracked = [item for item in tracked if item.is_file()]
    created = datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")
    namespace = f"https://kmatzen.com/spdx/millennium/{version}/{commit}"
    files = []
    relationships = []
    for index, path in enumerate(sorted(tracked), 1):
        spdx_id = f"SPDXRef-File-{index}"
        files.append({"SPDXID": spdx_id, "fileName": "./" + str(path.relative_to(ROOT)),
                      "checksums": [{"algorithm": "SHA256", "checksumValue": sha256(path)}]})
        relationships.append({"spdxElementId": "SPDXRef-Package-Millennium",
                              "relationshipType": "CONTAINS", "relatedSpdxElement": spdx_id})
    sbom = {"spdxVersion": "SPDX-2.3", "dataLicense": "CC0-1.0",
            "SPDXID": "SPDXRef-DOCUMENT", "name": f"millennium-{version}",
            "documentNamespace": namespace,
            "creationInfo": {"created": created, "creators": ["Tool: millennium-release-evidence-1"]},
            "packages": [{"name": "millennium", "SPDXID": "SPDXRef-Package-Millennium",
                          "versionInfo": version, "downloadLocation": "NOASSERTION",
                          "filesAnalyzed": True}], "files": files,
            "relationships": [{"spdxElementId": "SPDXRef-DOCUMENT",
                               "relationshipType": "DESCRIBES",
                               "relatedSpdxElement": "SPDXRef-Package-Millennium"}] + relationships}
    (args.output_dir / "millennium.spdx.json").write_text(json.dumps(sbom, indent=2, sort_keys=True) + "\n")

    artifacts = [ROOT / "Arduino/build/keypad/keypad.ino.hex",
                 ROOT / "Arduino/build/display/display.ino.hex"]
    if args.daemon:
        artifacts.append(args.daemon.resolve())
    checksum_lines = [f"{sha256(path)}  {path.name}" for path in artifacts if path.is_file()]
    (args.output_dir / "SHA256SUMS").write_text("\n".join(checksum_lines) + "\n")
    provenance = {"schema": 1, "version": version, "source_commit": commit,
                  "created_at": created, "tools": {
                      "cc": run(["cc", "--version"]).splitlines()[0],
                      "python": run(["python3", "--version"]),
                      "arduino_cli": "1.5.1", "arduino_avr_core": "1.8.8",
                      "kicad_ci_image": "kicad/kicad:9.0.9"},
                  "artifacts": checksum_lines}
    (args.output_dir / "provenance.json").write_text(
        json.dumps(provenance, indent=2, sort_keys=True) + "\n")


if __name__ == "__main__":
    raise SystemExit(main())
