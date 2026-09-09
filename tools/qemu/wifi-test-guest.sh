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

# Exercise systemd's real runtime-directory ownership.  The portal runs as the
# unprivileged millennium-wifi user, while bootstrap creates and preserves the
# directory before the privileged helper binds its group-writable socket.  A
# source-level fixture previously bypassed this boundary and missed a physical
# EACCES on every portal request.
systemctl stop millennium-wifi-portal.service millennium-wifi-helper.service \
    millennium-wifi-bootstrap.service >/dev/null 2>&1 || true
rm -rf /run/millennium-wifi
systemctl start millennium-wifi-bootstrap.service
test "$(stat -c %U:%G /run/millennium-wifi)" = root:millennium-wifi
runuser -u millennium-wifi -- test -x /run/millennium-wifi
touch /run/millennium-wifi/setup-active
systemctl start millennium-wifi-helper.service
for _ in {1..30}; do
    test -S /run/millennium-wifi/helper.sock && break
    sleep 0.1
done
test "$(stat -c %U:%G /run/millennium-wifi/helper.sock)" = root:millennium-wifi
runuser -u millennium-wifi -- python3 - <<'PY'
import json
import socket

with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
    connection.connect("/run/millennium-wifi/helper.sock")
    connection.sendall(b'{"action":"status"}\n')
    response = json.loads(connection.makefile("rb").readline())
assert response["ok"] is True, response
assert response["state"] in {"setup", "connecting", "connected", "failed"}, response
PY
systemctl stop millennium-wifi-helper.service

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
