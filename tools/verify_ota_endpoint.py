#!/usr/bin/env python3
"""Verify a published OTA endpoint exactly as a device would, without applying."""

import argparse
import importlib.util
import json
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
WORKER = ROOT / "host/ota/millennium_ota.py"
spec = importlib.util.spec_from_file_location("millennium_ota", WORKER)
ota = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ota)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest-url", default="https://updates.kmatzen.com/millennium/stable/manifest.json")
    parser.add_argument("--public-key", type=Path, required=True)
    parser.add_argument("--channel", default="stable")
    parser.add_argument("--architecture", default="auto")
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="verify-millennium-ota-") as name:
        root = Path(name)
        manifest_path = root / "manifest.json"
        signature_path = root / "manifest.json.sig"
        bundle_path = root / "bundle.tar.gz"
        release_path = root / "release"
        ota.download(args.manifest_url, manifest_path, maximum=1024 * 1024)
        ota.download(args.manifest_url + ".sig", signature_path, maximum=4096)
        ota.verify_signature(args.public_key, manifest_path, signature_path)
        manifest = json.loads(manifest_path.read_text())
        ota.validate_manifest(manifest, args.channel)
        ota.download(manifest["bundle"]["url"], bundle_path, maximum=manifest["bundle"]["size"])
        if bundle_path.stat().st_size != manifest["bundle"]["size"] or ota.sha256(bundle_path) != manifest["bundle"]["sha256"]:
            raise ota.OtaError("published bundle digest or size mismatch")
        ota.safe_extract(bundle_path, release_path)
        ota.verify_release(release_path, manifest, args.architecture)
        print(json.dumps({
            "verified": True,
            "version": manifest["version"],
            "sequence": manifest["sequence"],
            "architecture": json.loads((release_path / "release.json").read_text())["architecture"],
        }, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print("endpoint verification failed: %s" % error, file=sys.stderr)
        sys.exit(1)
