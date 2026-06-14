#!/usr/bin/env bash
#
# operator_smoke.sh — drive "The Operator: The Last Call" end-to-end against a
# LIVE daemon over the web API and assert the VFD reaches each expected state.
# Exercises everything that does NOT need physical hardware (mission flow, year
# parsing, pieces, the sealed era forced by a coin, the win), so you can confirm
# the software is healthy right before a hands-on session.
#
# It does NOT verify audio playback or a real magstripe swipe (the API can't do
# those) — use OPERATOR_PLAYTEST.md to check clips/coins/card/hook by hand.
#
# Usage:  ./operator_smoke.sh [HOST]          # default 192.168.86.152
#         RUN_DRIFT=1 ./operator_smoke.sh     # also run the slow ~32s drift test
#
set -uo pipefail

HOST="${1:-192.168.86.152}"
BASE="http://${HOST}"
PASS=0; FAIL=0

post() { curl -s --max-time 5 -X POST -H 'Content-Type: application/json' \
             -d "$1" "${BASE}/api/control" >/dev/null; }
key()  { post "{\"action\":\"keypad_press\",\"key\":\"$1\"}"; sleep 0.4; }
keys() { local d; for d in $(echo "$1" | fold -w1); do key "$d"; done; }
coin() { post "{\"action\":\"coin_insert\",\"cents\":${1:-25}}"; sleep 0.4; }
reset(){ post '{"action":"activate_plugin","plugin":"The Operator"}'; sleep 0.6
         post '{"action":"handset_up"}'; sleep 1; }
vfd()  { curl -s --max-time 5 "${BASE}/api/state" | python3 -c \
             'import sys,json; d=json.load(sys.stdin); print(d.get("line1",""),"|",d.get("line2",""))'; }
expect() {  # <label> <substring>
    local got; got="$(vfd)"
    if printf '%s' "$got" | grep -q "$2"; then
        printf '  PASS  %-26s [%s]\n' "$1" "$2"; PASS=$((PASS+1))
    else
        printf '  FAIL  %-26s want [%s] got %s\n' "$1" "$2" "$got"; FAIL=$((FAIL+1))
    fi
}
# Earn a piece: dial the year, wait for the era, LISTEN. ($1=year)
get_piece() { keys "$1"; sleep 3; key 1; sleep 3; }

echo "== Operator smoke test against ${HOST} =="
if ! curl -s --max-time 5 "${BASE}/api/plugins" | grep -q "The Operator"; then
    echo "  FAIL  daemon unreachable or plugin missing at ${HOST}"; exit 1
fi

echo "-- intro / HUD --";     reset;            expect "greeting" "DIAL A YEAR"
                                                expect "hud 0/3"  "0/3"
echo "-- find piece A --";    keys 1955; sleep 3; expect "1950s era" "1=LISTEN"
                              key 1;             expect "piece A" "PIECE A"
echo "-- flavor nudge --";    reset; keys 1925; sleep 3; expect "too early" "TOO EARLY"
echo "-- tangle/recover --";  reset; keys 1955; sleep 3; key 2
                              expect "tangled" "LINE TANGLED"
                              key 1;             expect "recovered" "PIECE A"
echo "-- sealed refusal --";  reset; keys 1998; sleep 3; expect "sealed" "SEALED"
echo "-- full win (coin-forces the sealed era) --"
                              reset
                              get_piece 1955;    expect "after A" "1/3"
                              get_piece 1978;    expect "after B" "2/3"
                              keys 1998; sleep 3; expect "sealed C" "SEALED"
                              coin 25; sleep 1;   expect "forced open" "1=LISTEN"
                              key 1; sleep 3;     expect "ready" "DIAL HER NUMBER"
                              keys 364155;        expect "ringing" "RINGING"
                              sleep 8;            expect "she is free" "SHE IS FREE"

if [ "${RUN_DRIFT:-0}" = "1" ]; then
    echo "-- temporal drift (~32s) --"; reset; keys 1955; sleep 3
                              expect "in era" "1=LISTEN"
                              echo "  ...idling 32s..."; sleep 32
                              expect "drifted back" "DIAL A YEAR"
fi

post '{"action":"handset_down"}'
echo "== ${PASS} passed, ${FAIL} failed =="
[ "$FAIL" -eq 0 ]
