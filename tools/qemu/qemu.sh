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
DISK_SIZE=${MILLENNIUM_QEMU_DISK_SIZE:-16G}
SSH_KEY="$STATE_DIR/id_ed25519"
SSH=(ssh -i "$SSH_KEY" -p "$SSH_PORT" -o BatchMode=yes -o ConnectTimeout=15 \
    -o ConnectionAttempts=1 -o ServerAliveInterval=5 -o ServerAliveCountMax=2 \
    -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR \
    millennium@127.0.0.1)

die() { printf 'error: %s\n' "$*" >&2; exit 1; }
need() { command -v "$1" >/dev/null || die "missing '$1' ($2)"; }
running() {
    test -f "$STATE_DIR/qemu.pid" || return 1
    local pid
    pid=$(cat "$STATE_DIR/qemu.pid")
    [[ "$pid" =~ ^[0-9]+$ ]] || return 1
    kill -0 "$pid" 2>/dev/null || return 1
    ps -p "$pid" -o command= 2>/dev/null | grep -q '[q]emu-system-aarch64'
}

monitor_command() {
    need socat "install socat"
    running || die "VM is not running"
    printf '%s\n' "$1" | socat - "UNIX-CONNECT:$STATE_DIR/monitor.sock" >/dev/null
}

network_link() {
    case ${1:-} in
        down) monitor_command "set_link net0 off" ;;
        up) monitor_command "set_link net0 on" ;;
        *) die "network requires up or down" ;;
    esac
    printf 'guest network %s\n' "$1"
}

power_cut() {
    monitor_command quit || true
    for _ in {1..20}; do running || break; sleep 0.25; done
    if test -f "$STATE_DIR/mcu.pid"; then
        kill "$(cat "$STATE_DIR/mcu.pid")" 2>/dev/null || true
    fi
    rm -f "$STATE_DIR/qemu.pid" "$STATE_DIR/mcu.pid"
    printf 'VM power cut; disk was not gracefully shut down\n'
}

virtual_firmware_identity() {
    python3 - "$REPO_DIR" <<'PY'
import importlib.util, pathlib, sys
root = pathlib.Path(sys.argv[1])
spec = importlib.util.spec_from_file_location("ota_builder", root / "tools/build_ota_release.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
version = (root / "VERSION").read_text().strip()
keypad = module.firmware_identity(root / "Arduino/build/keypad/keypad.ino.hex", "keypad", version)
display = module.firmware_identity(root / "Arduino/build/display/display.ino.hex", "display", version)
if keypad["build"] != display["build"]:
    raise SystemExit("keypad/display firmware build identities differ")
print(version, keypad["build"])
PY
}

start_virtual_mcu() {
    local identity firmware_version firmware_build
    identity=$(virtual_firmware_identity)
    read -r firmware_version firmware_build <<<"$identity"
    python3 "$SCRIPT_DIR/virtual_mcu.py" serve --daemonize \
        --pid-file "$STATE_DIR/mcu.pid" --log-file "$STATE_DIR/mcu.log" \
        --serial "$STATE_DIR/mcu.sock" --control "$STATE_DIR/control.sock" \
        --display "$STATE_DIR/display.json" \
        --firmware-version "$firmware_version" --firmware-build "$firmware_build"
}

restart_virtual_mcu() {
    running || die "VM is not running"
    if test -f "$STATE_DIR/mcu.pid"; then
        kill "$(cat "$STATE_DIR/mcu.pid")" 2>/dev/null || true
        for _ in {1..20}; do
            kill -0 "$(cat "$STATE_DIR/mcu.pid")" 2>/dev/null || break
            sleep 0.1
        done
    fi
    rm -f "$STATE_DIR/mcu.pid" "$STATE_DIR/control.sock"
    start_virtual_mcu
    for _ in {1..50}; do test -S "$STATE_DIR/control.sock" && break; sleep 0.1; done
    test -S "$STATE_DIR/control.sock" || die "virtual MCU control socket did not return"
    printf 'virtual MCU restarted\n'
}

checkpoint() {
    need qemu-img "install QEMU"
    local action=${1:-list} name=${2:-}
    running && die "checkpoints require a stopped VM"
    case "$action" in
        list) qemu-img snapshot -l "$DISK_IMAGE" ;;
        save|load|delete)
            [[ "$name" =~ ^[A-Za-z0-9][A-Za-z0-9_.-]{0,63}$ ]] || die "invalid checkpoint name"
            case "$action" in
                save) qemu-img snapshot -c "$name" "$DISK_IMAGE" ;;
                load) qemu-img snapshot -a "$name" "$DISK_IMAGE" ;;
                delete) qemu-img snapshot -d "$name" "$DISK_IMAGE" ;;
            esac
            printf 'checkpoint %s: %s\n' "$action" "$name"
            ;;
        *) die "checkpoint requires list, save NAME, load NAME, or delete NAME" ;;
    esac
}

