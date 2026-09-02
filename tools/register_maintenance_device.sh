#!/usr/bin/env bash
set -euo pipefail

usage() { echo "usage: $0 DEVICE_NAME DEVICE_PUBLIC_KEY DEVICE_IP" >&2; exit 2; }
[ "$#" -eq 3 ] || usage
[ "$(id -u)" -eq 0 ] || { echo "run as root" >&2; exit 1; }
NAME="$1"
PUBLIC_KEY="$2"
IP="$3"
INTERFACE="${WG_INTERFACE:-millennium}"
CONFIG="/etc/wireguard/${INTERFACE}.conf"
[[ "$NAME" =~ ^[A-Za-z0-9_.-]+$ ]] || { echo "invalid device name" >&2; exit 2; }
[[ "$IP" =~ ^[0-9.]+$ ]] || { echo "DEVICE_IP must be an IPv4 address without CIDR" >&2; exit 2; }
[ -f "$CONFIG" ] || { echo "$CONFIG does not exist" >&2; exit 1; }
python3 - "$PUBLIC_KEY" <<'PY' || { echo "invalid WireGuard public key" >&2; exit 2; }
import base64, binascii, sys
try:
    assert len(base64.b64decode(sys.argv[1], validate=True)) == 32
except (AssertionError, binascii.Error):
    raise SystemExit(1)
PY
grep -qF "# device: $NAME" "$CONFIG" && { echo "device name already registered" >&2; exit 1; }
grep -qF "AllowedIPs = $IP/32" "$CONFIG" && { echo "device IP already registered" >&2; exit 1; }
cat >> "$CONFIG" <<EOF

# device: $NAME
[Peer]
PublicKey = $PUBLIC_KEY
AllowedIPs = $IP/32
EOF
wg set "$INTERFACE" peer "$PUBLIC_KEY" allowed-ips "$IP/32"
echo "Registered $NAME. From this server: ssh matzen@$IP"
