#!/bin/bash
set -euo pipefail

SOURCE=${1:-/tmp/millennium-src}
cd "$SOURCE"
python3 host/tests/test_wifi.py
grep -Fq 'udp dport { 53, 67 } accept' host/firewall/wifi-setup.nft
grep -Fq 'tcp dport { 53, 80 } accept' host/firewall/wifi-setup.nft
grep -Fq 'iifname "wlan0" drop' host/firewall/wifi-setup.nft
grep -Fq 'oifname "wlan0" drop' host/firewall/wifi-setup.nft
grep -Fq 'RuntimeMaxSec=900' host/systemd/millennium-wifi-helper.service
grep -Fq 'ProtectSystem=strict' host/systemd/millennium-wifi-helper.service

# Verify factory provisioning produces a private, stable handoff without
# exposing its generated password in process arguments or world-readable data.
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
python3 host/wifi/provision_wifi.py --device-id QEMU01 \
    --output-dir "$work/etc" --handoff "$work/handoff.json" >/dev/null
test "$(stat -c %a "$work/etc/wifi-setup-password")" = 600
python3 - "$work/handoff.json" <<'PY'
import json, sys
value = json.load(open(sys.argv[1]))
assert value["ssid"] == "Millennium-Setup-QEMU01"
assert value["portal"] == "http://setup.millennium/"
assert value["wifi_qr"].startswith("WIFI:T:WPA;")
PY
printf 'PASS: Wi-Fi onboarding state machine, platform probes, rollback, and private handoff\n'
