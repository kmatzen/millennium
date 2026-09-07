#!/usr/bin/env python3
"""Generate the checked-in JSON Schemas for downloadable experiences."""

import argparse
import json
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]
SCHEMA_ROOT = ROOT / "docs" / "schemas"
ID_PATTERN = r"^[a-z0-9][a-z0-9._-]{0,63}$"
VERSION_PATTERN = r"^[0-9]+\.[0-9]+\.[0-9]+$"
DIGEST_PATTERN = r"^[0-9a-f]{64}$"


def package_schema():
    return {
        "$schema": "https://json-schema.org/draft/2020-12/schema",
        "$id": "https://updates.kmatzen.com/schemas/experience-package-v2.json",
        "title": "Millennium downloadable experience package",
        "type": "object",
        "additionalProperties": False,
        "required": ["schema", "id", "version", "sequence", "key_id",
                     "bundle", "sha256", "size", "compatibility",
                     "capabilities", "rating", "locales", "quotas", "files",
                     "state"],
        "properties": {
            "schema": {"const": 2},
            "id": {"type": "string", "pattern": ID_PATTERN},
            "version": {"type": "string", "pattern": VERSION_PATTERN},
            "sequence": {"type": "integer", "minimum": 1},
            "key_id": {"type": "string", "pattern": r"^[A-Za-z0-9._-]{1,64}$"},
            "bundle": {"type": "string", "pattern": r"^[a-z0-9._-]+\.tar\.gz$"},
            "sha256": {"type": "string", "pattern": DIGEST_PATTERN},
            "size": {"type": "integer", "minimum": 1, "maximum": 268435456},
            "compatibility": {
                "type": "object", "additionalProperties": False,
                "required": ["runtime_schema_min", "runtime_schema_max"],
                "properties": {
                    "runtime_schema_min": {"type": "integer", "minimum": 1},
                    "runtime_schema_max": {"type": "integer", "minimum": 1},
                    "daemon_min": {"type": "string", "pattern": VERSION_PATTERN},
                    "daemon_max": {"type": "string", "pattern": VERSION_PATTERN},
                },
            },
            "capabilities": {
                "type": "array", "uniqueItems": True,
                "items": {"enum": ["audio", "display", "handset", "keypad",
                                   "coin", "credential", "timers", "state"]},
            },
            "rating": {"enum": ["everyone", "teen", "mature"]},
            "locales": {"type": "array", "minItems": 1, "uniqueItems": True,
                        "items": {"type": "string",
                                  "pattern": r"^[a-z]{2,3}(-[A-Z]{2})?$"}},
            "quotas": {
                "type": "object", "additionalProperties": False,
                "required": ["storage_bytes", "state_bytes", "session_seconds"],
                "properties": {
                    "storage_bytes": {"type": "integer", "minimum": 1,
                                      "maximum": 268435456},
                    "state_bytes": {"type": "integer", "minimum": 0,
                                    "maximum": 1048576},
                    "session_seconds": {"type": "integer", "minimum": 1,
                                        "maximum": 86400},
                },
            },
            "files": {
                "type": "array", "minItems": 2,
                "items": {
                    "type": "object", "additionalProperties": False,
                    "required": ["path", "sha256", "size"],
                    "properties": {
                        "path": {"type": "string", "pattern": r"^[A-Za-z0-9_.-]+(/[A-Za-z0-9_.-]+)*$"},
                        "sha256": {"type": "string", "pattern": DIGEST_PATTERN},
                        "size": {"type": "integer", "minimum": 0,
                                 "maximum": 268435456},
                    },
                },
            },
            "state": {
                "type": "object", "additionalProperties": False,
                "required": ["schema", "rollback_compatible"],
                "properties": {
                    "schema": {"type": "integer", "minimum": 1},
                    "rollback_compatible": {"type": "boolean"},
                    "migrate_from": {"type": "array", "uniqueItems": True,
                                     "items": {"type": "integer", "minimum": 1}},
                },
            },
        },
    }


def catalog_schema():
    rollout = {
        "type": "object", "additionalProperties": False,
        "required": ["percentage"],
        "properties": {
            "percentage": {"type": "integer", "minimum": 0, "maximum": 100},
            "groups": {"type": "array", "uniqueItems": True,
                       "items": {"type": "string", "pattern": ID_PATTERN}},
            "hold": {"type": "boolean"},
        },
    }
    return {
        "$schema": "https://json-schema.org/draft/2020-12/schema",
        "$id": "https://updates.kmatzen.com/schemas/experience-catalog-v1.json",
        "title": "Millennium downloadable experience catalog",
        "type": "object", "additionalProperties": False,
        "required": ["schema", "channel", "sequence", "key_id", "packages",
                     "withdrawn", "denied"],
        "properties": {
            "schema": {"const": 1},
            "channel": {"enum": ["stable", "beta", "lab"]},
            "sequence": {"type": "integer", "minimum": 1},
            "key_id": {"type": "string", "pattern": r"^[A-Za-z0-9._-]{1,64}$"},
            "packages": {
                "type": "array",
                "items": {
                    "type": "object", "additionalProperties": False,
                    "required": ["id", "version", "sequence", "manifest_url",
                                 "manifest_sha256", "signature_url", "rollout"],
                    "properties": {
                        "id": {"type": "string", "pattern": ID_PATTERN},
                        "version": {"type": "string", "pattern": VERSION_PATTERN},
                        "sequence": {"type": "integer", "minimum": 1},
                        "manifest_url": {"type": "string", "pattern": r"^https://"},
                        "manifest_sha256": {"type": "string", "pattern": DIGEST_PATTERN},
                        "signature_url": {"type": "string", "pattern": r"^https://"},
                        "rollout": rollout,
                    },
                },
            },
            "withdrawn": {"type": "array", "uniqueItems": True,
                          "items": {"type": "string", "pattern": DIGEST_PATTERN}},
            "denied": {"type": "array", "uniqueItems": True,
                       "items": {"type": "string", "pattern": ID_PATTERN}},
        },
    }


def outputs():
    return {
        SCHEMA_ROOT / "experience-package-v2.schema.json": package_schema(),
        SCHEMA_ROOT / "experience-catalog-v1.schema.json": catalog_schema(),
    }


def render(value):
    return json.dumps(value, indent=2, sort_keys=True) + "\n"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    stale = []
    for path, value in outputs().items():
        text = render(value)
        if args.check:
            if not path.exists() or path.read_text() != text:
                stale.append(str(path.relative_to(ROOT)))
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)
            print(path.relative_to(ROOT))
    if stale:
        print("stale generated experience schemas: " + ", ".join(stale), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
