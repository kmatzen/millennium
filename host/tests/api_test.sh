#!/usr/bin/env bash
# API integration test — run against a live daemon
# Usage: ./api_test.sh [HOST]
#   HOST=localhost ./api_test.sh          # run on same machine as daemon
#   ssh pi 'cd ~/millennium/host && make api-test'   # run on Pi
#   HOST=192.168.86.145 ./api_test.sh     # run from another machine (if reachable)
# Requires: curl

set -e
HOST="${HOST:-${1:-localhost}}"
BASE="http://${HOST}:8081"
FAILED=0

run() {
    local name="$1"
    local cmd="$2"
    local want="$3"
    local got
    got=$(eval "$cmd" 2>/dev/null || true)
    if echo "$got" | grep -q "$want"; then
        echo "  PASS: $name"
    else
        echo "  FAIL: $name (expected '$want' in response)"
        printf "    got: %.150s\n" "${got:-<empty or connection failed>}"
        FAILED=$((FAILED + 1))
    fi
}

# Quick connectivity check
if ! curl -s -m 3 "$BASE/api/state" >/dev/null 2>&1; then
    echo "Cannot reach $BASE — is the daemon running? (Try: ssh pi 'cd ~/millennium/host && make api-test')"
    exit 1
fi

echo "=== API test against $BASE ==="
echo ""

# State and health
run "GET /api/state returns JSON" "curl -s $BASE/api/state" "current_state"
run "GET /api/state has sip_registered" "curl -s $BASE/api/state" "sip_registered"
run "GET /api/health returns JSON" "curl -s $BASE/api/health" "overall_status"

# Control: handset_up
run "POST handset_up" \
    "curl -s -X POST $BASE/api/control -H 'Content-Type: application/json' -d '{\"action\":\"handset_up\"}'" \
    '"success":true'
run "State reflects IDLE_UP after handset_up" "curl -s $BASE/api/state" '"current_state":2'

# Control: coin_insert with cents
run "POST coin_insert (cents)" \
    "curl -s -X POST $BASE/api/control -H 'Content-Type: application/json' -d '{\"action\":\"coin_insert\",\"cents\":25}'" \
    '"success":true'
run "POST coin_insert (arg fallback)" \
    "curl -s -X POST $BASE/api/control -H 'Content-Type: application/json' -d '{\"action\":\"coin_insert\",\"arg\":\"25\"}'" \
    '"success":true'

# Control: keypad_press with key
run "POST keypad_press (key)" \
    "curl -s -X POST $BASE/api/control -H 'Content-Type: application/json' -d '{\"action\":\"keypad_press\",\"key\":\"5\"}'" \
    '"success":true'
run "POST keypad_press (arg fallback)" \
    "curl -s -X POST $BASE/api/control -H 'Content-Type: application/json' -d '{\"action\":\"keypad_press\",\"arg\":\"1\"}'" \
    '"success":true'

# Reset and handset_down
run "POST reset_system" \
    "curl -s -X POST $BASE/api/control -H 'Content-Type: application/json' -d '{\"action\":\"reset_system\"}'" \
    '"success":true'
run "POST handset_down" \
    "curl -s -X POST $BASE/api/control -H 'Content-Type: application/json' -d '{\"action\":\"handset_down\"}'" \
    '"success":true'

# Plugin switch
run "POST activate_plugin" \
    "curl -s -X POST $BASE/api/control -H 'Content-Type: application/json' -d '{\"action\":\"activate_plugin\",\"plugin\":\"Classic Phone\"}'" \
    '"success":true'

echo ""
if [ $FAILED -eq 0 ]; then
    echo "All API tests passed."
    exit 0
else
    echo "$FAILED test(s) failed."
    exit 1
fi