lifecycle_test() {
    running || die "start and provision the VM before lifecycle-test"
    "${SSH[@]}" true
    monitor_command stop
    sleep 1
    running || die "paused VM process exited"
    monitor_command cont
    for _ in {1..30}; do "${SSH[@]}" true 2>/dev/null && break; sleep 1; done
    "${SSH[@]}" true
    network_link down >/dev/null
    if "${SSH[@]}" curl --fail --silent --max-time 3 http://192.0.2.1/ >/dev/null 2>&1; then
        network_link up >/dev/null
        die "external request unexpectedly succeeded with network down"
    fi
    network_link up >/dev/null
    "${SSH[@]}" systemctl is-active --quiet daemon.service
    printf 'PASS: pause/resume and deterministic network isolation preserve the appliance\n'
}

recovery_test() {
    local name=qemu-recovery-test
    running || die "start and provision the VM before recovery-test"
    stop_vm
    qemu-img snapshot -d "$name" "$DISK_IMAGE" >/dev/null 2>&1 || true
    checkpoint save "$name"
    start_vm
    wait_ready
    "${SSH[@]}" systemctl is-active --quiet daemon.service
    power_cut
    checkpoint load "$name"
    start_vm
    wait_ready
    "${SSH[@]}" systemctl is-active --quiet daemon.service
    for _ in {1..30}; do
        if "${SSH[@]}" 'curl --fail --silent http://127.0.0.1:8081/api/version >/dev/null && curl --fail --silent http://127.0.0.1:8081/api/metrics | grep -Fq '\''"mcu_protocol_version": 2'\''' 2>/dev/null; then
            printf 'PASS: abrupt power cut and named-checkpoint restore recovered a healthy appliance\n'
            return
        fi
        sleep 1
    done
    die "restored guest did not regain daemon API and MCU protocol health"
}

write_artifact_summary() {
    local output=$1
    python3 - "$output" <<'PY'
import datetime, hashlib, json, pathlib, sys
root = pathlib.Path(sys.argv[1])
files = {}
for path in sorted(root.iterdir()):
    if path.name != "summary.json" and path.is_file():
        files[path.name] = hashlib.sha256(path.read_bytes()).hexdigest()
summary = {"schema": 1, "kind": "qemu-software-evidence",
           "physical_hardware_claimed": False,
           "created_at": datetime.datetime.now(datetime.timezone.utc).replace(microsecond=0).isoformat(),
           "files": files}
(root / "summary.json").write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")
PY
}

