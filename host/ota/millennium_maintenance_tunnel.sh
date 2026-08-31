#!/usr/bin/env bash
set -euo pipefail

CONFIG=/etc/millennium/maintenance-tunnel.conf
KEY=/etc/millennium/maintenance-tunnel-key
KNOWN_HOSTS=/etc/millennium/maintenance-known-hosts

if [ "${1:-}" = "provision" ]; then
    [ "$#" -eq 5 ] || {
        echo "usage: $0 provision SERVER USER SSH_PORT REMOTE_PORT" >&2
        exit 2
    }
    [ "$(id -u)" -eq 0 ] || { echo "provision must run as root" >&2; exit 1; }
    server="$2"
    user="$3"
    ssh_port="$4"
    remote_port="$5"
    case "$ssh_port:$remote_port" in
        *[!0-9:]*|:*|*:) echo "ports must be numeric" >&2; exit 2 ;;
    esac
    if [ ! -s "$KEY" ]; then
        ssh-keygen -q -t ed25519 -N '' -C millennium-maintenance -f "$KEY"
    fi
    ssh-keyscan -p "$ssh_port" "$server" >"$KNOWN_HOSTS.new"
    [ -s "$KNOWN_HOSTS.new" ] || { rm -f "$KNOWN_HOSTS.new"; echo "could not obtain server host key" >&2; exit 1; }
    mv "$KNOWN_HOSTS.new" "$KNOWN_HOSTS"
    chmod 0600 "$KEY" "$KNOWN_HOSTS"
    chmod 0644 "$KEY.pub"
    cat >"$CONFIG" <<EOF
server=$server
user=$user
ssh_port=$ssh_port
remote_port=$remote_port
EOF
    chmod 0600 "$CONFIG"
    cat "$KEY.pub"
    exit 0
fi

[ "${1:-}" = "run" ] && [ "$#" -eq 1 ] || {
    echo "usage: $0 run" >&2
    exit 2
}
[ -r "$CONFIG" ] && [ -s "$KEY" ] && [ -s "$KNOWN_HOSTS" ] || {
    echo "maintenance tunnel is not provisioned" >&2
    exit 1
}
# shellcheck disable=SC1090
. "$CONFIG"
exec /usr/bin/ssh -NT \
    -i "$KEY" -o IdentitiesOnly=yes -o BatchMode=yes \
    -o ExitOnForwardFailure=yes -o ServerAliveInterval=30 \
    -o ServerAliveCountMax=3 -o StrictHostKeyChecking=yes \
    -o CheckHostIP=no \
    -o UserKnownHostsFile="$KNOWN_HOSTS" -p "$ssh_port" \
    -R "127.0.0.1:${remote_port}:127.0.0.1:22" "$user@$server"
