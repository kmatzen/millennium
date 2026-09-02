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
if [ -d "$SOURCE/content/build/current" ]; then
    cp -R "$SOURCE/content/build/current" /var/lib/millennium/content/
fi
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
systemctl restart daemon.service