collect_artifacts() {
    running || die "VM is not running"
    local name=${1:-$(date -u +%Y%m%dT%H%M%SZ)}
    [[ "$name" =~ ^[A-Za-z0-9][A-Za-z0-9_.-]{0,63}$ ]] || die "invalid artifact name"
    local output="$STATE_DIR/artifacts/$name"
    mkdir -p "$output"
    for file in display.json mcu.log console.log provision.log; do
        test -f "$STATE_DIR/$file" && cp "$STATE_DIR/$file" "$output/$file"
    done
    if test -f "$STATE_DIR/exact-image/result.json"; then
        cp "$STATE_DIR/exact-image/result.json" "$output/exact-image-result.json"
        cp "$STATE_DIR/exact-image/console.log" "$output/exact-image-console.log"
    fi
    "${SSH[@]}" sudo journalctl -u daemon.service --no-pager > "$output/daemon-journal.log"
    "${SSH[@]}" curl --silent --show-error http://127.0.0.1:8080/metrics > "$output/metrics.prom" || true
    "${SSH[@]}" curl --silent --show-error http://127.0.0.1:8081/api/health > "$output/health.json" || true
    "${SSH[@]}" curl --silent --output /dev/null --write-out '%{http_code}' \
        http://127.0.0.1:8081/api/health > "$output/health-http-status.txt" || true
    python3 "$SCRIPT_DIR/virtual_mcu.py" send --control "$STATE_DIR/control.sock" status > "$output/peripherals.json"
    "${SSH[@]}" sudo cat /var/lib/millennium/state > "$output/daemon-state.txt" 2>/dev/null || true
    "${SSH[@]}" sudo cat /var/lib/millennium/story-state > "$output/story-state.txt" 2>/dev/null || true
    "${SSH[@]}" sudo cat /var/lib/millennium/ota/status.json > "$output/ota-status.json" 2>/dev/null || true
    "${SSH[@]}" sudo cat /var/log/millennium/qemu-flash.log > "$output/ota-flash.log" 2>/dev/null || true
    "${SSH[@]}" sudo journalctl -u millennium-update-recover.service --no-pager > "$output/ota-recovery-journal.log" 2>/dev/null || true
    "${SSH[@]}" sha256sum /var/lib/millennium/content/current/story.mst \
        /var/lib/millennium/content/current/story.json > "$output/content-sha256.txt" 2>/dev/null || true
    write_artifact_summary "$output"
    printf '%s\n' "$output"
}

wait_display() {
    local wanted=$1
    for _ in {1..200}; do
        if test -f "$STATE_DIR/display.json" && grep -Fq "$wanted" "$STATE_DIR/display.json"; then
            return 0
        fi
        sleep 0.2
    done
    die "display did not reach: $wanted"
}

experience_test() {
    running || die "start and provision the VM before experience-test"
    python3 "$SCRIPT_DIR/virtual_mcu.py" send --control "$STATE_DIR/control.sock" hook down >/dev/null
    sleep 1
    "${SSH[@]}" sudo rm -f /var/lib/millennium/story-state
    # The token expansion intentionally happens in the single-quoted remote shell.
    # shellcheck disable=SC2016
    "${SSH[@]}" 'token=$(sudo cat /etc/millennium/admin-token); curl --fail --silent --request POST --header "Content-Type: application/json" --header "Authorization: Bearer $token" --data '\''{"action":"activate_plugin","plugin":"Story Mode"}'\'' http://127.0.0.1:8081/api/control | grep -Fq '\''"success":true'\'''
    wait_display "THIS CALL IS FOR"
    network_link down >/dev/null
    trap 'network_link up >/dev/null 2>&1 || true' EXIT
    python3 "$SCRIPT_DIR/virtual_mcu.py" send --control "$STATE_DIR/control.sock" hook up >/dev/null
    wait_display "OPERATOR 17"
    python3 "$SCRIPT_DIR/virtual_mcu.py" send --control "$STATE_DIR/control.sock" key 9 >/dev/null
    wait_display "A CHILD IS WAITING"
    sleep 12
    python3 "$SCRIPT_DIR/virtual_mcu.py" send --control "$STATE_DIR/control.sock" key '*' >/dev/null
    wait_display "A CHILD IS WAITING"
    sleep 12
    python3 "$SCRIPT_DIR/virtual_mcu.py" send --control "$STATE_DIR/control.sock" key '#' >/dev/null
    wait_display "MARA, AGE 9"
    sleep 12
    python3 "$SCRIPT_DIR/virtual_mcu.py" send --control "$STATE_DIR/control.sock" key 1 >/dev/null
    wait_display "DO NOT HANG UP"
    python3 "$SCRIPT_DIR/virtual_mcu.py" send --control "$STATE_DIR/control.sock" hook down >/dev/null
    wait_display "THE LINE HOLDS"
    sleep 12
    python3 "$SCRIPT_DIR/virtual_mcu.py" send --control "$STATE_DIR/control.sock" hook up >/dev/null
    wait_display "YOUR WORDS CROSS"
    sleep 12
    python3 "$SCRIPT_DIR/virtual_mcu.py" send --control "$STATE_DIR/control.sock" coin 25 >/dev/null
    wait_display "A LOST VOICEMAIL"
    sleep 12
    python3 "$SCRIPT_DIR/virtual_mcu.py" send --control "$STATE_DIR/control.sock" key '#' >/dev/null
    wait_display "YOUR WORDS CROSS"
    sleep 12
    python3 "$SCRIPT_DIR/virtual_mcu.py" send --control "$STATE_DIR/control.sock" card qemu-purpose-token >/dev/null
    wait_display "A DEED FROM 2019"
    sleep 12
    python3 "$SCRIPT_DIR/virtual_mcu.py" send --control "$STATE_DIR/control.sock" key '#' >/dev/null
    wait_display "YOUR WORDS CROSS"
    sleep 12
    python3 "$SCRIPT_DIR/virtual_mcu.py" send --control "$STATE_DIR/control.sock" key '*' >/dev/null
    wait_display "WHAT SHOULD SHE DO?"
    python3 "$SCRIPT_DIR/virtual_mcu.py" send --control "$STATE_DIR/control.sock" key 1 >/dev/null
    wait_display "MESSAGE: LEAVE NOW"
    wait_display "A DOOR SLAMS"
    python3 "$SCRIPT_DIR/virtual_mcu.py" send --control "$STATE_DIR/control.sock" hook down >/dev/null
    wait_display "THIS CALL IS FOR"
    python3 "$SCRIPT_DIR/virtual_mcu.py" send --control "$STATE_DIR/control.sock" hook up >/dev/null
    wait_display "MARA VALE, 2019"
    sleep 12
    python3 "$SCRIPT_DIR/virtual_mcu.py" send --control "$STATE_DIR/control.sock" key '#' >/dev/null
    wait_display "I BUILT THIS PHONE"
    network_link up >/dev/null
    trap - EXIT
    printf 'PASS: offline story activation, recovery, optional inputs, ending, and return visit\n'
}

