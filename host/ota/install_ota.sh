#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HOST_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
RUN_USER="${OTA_USER:-${SUDO_USER:-}}"

if [ "$(id -u)" -ne 0 ]; then
    echo "install_ota.sh must run as root (use sudo)" >&2
    exit 1
fi
[ -n "$RUN_USER" ] && [ "$RUN_USER" != root ] || {
    echo "cannot determine daemon user; run with sudo or set OTA_USER" >&2
    exit 1
}
id "$RUN_USER" >/dev/null 2>&1 || { echo "unknown OTA_USER: $RUN_USER" >&2; exit 1; }

install -d -m 0755 /usr/local/libexec /etc/millennium /var/lib/millennium/ota
[ -e /etc/millennium/ota.conf ] || install -m 0644 "$SCRIPT_DIR/ota.conf.example" /etc/millennium/ota.conf

# Convert the currently built checkout into the first immutable release. This
# makes enabling the ExecStart override safe before a signed release exists.
[ -x "$HOST_DIR/daemon" ] || { echo "build host/daemon before installing OTA" >&2; exit 1; }
BOOTSTRAP=/opt/millennium/releases/bootstrap
install -d -m 0755 "$BOOTSTRAP/host" "$BOOTSTRAP/arduino" "$BOOTSTRAP/ota"
install -m 0755 "$HOST_DIR/daemon" "$BOOTSTRAP/host/millennium-daemon"
install -m 0644 "$HOST_DIR/web_portal.html" "$BOOTSTRAP/host/web_portal.html"
install -m 0755 "$HOST_DIR/../Arduino/pi_flash.sh" "$BOOTSTRAP/arduino/pi_flash.sh"
install -m 0644 "$HOST_DIR/../Arduino/build/keypad/keypad.ino.hex" "$BOOTSTRAP/arduino/keypad.hex"
install -m 0644 "$HOST_DIR/../Arduino/build/display/display.ino.hex" "$BOOTSTRAP/arduino/display.hex"
install -m 0755 "$SCRIPT_DIR/millennium_ota.py" "$BOOTSTRAP/ota/millennium-ota"
python3 - "$BOOTSTRAP/arduino/keypad.hex" "$BOOTSTRAP/arduino/display.hex" \
    "$BOOTSTRAP/release.json" <<'PY'
import json, pathlib, re, sys
pattern = re.compile(rb"MILLENNIUM role=(keypad|display) version=([^ ]+) protocol=([0-9]+) build=([^ ]+) selftest=ok")
firmware = {}
for name in sys.argv[1:3]:
    raw = bytearray()
    for line in pathlib.Path(name).read_text().splitlines():
        record = bytes.fromhex(line[1:])
        if record[3] == 0:
            raw.extend(record[4:4 + record[0]])
    match = pattern.search(raw)
    if not match:
        raise SystemExit("firmware identity missing from " + name)
    role = match.group(1).decode()
    firmware[role] = {"role": role, "version": match.group(2).decode(),
                      "protocol": int(match.group(3)), "build": match.group(4).decode()}
if set(firmware) != {"keypad", "display"}:
    raise SystemExit("bootstrap firmware identities are incomplete")
pathlib.Path(sys.argv[3]).write_text(json.dumps({"firmware": firmware}, sort_keys=True) + "\n")
PY
ln -sfn "$BOOTSTRAP" /opt/millennium/current
ln -sfn /opt/millennium/current/ota/millennium-ota /usr/local/libexec/millennium-ota
install -d -m 0755 /etc/systemd/system/daemon.service.d
install -m 0644 "$HOST_DIR/systemd/20-ota-release.conf" \
    /etc/systemd/system/daemon.service.d/20-ota-release.conf

for unit in millennium-update-check.service millennium-update-check.timer \
            millennium-update-apply.service millennium-update-auto-apply.service \
            millennium-update-apply.timer millennium-update-recover.service; do
    install -m 0644 "$HOST_DIR/systemd/$unit" "/etc/systemd/system/$unit"
done

if getent group millennium >/dev/null; then
    :
else
    groupadd --system millennium
fi
usermod -a -G millennium "$RUN_USER"
install -m 0440 "$HOST_DIR/systemd/millennium-ota-sudoers" /etc/sudoers.d/millennium-ota
visudo -cf /etc/sudoers.d/millennium-ota >/dev/null
systemctl daemon-reload
systemctl enable millennium-update-recover.service
systemctl enable --now millennium-update-check.timer millennium-update-apply.timer
systemctl restart daemon.service

echo "OTA installed. Provision /etc/millennium/update-signing-key.pem before the first check."
