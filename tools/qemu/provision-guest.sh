#!/bin/sh
set -eu

SOURCE=${1:-/tmp/millennium-src}
SOURCE_COMMIT=$(cat "$SOURCE/.millennium-source-commit")
case "$SOURCE_COMMIT" in
    *[!0-9a-f]*|'')
        printf 'invalid QEMU source commit identity\n' >&2
        exit 1
        ;;
esac
test "${#SOURCE_COMMIT}" -eq 40 || {
    printf 'invalid QEMU source commit identity\n' >&2
    exit 1
}
SOURCE_COMMIT_SHORT=$(printf '%s' "$SOURCE_COMMIT" | cut -c1-12)
if ! PKG_CONFIG_PATH=/opt/pjproject/lib/pkgconfig pkg-config --exists libpjproject; then
    rm -rf /tmp/pjproject
    git clone --quiet --filter=blob:none https://github.com/pjsip/pjproject.git /tmp/pjproject
    git -C /tmp/pjproject checkout --quiet --detach 2f4bc29b2fa65cc29e50ba03f0b8b6de820eaf6b
    test "$(git -C /tmp/pjproject rev-parse HEAD)" = 2f4bc29b2fa65cc29e50ba03f0b8b6de820eaf6b
    cd /tmp/pjproject
    ./configure --prefix=/opt/pjproject --disable-video --disable-sdl \
        --disable-ffmpeg --disable-v4l2 --disable-openh264
    make dep
    make -j2
    make install
fi
export PKG_CONFIG_PATH=/opt/pjproject/lib/pkgconfig
cd "$SOURCE/host"
make clean
make daemon GIT_HASH="$SOURCE_COMMIT_SHORT"

install -d -m 0755 /etc/millennium /var/lib/millennium /var/log/millennium \
    /usr/local/libexec
install -m 0755 daemon /usr/local/bin/millennium-daemon
install -m 0644 systemd/daemon.service /etc/systemd/system/daemon.service
install -m 0755 ota/millennium_maintenance_tunnel.sh \
    /usr/local/libexec/millennium-maintenance-tunnel
install -m 0644 systemd/millennium-maintenance-tunnel.service \
    /etc/systemd/system/millennium-maintenance-tunnel.service
# Install the production Wi-Fi service boundary.  The guest has no emulated
# WLAN, but must still exercise systemd's real users, groups, runtime-directory
# ownership and the privileged broker socket instead of a permissive fixture.
install -m 0644 systemd/millennium-wifi.sysusers \
    /usr/lib/sysusers.d/millennium-wifi.conf
systemd-sysusers /usr/lib/sysusers.d/millennium-wifi.conf
install -m 0755 wifi/millennium_wifi.py /usr/local/libexec/millennium_wifi.py
install -m 0755 wifi/millennium_wifi_bootstrap.py \
    /usr/local/libexec/millennium-wifi-bootstrap
install -m 0755 wifi/millennium_wifi_helper.py \
    /usr/local/libexec/millennium-wifi-helper
install -m 0755 wifi/millennium_wifi_portal.py \
    /usr/local/libexec/millennium-wifi-portal
install -m 0644 systemd/millennium-wifi-bootstrap.service \
    systemd/millennium-wifi-helper.service \
    systemd/millennium-wifi-portal.service /etc/systemd/system/
install -m 0644 "$SOURCE/tools/qemu/daemon.conf" /etc/millennium/daemon.conf
cat >/etc/udev/rules.d/99-millennium-qemu.rules <<'EOF'
KERNEL=="vport*", GROUP="dialout", MODE="0660"
EOF
udevadm control --reload-rules
MCU_PORT=/dev/virtio-ports/millennium.mcu
udevadm trigger --name-match="$MCU_PORT" || true
chgrp dialout "$MCU_PORT"
chmod 0660 "$MCU_PORT"
if [ ! -s /etc/millennium/admin-token ]; then
    umask 077
    od -An -N32 -tx1 /dev/urandom | tr -d ' \n' > /etc/millennium/admin-token