experience_lifecycle_test() {
    running || die "start and provision the VM before experience-lifecycle-test"
    # The preceding interactive story deliberately exercises a return visit
    # and may leave the handset off-hook.  Signed activation is correctly
    # deferred while busy, so establish an idle precondition here rather than
    # making this acceptance test depend on scenario ordering.
    python3 "$SCRIPT_DIR/virtual_mcu.py" send \
        --control "$STATE_DIR/control.sock" hook down >/dev/null
    for _ in {1..30}; do
        if "${SSH[@]}" curl -fsS http://127.0.0.1:8081/api/state 2>/dev/null | \
                python3 -c 'import json,sys; raise SystemExit(json.load(sys.stdin)["current_state"] != 1)'; then
            break
        fi
        sleep 1
    done
    "${SSH[@]}" curl -fsS http://127.0.0.1:8081/api/state | \
        python3 -c 'import json,sys; raise SystemExit(json.load(sys.stdin)["current_state"] != 1)'
    "${SSH[@]}" sudo /tmp/millennium-src/tools/qemu/experience-lifecycle-test-guest.sh /tmp/millennium-src
}

experience_power_test() {
    local phase digest
    running || die "run experience-lifecycle-test before experience-power-test"
    "${SSH[@]}" test -d /var/lib/millennium/content/releases/last-line-2.1.0
    "${SSH[@]}" test -d /var/lib/millennium/content/releases/last-line-2.1.1
    for phase in activating selecting; do
        if test "$phase" = activating; then
            digest=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
        else
            digest=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb
        fi
        "${SSH[@]}" sudo /usr/local/libexec/millennium-experience-power-prepare "$phase" | grep -Fqx "$digest"
        power_cut
        start_vm
        wait_ready
        for _ in {1..30}; do
            if "${SSH[@]}" sudo /usr/local/libexec/millennium-experience-power-verify "$phase" \
                    2>/dev/null | grep -Fqx "$digest"; then
                break
            fi
            sleep 1
        done
        "${SSH[@]}" sudo /usr/local/libexec/millennium-experience-power-verify "$phase" | grep -Fqx "$digest"
    done
    printf 'PASS: abrupt VM power cuts at activating and selecting journal boundaries recover the permanent fallback\n'
}

