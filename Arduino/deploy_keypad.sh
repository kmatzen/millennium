#!/usr/bin/env bash
# Build (on macOS), sync, then flash keypad firmware on the remote device (Pi).
# Uses GPIO17/GEN0 (pin 11) to assert Arduino Alpha's reset pin directly.
#
# Usage: ./deploy_keypad.sh [user@host]
#   Default host: matzen@192.168.86.145
#
# Env: BRANCH=      deploy specific branch on remote
#      REPO_DIR=    repo path on remote (default: millennium)
#      SKIP_BUILD=1 skip local build (hex already committed)
#      VIA_SCP=1    copy hex via scp instead of git (use when hex not yet pushed)

set -e

REMOTE="${1:-matzen@192.168.86.145}"
REPO_DIR="${REPO_DIR:-millennium}"
BRANCH="${BRANCH:-}"
SKIP_BUILD="${SKIP_BUILD:-0}"
VIA_SCP="${VIA_SCP:-0}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# GPIO pin on Pi connected to Arduino Alpha RST (GPIO17/GEN0, pin 11)
RESET_GPIO=17

echo "Deploying keypad firmware (Millennium Alpha) to $REMOTE"

# Step 1: Build on macOS
if [ "$SKIP_BUILD" != "1" ]; then
  echo "  Step 1: Building keypad sketch..."
  cd "$SCRIPT_DIR"
  if ! BUILD_CONFIG=0 make build 2>/dev/null; then
    make build
  fi
  cd "$REPO_ROOT"
  if ! git diff --quiet Arduino/build/keypad/keypad.ino.hex 2>/dev/null; then
    if [ "$VIA_SCP" = "1" ]; then
      echo "    Hex changed - will copy via scp (VIA_SCP=1)"
    else
      echo "    Hex changed - commit and push first, or use VIA_SCP=1"
      echo "      git add Arduino/build/keypad/keypad.ino.hex && git commit -m '...' && git push"
      exit 1
    fi
  fi
else
  echo "  Step 1: Skipping build (SKIP_BUILD=1)"
fi

# Step 2: Sync
echo "  Step 2: Syncing..."
if [ "$VIA_SCP" = "1" ] && [ -f "$SCRIPT_DIR/build/keypad/keypad.ino.hex" ]; then
  ssh "$REMOTE" "mkdir -p $REPO_DIR/Arduino/build/keypad"
  scp "$SCRIPT_DIR/build/keypad/keypad.ino.hex" "$REMOTE:$REPO_DIR/Arduino/build/keypad/"
else
  if [ -n "$BRANCH" ]; then
    ssh "$REMOTE" "cd $REPO_DIR && git fetch origin && git checkout $BRANCH && git pull --ff-only || git pull || true"
  else
    ssh "$REMOTE" "cd $REPO_DIR && git pull --ff-only || git pull || true"
  fi
fi

# Step 3: Flash via the on-Pi orchestrator. pi_flash.sh runs entirely on the Pi,
# so there's no SSH latency between detecting the Caterina bootloader and
# launching avrdude (that ~750ms window is easily missed when the steps are split
# across separate ssh calls). It resets, detects, flashes with retries and a
# timeout, and always restarts the daemon on exit.
echo "  Step 3: Flashing keypad Arduino (Alpha) on $REMOTE via pi_flash.sh..."
scp "$SCRIPT_DIR/pi_flash.sh" "$REMOTE:/tmp/pi_flash.sh"
ssh "$REMOTE" "bash -l -c 'bash /tmp/pi_flash.sh keypad $REPO_DIR/Arduino/build/keypad/keypad.ino.hex'"

echo "Done."
