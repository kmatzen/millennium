#!/bin/bash
set -euo pipefail

SOURCE=${1:-/tmp/millennium-src}
STATE=/var/lib/millennium/ota
ORIGIN=/var/lib/millennium/qemu-origin/lab/stable
KEY=/var/lib/millennium/qemu-ota/signing-key.pem
WORK=$(mktemp -d)
trap 'systemctl start millennium-qemu-origin.service >/dev/null 2>&1 || true; rm -rf "$WORK"' EXIT
cd "$SOURCE"

# The deterministic suite injects interruption at manifest download, bundle
# download, MCU flash, activation, health-check, recovery, and quarantine.
python3 host/tests/test_ota.py

current_before=$(readlink -f /opt/millennium/current)
previous_before=$(readlink -f /opt/millennium/previous)
cp "$ORIGIN/manifest.json" "$WORK/manifest.json"
cp "$ORIGIN/manifest.json.sig" "$WORK/manifest.json.sig"

systemctl stop millennium-qemu-origin.service
if /usr/local/libexec/millennium-ota check >/dev/null 2>&1; then
    echo "manifest check unexpectedly survived origin loss" >&2; exit 1
fi
systemctl start millennium-qemu-origin.service

printf x >>"$ORIGIN/manifest.json.sig"
if /usr/local/libexec/millennium-ota check >/dev/null 2>&1; then
    echo "corrupt signature unexpectedly verified" >&2; exit 1
fi
cp "$WORK/manifest.json.sig" "$ORIGIN/manifest.json.sig"

# Publish a validly signed next sequence whose bundle is absent. Repeated
# automatic attempts must quarantine it without changing either release link.
python3 - "$WORK/manifest.json" "$ORIGIN/manifest.json" <<'PY'
import json, sys
value = json.load(open(sys.argv[1]))
value["sequence"] += 1
value["bundle"]["url"] = "https://127.0.0.1:18080/lab/releases/missing/bundle.tar.gz"
open(sys.argv[2], "w").write(json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n")
PY
openssl pkeyutl -sign -rawin -inkey "$KEY" -in "$ORIGIN/manifest.json" \
    -out "$ORIGIN/manifest.json.sig"
/usr/local/libexec/millennium-ota check >/dev/null
for attempt in 1 2 3; do
    /usr/local/libexec/millennium-ota auto-apply >/dev/null 2>&1 || true
    # Exponential backoff is 1, 2, then 4 seconds in the lab configuration.
    sleep 5
done
output=$(/usr/local/libexec/millennium-ota auto-apply)
case "$output" in *quarantined*) ;; *) echo "release was not quarantined" >&2; exit 1 ;; esac
test "$(readlink -f /opt/millennium/current)" = "$current_before"
test "$(readlink -f /opt/millennium/previous)" = "$previous_before"

# A signer withdrawal removes the pending candidate without activation.
python3 - "$WORK/manifest.json" "$ORIGIN/manifest.json" <<'PY'
import json, sys
value = json.load(open(sys.argv[1]))
value["sequence"] += 2
value["rollout"]["withdrawn"] = True
open(sys.argv[2], "w").write(json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n")
PY
openssl pkeyutl -sign -rawin -inkey "$KEY" -in "$ORIGIN/manifest.json" \
    -out "$ORIGIN/manifest.json.sig"
withdrawn_output=$(/usr/local/libexec/millennium-ota check)
case "$withdrawn_output" in
    *'release was withdrawn by its signer'*) ;;
    *) echo "withdrawn release was not held" >&2; exit 1 ;;
esac
test ! -e "$STATE/pending/manifest.json"
cp "$WORK/manifest.json" "$ORIGIN/manifest.json"
cp "$WORK/manifest.json.sig" "$ORIGIN/manifest.json.sig"
printf 'PASS: OTA loss, corruption, interruption, quarantine, withdrawal, and link invariants\n'
