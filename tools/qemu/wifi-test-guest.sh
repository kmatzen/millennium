#!/bin/bash
set -euo pipefail

SOURCE=${1:-/tmp/millennium-src}
cd "$SOURCE"
python3 host/tests/test_wifi.py
grep -Fq 'ct state established,related accept' host/firewall/wifi-setup.nft
grep -Fq 'udp dport { 53, 67 } accept' host/firewall/wifi-setup.nft
grep -Fq 'tcp dport { 53, 80 } accept' host/firewall/wifi-setup.nft
grep -Fq 'iifname "wlan0" drop' host/firewall/wifi-setup.nft
grep -Fq 'oifname "wlan0" drop' host/firewall/wifi-setup.nft
grep -Fq 'RuntimeMaxSec=900' host/systemd/millennium-wifi-helper.service
grep -Fq 'ProtectSystem=strict' host/systemd/millennium-wifi-helper.service

# Exercise the production nftables rules across a real packet boundary. The
# device must be able to receive replies to its own connectivity probe after
# wlan0 changes from AP to station mode, while a setup client must still be
# unable to initiate a connection to an arbitrary device service.
firewall_work=$(mktemp -d)
firewall_cleanup() {
    kill "${device_server_pid:-}" "${peer_server_pid:-}" >/dev/null 2>&1 || true
    nft delete table inet millennium_wifi_setup >/dev/null 2>&1 || true
    ip netns delete millennium-wifi-peer >/dev/null 2>&1 || true
    ip link delete wlan0 >/dev/null 2>&1 || true
    rm -rf "$firewall_work"
}
trap firewall_cleanup EXIT
ip netns add millennium-wifi-peer
ip link add wlan0 type veth peer name wifi-peer
ip link set wifi-peer netns millennium-wifi-peer
ip address add 10.77.0.1/24 dev wlan0
ip link set wlan0 up
ip -n millennium-wifi-peer address add 10.77.0.2/24 dev wifi-peer
ip -n millennium-wifi-peer link set lo up
ip -n millennium-wifi-peer link set wifi-peer up
ip netns exec millennium-wifi-peer python3 -m http.server 18080 \
    --bind 10.77.0.2 --directory "$firewall_work" >/dev/null 2>&1 &
peer_server_pid=$!
python3 -m http.server 18081 --bind 10.77.0.1 \
    --directory "$firewall_work" >/dev/null 2>&1 &
device_server_pid=$!
for _ in {1..30}; do
    curl --fail --silent --max-time 1 http://10.77.0.2:18080/ >/dev/null 2>&1 && break
    sleep 0.1
done
curl --fail --silent --max-time 2 http://10.77.0.2:18080/ >/dev/null
nft -f host/firewall/wifi-setup.nft
curl --fail --silent --max-time 2 http://10.77.0.2:18080/ >/dev/null
if ip netns exec millennium-wifi-peer curl --fail --silent --max-time 2 \
        http://10.77.0.1:18081/ >/dev/null 2>&1; then
    echo 'setup client unexpectedly reached a device service' >&2
    exit 1
fi
nft delete table inet millennium_wifi_setup
ip netns delete millennium-wifi-peer
ip link delete wlan0
kill "$device_server_pid" "$peer_server_pid" >/dev/null 2>&1 || true
trap - EXIT
rm -rf "$firewall_work"

# Exercise systemd's real runtime-directory ownership.  The portal runs as the
# unprivileged millennium-wifi user, while bootstrap creates and preserves the
# directory before the privileged helper binds its group-writable socket.  A
# source-level fixture previously bypassed this boundary and missed a physical
# EACCES on every portal request.
systemctl stop millennium-wifi-portal.service millennium-wifi-helper.service \
    millennium-wifi-bootstrap.service >/dev/null 2>&1 || true
rm -rf /run/millennium-wifi
# QEMU virt has no WLAN device. Replace only the radio/AP action while keeping
# the production bootstrap unit's real Group and RuntimeDirectory directives.
# The bootstrap state machine itself is covered above; this block specifically
# tests the deployed systemd identity and helper socket boundary.
mkdir -p /etc/systemd/system/millennium-wifi-bootstrap.service.d
cat >/etc/systemd/system/millennium-wifi-bootstrap.service.d/qemu-radio.conf <<'EOF'
[Service]
ExecStart=
ExecStart=/bin/true
EOF
systemctl daemon-reload
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
