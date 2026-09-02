#!/usr/bin/env bash
set -euo pipefail

# Codex/non-login shells on macOS may not inherit the Apple Silicon Homebrew
# prefix even when QEMU is installed there.
PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_DIR=$(cd "$SCRIPT_DIR/../.." && pwd)
STATE_DIR=${MILLENNIUM_QEMU_STATE:-"$SCRIPT_DIR/state"}
IMAGE_URL=${MILLENNIUM_QEMU_IMAGE_URL:-https://cloud.debian.org/images/cloud/bookworm/latest/debian-12-genericcloud-arm64.qcow2}
BASE_IMAGE="$STATE_DIR/debian-12-arm64.qcow2"
DISK_IMAGE="$STATE_DIR/millennium.qcow2"
SSH_PORT=${MILLENNIUM_QEMU_SSH_PORT:-2222}
SSH_KEY="$STATE_DIR/id_ed25519"
SSH=(ssh -i "$SSH_KEY" -p "$SSH_PORT" -o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR millennium@127.0.0.1)

die() { printf 'error: %s\n' "$*" >&2; exit 1; }
need() { command -v "$1" >/dev/null || die "missing '$1' ($2)"; }
running() { test -f "$STATE_DIR/qemu.pid" && kill -0 "$(cat "$STATE_DIR/qemu.pid")" 2>/dev/null; }

firmware_path() {
    local candidate
    for candidate in \
        /opt/homebrew/share/qemu/edk2-aarch64-code.fd \
        /usr/local/share/qemu/edk2-aarch64-code.fd \
        /usr/share/qemu-efi-aarch64/QEMU_EFI.fd \
        /usr/share/AAVMF/AAVMF_CODE.fd; do
        test -f "$candidate" && { printf '%s\n' "$candidate"; return; }
    done
    die "AArch64 UEFI firmware not found; install QEMU/edk2"
}

make_seed() {
    mkdir -p "$STATE_DIR/seed"
    test -f "$SSH_KEY" || ssh-keygen -q -t ed25519 -N '' -f "$SSH_KEY"
    sed "s|__SSH_PUBLIC_KEY__|$(cat "$SSH_KEY.pub")|" \
        "$SCRIPT_DIR/cloud-init/user-data.template" > "$STATE_DIR/seed/user-data"
    cp "$SCRIPT_DIR/cloud-init/meta-data" "$STATE_DIR/seed/meta-data"
    rm -f "$STATE_DIR/seed.iso"
    if command -v hdiutil >/dev/null; then
        hdiutil makehybrid -quiet -iso -joliet -default-volume-name cidata \
            -o "$STATE_DIR/seed.iso" "$STATE_DIR/seed"
    elif command -v xorriso >/dev/null; then
        xorriso -as mkisofs -quiet -volid cidata -joliet -rock \
            -output "$STATE_DIR/seed.iso" "$STATE_DIR/seed"
    elif command -v genisoimage >/dev/null; then
        genisoimage -quiet -volid cidata -joliet -rock \
            -output "$STATE_DIR/seed.iso" "$STATE_DIR/seed"
    else
        die "need hdiutil, xorriso, or genisoimage to create the cloud-init seed disk"
    fi
}

fetch_image() {
    need curl "install curl"
    mkdir -p "$STATE_DIR"
    if test -s "$BASE_IMAGE"; then
        printf 'base image already present: %s\n' "$BASE_IMAGE"
        return
    fi
    local temp="$BASE_IMAGE.partial"
    local sums="$STATE_DIR/SHA512SUMS"
    local image_name=${IMAGE_URL##*/}
    curl --fail --location --retry 3 --output "$temp" "$IMAGE_URL"
    curl --fail --location --retry 3 --output "$sums" "${IMAGE_URL%/*}/SHA512SUMS"
    local expected
    expected=$(awk -v name="$image_name" '$2 == name || $2 == "*" name {print $1}' "$sums")
    test -n "$expected" || die "image checksum is absent from the official SHA512SUMS"
    local actual
    if command -v sha512sum >/dev/null; then
        actual=$(sha512sum "$temp" | awk '{print $1}')
    else
        actual=$(shasum -a 512 "$temp" | awk '{print $1}')
    fi
    test "$actual" = "$expected" || die "downloaded image failed SHA-512 verification"
    mv "$temp" "$BASE_IMAGE"
    printf 'downloaded %s\n' "$BASE_IMAGE"
}

init_disk() {
    need qemu-img "install QEMU"
    fetch_image
    make_seed
    test -f "$DISK_IMAGE" || qemu-img create -f qcow2 -F qcow2 -b "$BASE_IMAGE" "$DISK_IMAGE" 16G
}

start_vm() {
    need qemu-system-aarch64 "install QEMU"
    init_disk
    running && die "VM is already running"
    rm -f "$STATE_DIR/mcu.sock" "$STATE_DIR/control.sock" "$STATE_DIR/monitor.sock"
    python3 "$SCRIPT_DIR/virtual_mcu.py" serve --daemonize \
        --pid-file "$STATE_DIR/mcu.pid" --log-file "$STATE_DIR/mcu.log" \
        --serial "$STATE_DIR/mcu.sock" --control "$STATE_DIR/control.sock" \
        --display "$STATE_DIR/display.json"
    local accel=${MILLENNIUM_QEMU_ACCEL:-tcg} cpu=cortex-a72
    local qemu_binary
    qemu_binary=$(command -v qemu-system-aarch64)
    if test -z "${MILLENNIUM_QEMU_ACCEL:-}" && test "$(uname -s)" = Darwin && \
            file "$qemu_binary" | grep -q 'arm64'; then
        accel=hvf
        cpu=host
    fi
    qemu-system-aarch64 \
        -name millennium \
        -machine "virt,accel=$accel" -cpu "$cpu" -smp 4 -m 2048 \
        -bios "$(firmware_path)" \
        -drive "if=virtio,format=qcow2,file=$DISK_IMAGE" \
        -drive "if=virtio,format=raw,readonly=on,file=$STATE_DIR/seed.iso" \
        -device virtio-net-device,netdev=net0 \
        -netdev "user,id=net0,hostfwd=tcp:127.0.0.1:$SSH_PORT-:22" \
        -device virtio-rng-device \
        -device virtio-serial-device \
        -chardev "socket,id=mcu,path=$STATE_DIR/mcu.sock,server=on,wait=off" \
        -device virtserialport,chardev=mcu,name=millennium.mcu \
        -monitor "unix:$STATE_DIR/monitor.sock,server=on,wait=off" \
        -serial "file:$STATE_DIR/console.log" -display none \
        -daemonize -pidfile "$STATE_DIR/qemu.pid"
    printf 'VM started. Run "%s wait", then "%s provision".\n' "$0" "$0"
}

wait_ready() {
    local attempts=180
    while (( attempts-- > 0 )); do
        if "${SSH[@]}" test -f /var/lib/cloud/millennium-ready 2>/dev/null; then
            printf 'guest is ready\n'; return
        fi
        sleep 2
    done
    die "guest did not finish cloud-init; inspect $STATE_DIR/console.log"
}

provision() {
    wait_ready
    local tar_metadata=()
    test "$(uname -s)" = Darwin && tar_metadata+=(--no-xattrs)
    COPYFILE_DISABLE=1 tar "${tar_metadata[@]}" --exclude=.git --exclude='tools/qemu/state' --exclude='*.o' \
        --exclude=host/daemon --exclude=host/simulator -C "$REPO_DIR" -czf - . | \
        "${SSH[@]}" 'rm -rf /tmp/millennium-src && mkdir /tmp/millennium-src && tar -xzf - -C /tmp/millennium-src'
    "${SSH[@]}" sudo /tmp/millennium-src/tools/qemu/provision-guest.sh /tmp/millennium-src | tee "$STATE_DIR/provision.log"
}

stop_vm() {
    need socat "install socat"
    if running; then
        printf 'system_powerdown\n' | socat - "UNIX-CONNECT:$STATE_DIR/monitor.sock" >/dev/null 2>&1 || true
        for _ in {1..20}; do running || break; sleep 0.5; done
        if running; then
            printf 'quit\n' | socat - "UNIX-CONNECT:$STATE_DIR/monitor.sock" >/dev/null 2>&1 || true
            for _ in {1..20}; do running || break; sleep 0.25; done
        fi
    fi
    test -f "$STATE_DIR/mcu.pid" && kill "$(cat "$STATE_DIR/mcu.pid")" 2>/dev/null || true
    rm -f "$STATE_DIR/qemu.pid" "$STATE_DIR/mcu.pid"
    printf 'VM stopped\n'
}

case ${1:-help} in
    fetch) fetch_image ;;
    init) init_disk ;;
    start) start_vm ;;
    wait) wait_ready ;;
    provision) provision ;;
    stop) stop_vm ;;
    status) running && printf 'running (pid %s)\n' "$(cat "$STATE_DIR/qemu.pid")" || { printf 'stopped\n'; exit 1; } ;;
    ssh) shift; "${SSH[@]}" "$@" ;;
    logs) "${SSH[@]}" sudo journalctl -u daemon.service -f ;;
    token) "${SSH[@]}" sudo cat /etc/millennium/admin-token ;;
    tunnel) exec ssh -N -L 8081:127.0.0.1:8081 -L 8080:127.0.0.1:8080 "${SSH[@]:1}" ;;
    display) test -f "$STATE_DIR/display.json" && cat "$STATE_DIR/display.json" || die "no display update captured" ;;
    key|hook|coin|card) python3 "$SCRIPT_DIR/virtual_mcu.py" send --control "$STATE_DIR/control.sock" "$@" ;;
    smoke) "$SCRIPT_DIR/smoke-test.sh" ;;
    reset)
        stop_vm
        test -f "$DISK_IMAGE" && mv "$DISK_IMAGE" "$DISK_IMAGE.$(date +%Y%m%d%H%M%S).bak"
        init_disk
        printf 'fresh overlay created; previous disk retained as a timestamped backup\n'
        ;;
    help|*)
        printf 'usage: %s {fetch|init|start|wait|provision|stop|status|ssh|logs|token|tunnel|display|key|hook|coin|card|smoke|reset}\n' "$0"
        ;;
esac
