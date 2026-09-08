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
network_down=false

stop_server() {
    test -n "$server_pid" || return 0
    children=$(pgrep -P "$server_pid" 2>/dev/null || true)
    test -z "$children" || kill $children >/dev/null 2>&1 || true
    kill -- -"$server_pid" >/dev/null 2>&1 || true
    wait "$server_pid" 2>/dev/null || true
    server_pid=
}

cleanup() {
    if "$network_down"; then
        "$QEMU" network up >/dev/null 2>&1 || true
        network_down=false
    fi
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

wait_reverse() {
    for _ in {1..50}; do
        if ssh -i "$SSH_KEY" -p "$REVERSE_PORT" -o BatchMode=yes \
                -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
                -o LogLevel=ERROR millennium@127.0.0.1 true >/dev/null 2>&1; then
            return 0
        fi
        sleep 0.5
    done
    echo "reverse maintenance path did not become usable" >&2
    return 1
}

require_reverse_closed() {
    for _ in {1..40}; do
        if ! nc -z 127.0.0.1 "$REVERSE_PORT" >/dev/null 2>&1; then
            return 0
        fi
        sleep 0.25
    done
    echo "reverse maintenance port remained reachable" >&2
    return 1
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
printf 'restrict,port-forwarding,permitlisten="127.0.0.1:%s",command="/bin/false" %s\n' \
    "$REVERSE_PORT" "$(cat "$work/client-key.pub")" >"$work/authorized_keys"
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
start_server
"${GUEST_SSH[@]}" sudo /usr/local/libexec/millennium-maintenance-tunnel \
    provision 10.0.2.2 root "$SERVER_PORT" "$REVERSE_PORT" >/dev/null

if "${GUEST_SSH[@]}" sudo ssh -i /etc/millennium/maintenance-tunnel-key \
        -o IdentitiesOnly=yes -o BatchMode=yes -o StrictHostKeyChecking=yes \
        -o CheckHostIP=no \
        -o UserKnownHostsFile=/etc/millennium/maintenance-known-hosts \
        -p "$SERVER_PORT" root@10.0.2.2 true >/dev/null 2>&1; then
    echo "restricted maintenance key unexpectedly executed a command" >&2
    exit 1
fi
if "${GUEST_SSH[@]}" sudo ssh -tt -A \
        -i /etc/millennium/maintenance-tunnel-key \
        -o IdentitiesOnly=yes -o BatchMode=yes -o StrictHostKeyChecking=yes \
        -o CheckHostIP=no \
        -o UserKnownHostsFile=/etc/millennium/maintenance-known-hosts \
        -p "$SERVER_PORT" root@10.0.2.2 true >/dev/null 2>&1; then
    echo "restricted maintenance key unexpectedly accepted PTY/agent forwarding" >&2
    exit 1
fi
if "${GUEST_SSH[@]}" sudo ssh -NT -i /etc/millennium/maintenance-tunnel-key \
        -o IdentitiesOnly=yes -o BatchMode=yes -o ExitOnForwardFailure=yes \
        -o StrictHostKeyChecking=yes -o CheckHostIP=no \
        -o UserKnownHostsFile=/etc/millennium/maintenance-known-hosts \
        -p "$SERVER_PORT" -R 127.0.0.1:18086:127.0.0.1:22 \
        root@10.0.2.2 >/dev/null 2>&1; then
    echo "restricted maintenance key unexpectedly opened an unrelated forward" >&2
    exit 1
fi

"${GUEST_SSH[@]}" sudo systemctl enable --now millennium-maintenance-tunnel.service
wait_reverse
ssh -i "$SSH_KEY" -p "$REVERSE_PORT" -o BatchMode=yes \
    -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
    -o LogLevel=ERROR millennium@127.0.0.1 \
    'test -x /usr/local/bin/millennium-daemon'

external_ip=$(hostname -I | awk '{print $1}')
if test -n "$external_ip" && nc -z "$external_ip" "$REVERSE_PORT" >/dev/null 2>&1; then
    echo "reverse maintenance port was exposed beyond loopback" >&2
    exit 1
fi

"${GUEST_SSH[@]}" sudo systemctl kill --kill-who=main \
    millennium-maintenance-tunnel.service
require_reverse_closed
wait_reverse

stop_server
require_reverse_closed
mv "$work/host-key" "$work/host-key.trusted"
mv "$work/host-key.pub" "$work/host-key.pub.trusted"
ssh-keygen -q -t ed25519 -N '' -f "$work/host-key"
start_server
sleep 17
if nc -z 127.0.0.1 "$REVERSE_PORT" >/dev/null 2>&1; then
    echo "maintenance client accepted a changed server host key" >&2
    exit 1
fi
stop_server
mv "$work/host-key.trusted" "$work/host-key"
mv "$work/host-key.pub.trusted" "$work/host-key.pub"
start_server
wait_reverse

tunnel_pid=$("${GUEST_SSH[@]}" systemctl show --property=MainPID --value \
    millennium-maintenance-tunnel.service)
"$QEMU" network down >/dev/null
network_down=true
sleep 125
if timeout 5 ssh -i "$SSH_KEY" -p "$REVERSE_PORT" -o BatchMode=yes \
        -o ConnectTimeout=3 -o StrictHostKeyChecking=no \
        -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR \
        millennium@127.0.0.1 true >/dev/null 2>&1; then
    echo "blackholed reverse maintenance path still traversed to the guest" >&2
    exit 1
fi
"$QEMU" network up >/dev/null
network_down=false
"$QEMU" wait >/dev/null
wait_reverse
recovered_pid=$("${GUEST_SSH[@]}" systemctl show --property=MainPID --value \
    millennium-maintenance-tunnel.service)
if test "$recovered_pid" = "$tunnel_pid"; then
    echo "maintenance tunnel process did not expire across its keepalive window" >&2
    exit 1
fi
ssh -i "$SSH_KEY" -p "$REVERSE_PORT" -o BatchMode=yes \
    -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
    -o LogLevel=ERROR millennium@127.0.0.1 \
    'systemctl is-active --quiet ssh.service'
"${GUEST_SSH[@]}" systemctl is-active --quiet millennium-maintenance-tunnel.service
printf 'PASS: production reverse-maintenance service enforced host/key/loopback policy and recovered from process, server, and keepalive-window failures\n'
