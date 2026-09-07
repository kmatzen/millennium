#!/usr/bin/env python3
"""Build, validate, sign, and evaluate Millennium experience catalogs."""

import argparse
import hashlib
import json
from pathlib import Path
import re
from urllib.parse import urljoin, urlparse

from storytool import canonical, sha256, sign_ed25519


class CatalogError(ValueError):
    pass


ID_RE = re.compile(r"^[a-z0-9][a-z0-9._-]{0,63}$")
SEMVER_RE = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+$")
DIGEST_RE = re.compile(r"^[0-9a-f]{64}$")
KEY_RE = re.compile(r"^[A-Za-z0-9._-]{1,64}$")
CHANNELS = {"stable", "beta", "lab"}


def https_url(value):
    parsed = urlparse(value)
    return parsed.scheme == "https" and bool(parsed.netloc) and not parsed.fragment


def validate_catalog(value):
    required = {"schema", "channel", "sequence", "key_id", "packages",
                "withdrawn", "denied"}
    if not isinstance(value, dict) or set(value) != required or value.get("schema") != 1:
        raise CatalogError("catalog schema is invalid")
    if value["channel"] not in CHANNELS:
        raise CatalogError("catalog channel is invalid")
    if (not isinstance(value["sequence"], int) or isinstance(value["sequence"], bool)
            or value["sequence"] < 1 or not KEY_RE.fullmatch(str(value["key_id"]))):
        raise CatalogError("catalog identity is invalid")
    if (not isinstance(value["withdrawn"], list)
            or len(value["withdrawn"]) != len(set(value["withdrawn"]))
            or any(not DIGEST_RE.fullmatch(str(item)) for item in value["withdrawn"])):
        raise CatalogError("catalog withdrawals are invalid")
    if (not isinstance(value["denied"], list)
            or len(value["denied"]) != len(set(value["denied"]))
            or any(not ID_RE.fullmatch(str(item)) for item in value["denied"])):
        raise CatalogError("catalog denylist is invalid")
    if not isinstance(value["packages"], list):
        raise CatalogError("catalog packages are invalid")
    identities = set()
    for item in value["packages"]:
        item_required = {"id", "version", "sequence", "manifest_url",
                         "manifest_sha256", "signature_url", "rollout"}
        if not isinstance(item, dict) or set(item) != item_required:
            raise CatalogError("catalog package entry is invalid")
        identity = (item["id"], item["version"], item["sequence"])
        if (not ID_RE.fullmatch(str(item["id"]))
                or not SEMVER_RE.fullmatch(str(item["version"]))
                or not isinstance(item["sequence"], int)
                or isinstance(item["sequence"], bool) or item["sequence"] < 1
                or identity in identities
                or not https_url(item["manifest_url"])
                or not https_url(item["signature_url"])
                or not DIGEST_RE.fullmatch(str(item["manifest_sha256"]))):
            raise CatalogError("catalog package identity is invalid")
        identities.add(identity)
        rollout = item["rollout"]
        if (not isinstance(rollout, dict)
                or set(rollout) - {"percentage", "groups", "hold"}
                or "percentage" not in rollout
                or not isinstance(rollout["percentage"], int)
                or isinstance(rollout["percentage"], bool)
                or not 0 <= rollout["percentage"] <= 100
                or not isinstance(rollout.get("hold", False), bool)):
            raise CatalogError("catalog rollout is invalid")
        groups = rollout.get("groups", [])
        if (not isinstance(groups, list) or len(groups) != len(set(groups))
                or any(not ID_RE.fullmatch(str(group)) for group in groups)):
            raise CatalogError("catalog rollout groups are invalid")
    return value


def load_package_manifest(path):
    try:
        value = json.loads(path.read_text())
    except (OSError, json.JSONDecodeError) as exc:
        raise CatalogError(f"cannot read package manifest {path}: {exc}") from exc
    required = {"schema", "id", "version", "sequence", "key_id", "bundle",
                "sha256", "size", "compatibility", "capabilities", "rating",
                "locales", "quotas", "files", "state"}
    if not isinstance(value, dict) or set(value) != required or value.get("schema") != 2:
        raise CatalogError(f"catalog requires a schema-2 package manifest: {path}")
    return value