ota_test() {
    running || die "start and provision the VM before ota-test"
    "${SSH[@]}" sudo /tmp/millennium-src/tools/qemu/ota-test-guest.sh /tmp/millennium-src
}

ota_fault_test() {
    running || die "start and provision the VM before ota-fault-test"
    "${SSH[@]}" sudo /tmp/millennium-src/tools/qemu/ota-fault-test-guest.sh /tmp/millennium-src
}

ota_external_origin_test() {
    running || die "start and provision the VM before ota-external-origin-test"
    "$SCRIPT_DIR/ota-external-origin-test.sh"
}

os_ota_test() {
    running || die "start and provision the VM before os-ota-test"
    "${SSH[@]}" 'cd /tmp/millennium-src && python3 -m unittest \
        host.tests.test_os_ota \
        host.tests.test_persistent_state \
        tools.tests.test_admin_token_permissions \
        tools.tests.test_factory_seed_zero2w \
        tools.tests.test_zero2w_image_layer \
        tools.tests.test_slot_shared_generator'
}

wifi_test() {
    running || die "start and provision the VM before wifi-test"
    "${SSH[@]}" sudo /tmp/millennium-src/tools/qemu/wifi-test-guest.sh /tmp/millennium-src
}

wifi_external_client_test() {
    running || die "start and provision the VM before wifi-external-client-test"
    "$SCRIPT_DIR/wifi-external-client-test.sh"
}

maintenance_external_tunnel_test() {
    running || die "start and provision the VM before maintenance-external-tunnel-test"
    "$SCRIPT_DIR/maintenance-external-tunnel-test.sh"
}

full_test() {
    local run exact_image_ran=false exact_inputs=0
    run="full-$(date -u +%Y%m%dT%H%M%SZ)"
    for value in "${MILLENNIUM_QEMU_EXACT_IMAGE:-}" \
        "${MILLENNIUM_QEMU_EXACT_KERNEL:-}" \
        "${MILLENNIUM_QEMU_EXACT_INITRD:-}"; do
        test -n "$value" && exact_inputs=$((exact_inputs + 1))
    done
    test "$exact_inputs" -eq 0 || test "$exact_inputs" -eq 3 || \
        die "set all three MILLENNIUM_QEMU_EXACT_{IMAGE,KERNEL,INITRD} variables"
    rm -f "$STATE_DIR/exact-image/result.json" \
        "$STATE_DIR/exact-image/console.log"
    if test "$exact_inputs" -eq 3; then
        exact_image_test
        exact_image_ran=true
    fi
    if ! running; then
        start_vm
    fi
    wait_ready
    provision
    python3 "$SCRIPT_DIR/test_virtual_mcu.py"
    "$SCRIPT_DIR/smoke-test.sh"
    "$SCRIPT_DIR/peripheral-fault-test.sh"
    ota_test
    ota_fault_test
    ota_external_origin_test
    os_ota_test
    wifi_test
    wifi_external_client_test
    maintenance_external_tunnel_test
    experience_test
    experience_lifecycle_test
    experience_power_test
    lifecycle_test
    recovery_test
    local artifact
    artifact=$(collect_artifacts "$run")
    python3 - "$artifact/full-test-result.json" "$exact_image_ran" <<'PY'
import datetime, json, pathlib, sys
acceptance = ["virtual-mcu-unit", "appliance-smoke", "lifecycle", "power-recovery",
              "peripheral-faults", "signed-ota", "ota-faults", "ota-external-origin", "wifi-onboarding",
              "wifi-independent-client",
              "maintenance-external-tunnel",
              "offline-experience", "signed-experience-lifecycle", "experience-power-recovery", "production-image-contracts", "evidence-export"]
exact_image = sys.argv[2] == "true"
if exact_image:
    acceptance.append("exact-production-image-userspace")
result = {"schema": 1, "passed": True, "physical_hardware_claimed": False,
          "exact_production_image_tested": exact_image,
          "completed_at": datetime.datetime.now(datetime.timezone.utc).replace(microsecond=0).isoformat(),
          "acceptance": acceptance}
pathlib.Path(sys.argv[1]).write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
PY
    write_artifact_summary "$artifact"
    printf 'PASS: full QEMU software lab\n%s\n' "$artifact"
}

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
    [[ "$DISK_SIZE" =~ ^[1-9][0-9]*[GM]$ ]] || \
        die "MILLENNIUM_QEMU_DISK_SIZE must be a positive whole number of G or M"
    fetch_image
    make_seed
    test -f "$DISK_IMAGE" || qemu-img create -f qcow2 -F qcow2 \
        -b "$BASE_IMAGE" "$DISK_IMAGE" "$DISK_SIZE"
}

