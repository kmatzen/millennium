#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
QEMU="$SCRIPT_DIR/qemu.sh"
work=$(mktemp -d)
cleanup() {
    "$QEMU" ssh sudo systemctl stop wifi-external-fixture.service >/dev/null 2>&1 || true
    "$QEMU" ssh sudo pkill -f '[w]ifi-external-fixture.py' >/dev/null 2>&1 || true
    "$QEMU" ssh sudo ip link delete wifi-e2e >/dev/null 2>&1 || true
    rm -rf "$work"
}
trap cleanup EXIT

"$QEMU" status >/dev/null
"$QEMU" ssh sudo systemctl stop wifi-external-fixture.service >/dev/null 2>&1 || true
"$QEMU" ssh sudo pkill -f '[w]ifi-external-fixture.py' >/dev/null 2>&1 || true
"$QEMU" ssh sudo ip link delete wifi-e2e >/dev/null 2>&1 || true
"$QEMU" ssh sudo rm -rf /run/millennium-wifi-external
"$QEMU" ssh sudo ip link add wifi-e2e type dummy
"$QEMU" ssh sudo ip address add 10.42.0.1/24 dev wifi-e2e
"$QEMU" ssh sudo ip link set wifi-e2e up
"$QEMU" ssh sudo systemd-run --quiet --collect --unit=wifi-external-fixture \
    --property=WorkingDirectory=/tmp/millennium-src \
    /usr/bin/python3 /tmp/millennium-src/tools/qemu/wifi-external-fixture.py
for _ in {1..30}; do
    "$QEMU" ssh sudo test -f /run/millennium-wifi-external/ready && break
    sleep 1
done
"$QEMU" ssh sudo test -f /run/millennium-wifi-external/ready

base=http://127.0.0.1:18082
curl --fail --silent --show-error -H 'Host: setup.millennium' \
    -A 'Mozilla/5.0 (Linux; Android 14)' -c "$work/cookies" \
    "$base/generate_204" -o /dev/null
curl --fail --silent --show-error -H 'Host: setup.millennium' \
    -A 'Mozilla/5.0 (Linux; Android 14)' -b "$work/cookies" \
    "$base/acceptance.json?platform=android" >"$work/acceptance.json"
python3 - "$work/acceptance.json" <<'PY'
import json, sys
value = json.load(open(sys.argv[1]))
assert value["passed"] is True
assert value["platform"] == "android"
assert value["captive_probe_path"] == "/generate_204"
PY

curl --fail --silent --show-error -H 'Host: setup.millennium' \
    -b "$work/cookies" -c "$work/cookies" "$base/" >"$work/page.html"
csrf=$(sed -n 's/.*name=csrf value="\([^"]*\)".*/\1/p' "$work/page.html")
test -n "$csrf"
curl --fail --silent --show-error -H 'Host: setup.millennium' \
    -b "$work/cookies" -X POST \
    --data-urlencode "csrf=$csrf" --data-urlencode 'ssid=Outside QEMU' \
    --data-urlencode 'security=wpa-psk' --data-urlencode 'passphrase=test-password-42' \
    --data-urlencode 'hidden=0' "$base/connect" | grep -q 'Connecting'
"$QEMU" ssh sudo cat /run/millennium-wifi-external/connect.json >"$work/connect.json"
python3 - "$work/connect.json" <<'PY'
import json, sys
value = json.load(open(sys.argv[1]))
assert value["network"]["ssid"] == "Outside QEMU"
assert value["network"]["passphrase"] == "test-password-42"
PY
printf 'PASS: independent host client traversed QEMU boundary, portal probe, session, CSRF, and credential handoff\n'