fi
chown millennium:millennium /etc/millennium/admin-token
chmod 0600 /etc/millennium/admin-token
install -d -o root -g root -m 0755 /var/lib/millennium/content
install -d -o millennium -g millennium -m 0750 /var/lib/millennium/content/owner-requests
install -m 0755 "$SOURCE/content/experience_agent.py" /usr/local/libexec/millennium-experience
install -m 0644 "$SOURCE/content/install_content.py" /usr/local/libexec/install_content.py
install -m 0644 "$SOURCE/content/catalogtool.py" /usr/local/libexec/catalogtool.py
install -m 0644 "$SOURCE/content/storytool.py" /usr/local/libexec/storytool.py
install -m 0755 "$SOURCE/tools/qemu/experience-power-prepare-guest.py" \
    /usr/local/libexec/millennium-experience-power-prepare
install -m 0755 "$SOURCE/tools/qemu/experience-power-verify-guest.py" \
    /usr/local/libexec/millennium-experience-power-verify
install -m 0644 "$SOURCE/host/systemd/millennium-experience-update.service" \
    "$SOURCE/host/systemd/millennium-experience-update.timer" \
    "$SOURCE/host/systemd/millennium-experience-recover.service" \
    /etc/systemd/system/
CONTENT_ID=$(python3 - "$SOURCE/content/stories/last_line/story.json" <<'PY'
import json, sys
value = json.load(open(sys.argv[1], encoding="utf-8"))
print("%s-%s" % (value["id"], value["version"]))
PY
)
CONTENT_RELEASE="/var/lib/millennium/content/releases/$CONTENT_ID"
install -d -o millennium -g millennium -m 0750 "$CONTENT_RELEASE/media"
python3 "$SOURCE/content/storytool.py" compile \
    "$SOURCE/content/stories/last_line/story.json" --output "$CONTENT_RELEASE/story.mst"
install -m 0640 -o millennium -g millennium \
    "$SOURCE/content/stories/last_line/story.json" "$CONTENT_RELEASE/story.json"
find "$SOURCE/content/stories/last_line/media" -type f -name '*.wav' -exec \
    install -m 0640 -o millennium -g millennium {} "$CONTENT_RELEASE/media/" \;
ln -sfn "releases/$CONTENT_ID" /var/lib/millennium/content/current
chown -h millennium:millennium /var/lib/millennium/content/current

# Install the production updater with a disposable QEMU-only trust root and a
# guest-local origin. No production signing material enters the simulation.
install -d -m 0700 /var/lib/millennium/qemu-ota
if [ ! -s /var/lib/millennium/qemu-ota/signing-key.pem ]; then
    openssl genpkey -algorithm ED25519 \
        -out /var/lib/millennium/qemu-ota/signing-key.pem
fi
openssl pkey -in /var/lib/millennium/qemu-ota/signing-key.pem -pubout \
    -out /etc/millennium/qemu-update-signing-key.pem
chmod 0600 /var/lib/millennium/qemu-ota/signing-key.pem
chmod 0644 /etc/millennium/qemu-update-signing-key.pem
cat >/etc/millennium/ota.conf <<'EOF'
channel=stable
device_group=qemu
manifest_url=https://127.0.0.1:18080/lab/stable/manifest.json
public_key=/etc/millennium/qemu-update-signing-key.pem
trusted_keys=qemu-lab:/etc/millennium/qemu-update-signing-key.pem
state_dir=/var/lib/millennium/ota
release_dir=/opt/millennium/releases
current_link=/opt/millennium/current
previous_link=/opt/millennium/previous
service=daemon.service
phone_state_url=http://127.0.0.1:8081/api/state
keypad_device=/run/millennium-mcu/keypad
display_device=/run/millennium-mcu/display
version_url=http://127.0.0.1:8081/api/version
health_url=https://127.0.0.1:18080/health.json
metrics_url=http://127.0.0.1:8081/api/metrics
health_timeout_seconds=30
max_failure_attempts=3
failure_backoff_seconds=1
automatic=true
architecture=aarch64
install_window_start=00:00
install_window_end=00:00
EOF
OTA_USER=millennium "$SOURCE/host/ota/install_ota.sh"
install -m 0755 "$SOURCE/tools/qemu/qemu-flash.sh" \
    /opt/millennium/releases/bootstrap/arduino/pi_flash.sh
install -m 0755 "$SOURCE/tools/qemu/identity-devices.py" \
    /usr/local/libexec/millennium-qemu-identity-devices
cat >/etc/systemd/system/millennium-qemu-identity-devices.service <<'EOF'
[Unit]
Description=Millennium QEMU dual-MCU identity endpoints
Before=daemon.service millennium-update-recover.service

