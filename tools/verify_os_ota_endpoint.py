#!/usr/bin/env python3
"""Download and verify a published OS release exactly as a device check does."""

import argparse
import importlib.util
import json
from pathlib import Path
import tempfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
MODULE = ROOT / "host/os_ota/millennium_os_ota.py"
spec = importlib.util.spec_from_file_location("millennium_os_ota", MODULE)
ota = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ota)


def download(url, destination, maximum):
    size = 0
    with urllib.request.urlopen(url, timeout=60) as response, destination.open("xb") as output:
        if response.geturl().split(":", 1)[0] != "https":
            raise ota.OsOtaError("OS endpoint redirected away from HTTPS")
        while True:
            chunk = response.read(1024 * 1024)
            if not chunk:
                break
            size += len(chunk)
            if size > maximum:
                raise ota.OsOtaError("OS endpoint response exceeds signed size")
            output.write(chunk)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest-url", default="https://updates.kmatzen.com/millennium/os/stable/manifest.json")
    parser.add_argument("--public-key", type=Path, required=True)
    parser.add_argument("--architecture", default="arm64")
    parser.add_argument("--layout-id", default="zero2w-ab-mbr-v1")
    parser.add_argument("--board-model", default="Raspberry Pi Zero 2 W")
    parser.add_argument("--device-group", default="production")
    parser.add_argument("--installed-sequence", type=int, default=-1)
    args = parser.parse_args()

    expected = {"channel": "stable", "architecture": args.architecture,
                "layout_id": args.layout_id, "board_model": args.board_model,
                "device_group": args.device_group,
                "installed_sequence": args.installed_sequence}
    with tempfile.TemporaryDirectory(prefix="verify-millennium-os-") as name:
        work = Path(name)
        manifest_path = work / "manifest.json"
        signature_path = work / "manifest.json.sig"
        download(args.manifest_url, manifest_path, 1024 * 1024)
        download(args.manifest_url + ".sig", signature_path, 4096)
        manifest = ota.load_verified_manifest(
            manifest_path, signature_path, args.public_key, expected)
        images = {}
        for role in ("boot", "root"):
            metadata = manifest["images"][role]
            path = work / (role + ".img.gz")
            download(metadata["url"], path, metadata["compressed_size"])
            ota.verify_image(path, metadata)
            images[role] = metadata["expanded_sha256"]
        print(json.dumps({"verified": True, "sequence": manifest["sequence"],
                          "version": manifest["version"],
                          "source_commit": manifest["source_commit"],
                          "expanded_sha256": images}, sort_keys=True))


if __name__ == "__main__":
    main()
