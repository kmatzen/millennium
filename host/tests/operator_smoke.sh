#!/usr/bin/env bash
#
# operator_smoke.sh — drive "The Operator" plugin end-to-end against a LIVE
# daemon over the web API and assert the VFD reaches each expected state. This
# exercises everything that does NOT need physical hardware (state machine,
# fares, year parsing, paradox, drift), so you can confirm the software is
# healthy right before a hands-on session with real coins/handset/card/audio.
#
# It does NOT verify audio playback (the API can't read that) — use the
# companion OPERATOR_PLAYTEST.md to check clips/coins/card/hook by hand.
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
         post '{"action":"handset_up"}'; sleep 1; }   # fresh session, 0 coins
vfd()  { curl -s --max-time 5 "${BASE}/api/state" \
             | tr ',' '\n' | grep -E '"line1"|"line2"' | sed 's/.*://;s/}//' | tr '\n' ' '; }
# expect <label> <substring>
expect() {
    local got; got="$(vfd)"
    if printf '%s' "$got" | grep -q "$2"; then
        printf '  PASS  %-28s [%s]\n' "$1" "$2"; PASS=$((PASS+1))
    else
        printf '  FAIL  %-28s want [%s] got %s\n' "$1" "$2" "$got"; FAIL=$((FAIL+1))
    fi
}

echo "== Operator smoke test against ${HOST} =="

# Sanity: daemon reachable + plugin present
if ! curl -s --max-time 5 "${BASE}/api/plugins" | grep -q "The Operator"; then
    echo "  FAIL  daemon unreachable or plugin missing at ${HOST}"; exit 1
fi

echo "-- greeting --";        reset;            expect "greeting" "DIAL A YEAR"
echo "-- era (2005) --";      coin 25; keys 2005; sleep 6; expect "era static" "STATIC YEARS"
echo "-- home (2000, free) --"; reset; keys 2000; sleep 6; expect "frozen minute" "FROZEN MINUTE"
echo "-- need-fare then pay --"; reset; keys 2005; expect "need fare" "NEED"
                              coin 25; sleep 6;   expect "connects" "STATIC YEARS"
echo "-- 1999 drop --";       reset; coin 25; keys 1999; sleep 6; expect "drop" "MIDNIGHT"
                              sleep 4;            expect "back to operator" "DIAL A YEAR"
echo "-- sealed future + pass --"; reset; keys 2080; expect "sealed refused" "SEALED"
                              post '{"action":"activate_plugin","plugin":"The Operator"}'; sleep 0.6
                              post '{"action":"handset_up"}'; sleep 1
                              # NOTE: a real magstripe swipe can't be injected via this API;
                              # this leg only confirms the refusal. Swipe a card by hand for the pass.
echo "-- paradox + repair --"; reset
                              coin 25; coin 25; coin 25; coin 25
                              keys 1995; sleep 6; expect "era A" "LAST ANALOG"
                              key 2;              expect "armed" "BENT"
                              key '#'; keys 2005; expect "jammed" "LINE FAULT"
                              key '#'; keys 1995; sleep 6; key 1; expect "repaired" "MENDED"

if [ "${RUN_DRIFT:-0}" = "1" ]; then
    echo "-- temporal drift (~32s) --"; reset; coin 25; keys 2005; sleep 6
                              expect "in era" "STATIC YEARS"
                              echo "  ...idling 32s..."; sleep 32
                              expect "drifted back" "TEMPORAL DRIFT"
fi

post '{"action":"handset_down"}'
echo "== ${PASS} passed, ${FAIL} failed =="
[ "$FAIL" -eq 0 ]
