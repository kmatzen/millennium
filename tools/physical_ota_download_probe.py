#!/usr/bin/env python3
"""Verify a signed OTA download over a controllable relay without installing it."""

import argparse
from datetime import datetime, timezone
import hashlib
import importlib.util
from importlib.machinery import SourceFileLoader
import json
import os
from pathlib import Path
import socket
import ssl
import tempfile
import time
from urllib.parse import urlsplit, urlunsplit
from urllib.request import Request, urlopen


def now():
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat()


def load_worker(path):
    loader = SourceFileLoader("millennium_ota_probe_worker", str(path))
    spec = importlib.util.spec_from_loader(loader.name, loader)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def relay_url(url, port):
    parsed = urlsplit(url)
    if parsed.scheme != "https" or not parsed.hostname:
        raise ValueError("probe accepts HTTPS OTA URLs only")
    host = parsed.hostname
    netloc = "%s:%d" % (host, port)
    return urlunsplit((parsed.scheme, netloc, parsed.path, parsed.query, parsed.fragment))


def atomic_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=".ota-probe-", dir=path.parent)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
            json.dump(value, stream, indent=2, sort_keys=True)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def fetch(url, destination, rate=0, progress=None):
    request = Request(url, headers={"User-Agent": "Millennium-Physical-OTA-Probe/1"})
    context = ssl.create_default_context()
    received = 0
    started = time.monotonic()
    with urlopen(request, timeout=15, context=context) as response, destination.open("wb") as stream:
        while True:
            chunk = response.read(16384)
            if not chunk:
                break
            stream.write(chunk)
            received += len(chunk)
            if progress:
                atomic_json(progress, {"phase": "bundle-download", "bytes_received": received,
                                       "updated_at": now()})
            if rate:
                delay = received / rate - (time.monotonic() - started)
                if delay > 0:
                    time.sleep(delay)
        stream.flush()
        os.fsync(stream.fileno())
    return received


class RelayResolution:
    """Resolve only the OTA hostname to the isolated relay without editing hosts."""

    def __init__(self, hostname, address, port):
        self.hostname = hostname
        self.address = address
        self.port = port
        self.original = socket.getaddrinfo

    def __enter__(self):
        def resolve(host, port, *args, **kwargs):
            if host == self.hostname and port == self.port:
                host = self.address
            return self.original(host, port, *args, **kwargs)
        socket.getaddrinfo = resolve

    def __exit__(self, _kind, _value, _traceback):
        socket.getaddrinfo = self.original


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--worker", type=Path,
                        default=Path("/usr/local/libexec/millennium-ota"))
    parser.add_argument("--config", type=Path, default=Path("/etc/millennium/ota.conf"))
    parser.add_argument("--relay-port", type=int, default=8443)
    parser.add_argument("--relay-address", default="192.168.8.239")
    parser.add_argument("--rate", type=int, default=65536,
                        help="bundle bytes per second; zero disables pacing")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    report = {"schema": 1, "operation": "physical-ota-download-probe",
              "started_at": now(), "passed": False}
    atomic_json(args.output, dict(report, phase="starting"))
    try:
        worker = load_worker(args.worker)
        config = worker.load_config(str(args.config))
        hostname = urlsplit(config["manifest_url"]).hostname
        with RelayResolution(hostname, args.relay_address, args.relay_port), \
                tempfile.TemporaryDirectory(prefix="physical-ota-probe-") as root_name:
            root = Path(root_name)
            manifest_path = root / "manifest.json"
            signature_path = root / "manifest.json.sig"
            manifest_url = config["manifest_url"]
            fetch(relay_url(manifest_url, args.relay_port), manifest_path)
            fetch(relay_url(manifest_url + ".sig", args.relay_port), signature_path)
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            worker.validate_manifest(manifest, config["channel"])
            public_key = worker.trusted_public_key(config, manifest)
            worker.verify_signature(public_key, manifest_path, signature_path)
            report.update({"phase": "manifest-verified", "sequence": manifest["sequence"],
                           "version": manifest["version"], "key_id": manifest["key_id"],
                           "manifest_sha256": hashlib.sha256(manifest_path.read_bytes()).hexdigest()})
            atomic_json(args.output, report)

            bundle_path = root / "bundle.tar.gz"
            received = fetch(relay_url(manifest["bundle"]["url"], args.relay_port),
                             bundle_path, args.rate, args.output)
            digest = hashlib.sha256(bundle_path.read_bytes()).hexdigest()
            if received != manifest["bundle"]["size"]:
                raise RuntimeError("bundle size mismatch")
            if digest != manifest["bundle"]["sha256"]:
                raise RuntimeError("bundle digest mismatch")
            report.update({"phase": "complete", "passed": True, "completed_at": now(),
                           "bundle_bytes": received, "bundle_sha256": digest})
    except Exception as error:
        report.update({"phase": "interrupted", "completed_at": now(),
                       "error": str(error)})
        atomic_json(args.output, report)
        print(json.dumps(report, sort_keys=True))
        return 1
    atomic_json(args.output, report)
    print(json.dumps(report, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
