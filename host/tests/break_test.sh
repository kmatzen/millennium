#!/usr/bin/env bash
# Break/fuzz experiments against live daemon API
# Usage: ./break_test.sh [HOST]
# Verifies: daemon doesn't crash, JSON responses stay valid
set -e
HOST="${HOST:-${1:-192.168.86.145}}"
BASE="http://${HOST}:8081"
FAILED=0

# Verify JSON is parseable (python -c "import json; json.loads(...)")
valid_json() {
    echo "$1" | python3 -c "import sys,json; json.load(sys.stdin)" 2>/dev/null
}

run() {
    local name="$1"
    local cmd="$2"
    local check_json="${3:-}"  # "json"=valid JSON; "reject"=valid JSON and success=false; "accept"=valid JSON and success=true
    local output
    output=$(eval "$cmd" 2>&1) || true
    if [ "$check_json" = "json" ]; then
        if valid_json "$output"; then
            echo "  PASS: $name (valid JSON)"
        else
            echo "  FAIL: $name (invalid JSON)"
            printf "    got: %.120s\n" "$output"
            FAILED=$((FAILED + 1))
        fi
    elif [ "$check_json" = "reject" ]; then
        if valid_json "$output"; then
            if echo "$output" | python3 -c "import sys,json; d=json.load(sys.stdin); sys.exit(0 if d.get('success')==False else 1)" 2>/dev/null; then
                echo "  PASS: $name (rejected as expected)"
            else
                echo "  FAIL: $name (expected success=false)"
                printf "    got: %.120s\n" "$output"
                FAILED=$((FAILED + 1))
            fi
        else
            echo "  FAIL: $name (invalid JSON)"
            printf "    got: %.120s\n" "$output"
            FAILED=$((FAILED + 1))
        fi
    elif [ "$check_json" = "accept" ]; then
        if valid_json "$output"; then
            if echo "$output" | python3 -c "import sys,json; d=json.load(sys.stdin); sys.exit(0 if d.get('success')==True else 1)" 2>/dev/null; then
                echo "  PASS: $name (accepted as expected)"
            else
                echo "  FAIL: $name (expected success=true)"
                printf "    got: %.120s\n" "$output"
                FAILED=$((FAILED + 1))
            fi
        else
            echo "  FAIL: $name (invalid JSON)"
            printf "    got: %.120s\n" "$output"
            FAILED=$((FAILED + 1))
        fi
    else
        echo "  CHECK: $name"
    fi
}

echo "=== Break experiments against $BASE ==="

# Connectivity
if ! curl -s -m 3 "$BASE/api/state" >/dev/null 2>&1; then
    echo "Cannot reach $BASE - is daemon running?"
    exit 1
fi

# JSON injection: control response must stay valid when input has quotes
run "POST control with quote in action" \
    "curl -s -X POST $BASE/api/control -H 'Content-Type: application/json' -d '{\"action\":\"\\\"}'" "json"

run "POST control with quote in plugin name" \
    "curl -s -X POST $BASE/api/control -H 'Content-Type: application/json' -d '{\"action\":\"activate_plugin\",\"plugin\":\"Classic\\\"Phone\"}'" "json"

# Normal case should succeed (success=true)
run "POST control handset_up" \
    "curl -s -X POST $BASE/api/control -H 'Content-Type: application/json' -d '{\"action\":\"handset_up\"}'" "accept"

# Config/Health/Logs/Plugins/Version APIs must return valid JSON (#98, #99, #112)
run "GET /api/config returns valid JSON" "curl -s $BASE/api/config" "json"
run "GET /api/health returns valid JSON" "curl -s $BASE/api/health" "json"
run "GET /api/logs returns valid JSON" "curl -s '$BASE/api/logs?level=INFO&max_entries=5'" "json"
run "GET /api/plugins returns valid JSON" "curl -s $BASE/api/plugins" "json"
run "GET /api/version returns valid JSON" "curl -s $BASE/api/version" "json"
run "GET /api/metrics returns valid JSON" "curl -s $BASE/api/metrics" "json"
run "GET /api/check-update returns valid JSON" "curl -s $BASE/api/check-update" "json"

# coin_insert validation (#129): reject invalid cents, accept valid
run "POST coin_insert invalid cents (negative)" \
    "curl -s -X POST $BASE/api/control -H 'Content-Type: application/json' -d '{\"action\":\"coin_insert\",\"cents\":-5}'" "reject"
run "POST coin_insert invalid cents (huge)" \
    "curl -s -X POST $BASE/api/control -H 'Content-Type: application/json' -d '{\"action\":\"coin_insert\",\"cents\":999999}'" "reject"
run "POST coin_insert valid" \
    "curl -s -X POST $BASE/api/control -H 'Content-Type: application/json' -d '{\"action\":\"coin_insert\",\"cents\":25}'" "accept"

# Control rejection during call (#97, #102): start_call and activate_plugin rejected when call active
# Setup: handset_up, start_call (puts daemon in CALL_INCOMING)
curl -s -X POST "$BASE/api/control" -H 'Content-Type: application/json' -d '{"action":"handset_up"}' >/dev/null || true
run "start_call when idle (succeeds)" \
    "curl -s -X POST $BASE/api/control -H 'Content-Type: application/json' -d '{\"action\":\"start_call\"}'" "accept"
run "start_call during call (rejected #102)" \
    "curl -s -X POST $BASE/api/control -H 'Content-Type: application/json' -d '{\"action\":\"start_call\"}'" "reject"
run "activate_plugin during call (rejected #97)" \
    "curl -s -X POST $BASE/api/control -H 'Content-Type: application/json' -d '{\"action\":\"activate_plugin\",\"plugin\":\"Jukebox\"}'" "reject"
# Cleanup: reset so daemon returns to idle
curl -s -X POST "$BASE/api/control" -H 'Content-Type: application/json' -d '{"action":"reset_system"}' >/dev/null || true

# Bad input - daemon should not crash
run "POST control invalid JSON" "curl -s -X POST $BASE/api/control -H 'Content-Type: application/json' -d 'not json'"
run "POST control empty body" "curl -s -X POST $BASE/api/control -H 'Content-Type: application/json' -d ''"
run "GET path traversal" "curl -s -o /dev/null -w '%{http_code}' '$BASE/../../../etc/passwd'"

echo ""
if [ $FAILED -eq 0 ]; then
    echo "All break tests passed."
else
    echo "$FAILED test(s) failed."
fi
exit $FAILED