def build_catalog(manifests, base_url, channel, sequence, key_id,
                  percentage=100, groups=(), hold=False, withdrawn=(), denied=()):
    if not base_url.endswith("/"):
        base_url += "/"
    if not https_url(base_url):
        raise CatalogError("catalog base URL must use HTTPS")
    packages = []
    for path in sorted(manifests, key=lambda item: item.name):
        manifest = load_package_manifest(path)
        # Package metadata lives with its immutable payload, never beneath the
        # mutable channel directory. Sequence and version together prevent a
        # later release from reusing an existing URL identity.
        release_path = (f"releases/{manifest['id']}/"
                        f"{manifest['sequence']:08d}-{manifest['version']}/{path.name}")
        manifest_url = urljoin(base_url, release_path)
        packages.append({
            "id": manifest["id"], "version": manifest["version"],
            "sequence": manifest["sequence"], "manifest_url": manifest_url,
            "manifest_sha256": sha256(path),
            "signature_url": manifest_url + ".sig",
            "rollout": {"percentage": percentage, "groups": sorted(groups),
                        "hold": hold},
        })
    value = {"schema": 1, "channel": channel, "sequence": sequence,
             "key_id": key_id, "packages": packages,
             "withdrawn": sorted(withdrawn), "denied": sorted(denied)}
    return validate_catalog(value)


def rollout_bucket(device_id, package_id):
    digest = hashlib.sha256(f"{package_id}:{device_id}".encode()).digest()
    return int.from_bytes(digest[:8], "big") % 100


def eligible_packages(catalog, device_id, groups=(), installed=None):
    validate_catalog(catalog)
    installed = installed or {}
    groups = set(groups)
    withdrawn = set(catalog["withdrawn"])
    denied = set(catalog["denied"])
    result = []
    for item in catalog["packages"]:
        rollout = item["rollout"]
        if (item["id"] in denied or item["manifest_sha256"] in withdrawn
                or rollout.get("hold", False)
                or item["sequence"] <= installed.get(item["id"], 0)):
            continue
        required_groups = set(rollout.get("groups", []))
        if required_groups and not required_groups & groups:
            continue
        if rollout_bucket(device_id, item["id"]) >= rollout["percentage"]:
            continue
        result.append(item)
    return result


def main():
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)
    build = subparsers.add_parser("build")
    build.add_argument("manifest", type=Path, nargs="+")
    build.add_argument("--base-url", required=True)
    build.add_argument("--channel", choices=sorted(CHANNELS), required=True)
    build.add_argument("--sequence", type=int, required=True)
    build.add_argument("--key-id", required=True)
    build.add_argument("--percentage", type=int, default=100)
    build.add_argument("--group", action="append", default=[])
    build.add_argument("--hold", action="store_true")
    build.add_argument("--withdraw", action="append", default=[])
    build.add_argument("--deny", action="append", default=[])
    build.add_argument("--output", type=Path, required=True)
    build.add_argument("--private-key", type=Path)
    validate = subparsers.add_parser("validate")
    validate.add_argument("catalog", type=Path)
    select = subparsers.add_parser("select")
    select.add_argument("catalog", type=Path)
    select.add_argument("--device-id", required=True)
    select.add_argument("--group", action="append", default=[])
    args = parser.parse_args()
    if args.command == "build":
        value = build_catalog(args.manifest, args.base_url, args.channel,
                              args.sequence, args.key_id, args.percentage,
                              args.group, args.hold, args.withdraw, args.deny)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(canonical(value))
        if args.private_key:
            sign_ed25519(args.private_key, args.output,
                         Path(str(args.output) + ".sig"))
        print(args.output)
    else:
        try:
            value = json.loads(args.catalog.read_text())
        except (OSError, json.JSONDecodeError) as exc:
            raise CatalogError(f"cannot read catalog: {exc}") from exc
        validate_catalog(value)
        if args.command == "validate":
            print(f"OK: {value['channel']} sequence {value['sequence']}")
        else:
            print(json.dumps(eligible_packages(value, args.device_id, args.group),
                             sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except CatalogError as exc:
        print(f"ERROR: {exc}")
        raise SystemExit(1)
