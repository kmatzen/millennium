#!/bin/bash
set -euo pipefail

SOURCE=${1:-/tmp/millennium-src}
STATE=/var/lib/millennium/ota
ORIGIN=/var/lib/millennium/qemu-origin
BUILD=/var/lib/millennium/qemu-build
KEY=/var/lib/millennium/qemu-ota/signing-key.pem
cd "$SOURCE"

installed=-1
if test -s "$STATE/installed-sequence"; then
    installed=$(<"$STATE/installed-sequence")
fi
sequence=$((installed + 1))
rm -rf "$BUILD"
install -d -m 0700 "$BUILD"
python3 "$SOURCE/tools/build_ota_release.py" \
    --sequence "$sequence" \
    --base-url https://127.0.0.1:18080/lab \
    --daemon "$SOURCE/host/daemon" \
    --flash-script "$SOURCE/tools/qemu/qemu-flash.sh" \
    --private-key "$KEY" --key-id qemu-lab \
    --device-groups qemu --architecture aarch64 \
    --output-dir "$BUILD" >"$BUILD/result.json"

identity=$(printf '%08d-%s' "$sequence" "$(<"$SOURCE/VERSION")")
bundle=$(python3 - "$BUILD/result.json" <<'PY'
import json, pathlib, sys
print(pathlib.Path(json.load(open(sys.argv[1]))["bundle"]).name)
PY
)
install -d -m 0755 "$ORIGIN/lab/stable" "$ORIGIN/lab/releases/$identity"
install -m 0644 "$BUILD/manifest.json" "$ORIGIN/lab/stable/manifest.json"
install -m 0644 "$BUILD/manifest.json.sig" "$ORIGIN/lab/stable/manifest.json.sig"
install -m 0644 "$BUILD/$bundle" "$ORIGIN/lab/releases/$identity/$bundle"
systemctl restart millennium-qemu-origin.service

/usr/local/libexec/millennium-ota check
# Every acceptance run exercises both flash/attestation branches, even when a
# prior lab release used byte-identical firmware.
rm -f "$STATE/firmware/keypad.sha256" "$STATE/firmware/display.sha256"
/usr/local/libexec/millennium-ota apply

test "$(<"$STATE/installed-sequence")" = "$sequence"
test "$(readlink -f /opt/millennium/current)" = "/opt/millennium/releases/$identity"
test "$(readlink -f /opt/millennium/previous)" = "/opt/millennium/releases/bootstrap" || \
    test -x "$(readlink -f /opt/millennium/previous)/host/millennium-daemon"
test -s "$STATE/firmware/keypad.sha256"
test -s "$STATE/firmware/display.sha256"
test -L /var/lib/millennium/content/current
systemctl is-active --quiet daemon.service millennium-qemu-origin.service
python3 - "$STATE/status.json" "$sequence" <<'PY'
import json, sys
status = json.load(open(sys.argv[1]))
assert status["state"] == "committed", status
assert status["sequence"] == int(sys.argv[2]), status
PY
printf 'PASS: signed release %s committed with dual-MCU attestation\n' "$identity"
