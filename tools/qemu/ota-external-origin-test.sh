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

stop_origin() {
    test -z "$origin_pid" || kill "$origin_pid" >/dev/null 2>&1 || true
    test -z "$origin_pid" || wait "$origin_pid" 2>/dev/null || true
    origin_pid=
}

cleanup() {
    stop_origin
    "${GUEST_SSH[@]}" sudo /tmp/millennium-src/tools/qemu/ota-external-origin-guest.sh \
        /tmp/millennium-src restore >/dev/null 2>&1 || true
    rm -rf "$work"
}
trap cleanup EXIT

start_origin() {
    local certificate=${1:-"$work/origin-cert.pem"}
    local key=${2:-"$work/origin-key.pem"}
    shift 2 || true
    python3 "$SCRIPT_DIR/https-origin.py" --directory "$work/origin" \
        --certificate "$certificate" --key "$key" --port 18083 "$@" &
    origin_pid=$!
    for _ in {1..30}; do
        nc -z 127.0.0.1 18083 >/dev/null 2>&1 && return
        kill -0 "$origin_pid" >/dev/null 2>&1 || break
        sleep 0.2
    done
    nc -z 127.0.0.1 18083
}

sync_publish() {
    rm -rf "$work/origin/external"
    "${GUEST_SCP[@]}" -r \
        millennium@127.0.0.1:/var/lib/millennium/qemu-external-publish/external \
        "$work/origin/"
}

assert_links() {
    test "$("${GUEST_SSH[@]}" readlink -f /opt/millennium/current)" = "$current"
    test "$("${GUEST_SSH[@]}" readlink -f /opt/millennium/previous)" = "$previous"
}

assert_rollback_links() {
    local recovered_previous
    test "$("${GUEST_SSH[@]}" readlink -f /opt/millennium/current)" = "$current"
    recovered_previous=$("${GUEST_SSH[@]}" readlink -f /opt/millennium/previous)
    case "$recovered_previous" in
        "$current"|"$previous") ;;
        *) echo "health rollback lost the last known-good release link" >&2; exit 1 ;;
    esac
    "${GUEST_SSH[@]}" test -x "$recovered_previous/host/millennium-daemon"
}

expect_check_failure() {
    if "${GUEST_SSH[@]}" sudo /usr/local/libexec/millennium-ota check \
            >/dev/null 2>&1; then
        echo "update check unexpectedly succeeded during $1" >&2
        exit 1
    fi
    assert_links
}

"$QEMU" status >/dev/null
mkdir -p "$work/origin"
printf '%s\n' '{"overall_status":"WARNING","source":"qemu-external-origin"}' \
    >"$work/origin/health.json"
openssl req -x509 -newkey rsa:2048 -nodes -days 1 \
    -subj /CN=Millennium-QEMU-External-Origin \
    -addext subjectAltName=IP:10.0.2.2 \
    -keyout "$work/origin-key.pem" -out "$work/origin-cert.pem" >/dev/null 2>&1
openssl req -x509 -newkey rsa:2048 -nodes -days 1 \
    -subj /CN=Wrong-QEMU-Origin -addext subjectAltName=IP:192.0.2.1 \
    -keyout "$work/wrong-key.pem" -out "$work/wrong-cert.pem" >/dev/null 2>&1
"${GUEST_SCP[@]}" "$work/origin-cert.pem" millennium@127.0.0.1:/tmp/external-origin.crt
"${GUEST_SSH[@]}" sudo install -m 0644 /tmp/external-origin.crt \
    /usr/local/share/ca-certificates/millennium-qemu-external-origin.crt
"${GUEST_SSH[@]}" sudo update-ca-certificates >/dev/null

