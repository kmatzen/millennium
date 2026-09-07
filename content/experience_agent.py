#!/usr/bin/env python3
"""Unattended signed downloadable-experience lifecycle worker."""

import argparse
import json
import os
from pathlib import Path
import shutil
import stat
import tempfile
import time
import re
from urllib.parse import urljoin, urlparse
from urllib.request import Request, urlopen

from catalogtool import eligible_packages, validate_catalog
from install_content import (InstallError, atomic_json, atomic_link, digest,
                             install, trusted_keys, verify_inventory,
                             verify_signature)


class ExperienceError(RuntimeError):
    pass


def read_json(path, default=None):
    try:
        return json.loads(path.read_text())
    except FileNotFoundError:
        return default
    except (OSError, json.JSONDecodeError) as exc:
        raise ExperienceError("invalid lifecycle state: %s" % path) from exc


def read_request(path):
    try:
        descriptor = os.open(path, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0))
    except FileNotFoundError:
        return None
    try:
        info = os.fstat(descriptor)
        if not stat.S_ISREG(info.st_mode) or info.st_size > 4096:
            raise ExperienceError("invalid owner request file")
        with os.fdopen(descriptor, "r") as stream:
            descriptor = -1
            return json.load(stream)
    except (OSError, json.JSONDecodeError) as exc:
        raise ExperienceError("invalid owner request") from exc
    finally:
        if descriptor >= 0:
            os.close(descriptor)


def https_url(value):
    parsed = urlparse(value)
    return parsed.scheme == "https" and bool(parsed.netloc) and not parsed.fragment


def fetch_https(url, destination, maximum, opener=urlopen):
    if not https_url(url):
        raise ExperienceError("experience transport must use HTTPS")
    request = Request(url, headers={"User-Agent": "millennium-experience/1"})
    temporary = destination.with_name("." + destination.name + ".part")
    temporary.unlink(missing_ok=True)
    total = 0
    try:
        with opener(request, timeout=30) as response, temporary.open("wb") as output:
            final_url = response.geturl()
            if not https_url(final_url):
                raise ExperienceError("experience transport redirected away from HTTPS")
            while True:
                block = response.read(min(1024 * 1024, maximum + 1 - total))
                if not block:
                    break
                total += len(block)
                if total > maximum:
                    raise ExperienceError("experience download exceeds its size limit")
                output.write(block)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, destination)
    finally:
        temporary.unlink(missing_ok=True)
    return total


def fetch_json(url, opener=urlopen):
    try:
        with opener(Request(url, headers={"User-Agent": "millennium-experience/1"}), timeout=5) as response:
            return response.getcode() == 200, json.loads(response.read(64 * 1024))
    except Exception:
        return False, {}


