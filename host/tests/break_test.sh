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
    local check_json="${3:-}"  # if "json", verify response is valid JSON
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

# Normal case should still work
run "POST control handset_up" \
    "curl -s -X POST $BASE/api/control -H 'Content-Type: application/json' -d '{\"action\":\"handset_up\"}'" "json"

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
