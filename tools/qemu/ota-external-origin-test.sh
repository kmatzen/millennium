#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
QEMU="$SCRIPT_DIR/qemu.sh"
STATE_DIR=${MILLENNIUM_QEMU_STATE:-"$SCRIPT_DIR/state"}
SSH_PORT=${MILLENNIUM_QEMU_SSH_PORT:-2222}
SSH_KEY="$STATE_DIR/id_ed25519"
GUEST_SSH=(ssh -i "$SSH_KEY" -p "$SSH_PORT" -o BatchMode=yes \
    -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR \
    millennium@127.0.0.1)
GUEST_SCP=(scp -i "$SSH_KEY" -P "$SSH_PORT" -o BatchMode=yes \
    -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR)
work=$(mktemp -d)
origin_pid=
cleanup() {
    test -z "$origin_pid" || kill "$origin_pid" >/dev/null 2>&1 || true
    "${GUEST_SSH[@]}" sudo /tmp/millennium-src/tools/qemu/ota-external-origin-guest.sh \
        /tmp/millennium-src restore >/dev/null 2>&1 || true
    rm -rf "$work"
}
trap cleanup EXIT

"$QEMU" status >/dev/null
mkdir -p "$work/origin"
openssl req -x509 -newkey rsa:2048 -nodes -days 1 \
    -subj /CN=Millennium-QEMU-External-Origin \
    -addext subjectAltName=IP:10.0.2.2 \
    -keyout "$work/origin-key.pem" -out "$work/origin-cert.pem" >/dev/null 2>&1
"${GUEST_SCP[@]}" "$work/origin-cert.pem" millennium@127.0.0.1:/tmp/external-origin.crt
"${GUEST_SSH[@]}" sudo install -m 0644 /tmp/external-origin.crt \
    /usr/local/share/ca-certificates/millennium-qemu-external-origin.crt
"${GUEST_SSH[@]}" sudo update-ca-certificates >/dev/null

identity=$("${GUEST_SSH[@]}" sudo /tmp/millennium-src/tools/qemu/ota-external-origin-guest.sh \
    /tmp/millennium-src prepare https://10.0.2.2:18083/external | tail -n 1)
"${GUEST_SCP[@]}" -r millennium@127.0.0.1:/var/lib/millennium/qemu-external-publish/. \
    "$work/origin/"
printf '%s\n' '{"overall_status":"WARNING","source":"qemu-external-origin"}' \
    >"$work/origin/health.json"
python3 "$SCRIPT_DIR/https-origin.py" --directory "$work/origin" \
    --certificate "$work/origin-cert.pem" --key "$work/origin-key.pem" \
    --port 18083 &
origin_pid=$!
for _ in {1..30}; do
    curl --insecure --fail --silent https://127.0.0.1:18083/health.json >/dev/null 2>&1 && break
    sleep 0.2
done
curl --insecure --fail --silent https://127.0.0.1:18083/health.json >/dev/null

"${GUEST_SSH[@]}" sudo systemctl stop millennium-qemu-origin.service
"${GUEST_SSH[@]}" sudo /tmp/millennium-src/tools/qemu/ota-external-origin-guest.sh \
    /tmp/millennium-src apply https://10.0.2.2:18083/external
"${GUEST_SSH[@]}" sudo /tmp/millennium-src/tools/qemu/ota-external-origin-guest.sh \
    /tmp/millennium-src verify https://10.0.2.2:18083/external "$identity"
current=$("${GUEST_SSH[@]}" readlink -f /opt/millennium/current)
previous=$("${GUEST_SSH[@]}" readlink -f /opt/millennium/previous)
kill "$origin_pid"
wait "$origin_pid" 2>/dev/null || true
origin_pid=
if "${GUEST_SSH[@]}" sudo /usr/local/libexec/millennium-ota check >/dev/null 2>&1; then
    echo "update check unexpectedly succeeded while external origin was down" >&2
    exit 1
fi
test "$("${GUEST_SSH[@]}" readlink -f /opt/millennium/current)" = "$current"
test "$("${GUEST_SSH[@]}" readlink -f /opt/millennium/previous)" = "$previous"
python3 "$SCRIPT_DIR/https-origin.py" --directory "$work/origin" \
    --certificate "$work/origin-cert.pem" --key "$work/origin-key.pem" \
    --port 18083 &
origin_pid=$!
for _ in {1..30}; do
    "${GUEST_SSH[@]}" curl --fail --silent https://10.0.2.2:18083/health.json >/dev/null 2>&1 && break
    sleep 0.2
done
"${GUEST_SSH[@]}" curl --fail --silent https://10.0.2.2:18083/health.json >/dev/null
printf 'PASS: production updater committed through an external HTTPS origin and preserved links across outage/recovery\n'