class ExperienceAgent:
    def __init__(self, root, catalog_url, catalog_keys, package_keys,
                 device_id, groups=(), runtime_schema=1, daemon_version=None,
                 fallback=None, max_releases=4, fetcher=fetch_https,
                 idle=lambda: True, hardware_healthy=lambda: True,
                 activation_healthy=lambda: True, allowed_time=lambda: True):
        self.root = Path(root)
        self.catalog_url = catalog_url
        self.catalog_keys = catalog_keys
        self.package_keys = package_keys
        self.device_id = device_id
        self.groups = tuple(groups)
        self.runtime_schema = runtime_schema
        self.daemon_version = daemon_version
        self.fallback = fallback
        self.max_releases = max(2, max_releases)
        self.fetcher = fetcher
        self.idle = idle
        self.hardware_healthy = hardware_healthy
        self.activation_healthy = activation_healthy
        self.allowed_time = allowed_time
        self.root.mkdir(parents=True, exist_ok=True)

    @property
    def journal_path(self):
        return self.root / "activation-journal.json"

    def status(self):
        value = read_json(self.root / "experience-status.json", {})
        value["active"] = os.readlink(self.root / "current") if (self.root / "current").is_symlink() else None
        value["fallback"] = self.fallback
        value["storage_bytes"] = sum(p.stat().st_size for p in (self.root / "releases").glob("**/*") if p.is_file()) if (self.root / "releases").exists() else 0
        value["quarantined"] = len(read_json(self.root / "quarantine.json", {}))
        value["disabled"] = sorted(read_json(self.root / "disabled.json", []))
        value["installed"] = self._installed_releases()
        return value

    def _installed_releases(self):
        result = []
        releases = self.root / "releases"
        if not releases.exists():
            return result
        for release in sorted(releases.iterdir()):
            try:
                manifest = read_json(release / "package.manifest.json")
            except ExperienceError:
                # A torn or corrupt inactive release must not prevent boot-time
                # recovery or owner status for the restored known-good release.
                continue
            if manifest:
                result.append({"id": manifest["id"], "version": manifest["version"],
                               "sequence": manifest.get("sequence", 0),
                               "rating": manifest.get("rating", "unknown"),
                               "bytes": sum(p.stat().st_size for p in release.glob("**/*") if p.is_file())})
        return result

    def _active_package_id(self):
        current = self.root / "current"
        if not current.is_symlink():
            return None
        target = (self.root / os.readlink(current)).resolve()
        manifest = read_json(target / "package.manifest.json")
        if manifest:
            return manifest.get("id")
        name = target.name
        # Factory and pre-agent releases can predate embedded package
        # manifests. Strip only a validated semantic-version suffix; package
        # IDs may themselves contain arbitrary hyphens.
        match = re.fullmatch(r"([a-z0-9][a-z0-9._-]{0,63})-[0-9]+\.[0-9]+\.[0-9]+", name)
        return match.group(1) if match else None

    def _write_status(self, **fields):
        value = read_json(self.root / "experience-status.json", {})
        value.update(fields)
        value["updated_at"] = int(time.time())
        atomic_json(self.root / "experience-status.json", value)
        self._publish_owner_status()

    def _publish_owner_status(self):
        atomic_json(self.root / "owner-status.json", self.status())
        os.chmod(self.root / "owner-status.json", 0o644)

    def _metric(self, name):
        # Deliberately aggregate only lifecycle outcome categories. No caller
        # input, credential, speech, timestamps per session, or branch history
        # is retained here.
        metrics = read_json(self.root / "metrics.json", {})
        metrics[name] = int(metrics.get(name, 0)) + 1
        atomic_json(self.root / "metrics.json", metrics)

    def recover(self):
        journal = read_json(self.journal_path)
        recovered = False
        # Staging directories are never selectable releases. They may remain
        # after power loss during download, extraction, or validation and are
        # safe to remove before considering activation recovery.
        for pattern in (".catalog-*", "releases/.install-*"):
            for stale in self.root.glob(pattern):
                if stale.is_dir() and not stale.is_symlink():
                    shutil.rmtree(stale)
                    recovered = True
        if not journal:
            return recovered
        previous = journal.get("previous")
        if previous and (self.root / previous).is_dir():
            atomic_link(self.root, "current", Path(previous))
        elif (self.root / "current").is_symlink():
            (self.root / "current").unlink()
        state_path = journal.get("state_path")
        if state_path:
            target = self.root / state_path
            if journal.get("state_before") is None:
                target.unlink(missing_ok=True)
            else:
                atomic_json(target, journal["state_before"])
        self._quarantine(journal.get("digest"), "interrupted_activation")
        self._metric("activation_interrupted")
        self.journal_path.unlink(missing_ok=True)
        self._write_status(last_result="rolled_back_interrupted_activation")
        return True

    def _quarantine(self, package_digest, reason):
        if not package_digest:
            return
        value = read_json(self.root / "quarantine.json", {})
        prior = value.get(package_digest, {})
        value[package_digest] = {"reason": reason,
                                 "attempts": prior.get("attempts", 0) + 1,
                                 "time": int(time.time())}
        atomic_json(self.root / "quarantine.json", value)

    def _download_catalog(self, directory):
        catalog_path = directory / "catalog.json"
        signature_path = directory / "catalog.json.sig"
        self.fetcher(self.catalog_url, catalog_path, 2 * 1024 * 1024)
        self.fetcher(self.catalog_url + ".sig", signature_path, 64 * 1024)
        catalog = read_json(catalog_path)
        validate_catalog(catalog)
        key = self.catalog_keys.get(catalog["key_id"])
        if key is None:
            raise ExperienceError("catalog uses an untrusted key ID")
        verify_signature(catalog_path, signature_path, key)
        prior = read_json(self.root / "catalog-state.json", {})
        if catalog["sequence"] < prior.get("sequence", 0):
            raise ExperienceError("catalog sequence rollback")
        catalog_digest = digest(catalog_path)
        if (catalog["sequence"] == prior.get("sequence") and prior.get("sha256")
                and catalog_digest != prior["sha256"]):
            raise ExperienceError("catalog sequence identity changed")
        self.catalog_digest = catalog_digest
        return catalog

    def _apply_denylist(self, catalog):
        current = self.root / "current"
        if not current.is_symlink():
            return
        active = Path(os.readlink(current)).name
        denied = self._active_package_id() in set(catalog["denied"])
        if denied:
            if not self.fallback or not (self.root / "releases" / self.fallback).is_dir():
                raise ExperienceError("active experience denied and permanent fallback unavailable")
            atomic_link(self.root, "current", Path("releases") / self.fallback)
            self._write_status(last_result="denylist_fallback", denied=active)

    def _stage_entry(self, entry, directory):
        manifest = directory / "package.manifest.json"
        signature = directory / "package.manifest.json.sig"
        self.fetcher(entry["manifest_url"], manifest, 1024 * 1024)
        if digest(manifest) != entry["manifest_sha256"]:
            raise ExperienceError("catalog-bound manifest digest mismatch")
        self.fetcher(entry["signature_url"], signature, 64 * 1024)
        value = read_json(manifest)
        if (value.get("id"), value.get("version"), value.get("sequence")) != (entry["id"], entry["version"], entry["sequence"]):
            raise ExperienceError("catalog and package identities differ")
        bundle = value.get("bundle", "")
        if not isinstance(bundle, str) or "/" in bundle:
            raise ExperienceError("package bundle name is invalid")
        self.fetcher(urljoin(entry["manifest_url"], bundle), directory / bundle,
                     min(int(value.get("size", 0)) + 1, 256 * 1024 * 1024))
        return manifest, signature, value

    def _migrate_state(self, manifest):
        contract = manifest["state"]
        state_root = self.root / "state" / manifest["id"]
        current = read_json(state_root / "current.json")
        if current is None or current.get("schema") == contract["schema"]:
            return
        if current.get("schema") not in contract.get("migrate_from", []):
            raise ExperienceError("no declared state migration path")
        payload = current.get("data", {})
        encoded = json.dumps(payload, separators=(",", ":")).encode()
        if len(encoded) > manifest["quotas"]["state_bytes"]:
            raise ExperienceError("migrated state exceeds package quota")
        backup = state_root / ("schema-%s.json" % current["schema"])
        atomic_json(backup, current)
        atomic_json(state_root / "current.json", {"schema": contract["schema"], "data": payload})

    def set_enabled(self, package_id, enabled):
        disabled = set(read_json(self.root / "disabled.json", []))
        if enabled:
            disabled.discard(package_id)
        else:
            disabled.add(package_id)
            if self._active_package_id() == package_id:
                self.select_fallback()
        atomic_json(self.root / "disabled.json", sorted(disabled))
        self._publish_owner_status()
        return self.status()

    def select_fallback(self):
        if not self.fallback or not (self.root / "releases" / self.fallback).is_dir():
            raise ExperienceError("permanent fallback unavailable")
        current = self.root / "current"
        if current.is_symlink():
            atomic_link(self.root, "previous", Path(os.readlink(current)))
        atomic_link(self.root, "current", Path("releases") / self.fallback)
        self._write_status(last_result="fallback_selected")
        return self.status()

    def select_package(self, package_id):
        if package_id in set(read_json(self.root / "disabled.json", [])):
            raise ExperienceError("disabled experience cannot be selected")
        candidates = []
        for release in (self.root / "releases").iterdir():
            manifest = read_json(release / "package.manifest.json") if release.is_dir() else None
            if manifest and manifest.get("id") == package_id:
                candidates.append((manifest.get("sequence", 0), release, manifest))
        if not candidates:
            raise ExperienceError("installed experience is unavailable")
        unused_sequence, release, manifest = max(candidates, key=lambda item: item[0])
        if not self.idle() or not self.allowed_time() or not self.hardware_healthy():
            raise ExperienceError("experience selection is currently unsafe")
        previous = os.readlink(self.root / "current") if (self.root / "current").is_symlink() else None
        state_path = Path("state") / manifest["id"] / "current.json"
        state_before = read_json(self.root / state_path)
        manifest_digest = digest(release / "package.manifest.json")
        atomic_json(self.journal_path, {"phase": "selecting", "previous": previous,
                    "digest": manifest_digest, "state_path": str(state_path),
                    "state_before": state_before})
        try:
            key = self.package_keys.get(manifest["key_id"])
            if key is None:
                raise ExperienceError("installed experience key is no longer trusted")
            verify_signature(release / "package.manifest.json",
                             release / "package.manifest.json.sig", key)
            verify_inventory(release, manifest, ignored={"package.manifest.json",
                                                         "package.manifest.json.sig"})
            self._migrate_state(manifest)
            if previous:
                atomic_link(self.root, "previous", Path(previous))
            atomic_link(self.root, "current", Path("releases") / release.name)
            if not self.activation_healthy():
                raise ExperienceError("selected experience failed health gate")
        except Exception:
            if previous:
                atomic_link(self.root, "current", Path(previous))
            if state_before is None:
                (self.root / state_path).unlink(missing_ok=True)
            else:
                atomic_json(self.root / state_path, state_before)
            self._quarantine(manifest_digest, "selection_failure")
            self.journal_path.unlink(missing_ok=True)
            raise
        self.journal_path.unlink(missing_ok=True)
        self._metric("owner_select")
        self._write_status(last_result="healthy", active=release.name,
                           rating=manifest["rating"])
        return self.status()

    def process_owner_request(self):
        path = self.root / "owner-requests" / "request.json"
        request = read_request(path)
        if request is None:
            return False
        try:
            if not isinstance(request, dict) or set(request) - {"action", "id"}:
                raise ExperienceError("invalid owner request contract")
            action = request.get("action")
            package_id = request.get("id")
            if action in ("enable", "disable", "select"):
                if (not isinstance(package_id, str) or
                        not re.fullmatch(r"[a-z0-9][a-z0-9._-]{0,63}", package_id)):
                    raise ExperienceError("invalid owner package ID")
                if action == "select":
                    self.select_package(package_id)
                else:
                    self.set_enabled(package_id, action == "enable")
            elif action == "fallback" and "id" not in request:
                self.select_fallback()
            else:
                raise ExperienceError("invalid owner action")
            self._metric("owner_" + action)
            return True
        finally:
            path.unlink(missing_ok=True)

    def _activate(self, entry, manifest, signature, value, activate=True):
        if activate and not self.idle():
            self._write_status(last_result="deferred_busy")
            return False
        if activate and not self.allowed_time():
            self._write_status(last_result="deferred_maintenance_window")
            return False
        if activate and not self.hardware_healthy():
            self._write_status(last_result="deferred_unhealthy_hardware")
            return False
        previous = os.readlink(self.root / "current") if (self.root / "current").is_symlink() else None
        state_path = Path("state") / value["id"] / "current.json"
        state_before = read_json(self.root / state_path)
        if activate:
            atomic_json(self.journal_path, {"phase": "activating", "previous": previous,
                        "digest": entry["manifest_sha256"], "state_path": str(state_path),
                        "state_before": state_before})
        try:
            if activate:
                self._migrate_state(value)
            result = install(manifest, signature, self.package_keys, self.root,
                             self.runtime_schema, self.daemon_version, activate)
            if activate and not self.activation_healthy():
                raise ExperienceError("activation health gate failed")
        except Exception as exc:
            if activate and previous and (self.root / previous).is_dir():
                atomic_link(self.root, "current", Path(previous))
            elif activate and (self.root / "current").is_symlink():
                (self.root / "current").unlink()
            if activate and state_before is None:
                (self.root / state_path).unlink(missing_ok=True)
            elif activate:
                atomic_json(self.root / state_path, state_before)
            self._quarantine(entry["manifest_sha256"], type(exc).__name__)
            self._metric("activation_rollback")
            if activate:
                self.journal_path.unlink(missing_ok=True)
            self._write_status(last_result="rolled_back", error=type(exc).__name__)
            raise
        if activate:
            self.journal_path.unlink(missing_ok=True)
            self._metric("activation_healthy")
            self._write_status(last_result="healthy", active=result["identity"], rating=value["rating"])
        else:
            self._metric("install_inactive")
            self._write_status(last_result="installed", installed=result["identity"],
                               rating=value["rating"])
        return True

    def collect_garbage(self):
        releases = self.root / "releases"
        if not releases.exists():
            return []
        protected = {self.fallback}
        for name in ("current", "previous"):
            link = self.root / name
            if link.is_symlink():
                protected.add(Path(os.readlink(link)).name)
        candidates = sorted((p for p in releases.iterdir() if p.is_dir() and p.name not in protected), key=lambda p: p.stat().st_mtime)
        removed = []
        while len(list(releases.iterdir())) > self.max_releases and candidates:
            victim = candidates.pop(0)
            shutil.rmtree(victim)
            removed.append(victim.name)
        return removed

    def check(self):
        self.recover()
        self.process_owner_request()
        with tempfile.TemporaryDirectory(dir=self.root, prefix=".catalog-") as tmp:
            directory = Path(tmp)
            catalog = self._download_catalog(directory)
            self._apply_denylist(catalog)
            installed = read_json(self.root / "sequences.json", {})
            quarantine = read_json(self.root / "quarantine.json", {})
            disabled = set(read_json(self.root / "disabled.json", []))
            entries = [e for e in eligible_packages(catalog, self.device_id, self.groups, installed)
                       if e["manifest_sha256"] not in quarantine and e["id"] not in disabled]
            activated = []
            active_id = self._active_package_id()
            for entry in entries:
                package_dir = directory / entry["id"]
                package_dir.mkdir()
                manifest, signature, value = self._stage_entry(entry, package_dir)
                should_activate = (active_id == entry["id"] or
                                   active_id is None)
                if self._activate(entry, manifest, signature, value, should_activate):
                    activated.append(entry["id"])
                    if should_activate:
                        active_id = entry["id"]
            atomic_json(self.root / "catalog-state.json", {"sequence": catalog["sequence"],
                        "channel": catalog["channel"], "sha256": self.catalog_digest})
        self.collect_garbage()
        self._publish_owner_status()
        return activated


