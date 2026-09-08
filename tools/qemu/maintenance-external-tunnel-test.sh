#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
QEMU="$SCRIPT_DIR/qemu.sh"
STATE_DIR=${MILLENNIUM_QEMU_STATE:-"$SCRIPT_DIR/state"}
SSH_PORT=${MILLENNIUM_QEMU_SSH_PORT:-2222}
SSH_KEY="$STATE_DIR/id_ed25519"
SERVER_PORT=${MILLENNIUM_QEMU_MAINTENANCE_SERVER_PORT:-18084}
REVERSE_PORT=${MILLENNIUM_QEMU_MAINTENANCE_REVERSE_PORT:-18085}
GUEST_SSH=(ssh -i "$SSH_KEY" -p "$SSH_PORT" -o BatchMode=yes \
    -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR \
    millennium@127.0.0.1)
work=$(mktemp -d)
server_pid=
cleanup() {
    stop_server
    "${GUEST_SSH[@]}" sudo systemctl disable --now \
        millennium-maintenance-tunnel.service >/dev/null 2>&1 || true
    "${GUEST_SSH[@]}" sudo rm -f /etc/millennium/maintenance-tunnel.conf \
        /etc/millennium/maintenance-tunnel-key \
        /etc/millennium/maintenance-tunnel-key.pub \
        /etc/millennium/maintenance-known-hosts >/dev/null 2>&1 || true
    rm -rf "$work"
}
trap cleanup EXIT

stop_server() {
    test -n "$server_pid" || return 0
    # OpenSSH gives authenticated sessions their own process group. Terminate
    # the direct fixture children before its listener so no forwarded socket
    # survives merely because the test server's parent exited.
    children=$(pgrep -P "$server_pid" 2>/dev/null || true)
    test -z "$children" || kill $children >/dev/null 2>&1 || true
    kill -- -"$server_pid" >/dev/null 2>&1 || true
    wait "$server_pid" 2>/dev/null || true
    server_pid=
}

command -v sshd >/dev/null || {
    echo "maintenance external-tunnel test requires sshd on the QEMU host" >&2
    exit 1
}
"$QEMU" status >/dev/null
"${GUEST_SSH[@]}" sudo install -d -m 0755 /etc/millennium
"${GUEST_SSH[@]}" sudo rm -f /etc/millennium/maintenance-tunnel-key \
    /etc/millennium/maintenance-tunnel-key.pub
ssh-keygen -q -t ed25519 -N '' -C millennium-maintenance \
    -f "$work/client-key"
scp -i "$SSH_KEY" -P "$SSH_PORT" -o BatchMode=yes \
    -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR \
    "$work/client-key" "$work/client-key.pub" millennium@127.0.0.1:/tmp/
"${GUEST_SSH[@]}" sudo install -m 0600 /tmp/client-key \
    /etc/millennium/maintenance-tunnel-key
"${GUEST_SSH[@]}" sudo install -m 0644 /tmp/client-key.pub \
    /etc/millennium/maintenance-tunnel-key.pub
cp "$work/client-key.pub" "$work/authorized_keys"
ssh-keygen -q -t ed25519 -N '' -f "$work/host-key"
cat >"$work/sshd_config" <<EOF
Port $SERVER_PORT
ListenAddress 0.0.0.0
HostKey $work/host-key
AuthorizedKeysFile $work/authorized_keys
PidFile $work/sshd.pid
StrictModes no
PermitRootLogin prohibit-password
PasswordAuthentication no
KbdInteractiveAuthentication no
UsePAM no
AllowTcpForwarding remote
GatewayPorts no
PermitTTY no
LogLevel VERBOSE
EOF
start_server() {
    setsid /usr/sbin/sshd -D -e -f "$work/sshd_config" >"$work/sshd.log" 2>&1 &
    server_pid=$!
    for _ in {1..30}; do
        kill -0 "$server_pid" >/dev/null 2>&1 || {
            cat "$work/sshd.log" >&2
            exit 1
        }
        nc -z 127.0.0.1 "$SERVER_PORT" >/dev/null 2>&1 && return
        sleep 0.2
    done
    echo "external SSH fixture did not start" >&2
    exit 1
}
start_server
"${GUEST_SSH[@]}" sudo /usr/local/libexec/millennium-maintenance-tunnel \
    provision 10.0.2.2 root "$SERVER_PORT" "$REVERSE_PORT" >/dev/null
"${GUEST_SSH[@]}" sudo systemctl enable --now millennium-maintenance-tunnel.service
for _ in {1..40}; do
    ssh -i "$SSH_KEY" -p "$REVERSE_PORT" -o BatchMode=yes \
        -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
        -o LogLevel=ERROR millennium@127.0.0.1 true >/dev/null 2>&1 && break
    sleep 0.5
done
ssh -i "$SSH_KEY" -p "$REVERSE_PORT" -o BatchMode=yes \
    -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
    -o LogLevel=ERROR millennium@127.0.0.1 \
    'test -x /usr/local/bin/millennium-daemon'
stop_server
for _ in {1..30}; do
    ! nc -z 127.0.0.1 "$REVERSE_PORT" >/dev/null 2>&1 && break
    sleep 0.2
done
if nc -z 127.0.0.1 "$REVERSE_PORT" >/dev/null 2>&1; then
    echo "reverse port remained reachable after the external server stopped" >&2
    exit 1
fi
start_server
for _ in {1..50}; do
    ssh -i "$SSH_KEY" -p "$REVERSE_PORT" -o BatchMode=yes \
        -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
        -o LogLevel=ERROR millennium@127.0.0.1 true >/dev/null 2>&1 && break
    sleep 0.5
done
ssh -i "$SSH_KEY" -p "$REVERSE_PORT" -o BatchMode=yes \
    -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
    -o LogLevel=ERROR millennium@127.0.0.1 \
    'systemctl is-active --quiet ssh.service'
"${GUEST_SSH[@]}" systemctl is-active --quiet millennium-maintenance-tunnel.service
printf 'PASS: production reverse-maintenance service authenticated, forwarded, failed closed, and recovered through an external SSH server\n'
