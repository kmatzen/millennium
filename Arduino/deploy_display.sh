#!/usr/bin/env bash
# Build (on macOS), sync, then flash display firmware on the remote device (Pi).
# Uses millennium_beta FQBN so the display Arduino keeps its "Millennium Beta" USB identity.
#
# Usage: ./deploy_display.sh [user@host]
#   Default host from DEVICE_TEST.md: matzen@192.168.86.145
#
# Env: BRANCH=      deploy specific branch on remote (e.g. fix/issue-59-serial-keepalive)
#      REPO_DIR=    repo path on remote (default: millennium)
#      SKIP_BUILD=1 skip local build (hex already committed)
#      VIA_SCP=1    copy hex via scp instead of git (use when hex not yet pushed)
#      FLASH_PORT=  avrdude port on remote (default: /dev/ttyACM0 when in bootloader)

set -e

REMOTE="${1:-matzen@192.168.86.145}"
REPO_DIR="${REPO_DIR:-millennium}"
BRANCH="${BRANCH:-}"
SKIP_BUILD="${SKIP_BUILD:-0}"
VIA_SCP="${VIA_SCP:-0}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

echo "Deploying display firmware (Millennium Beta) to $REMOTE"

# Step 1: Build on macOS (BUILD_CONFIG=0 skips arduino-cli.yaml if it has Linux-only paths)
if [ "$SKIP_BUILD" != "1" ]; then
  echo "  Step 1: Building display sketch on this machine..."
  cd "$SCRIPT_DIR"
  if ! BUILD_CONFIG=0 make build_display 2>/dev/null; then
    make build_display
  fi
  cd "$REPO_ROOT"
  if ! git diff --quiet Arduino/build/display/display.ino.hex 2>/dev/null; then
    if [ "$VIA_SCP" = "1" ]; then
      echo "    Hex changed - will copy via scp (VIA_SCP=1)"
    else
      echo "    Hex changed - add, commit, push, then re-run; or use VIA_SCP=1 to scp hex"
      echo "      git add Arduino/build/display/display.ino.hex && git commit -m '...' && git push"
      exit 1
    fi
  fi
else
  echo "  Step 1: Skipping build (SKIP_BUILD=1)"
fi

# Step 2: Sync (git pull on remote, or scp hex if VIA_SCP)
echo "  Step 2: Syncing..."
if [ "$VIA_SCP" = "1" ] && [ -f "$SCRIPT_DIR/build/display/display.ino.hex" ]; then
  ssh "$REMOTE" "mkdir -p $REPO_DIR/Arduino/build/display"
  scp "$SCRIPT_DIR/build/display/display.ino.hex" "$REMOTE:$REPO_DIR/Arduino/build/display/"
else
  if [ -n "$BRANCH" ]; then
    ssh "$REMOTE" "cd $REPO_DIR && git fetch origin && git checkout $BRANCH && git pull"
  else
    ssh "$REMOTE" "cd $REPO_DIR && git pull"
  fi
fi

# Step 3: Flash on remote. Triggers bootloader via 1200-baud (no physical touch needed).
# Uses avrdude; FLASH_PORT is display Arduino's normal port (e.g. /dev/serial/by-id/... or /dev/ttyACM0).
echo "  Step 3: Flashing display Arduino on $REMOTE..."
ssh "$REMOTE" "cd $REPO_DIR/Arduino && PATH=\$HOME/bin:\$PATH make flash_display FLASH_PORT=\${FLASH_PORT:-/dev/ttyACM0}"

echo "Done."