def parse_config(path):
    values = {}
    for raw in path.read_text().splitlines():
        line = raw.strip()
        if line and not line.startswith("#"):
            key, separator, value = line.partition("=")
            if not separator:
                raise ExperienceError("invalid configuration line")
            values[key.strip()] = value.strip()
    return values


def within_window(start, end, now=None):
    now = now or time.localtime()
    current = now.tm_hour * 60 + now.tm_min
    def minutes(value):
        hour, minute = (int(item) for item in value.split(":"))
        if not 0 <= hour <= 23 or not 0 <= minute <= 59:
            raise ExperienceError("invalid maintenance window")
        return hour * 60 + minute
    low, high = minutes(start), minutes(end)
    if low == high:
        return True
    return low <= current < high if low < high else current >= low or current < high


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=("check", "recover", "status", "gc",
                                             "enable", "disable", "fallback"))
    parser.add_argument("--package-id")
    parser.add_argument("--config", type=Path, default=Path("/etc/millennium/experiences.conf"))
    args = parser.parse_args()
    config = parse_config(args.config)
    state_url = config.get("phone_state_url", "http://127.0.0.1:8081/api/state")
    health_url = config.get("health_url", "http://127.0.0.1:8081/api/health")
    def phone_idle():
        ok, value = fetch_json(state_url)
        # State one is the daemon's on-hook idle state. Fail closed if the
        # daemon is unavailable or returns an unexpected contract.
        return ok and value.get("current_state") == 1
    def phone_healthy():
        ok, value = fetch_json(health_url)
        return ok and value.get("overall_status") in ("healthy", "ok")
    agent = ExperienceAgent(Path(config.get("state_dir", "/var/lib/millennium/content")),
                            config["catalog_url"], trusted_keys(config["catalog_keys"].split(",")),
                            trusted_keys(config["package_keys"].split(",")), config["device_id"],
                            config.get("groups", "").split(",") if config.get("groups") else (),
                            int(config.get("runtime_schema", "1")), config.get("daemon_version"),
                            config.get("fallback"), int(config.get("max_releases", "4")),
                            idle=phone_idle, hardware_healthy=phone_healthy,
                            activation_healthy=phone_healthy,
                            allowed_time=lambda: within_window(
                                config.get("install_window_start", "00:00"),
                                config.get("install_window_end", "00:00")))
    if args.command in ("enable", "disable"):
        if not args.package_id:
            parser.error("enable/disable requires --package-id")
        result = agent.set_enabled(args.package_id, args.command == "enable")
    elif args.command == "fallback":
        result = agent.select_fallback()
    else:
        result = getattr(agent, args.command)()
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ExperienceError, InstallError, OSError, ValueError) as exc:
        print("ERROR: %s" % exc)
        raise SystemExit(1)
