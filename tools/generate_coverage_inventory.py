#!/usr/bin/env python3
"""Generate the item-level production surface inventory used by coverage CI."""
import argparse
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "acceptance/inventory.json"


def text(relative):
    return (ROOT / relative).read_text(encoding="utf-8")


def owner(category, name, source):
    value = (name + " " + source).lower()
    if category == "recovery-tool" and any(
            word in value for word in ("write_recovery", "as_built", "handoff")):
        return "factory-seed-recovery-media-and-key-custody"
    if category in {"mcu-message"}:
        return "serial-reconnect-and-role-routing"
    if category in {"experience-event", "plugin"}:
        return "story-branches-persistence-and-accessibility"
    if "wifi" in value or category == "wifi-route":
        return "wifi-onboarding-recovery-and-privacy"
    if "os-update" in value or "os_ota" in value:
        return "ab-operating-system-ota"
    if "maintenance" in value:
        return "reverse-maintenance-tunnel"
    if any(word in value for word in ("experience", "content", "catalog", "storytool")):
        return "signed-experience-install-rollback"
    if any(word in value for word in ("backup", "monitor", "hil-smoke")):
        return "monitoring-alerting-and-backup"
    if any(word in value for word in ("ota", "update")) or category == "updater-state":
        return "signed-host-and-mcu-ota"
    if category == "api-route" or "firewall" in value:
        return "admin-api-auth-csrf-and-firewall"
    if "physical-interruption" in value or "recover" in value:
        return "power-network-and-update-interruptions"
    return "daemon-core"


def add(items, category, name, source):
    identity = f"{category}:{name}"
    items[identity] = {"id": identity, "category": category, "name": name,
                       "source": source, "function": owner(category, name, source)}


def quoted_choices(source):
    result = set()
    for match in re.finditer(r"choices=\((.*?)\)", source, re.DOTALL):
        result.update(re.findall(r'["\']([a-z][a-z0-9-]*)["\']', match.group(1)))
    result.update(re.findall(r'add_parser\(["\']([a-z][a-z0-9-]*)["\']', source))
    return result


def generate():
    items = {}
    stage = text("tools/stage_zero2w_image_payload.sh")
    add(items, "production-executable", "millennium-daemon",
        "tools/stage_zero2w_image_payload.sh")
    for match in re.finditer(r'"\$(repo|host)/([^":]+):([^"/]+)"', stage):
        source = match.group(2) if match.group(1) == "repo" else "host/" + match.group(2)
        add(items, "production-executable", match.group(3), source)

    for path in sorted((ROOT / "host/systemd").glob("*")):
        if path.suffix in {".service", ".timer", ".path"}:
            add(items, "systemd-unit", path.name, path.relative_to(ROOT).as_posix())

    web = text("host/web_server.c")
    for method, route in re.findall(
            r'web_server_add_route\(server, "([A-Z]+)", "([^"]+)"', web):
        add(items, "api-route", f"{method} {route}", "host/web_server.c")

    portal = text("host/wifi/millennium_wifi_portal.py")
    for route in sorted(set(re.findall(r'path\s*(?:==|!=)\s*"(/[^"]+)"', portal))):
        add(items, "wifi-route", route, "host/wifi/millennium_wifi_portal.py")

    command_sources = [
        "host/ota/millennium_ota.py", "host/os_ota/millennium_os_ota_agent.py",
        "content/install_content.py", "content/experience_agent.py",
        "content/catalogtool.py", "content/storytool.py",
        "content/playtest_record.py", "tools/handoff_acceptance.py",
        "tools/as_built_record.py", "tools/write_recovery_media.py",
    ]
    for relative in command_sources:
        for command in sorted(quoted_choices(text(relative))):
            add(items, "cli-action", f"{Path(relative).stem}:{command}", relative)
    for action in ("provision", "run"):
        add(items, "maintenance-action", action,
            "host/ota/millennium_maintenance_tunnel.sh")

    for relative in (
            "host/ota/repair_maintenance_access.py",
            "host/monitoring/millennium_physical_interruption.py",
            "tools/write_recovery_media.py", "tools/as_built_record.py",
            "tools/handoff_acceptance.py"):
        add(items, "recovery-tool", Path(relative).name, relative)

    for relative in ("host/ota/millennium_ota.py",
                     "host/os_ota/millennium_os_ota_agent.py"):
        source = text(relative)
        states = set(re.findall(
            r'write_status\(state_dir,\s*["\']([a-z][a-z0-9-]+)["\']', source))
        states.update(re.findall(r'["\'](?:phase|state)["\']:\s*'
                                 r'["\']([a-z][a-z0-9-]+)["\']', source))
        for state in sorted(states):
            add(items, "updater-state", f"{Path(relative).stem}:{state}", relative)

    for path in sorted((ROOT / "host/plugins").glob("*.c")):
        add(items, "plugin", path.stem, path.relative_to(ROOT).as_posix())

    story = text("content/storytool.py")
    event_match = re.search(r'EVENT_RE\s*=\s*re\.compile\(r"\^\((.*?)\)\$"\)', story)
    if event_match:
        for event in event_match.group(1).split("|"):
            add(items, "experience-event", event, "content/storytool.py")

    protocol = text("host/mcu_protocol.h")
    for message in re.findall(r'\b(MCU_(?:MSG|CMD)_[A-Z0-9_]+)\s*=', protocol):
        add(items, "mcu-message", message, "host/mcu_protocol.h")

    return {"schema": 1, "generator": "tools/generate_coverage_inventory.py",
            "items": sorted(items.values(), key=lambda value: value["id"])}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--output", type=Path, default=OUTPUT)
    args = parser.parse_args()
    encoded = json.dumps(generate(), indent=2, sort_keys=True) + "\n"
    if args.check:
        if not args.output.is_file() or args.output.read_text(encoding="utf-8") != encoded:
            raise SystemExit("coverage inventory is stale; run tools/generate_coverage_inventory.py")
        print(f"VALID: generated coverage inventory matches {args.output}")
        return
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(encoded, encoding="utf-8")
    print(args.output)


if __name__ == "__main__":
    main()
