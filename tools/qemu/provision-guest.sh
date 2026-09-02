#!/bin/sh
set -eu

SOURCE=${1:-/tmp/millennium-src}
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
make daemon GIT_HASH="qemu-$(sed -n '1p' "$SOURCE/VERSION")"

install -d -m 0755 /etc/millennium /var/lib/millennium /var/log/millennium
install -m 0755 daemon /usr/local/bin/millennium-daemon
install -m 0644 systemd/daemon.service /etc/systemd/system/daemon.service
install -m 0644 "$SOURCE/tools/qemu/daemon.conf" /etc/millennium/daemon.conf
cat >/etc/udev/rules.d/99-millennium-qemu.rules <<'EOF'
KERNEL=="vport*", GROUP="dialout", MODE="0660"
EOF
udevadm control --reload-rules
udevadm trigger --name-match=/dev/vport0p1 || true
chgrp dialout /dev/vport0p1
chmod 0660 /dev/vport0p1
if [ ! -s /etc/millennium/admin-token ]; then
    umask 077
    od -An -N32 -tx1 /dev/urandom | tr -d ' \n' > /etc/millennium/admin-token
fi
chown millennium:millennium /etc/millennium/admin-token
chmod 0600 /etc/millennium/admin-token
install -d -o millennium -g millennium -m 0750 /var/lib/millennium/content
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
DeviceAllow=/dev/vport0p1 rw
AmbientCapabilities=
CapabilityBoundingSet=
LimitRTPRIO=0
LimitMEMLOCK=0
EOF
chown -R millennium:millennium /var/lib/millennium /var/log/millennium
systemctl daemon-reload
systemctl enable daemon.service
systemctl enable --now millennium-qemu-identity-devices.service
systemctl enable --now millennium-qemu-origin.service
systemctl restart daemon.service
