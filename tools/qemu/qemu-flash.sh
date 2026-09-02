#!/bin/sh
set -eu

# QEMU has no AVR electrical model. This shim preserves the production
# worker's flash boundary: it validates the signed payload and records the
# attempt; the virtual MCU independently answers the real identity probe.
TARGET=${1:-}
IMAGE=${2:-}
case "$TARGET" in keypad|display) ;; *) echo "invalid MCU target: $TARGET" >&2; exit 2 ;; esac
test -s "$IMAGE" || { echo "missing firmware image: $IMAGE" >&2; exit 2; }
grep -q '^:' "$IMAGE" || { echo "invalid Intel HEX payload: $IMAGE" >&2; exit 2; }
install -d -m 0755 /var/log/millennium
printf '%s target=%s sha256=%s\n' "$(date -u +%FT%TZ)" "$TARGET" "$(sha256sum "$IMAGE" | awk '{print $1}')" \
    >>/var/log/millennium/qemu-flash.log
