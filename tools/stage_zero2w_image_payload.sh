#!/usr/bin/env bash
set -euo pipefail

repo=$(cd "$(dirname "$0")/.." && pwd)
payload="$repo/host/os_image/payload/rootfs"
host="$repo/host"
source_commit=${MILLENNIUM_SOURCE_COMMIT:-}

test "$(uname -s)" = Linux || { echo "run on the native arm64 image-builder host" >&2; exit 1; }
test "$(uname -m)" = aarch64 || { echo "payload must be built natively for arm64" >&2; exit 1; }
test ! -e "$repo/host/os_image/payload" || {
    echo "refusing to overwrite host/os_image/payload" >&2
    exit 1
}
if [[ -z "$source_commit" ]] && git -C "$repo" rev-parse HEAD >/dev/null 2>&1; then
    source_commit=$(git -C "$repo" rev-parse HEAD)
fi
[[ "$source_commit" =~ ^[0-9a-f]{40}$ ]] || {
    echo "set MILLENNIUM_SOURCE_COMMIT to the exact 40-character source commit" >&2
    exit 1
}

make -C "$host" clean
make -C "$host" daemon GIT_HASH="${source_commit:0:12}"
mkdir -p "$payload/usr/local/bin" "$payload/usr/local/libexec" \
    "$payload/usr/local/share/millennium/audio" "$payload/etc/millennium" \
    "$payload/etc/NetworkManager/dnsmasq-shared.d" \
    "$payload/etc/systemd/system/daemon.service.d" \
    "$payload/etc/systemd/system" "$payload/etc/sudoers.d" \
    "$payload/etc/rpi-image-gen/slot-shared.d" \
    "$payload/usr/lib/sysusers.d" "$payload/usr/lib/tmpfiles.d" "$payload/usr/lib/systemd/system-generators" \
    "$payload/opt/millennium/releases/bootstrap/host" \
    "$payload/opt/millennium/releases/bootstrap/arduino" \
    "$payload/opt/millennium/releases/bootstrap/ota" \
    "$payload/var/lib/millennium/content/releases" "$payload/var/log/millennium"

install -m 0755 "$host/daemon" "$payload/usr/local/bin/millennium-daemon"
install -m 0644 "$host/web_portal.html" "$payload/usr/local/share/millennium/web_portal.html"
install -m 0644 "$host/asoundrc.example" "$payload/etc/asound.conf"
install -m 0644 "$host/daemon.conf.example" "$payload/etc/millennium/daemon.conf"
install -m 0644 "$host/firewall/millennium.nft" "$payload/etc/millennium/millennium.nft"
install -m 0644 "$host/firewall/wifi-setup.nft" "$payload/etc/millennium/wifi-setup.nft"
install -m 0644 "$host/wifi/dnsmasq-shared.conf" \
    "$payload/etc/NetworkManager/dnsmasq-shared.d/millennium.conf"

for source in \
    "$repo/content/install_content.py:millennium-content" \
    "$repo/content/experience_agent.py:millennium-experience" \
    "$repo/content/catalogtool.py:catalogtool.py" \
    "$repo/content/storytool.py:storytool.py" \
    "$host/monitoring/millennium_monitor.py:millennium-monitor" \
    "$host/monitoring/millennium_backup.sh:millennium-backup" \
    "$host/monitoring/millennium_backup_export.sh:millennium-backup-export" \
    "$host/monitoring/millennium_hil_smoke.py:millennium-hil-smoke" \
    "$host/monitoring/millennium_physical_interruption.py:millennium-physical-interruption" \
    "$host/os_ota/millennium_os_ota_agent.py:millennium-os-ota" \
    "$host/ota/millennium_maintenance_tunnel.sh:millennium-maintenance-tunnel" \
    "$host/ota/repair_maintenance_access.py:millennium-repair-maintenance-access" \
    "$host/wifi/millennium_wifi.py:millennium_wifi.py" \
    "$host/wifi/millennium_wifi_bootstrap.py:millennium-wifi-bootstrap" \
    "$host/wifi/millennium_wifi_helper.py:millennium-wifi-helper" \
    "$host/wifi/millennium_wifi_portal.py:millennium-wifi-portal" \
    "$host/wifi/provision_wifi.py:millennium-wifi-provision"; do
    install -m 0755 "${source%%:*}" "$payload/usr/local/libexec/${source##*:}"
done
install -m 0644 "$host/os_ota/millennium_os_ota.py" \
    "$payload/usr/local/libexec/millennium_os_ota.py"

