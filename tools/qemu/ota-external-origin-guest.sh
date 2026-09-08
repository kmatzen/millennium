#!/bin/bash
set -euo pipefail

SOURCE=${1:-/tmp/millennium-src}
ACTION=${2:-}
BASE_URL=${3:-https://10.0.2.2:18083/external}
STATE=/var/lib/millennium/ota
BUILD=/var/lib/millennium/qemu-external-build
PUBLISH=/var/lib/millennium/qemu-external-publish
KEY=/var/lib/millennium/qemu-ota/signing-key.pem
CONFIG=/etc/millennium/ota.conf
BACKUP=/etc/millennium/ota.conf.external-test-backup
cd "$SOURCE"

case "$ACTION" in
prepare)
    installed=-1
    test ! -s "$STATE/installed-sequence" || installed=$(<"$STATE/installed-sequence")
    sequence=$((installed + 1))
    rm -rf "$BUILD" "$PUBLISH"
    install -d -m 0700 "$BUILD"
    python3 "$SOURCE/tools/build_ota_release.py" \
        --source-commit "$(<"$SOURCE/.millennium-source-commit")" \
        --sequence "$sequence" --base-url "$BASE_URL" \
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
    install -d -m 0755 "$PUBLISH/external/stable" "$PUBLISH/external/releases/$identity"
    install -m 0644 "$BUILD/manifest.json" "$PUBLISH/external/stable/manifest.json"
    install -m 0644 "$BUILD/manifest.json.sig" "$PUBLISH/external/stable/manifest.json.sig"
    install -m 0644 "$BUILD/$bundle" "$PUBLISH/external/releases/$identity/$bundle"
    test -e "$BACKUP" || cp "$CONFIG" "$BACKUP"
    python3 - "$CONFIG" "$BASE_URL" <<'PY'
import pathlib, sys
path = pathlib.Path(sys.argv[1])
base = sys.argv[2]
lines = []
for line in path.read_text().splitlines():
    if line.startswith("manifest_url="):
        line = "manifest_url=" + base + "/stable/manifest.json"
    elif line.startswith("health_url="):
        line = "health_url=" + base.rsplit("/external", 1)[0] + "/health.json"
    lines.append(line)
path.write_text("\n".join(lines) + "\n")
PY
    printf '%s\n' "$identity"
    ;;
apply)
    /usr/local/libexec/millennium-ota check
    rm -f "$STATE/firmware/keypad.sha256" "$STATE/firmware/display.sha256"
    /usr/local/libexec/millennium-ota apply
    ;;
verify)
    identity=${4:?identity required}
    test "$(readlink -f /opt/millennium/current)" = "/opt/millennium/releases/$identity"
    test -s "$STATE/firmware/keypad.sha256"
    test -s "$STATE/firmware/display.sha256"
    python3 - "$STATE/status.json" <<'PY'
import json, sys
value = json.load(open(sys.argv[1]))
assert value["state"] == "committed", value
PY
    ;;
restore)
    if test -e "$BACKUP"; then
        mv "$BACKUP" "$CONFIG"
    fi
    ;;
*)
    echo "usage: $0 SOURCE {prepare|apply|verify|restore} [BASE_URL] [IDENTITY]" >&2
    exit 2
    ;;
esac
