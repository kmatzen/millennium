#!/bin/bash
set -euo pipefail

SOURCE=${1:-/tmp/millennium-src}
ORIGIN=/var/lib/millennium/qemu-origin/experiences
BUILD=/var/lib/millennium/qemu-experience-build
KEY=/var/lib/millennium/qemu-ota/signing-key.pem
STORY=$BUILD/source/story.json
rm -rf "$BUILD" "$ORIGIN"
# Provisioning intentionally preserves /var/lib. Reset only artifacts owned by
# this QEMU test so a reused guest starts from the provisioned 2.1.0 fallback.
rm -rf /var/lib/millennium/content/releases/last-line-2.1.1
rm -f /var/lib/millennium/content/catalog-state.json \
    /var/lib/millennium/content/sequences.json \
    /var/lib/millennium/content/quarantine.json \
    /var/lib/millennium/content/activation-journal.json \
    /var/lib/millennium/content/experience-status.json \
    /var/lib/millennium/content/owner-status.json \
    /var/lib/millennium/content/disabled.json
ln -sfn releases/last-line-2.1.0 /var/lib/millennium/content/current
rm -f /var/lib/millennium/content/previous
install -d -m 0700 "$BUILD/source" "$BUILD/packages"
cp -a "$SOURCE/content/stories/last_line/media" "$BUILD/source/media"
python3 - "$SOURCE/content/stories/last_line/story.json" "$STORY" <<'PY'
import json, pathlib, sys
value = json.load(open(sys.argv[1], encoding="utf-8"))
value["version"] = "2.1.1"
value["distribution"]["sequence"] = 2
pathlib.Path(sys.argv[2]).write_text(json.dumps(value), encoding="utf-8")
PY
python3 "$SOURCE/content/storytool.py" package "$STORY" --output "$BUILD/packages" \
    --private-key "$KEY" --key-id qemu-lab
manifest=$(printf '%s\n' "$BUILD"/packages/*.manifest.json)
python3 "$SOURCE/content/catalogtool.py" build "$manifest" \
    --base-url https://127.0.0.1:18080/experiences/ \
    --channel lab --sequence 1 --key-id qemu-lab --group qemu \
    --private-key "$KEY" --output "$BUILD/packages/catalog.json"
release="$ORIGIN/releases/last-line/00000002-2.1.1"
install -d -m 0755 "$ORIGIN/stable" "$release"
install -m 0644 "$BUILD/packages/catalog.json" "$BUILD/packages/catalog.json.sig" "$ORIGIN/stable/"
install -m 0644 "$manifest" "$manifest.sig" "$BUILD/packages"/*.tar.gz "$release/"
systemctl restart millennium-qemu-origin.service
/usr/local/libexec/millennium-experience check
test "$(readlink -f /var/lib/millennium/content/current)" = \
    "/var/lib/millennium/content/releases/last-line-2.1.1"
test "$(python3 -c 'import json; print(json.load(open("/var/lib/millennium/content/catalog-state.json"))["sequence"])')" = 1
test "$(python3 -c 'import json; print(json.load(open("/var/lib/millennium/content/experience-status.json"))["last_result"])')" = healthy
known_good=$(readlink -f /var/lib/millennium/content/current)

# A compromised channel signature must fail before catalog state or the active
# release can change.
cp "$ORIGIN/stable/catalog.json.sig" "$BUILD/catalog.json.sig.good"
printf 'tamper\n' >> "$ORIGIN/stable/catalog.json.sig"
if /usr/local/libexec/millennium-experience check >/dev/null 2>&1; then
    echo "tampered catalog signature was accepted" >&2
    exit 1
fi
test "$(readlink -f /var/lib/millennium/content/current)" = "$known_good"
test "$(python3 -c 'import json; print(json.load(open("/var/lib/millennium/content/catalog-state.json"))["sequence"])')" = 1
install -m 0644 "$BUILD/catalog.json.sig.good" "$ORIGIN/stable/catalog.json.sig"

# Losing the HTTPS origin must preserve the known-good release and recover
# without cleanup or operator intervention when the service returns.
systemctl stop millennium-qemu-origin.service
if /usr/local/libexec/millennium-experience check >/dev/null 2>&1; then
    echo "catalog check unexpectedly succeeded without transport" >&2
    exit 1
fi
test "$(readlink -f /var/lib/millennium/content/current)" = "$known_good"
systemctl start millennium-qemu-origin.service

# Exercise a real ENOSPC path in an isolated, size-bounded guest filesystem.
# The production worker must leave its seeded fallback readable and selected.
disk_root=/mnt/millennium-experience-enospc
disk_config=/tmp/millennium-experience-enospc.conf
install -d -m 0755 "$disk_root"
mount -t tmpfs -o size=6m tmpfs "$disk_root"
trap 'umount "$disk_root" 2>/dev/null || true; rm -f "$disk_config"' EXIT
install -d -m 0755 "$disk_root/releases"
cp -a "$known_good" "$disk_root/releases/safe-1.0.0"
ln -s releases/safe-1.0.0 "$disk_root/current"
sed -e "s|^state_dir=.*|state_dir=$disk_root|" \
    -e 's|^fallback=.*|fallback=safe-1.0.0|' \
    /etc/millennium/experiences.conf > "$disk_config"
if /usr/local/libexec/millennium-experience check --config "$disk_config" \
        >/dev/null 2>&1; then
    echo "experience install unexpectedly succeeded on bounded filesystem" >&2
    exit 1
fi
test "$(readlink "$disk_root/current")" = releases/safe-1.0.0
test -r "$disk_root/current/story.mst"
umount "$disk_root"
rm -f "$disk_config"
trap - EXIT

# A later, correctly signed catalog can withdraw the candidate digest without
# removing or replacing the already committed known-good release.
manifest_digest=$(sha256sum "$manifest" | awk '{print $1}')
python3 "$SOURCE/content/catalogtool.py" build "$manifest" \
    --base-url https://127.0.0.1:18080/experiences/ \
    --channel lab --sequence 2 --key-id qemu-lab --group qemu \
    --withdraw "$manifest_digest" --private-key "$KEY" \
    --output "$BUILD/packages/catalog-withdrawn.json"
install -m 0644 "$BUILD/packages/catalog-withdrawn.json" "$ORIGIN/stable/catalog.json"
install -m 0644 "$BUILD/packages/catalog-withdrawn.json.sig" "$ORIGIN/stable/catalog.json.sig"
systemctl restart millennium-qemu-origin.service
/usr/local/libexec/millennium-experience check
test "$(readlink -f /var/lib/millennium/content/current)" = "$known_good"
test "$(python3 -c 'import json; print(json.load(open("/var/lib/millennium/content/catalog-state.json"))["sequence"])')" = 2

# Restart the real oneshot worker and prove it remains healthy with the signed
# withdrawn catalog rather than depending on process-local state.
systemctl restart millennium-experience-update.service
test "$(readlink -f /var/lib/millennium/content/current)" = "$known_good"
test "$(python3 -c 'import json; print(json.load(open("/var/lib/millennium/content/experience-status.json"))["last_result"])')" = healthy
# The next acceptance test deliberately removes VM power. Make this completed
# lifecycle the durable baseline rather than accidentally testing dirty cache.
sync
printf 'PASS: signed catalog commit, compromise rejection, network recovery, disk-full fallback, withdrawal, and worker restart\n'
