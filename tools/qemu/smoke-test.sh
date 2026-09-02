#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
QEMU="$SCRIPT_DIR/qemu.sh"

"$QEMU" status >/dev/null
for _ in {1..30}; do
    "$QEMU" ssh systemctl is-active --quiet daemon.service && break
    sleep 2
done
"$QEMU" ssh systemctl is-active --quiet daemon.service
"$QEMU" ssh test -c /dev/vport0p1
health=
for _ in {1..45}; do
    health=$("$QEMU" ssh curl --silent http://127.0.0.1:8081/api/health || true)
    printf '%s' "$health" | grep -q '"serial_connection":{"status":"HEALTHY"' && break
    sleep 2
done
printf '%s' "$health" | grep -q '"serial_connection":{"status":"HEALTHY"'
test -n "$("$QEMU" ssh curl --fail --silent http://127.0.0.1:8080/metrics)"
"$QEMU" hook up >/dev/null
"$QEMU" key 1 >/dev/null
"$QEMU" key 2 >/dev/null
sleep 1
"$QEMU" display | grep -q '"display"'
printf 'PASS: VM, systemd daemon, virtual MCU, admin API, metrics, and input/display loop\n'