identity=$("${GUEST_SSH[@]}" sudo /tmp/millennium-src/tools/qemu/ota-external-origin-guest.sh \
    /tmp/millennium-src prepare https://10.0.2.2:18083/external | tail -n 1)
sync_publish
current=$("${GUEST_SSH[@]}" readlink -f /opt/millennium/current)
previous=$("${GUEST_SSH[@]}" readlink -f /opt/millennium/previous)
"${GUEST_SSH[@]}" sudo systemctl stop millennium-qemu-origin.service

# No listener and an untrusted/wrong-identity TLS endpoint must both fail before
# a manifest can become pending.
expect_check_failure "origin loss before manifest"
start_origin "$work/wrong-cert.pem" "$work/wrong-key.pem"
expect_check_failure "bad TLS certificate and host identity"
stop_origin

bundle_path=$(python3 - "$work/origin/external/stable/manifest.json" <<'PY'
import json, sys
from urllib.parse import urlsplit
print(urlsplit(json.load(open(sys.argv[1]))["bundle"]["url"]).path)
PY
)
start_origin "$work/origin-cert.pem" "$work/origin-key.pem" \
    --cutoff-path "$bundle_path" --cutoff-bytes 1024
"${GUEST_SSH[@]}" sudo /usr/local/libexec/millennium-ota check >/dev/null
if "${GUEST_SSH[@]}" sudo /usr/local/libexec/millennium-ota apply >/dev/null 2>&1; then
    echo "truncated external bundle unexpectedly installed" >&2
    exit 1
fi
assert_links
stop_origin

# A health endpoint failure after activation must roll back both release links.
start_origin "$work/origin-cert.pem" "$work/origin-key.pem" --fail-path /health.json
"${GUEST_SSH[@]}" sudo /usr/local/libexec/millennium-ota clear-failure >/dev/null
"${GUEST_SSH[@]}" sudo /usr/local/libexec/millennium-ota check >/dev/null
if "${GUEST_SSH[@]}" sudo /usr/local/libexec/millennium-ota apply >/dev/null 2>&1; then
    echo "release unexpectedly committed while external health origin failed" >&2
    exit 1
fi
assert_rollback_links
stop_origin

start_origin "$work/origin-cert.pem" "$work/origin-key.pem"
"${GUEST_SSH[@]}" sudo /usr/local/libexec/millennium-ota check >/dev/null
"${GUEST_SSH[@]}" sudo /usr/local/libexec/millennium-ota clear-failure >/dev/null
"${GUEST_SSH[@]}" sudo /tmp/millennium-src/tools/qemu/ota-external-origin-guest.sh \
    /tmp/millennium-src apply https://10.0.2.2:18083/external
"${GUEST_SSH[@]}" sudo /tmp/millennium-src/tools/qemu/ota-external-origin-guest.sh \
    /tmp/millennium-src verify https://10.0.2.2:18083/external "$identity"
current=$("${GUEST_SSH[@]}" readlink -f /opt/millennium/current)
previous=$("${GUEST_SSH[@]}" readlink -f /opt/millennium/previous)

# Signed rollback and withdrawal decisions are enforced even though the
# manifest crossed the independent origin boundary.
stop_origin
"${GUEST_SSH[@]}" sudo /tmp/millennium-src/tools/qemu/ota-external-origin-guest.sh \
    /tmp/millennium-src mutate https://10.0.2.2:18083/external stale
sync_publish
start_origin "$work/origin-cert.pem" "$work/origin-key.pem"
expect_check_failure "stale signed manifest"
stop_origin
"${GUEST_SSH[@]}" sudo /tmp/millennium-src/tools/qemu/ota-external-origin-guest.sh \
    /tmp/millennium-src mutate https://10.0.2.2:18083/external withdrawn
sync_publish
start_origin "$work/origin-cert.pem" "$work/origin-key.pem"
withdrawn=$("${GUEST_SSH[@]}" sudo /usr/local/libexec/millennium-ota check 2>&1)
case "$withdrawn" in
    *'release was withdrawn by its signer'*) ;;
    *) echo "external withdrawn release was not held" >&2; exit 1 ;;
esac
assert_links

# Exercise the same update-check service that Wi-Fi station success and the
# maintenance API start, while the guest-local origin remains disabled.
"${GUEST_SSH[@]}" sudo /tmp/millennium-src/tools/qemu/ota-external-origin-guest.sh \
    /tmp/millennium-src service-recovery https://10.0.2.2:18083/external
"${GUEST_SSH[@]}" curl --fail --silent https://10.0.2.2:18083/health.json >/dev/null
printf 'PASS: external HTTPS OTA enforced transport, TLS, signature, hash, rollback, withdrawal, and service-recovery boundaries\n'