start_vm() {
    need qemu-system-aarch64 "install QEMU"
    init_disk
    running && die "VM is already running"
    rm -f "$STATE_DIR/mcu.sock" "$STATE_DIR/control.sock" "$STATE_DIR/monitor.sock"
    start_virtual_mcu
    local accel=${MILLENNIUM_QEMU_ACCEL:-tcg} cpu=cortex-a72
    local qemu_binary
    qemu_binary=$(command -v qemu-system-aarch64)
    if test -z "${MILLENNIUM_QEMU_ACCEL:-}" && test "$(uname -s)" = Darwin && \
            file "$qemu_binary" | grep -q 'arm64'; then
        accel=hvf
        cpu=host
    fi
    if ! qemu-system-aarch64 \
        -name millennium \
        -machine "virt,accel=$accel" -cpu "$cpu" -smp 4 -m 2048 \
        -bios "$(firmware_path)" \
        -drive "if=virtio,format=qcow2,file=$DISK_IMAGE" \
        -drive "if=virtio,format=raw,readonly=on,file=$STATE_DIR/seed.iso" \
        -device virtio-net-device,netdev=net0 \
        -netdev "user,id=net0,hostfwd=tcp:127.0.0.1:$SSH_PORT-:22,hostfwd=tcp:127.0.0.1:18082-:18082" \
        -device virtio-rng-device \
        -device virtio-serial-device \
        -chardev "socket,id=mcu,path=$STATE_DIR/mcu.sock,server=on,wait=off" \
        -device virtserialport,chardev=mcu,name=millennium.mcu \
        -monitor "unix:$STATE_DIR/monitor.sock,server=on,wait=off" \
        -serial "file:$STATE_DIR/console.log" -display none \
        -daemonize -pidfile "$STATE_DIR/qemu.pid"; then
        if test -f "$STATE_DIR/mcu.pid"; then
            kill "$(cat "$STATE_DIR/mcu.pid")" 2>/dev/null || true
        fi
        rm -f "$STATE_DIR/mcu.pid"
        die "QEMU failed to start; the newly started virtual MCU was cleaned up"
    fi
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
    local tar_metadata=() source_commit
    source_commit=$(git -C "$REPO_DIR" rev-parse HEAD)
    [[ "$source_commit" =~ ^[0-9a-f]{40}$ ]] || die "cannot determine source commit"
    test "$(uname -s)" = Darwin && tar_metadata+=(--no-xattrs)
    COPYFILE_DISABLE=1 tar "${tar_metadata[@]}" --exclude=.git \
        --exclude='./tools/qemu/state' --exclude='tools/qemu/state' --exclude='*.o' \
        --exclude=host/daemon --exclude=host/simulator -C "$REPO_DIR" -czf - . | \
        "${SSH[@]}" 'rm -rf /tmp/millennium-src && mkdir /tmp/millennium-src && tar -xzf - -C /tmp/millennium-src'
    "${SSH[@]}" "printf '%s\\n' '$source_commit' > /tmp/millennium-src/.millennium-source-commit"
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
    if test -f "$STATE_DIR/mcu.pid"; then
        kill "$(cat "$STATE_DIR/mcu.pid")" 2>/dev/null || true
    fi
    rm -f "$STATE_DIR/qemu.pid" "$STATE_DIR/mcu.pid"
    printf 'VM stopped\n'
}

