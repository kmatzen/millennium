#!/usr/bin/env python3
"""Publish a fully signed experience catalog into an immutable web root."""

import argparse
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import tempfile
from urllib.parse import urlparse

from catalogtool import validate_catalog
from install_content import (InstallError, digest, trusted_keys,
                             validate_manifest, verify_signature)


class PublishError(RuntimeError):
    pass


def fsync_directory(path):
    descriptor = os.open(path, os.O_RDONLY)
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def expected_release_parts(entry):
    manifest_name = PurePosixPath(urlparse(entry["manifest_url"]).path).name
    return (entry["id"], "%08d-%s" % (entry["sequence"], entry["version"]),
            manifest_name)


def publish(source, web_root, catalog_keys, package_keys):
    source, web_root = Path(source), Path(web_root)
    catalog_path = source / "catalog.json"
    catalog_signature = source / "catalog.json.sig"
    try:
        catalog = json.loads(catalog_path.read_text())
    except (OSError, json.JSONDecodeError) as exc:
        raise PublishError("signed catalog is missing or invalid") from exc
    validate_catalog(catalog)
    key = catalog_keys.get(catalog["key_id"])
    if key is None:
        raise PublishError("catalog signing key is not trusted")
    verify_signature(catalog_path, catalog_signature, key)
    channel = web_root / catalog["channel"]
    current = channel / "catalog.json"
    if current.exists():
        try:
            previous = json.loads(current.read_text())
        except (OSError, json.JSONDecodeError) as exc:
            raise PublishError("published catalog state is invalid") from exc
        if catalog["sequence"] <= previous.get("sequence", 0):
            raise PublishError("catalog sequence is not newer than published state")

    prepared = []
    for entry in catalog["packages"]:
        package_id, release_id, manifest_name = expected_release_parts(entry)
        parsed = PurePosixPath(urlparse(entry["manifest_url"]).path)
        expected_tail = PurePosixPath("releases") / package_id / release_id / manifest_name
        if len(parsed.parts) < len(expected_tail.parts) or parsed.parts[-len(expected_tail.parts):] != expected_tail.parts:
            raise PublishError("catalog package URL is not immutable")
        manifest_path = source / manifest_name
        signature_path = source / (manifest_name + ".sig")
        if digest(manifest_path) != entry["manifest_sha256"]:
            raise PublishError("package manifest does not match catalog digest")
        manifest = json.loads(manifest_path.read_text())
        validate_manifest(manifest)
        if (manifest["id"], manifest["version"], manifest["sequence"]) != (package_id, entry["version"], entry["sequence"]):
            raise PublishError("catalog and package identities differ")
        package_key = package_keys.get(manifest["key_id"])
        if package_key is None:
            raise PublishError("package signing key is not trusted")
        verify_signature(manifest_path, signature_path, package_key)
        bundle = source / manifest["bundle"]
        if bundle.stat().st_size != manifest["size"] or digest(bundle) != manifest["sha256"]:
            raise PublishError("package bundle does not match signed manifest")
        target = web_root / "releases" / package_id / release_id
        if target.exists():
            raise PublishError("immutable package release already exists")
        prepared.append((target, manifest_path, signature_path, bundle))

    web_root.mkdir(parents=True, exist_ok=True)
    published = []
    for target, manifest_path, signature_path, bundle in prepared:
        target.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=target.parent, prefix=".publish-") as temporary:
            stage = Path(temporary)
            for item in (manifest_path, signature_path, bundle):
                shutil.copy2(item, stage / item.name)
                os.chmod(stage / item.name, 0o644)
            fsync_directory(stage)
            os.rename(stage, target)
            published.append(target)
        fsync_directory(target.parent)

    channel.mkdir(parents=True, exist_ok=True)
    for source_path, name in ((catalog_signature, "catalog.json.sig"),
                              (catalog_path, "catalog.json")):
        temporary = channel / ("." + name + ".new")
        shutil.copy2(source_path, temporary)
        os.chmod(temporary, 0o644)
        with temporary.open("rb") as stream:
            os.fsync(stream.fileno())
        os.replace(temporary, channel / name)
    fsync_directory(channel)
    return {"channel": catalog["channel"], "sequence": catalog["sequence"],
            "releases": [str(path.relative_to(web_root)) for path in published]}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("web_root", type=Path)
    parser.add_argument("--catalog-key", action="append", default=[], required=True)
    parser.add_argument("--package-key", action="append", default=[], required=True)
    args = parser.parse_args()
    result = publish(args.source, args.web_root, trusted_keys(args.catalog_key),
                     trusted_keys(args.package_key))
    print(json.dumps(result, sort_keys=True))


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (PublishError, InstallError, OSError, ValueError) as exc:
        print("ERROR: %s" % exc)
        raise SystemExit(1)
