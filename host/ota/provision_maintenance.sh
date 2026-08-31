#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "usage: $0 --server-key BASE64 --address CIDR --maintainer-key FILE [--user USER] [--endpoint HOST:PORT] [--allowed-ips CIDR]" >&2
    exit 2
}

SERVER_KEY=""
ADDRESS=""
ENDPOINT="maintenance.kmatzen.com:51820"
ALLOWED_IPS="10.77.0.0/24"
MAINTAINER_KEY=""
MAINTENANCE_USER="matzen"
while [ "$#" -gt 0 ]; do
    case "$1" in
        --server-key) SERVER_KEY="${2:-}"; shift 2 ;;
        --address) ADDRESS="${2:-}"; shift 2 ;;
        --endpoint) ENDPOINT="${2:-}"; shift 2 ;;
        --allowed-ips) ALLOWED_IPS="${2:-}"; shift 2 ;;
        --maintainer-key) MAINTAINER_KEY="${2:-}"; shift 2 ;;
        --user) MAINTENANCE_USER="${2:-}"; shift 2 ;;
        *) usage ;;
    esac
done
[ -n "$SERVER_KEY" ] && [ -n "$ADDRESS" ] && [ -n "$MAINTAINER_KEY" ] || usage
[ "$(id -u)" -eq 0 ] || { echo "run as root" >&2; exit 1; }
command -v wg >/dev/null || { echo "wireguard-tools is required" >&2; exit 1; }
command -v sshd >/dev/null || { echo "OpenSSH server is required" >&2; exit 1; }
id "$MAINTENANCE_USER" >/dev/null 2>&1 || { echo "unknown maintenance user" >&2; exit 1; }
[ -f "$MAINTAINER_KEY" ] || { echo "maintainer public key not found" >&2; exit 1; }
if ! python3 - "$SERVER_KEY" "$ADDRESS" "$ALLOWED_IPS" "$ENDPOINT" <<'PY'
import base64, binascii, ipaddress, re, sys
try:
    if len(base64.b64decode(sys.argv[1], validate=True)) != 32:
        raise ValueError()
    ipaddress.ip_interface(sys.argv[2])
    for item in sys.argv[3].split(','):
        ipaddress.ip_network(item.strip())
    if not re.fullmatch(r"[A-Za-z0-9.-]+:[0-9]{1,5}", sys.argv[4]):
        raise ValueError()
    port = int(sys.argv[4].rsplit(':', 1)[1])
    if not 1 <= port <= 65535:
        raise ValueError()
except (ValueError, binascii.Error):
    raise SystemExit(1)
PY
then
    echo "invalid WireGuard key, address, allowed IPs, or endpoint" >&2
    exit 1
fi
KEY_LINE="$(sed -n '1p' "$MAINTAINER_KEY")"
case "$KEY_LINE" in
    ssh-ed25519\ *|sk-ssh-ed25519@openssh.com\ *) ;;
    *) echo "maintainer key must be an Ed25519 OpenSSH public key" >&2; exit 1 ;;
esac

umask 077
install -d -m 0700 /etc/wireguard
if [ ! -s /etc/wireguard/millennium.key ]; then
    wg genkey > /etc/wireguard/millennium.key
fi
PRIVATE_KEY="$(< /etc/wireguard/millennium.key)"
PUBLIC_KEY="$(printf '%s' "$PRIVATE_KEY" | wg pubkey)"
cat > /etc/wireguard/millennium.conf <<EOF
[Interface]
PrivateKey = $PRIVATE_KEY
Address = $ADDRESS

[Peer]
PublicKey = $SERVER_KEY
Endpoint = $ENDPOINT
AllowedIPs = $ALLOWED_IPS
PersistentKeepalive = 25
EOF
chmod 0600 /etc/wireguard/millennium.conf

USER_HOME="$(getent passwd "$MAINTENANCE_USER" | cut -d: -f6)"
USER_GROUP="$(id -gn "$MAINTENANCE_USER")"
install -d -m 0700 -o "$MAINTENANCE_USER" -g "$USER_GROUP" "$USER_HOME/.ssh"
touch "$USER_HOME/.ssh/authorized_keys"
grep -qxF "$KEY_LINE" "$USER_HOME/.ssh/authorized_keys" || printf '%s\n' "$KEY_LINE" >> "$USER_HOME/.ssh/authorized_keys"
chown "$MAINTENANCE_USER:$USER_GROUP" "$USER_HOME/.ssh/authorized_keys"
chmod 0600 "$USER_HOME/.ssh/authorized_keys"
install -d -m 0755 /etc/ssh/sshd_config.d
cat > /etc/ssh/sshd_config.d/60-millennium-maintenance.conf <<EOF
PermitRootLogin no
PasswordAuthentication no
KbdInteractiveAuthentication no
PubkeyAuthentication yes
AllowUsers $MAINTENANCE_USER
EOF
sshd -t
sshd -T | grep -qx 'passwordauthentication no'
sshd -T | grep -qx 'kbdinteractiveauthentication no'
sshd -T | grep -qx 'permitrootlogin no'
systemctl enable --now wg-quick@millennium.service
systemctl restart ssh.service
printf 'Register this device public key on the management server:\n%s\n' "$PUBLIC_KEY"