exact_image_test() {
    local image=${MILLENNIUM_QEMU_EXACT_IMAGE:-}
    local kernel=${MILLENNIUM_QEMU_EXACT_KERNEL:-}
    local initrd=${MILLENNIUM_QEMU_EXACT_INITRD:-}
    test -n "$image" || die "set MILLENNIUM_QEMU_EXACT_IMAGE to the expanded production image"
    test -n "$kernel" || die "set MILLENNIUM_QEMU_EXACT_KERNEL to a QEMU-virt arm64 kernel"
    test -n "$initrd" || die "set MILLENNIUM_QEMU_EXACT_INITRD to its initramfs"
    mkdir -p "$STATE_DIR/exact-image"
    python3 "$SCRIPT_DIR/exact_image_test.py" \
        --image "$image" --kernel "$kernel" --initrd "$initrd" \
        --output "$STATE_DIR/exact-image"
}

case ${1:-help} in
    fetch) fetch_image ;;
    init) init_disk ;;
    start) start_vm ;;
    wait) wait_ready ;;
    provision) provision ;;
    stop) stop_vm ;;
    power-cut) power_cut ;;
    pause) monitor_command stop; printf 'VM paused\n' ;;
    resume) monitor_command cont; printf 'VM resumed\n' ;;
    restart-virtual-mcu) restart_virtual_mcu ;;
    network) network_link "${2:-}" ;;
    checkpoint) shift; checkpoint "$@" ;;
    lifecycle-test) lifecycle_test ;;
    recovery-test) recovery_test ;;
    peripheral-fault-test) "$SCRIPT_DIR/peripheral-fault-test.sh" ;;
    ota-test) ota_test ;;
    ota-fault-test) ota_fault_test ;;
    ota-external-origin-test) ota_external_origin_test ;;
    os-ota-test) os_ota_test ;;
    wifi-test) wifi_test ;;
    wifi-external-client-test) wifi_external_client_test ;;
    maintenance-external-tunnel-test) maintenance_external_tunnel_test ;;
    experience-test) experience_test ;;
    experience-lifecycle-test) experience_lifecycle_test ;;
    experience-power-test) experience_power_test ;;
    exact-image-test) exact_image_test ;;
    full-test) full_test ;;
    collect-artifacts) collect_artifacts "${2:-}" ;;
    status)
        if running; then
            printf 'running (pid %s)\n' "$(cat "$STATE_DIR/qemu.pid")"
        else
            printf 'stopped\n'
            exit 1
        fi
        ;;
    ssh) shift; "${SSH[@]}" "$@" ;;
    logs) "${SSH[@]}" sudo journalctl -u daemon.service -f ;;
    token) "${SSH[@]}" sudo cat /etc/millennium/admin-token ;;
    tunnel) exec ssh -N -L 8081:127.0.0.1:8081 -L 8080:127.0.0.1:8080 "${SSH[@]:1}" ;;
    display)
        test -f "$STATE_DIR/display.json" || die "no display update captured"
        cat "$STATE_DIR/display.json"
        ;;
    key|hook|coin|card|fault|reset-mcu)
        action=$1; shift
        test "$action" = reset-mcu && action=reset
        python3 "$SCRIPT_DIR/virtual_mcu.py" send --control "$STATE_DIR/control.sock" "$action" "$@"
        ;;
    peripherals) python3 "$SCRIPT_DIR/virtual_mcu.py" send --control "$STATE_DIR/control.sock" status ;;
    smoke) "$SCRIPT_DIR/smoke-test.sh" ;;
    reset)
        stop_vm
        test -f "$DISK_IMAGE" && mv "$DISK_IMAGE" "$DISK_IMAGE.$(date +%Y%m%d%H%M%S).bak"
        init_disk
        printf 'fresh overlay created; previous disk retained as a timestamped backup\n'
        ;;
    help|*)
        printf 'usage: %s {fetch|init|start|wait|provision|stop|power-cut|pause|resume|restart-virtual-mcu|network|checkpoint|lifecycle-test|recovery-test|peripheral-fault-test|ota-test|ota-fault-test|ota-external-origin-test|os-ota-test|wifi-test|wifi-external-client-test|maintenance-external-tunnel-test|experience-test|experience-lifecycle-test|experience-power-test|full-test|collect-artifacts|status|ssh|logs|token|tunnel|display|peripherals|key|hook|coin|card|fault|reset-mcu|smoke|reset}\n' "$0"
        ;;
esac
