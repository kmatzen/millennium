#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
QEMU="$SCRIPT_DIR/qemu.sh"

check_json() {
    local expression=$1
    "$QEMU" peripherals | python3 -c \
        'import json,sys; value=json.load(sys.stdin); assert eval(sys.argv[1], {"value": value})' \
        "$expression"
}

"$QEMU" status >/dev/null
"$QEMU" fault i2c down >/dev/null
if "$QEMU" hook up >/dev/null 2>&1; then
    echo "I2C-down hook event unexpectedly succeeded" >&2
    exit 1
fi
check_json 'value["arduinos"]["alpha"]["i2c_drops"] >= 1'
"$QEMU" fault i2c up >/dev/null
"$QEMU" hook up >/dev/null

"$QEMU" fault coin jam >/dev/null
if "$QEMU" coin 25 >/dev/null 2>&1; then
    echo "jammed validator unexpectedly accepted a coin" >&2
    exit 1
fi
"$QEMU" fault coin clear >/dev/null
"$QEMU" coin 25 >/dev/null

"$QEMU" reset-mcu watchdog-alpha >/dev/null
"$QEMU" reset-mcu watchdog-beta >/dev/null
check_json 'value["arduinos"]["alpha"]["last_reset_cause"] == "watchdog"'
check_json 'value["arduinos"]["beta"]["last_reset_cause"] == "watchdog"'

"$QEMU" fault validator verify-fail >/dev/null
check_json 'value["peripherals"]["coin_validator"]["verify_failed"] is True'
"$QEMU" fault validator verify-clear >/dev/null

"$QEMU" fault serial down >/dev/null
check_json 'value["host_link"]["enabled"] is False'
"$QEMU" fault serial up >/dev/null
for _ in {1..50}; do
    if check_json 'value["host_link"]["connected"] is True' 2>/dev/null; then break; fi
    sleep 0.1
done
check_json 'value["host_link"]["connected"] is True'

"$QEMU" fault ack delay 25 >/dev/null
check_json 'value["host_link"]["ack_delay_ms"] == 25'
"$QEMU" fault ack delay 0 >/dev/null
"$QEMU" fault ack drop >/dev/null
check_json 'value["host_link"]["drop_next_ack"] is True'
"$QEMU" fault crc next >/dev/null
"$QEMU" key 5 >/dev/null
check_json 'any(item["event"] == "crc-corruption" for item in value["trace"])'

printf 'PASS: I2C, validator, watchdog, serial, ACK, and CRC fault controls\n'
