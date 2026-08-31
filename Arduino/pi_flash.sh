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
# Only the target is reset by default. Both boards' Caterina bootloaders
# enumerate under the SAME product string ("Arduino Micro") and carry no USB
# serial number, so resetting both at once leaves two devices contending for one
# by-id path and there is no way to tell which one avrdude would reach. Set
# BOTH_RESET=1 only if you need it and can accept that ambiguity.
#
# A trap restarts daemon.service however the script exits, so a failed or
# interrupted flash never leaves the phone's daemon down.
#
# Usage:  pi_flash.sh <keypad|display> <hex-path>
# Env:    BOTH_RESET=1        reset both Arduinos first (default; 0 = target only)
#         RETRIES=4           flash attempts before giving up
#         AVRDUDE_TIMEOUT=25  hard cap (s) per avrdude run, so it can't wedge
#         MANAGE_DAEMON=1     stop/start daemon around a standalone flash
#
set -u

TARGET="${1:-}"
HEX="${2:-}"
RETRIES="${RETRIES:-4}"
AVRDUDE_TIMEOUT="${AVRDUDE_TIMEOUT:-25}"
BOTH_RESET="${BOTH_RESET:-0}"
MANAGE_DAEMON="${MANAGE_DAEMON:-1}"

[ -n "$TARGET" ] && [ -n "$HEX" ] || { echo "usage: pi_flash.sh <keypad|display> <hex-path>"; exit 2; }
[ -f "$HEX" ] || { echo "hex not found: $HEX"; exit 2; }

# GPIO17 -> Alpha (keypad) RST, GPIO27 -> Beta (display) RST.
#
# BY_ID is the RUNNING SKETCH's port, named by the custom board's usb_product
# ("Millennium Alpha"/"Millennium Beta"). We wait for it to disappear to confirm
# the reset took.
#
# BOOT_BY_ID is where avrdude must actually talk. Once reset, the board runs the
# stock Caterina bootloader, which enumerates under ITS OWN product string --
# "Arduino Micro" -- not the sketch's. Flashing the sketch port instead just
# reaches the running firmware, which answers avrdude with nothing:
#   avrdude: butterfly_recv(): programmer is not responding
BOOT_BY_ID=/dev/serial/by-id/usb-Arduino_LLC_Arduino_Micro-if00
case "$TARGET" in
  keypad)  GPIO=17; BY_ID=/dev/serial/by-id/usb-Arduino_LLC_Millennium_Alpha-if00 ;;
  display) GPIO=27; BY_ID=/dev/serial/by-id/usb-Arduino_LLC_Millennium_Beta-if00 ;;
  *) echo "unknown target '$TARGET' (expected keypad|display)"; exit 2 ;;
esac
OTHER_GPIO=$([ "$GPIO" = 17 ] && echo 27 || echo 17)

cleanup() {
    [ "$MANAGE_DAEMON" = 1 ] && sudo systemctl start daemon.service >/dev/null 2>&1 || true
}
trap cleanup EXIT

echo "pi_flash: target=$TARGET hex=$HEX gpio=$GPIO both_reset=$BOTH_RESET"
[ "$MANAGE_DAEMON" = 1 ] && sudo systemctl stop daemon.service >/dev/null 2>&1 || true
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
    # Wait for the sketch port to drop (confirms the reset took).
    for i in $(seq 1 100); do [ ! -e "$BY_ID" ] && break; sleep 0.02; done
    # Tight poll for the BOOTLOADER port, resolve, and flash with NO added
    # latency. An external reset holds Caterina open for several seconds, but
    # poll tightly anyway so a USB-triggered reset works too.
    P=""
    for i in $(seq 1 600); do
        if [ -e "$BOOT_BY_ID" ]; then P=$(readlink -f "$BOOT_BY_ID"); break; fi
        sleep 0.01
    done
    [ -n "$P" ] || { echo "  bootloader did not enumerate at $BOOT_BY_ID"; return 1; }
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