[Service]
ExecStart=/usr/local/libexec/millennium-qemu-identity-devices --release /opt/millennium/releases/bootstrap/release.json
Restart=always
RuntimeDirectory=millennium-mcu
NoNewPrivileges=true
ProtectSystem=strict
ProtectHome=true
PrivateTmp=true

[Install]
WantedBy=multi-user.target
EOF
install -d -m 0755 /var/lib/millennium/qemu-origin
printf '%s\n' '{"overall_status":"WARNING","source":"qemu-sip-disabled"}' \
    >/var/lib/millennium/qemu-origin/health.json
printf '%s\n' '{"overall_status":"healthy","source":"qemu-experience-gate"}' \
    >/var/lib/millennium/qemu-origin/experience-health.json
install -m 0755 "$SOURCE/tools/qemu/https-origin.py" \
    /usr/local/libexec/millennium-qemu-origin
if [ ! -s /var/lib/millennium/qemu-ota/origin-key.pem ]; then
    openssl req -x509 -newkey rsa:2048 -nodes -days 3650 \
        -subj /CN=Millennium-QEMU-OTA \
        -addext subjectAltName=IP:127.0.0.1 \
        -keyout /var/lib/millennium/qemu-ota/origin-key.pem \
        -out /var/lib/millennium/qemu-ota/origin-cert.pem
fi
chmod 0600 /var/lib/millennium/qemu-ota/origin-key.pem
install -m 0644 /var/lib/millennium/qemu-ota/origin-cert.pem \
    /usr/local/share/ca-certificates/millennium-qemu-ota.crt
update-ca-certificates >/dev/null
cat >/etc/millennium/experiences.conf <<EOF
catalog_url=https://127.0.0.1:18080/experiences/stable/catalog.json
catalog_keys=qemu-lab:/etc/millennium/qemu-update-signing-key.pem
package_keys=qemu-lab:/etc/millennium/qemu-update-signing-key.pem
device_id=qemu-phone
groups=qemu
runtime_schema=1
daemon_version=$(cat "$SOURCE/VERSION")
state_dir=/var/lib/millennium/content
fallback=$CONTENT_ID
max_releases=4
install_window_start=00:00
install_window_end=00:00
phone_state_url=http://127.0.0.1:8081/api/state
health_url=https://127.0.0.1:18080/experience-health.json
EOF
cat >/etc/systemd/system/millennium-qemu-origin.service <<'EOF'
[Unit]
Description=Millennium QEMU lab-only OTA origin
After=network.target

[Service]
ExecStart=/usr/local/libexec/millennium-qemu-origin --directory /var/lib/millennium/qemu-origin --certificate /var/lib/millennium/qemu-ota/origin-cert.pem --key /var/lib/millennium/qemu-ota/origin-key.pem --port 18080
Restart=on-failure
NoNewPrivileges=true
ProtectSystem=strict
ProtectHome=true
PrivateTmp=true
ReadOnlyPaths=/var/lib/millennium/qemu-ota /var/lib/millennium/qemu-origin

[Install]
WantedBy=multi-user.target
EOF
cat >/etc/asound.conf <<'EOF'
pcm.!default { type null }
ctl.!default { type hw card 0 }
EOF
install -d -m 0755 /etc/systemd/system/daemon.service.d
cat >/etc/systemd/system/daemon.service.d/qemu.conf <<'EOF'
[Service]
User=millennium
Group=millennium
DevicePolicy=closed
DeviceAllow=/dev/virtio-ports/millennium.mcu rw
AmbientCapabilities=
CapabilityBoundingSet=
LimitRTPRIO=0
LimitMEMLOCK=0
EOF
chown -R millennium:millennium /var/lib/millennium /var/log/millennium
chown -R root:root /var/lib/millennium/content
find /var/lib/millennium/content/releases -type d -exec chmod 0755 {} +
find /var/lib/millennium/content/releases -type f -exec chmod 0444 {} +
chown millennium:millennium /var/lib/millennium/content/owner-requests
chmod 0750 /var/lib/millennium/content/owner-requests
systemctl daemon-reload
systemctl enable daemon.service
systemctl enable millennium-experience-recover.service \
    millennium-experience-update.timer
systemctl enable --now millennium-qemu-identity-devices.service
systemctl enable --now millennium-qemu-origin.service
systemctl restart daemon.service
# A successful provision is the durable baseline for subsequent abrupt-power
# tests. Do not let host-side QEMU termination test unflushed setup writes.
sync