install -m 0644 "$host/systemd/daemon.service" "$payload/etc/systemd/system/daemon.service"
for unit in "$host"/systemd/millennium-{firewall,monitor,hil-smoke,physical-interruption-reconcile,maintenance-tunnel,update-check,update-apply,update-auto-apply,update-recover,wifi-bootstrap,wifi-helper,wifi-portal,wifi-recovery}.{service,timer,path}; do
    test -f "$unit" || continue
    install -m 0644 "$unit" "$payload/etc/systemd/system/"
done
for unit in "$host"/systemd/millennium-experience-{update,recover}.{service,timer}; do
    test -f "$unit" || continue
    install -m 0644 "$unit" "$payload/etc/systemd/system/"
done
for unit in "$host"/systemd/millennium-os-update-{check,apply,recover,boot-health}.{service,timer}; do
    test -f "$unit" || continue
    install -m 0644 "$unit" "$payload/etc/systemd/system/"
done
install -m 0644 "$host/systemd/20-ota-release.conf" \
    "$payload/etc/systemd/system/daemon.service.d/20-ota-release.conf"
printf '[Service]\nUser=millennium\nGroup=millennium\n' \
    >"$payload/etc/systemd/system/daemon.service.d/30-user.conf"
install -m 0644 "$host/systemd/millennium-wifi.sysusers" \
    "$payload/usr/lib/sysusers.d/millennium-wifi.conf"
install -m 0644 "$host/systemd/millennium-experience.tmpfiles" \
    "$payload/usr/lib/tmpfiles.d/millennium-experience.conf"
install -m 0644 "$host/systemd/millennium-monitor.tmpfiles" \
    "$payload/usr/lib/tmpfiles.d/millennium-monitor.conf"
install -m 0755 "$repo/host/os_image/slot-shared-generator" \
    "$payload/usr/lib/systemd/system-generators/slot-shared-generator"
install -m 0440 "$host/systemd/millennium-ota-sudoers" \
    "$payload/etc/sudoers.d/millennium-ota"
install -m 0644 "$host/ota/ota.conf.example" "$payload/etc/millennium/ota.conf"
install -m 0600 "$host/experiences.conf.example" "$payload/etc/millennium/experiences.conf"
install -m 0644 "$host/os_ota/os-ota.conf.example" \
    "$payload/etc/millennium/os-ota.conf"

install -m 0755 "$host/daemon" \
    "$payload/opt/millennium/releases/bootstrap/host/millennium-daemon"
install -m 0644 "$host/web_portal.html" \
    "$payload/opt/millennium/releases/bootstrap/host/web_portal.html"
install -m 0755 "$repo/Arduino/pi_flash.sh" \
    "$payload/opt/millennium/releases/bootstrap/arduino/pi_flash.sh"
install -m 0644 "$repo/Arduino/build/keypad/keypad.ino.hex" \
    "$payload/opt/millennium/releases/bootstrap/arduino/keypad.hex"
install -m 0644 "$repo/Arduino/build/display/display.ino.hex" \
    "$payload/opt/millennium/releases/bootstrap/arduino/display.hex"
install -m 0755 "$host/ota/millennium_ota.py" \
    "$payload/opt/millennium/releases/bootstrap/ota/millennium-ota"

python3 "$repo/tools/write_bootstrap_release.py" \
    "$payload/opt/millennium/releases/bootstrap/arduino/keypad.hex" \
    "$payload/opt/millennium/releases/bootstrap/arduino/display.hex" \
    "$(<"$repo/VERSION")" "$source_commit" \
    "$payload/opt/millennium/releases/bootstrap/release.json"
ln -s releases/bootstrap "$payload/opt/millennium/current"
ln -s /opt/millennium/current/ota/millennium-ota \
    "$payload/usr/local/libexec/millennium-ota"

content_id=$(python3 -c 'import json,sys; v=json.load(open(sys.argv[1])); print(v["id"]+"-"+v["version"])' \
    "$repo/content/stories/last_line/story.json")
content="$payload/var/lib/millennium/content/releases/$content_id"
mkdir -p "$content/media"
install -m 0640 "$repo/content/stories/last_line/story.json" "$content/story.json"
install -m 0640 "$repo/content/stories/last_line/story.mst" "$content/story.mst"
install -m 0640 "$repo/content/stories/last_line/media/"*.wav "$content/media/"
ln -s "releases/$content_id" "$payload/var/lib/millennium/content/current"

cat >"$payload/etc/rpi-image-gen/slot-shared.d/millennium.conf" <<'EOF'
Version=1
Path=/etc/millennium
Path=/etc/ssh
Path=/etc/NetworkManager/system-connections
Path=/var/lib/millennium
Path=/var/log/millennium
EOF

find "$payload" -type d -exec chmod 0755 {} +
chmod 0700 "$payload/etc/NetworkManager/system-connections" 2>/dev/null || true
printf 'staged production payload: %s\n' "$payload"
