#!/usr/bin/env bash
set -euo pipefail

WG_INTERFACE="${WG_INTERFACE:-millennium}"
WG_ADDRESS="${WG_ADDRESS:-10.77.0.1/24}"
WG_PORT="${WG_PORT:-51820}"

[ "$(id -u)" -eq 0 ] || { echo "run as root" >&2; exit 1; }
command -v wg >/dev/null || { echo "wireguard-tools is required" >&2; exit 1; }
[[ "$WG_INTERFACE" =~ ^[A-Za-z0-9_.-]+$ ]] || { echo "invalid WG_INTERFACE" >&2; exit 1; }
python3 - "$WG_ADDRESS" "$WG_PORT" <<'PY' || { echo "invalid WG_ADDRESS or WG_PORT" >&2; exit 1; }
import ipaddress, sys
try:
    ipaddress.ip_interface(sys.argv[1])
    port = int(sys.argv[2])
    assert 1 <= port <= 65535
except (ValueError, AssertionError):
    raise SystemExit(1)
PY
umask 077
install -d -m 0700 /etc/wireguard
KEY_FILE="/etc/wireguard/${WG_INTERFACE}.key"
[ -s "$KEY_FILE" ] || wg genkey > "$KEY_FILE"
PRIVATE_KEY="$(< "$KEY_FILE")"
cat > "/etc/wireguard/${WG_INTERFACE}.conf" <<EOF
[Interface]
PrivateKey = $PRIVATE_KEY
Address = $WG_ADDRESS
ListenPort = $WG_PORT
SaveConfig = false
EOF
chmod 0600 "/etc/wireguard/${WG_INTERFACE}.conf"
systemctl enable --now "wg-quick@${WG_INTERFACE}.service"
printf 'Management server public key:\n'
printf '%s' "$PRIVATE_KEY" | wg pubkey
printf '\nPoint maintenance.kmatzen.com at this server and allow UDP %s.\n' "$WG_PORT"
