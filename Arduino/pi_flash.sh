#!/usr/bin/env bash
#
# pi_flash.sh — robust on-Pi flasher for the Millennium Arduino Micros.
#
# Runs ENTIRELY on the Pi, so there is no SSH round-trip latency between
# detecting the Caterina bootloader and launching avrdude. That matters: a
# hardware reset on a Micro opens the bootloader for only ~750 ms before it
# jumps to the sketch, and splitting reset -> detect -> flash across separate
# `ssh` calls reliably misses that window. Doing it in one local script catches
# it on the first try.
#
# Alpha (keypad) and Beta (display) share one USB hub; resetting both together
# (BOTH_RESET=1, the default) gives a clean re-enumeration so the target's
# bootloader appears reliably.
#
# A trap restarts daemon.service however the script exits, so a failed or
# interrupted flash never leaves the phone's daemon down.
#
# Usage:  pi_flash.sh <keypad|display> <hex-path>
# Env:    BOTH_RESET=1        reset both Arduinos first (default; 0 = target only)
#         RETRIES=4           flash attempts before giving up
#         AVRDUDE_TIMEOUT=25  hard cap (s) per avrdude run, so it can't wedge
#
set -u

TARGET="${1:-}"
HEX="${2:-}"
RETRIES="${RETRIES:-4}"
AVRDUDE_TIMEOUT="${AVRDUDE_TIMEOUT:-25}"
BOTH_RESET="${BOTH_RESET:-1}"

[ -n "$TARGET" ] && [ -n "$HEX" ] || { echo "usage: pi_flash.sh <keypad|display> <hex-path>"; exit 2; }
[ -f "$HEX" ] || { echo "hex not found: $HEX"; exit 2; }

# GPIO17 -> Alpha (keypad) RST, GPIO27 -> Beta (display) RST.
case "$TARGET" in
  keypad)  GPIO=17; BY_ID=/dev/serial/by-id/usb-Arduino_LLC_Millennium_Alpha-if00 ;;
  display) GPIO=27; BY_ID=/dev/serial/by-id/usb-Arduino_LLC_Millennium_Beta-if00 ;;
  *) echo "unknown target '$TARGET' (expected keypad|display)"; exit 2 ;;
esac
OTHER_GPIO=$([ "$GPIO" = 17 ] && echo 27 || echo 17)

cleanup() { sudo systemctl start daemon.service >/dev/null 2>&1 || true; }
trap cleanup EXIT

echo "pi_flash: target=$TARGET hex=$HEX gpio=$GPIO both_reset=$BOTH_RESET"
sudo systemctl stop daemon.service >/dev/null 2>&1 || true
sleep 0.3

flash_once() {
    local i P out
    # Release reset line(s), then pulse low to reset.
    raspi-gpio set "$GPIO" ip
    [ "$BOTH_RESET" = 1 ] && raspi-gpio set "$OTHER_GPIO" ip
    sleep 0.3
    if [ "$BOTH_RESET" = 1 ]; then
        raspi-gpio set "$GPIO" op dl; raspi-gpio set "$OTHER_GPIO" op dl; sleep 0.15
        raspi-gpio set "$GPIO" ip;    raspi-gpio set "$OTHER_GPIO" ip
    else
        raspi-gpio set "$GPIO" op dl; sleep 0.15; raspi-gpio set "$GPIO" ip
    fi
    # Wait for the port to drop (confirms the reset took).
    for i in $(seq 1 100); do [ ! -e "$BY_ID" ] && break; sleep 0.02; done
    # Tight poll for the bootloader, resolve, and flash with NO added latency.
    P=""
    for i in $(seq 1 600); do
        if [ -e "$BY_ID" ]; then P=$(readlink -f "$BY_ID"); break; fi
        sleep 0.01
    done
    [ -n "$P" ] || { echo "  bootloader did not enumerate"; return 1; }
    echo "  bootloader at $P, flashing..."
    out=$(timeout "$AVRDUDE_TIMEOUT" avrdude -p atmega32u4 -c avr109 -P "$P" \
            -b 57600 -U "flash:w:$HEX:i" 2>&1)
    echo "$out" | tail -8
    # avrdude returns nonzero on the benign Caterina "exit bootloader" handshake
    # (the freshly written sketch reboots before the final command), so key on a
    # completed verify rather than the exit code.
    echo "$out" | grep -qiE "[0-9]+ bytes of flash verified"
}

ok=0
for a in $(seq 1 "$RETRIES"); do
    echo "== attempt $a/$RETRIES =="
    if flash_once; then ok=1; break; fi
    sleep 1
done

[ "$ok" = 1 ] && echo "pi_flash: SUCCESS" || echo "pi_flash: FAILED after $RETRIES attempts"
[ "$ok" = 1 ]
