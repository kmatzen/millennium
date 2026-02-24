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
    ssh "$REMOTE" "cd $REPO_DIR && git fetch origin && git checkout $BRANCH && git pull --ff-only || git pull || true"
else
    ssh "$REMOTE" "cd $REPO_DIR && git pull --ff-only || git pull || true"
fi
fi

# Step 3: Flash on remote. Stop daemon, power-cycle display Arduino via uhubctl (Huasheng hub
# supports per-port toggle), trigger bootloader via 1200-baud, then avrdude. Use by-id path
# since tty number can change after uhubctl cycle. Restart daemon after.
FP="${FLASH_PORT:-/dev/serial/by-id/usb-Arduino_LLC_Millennium_Beta-if00}"
echo "  Step 3: Flashing display Arduino on $REMOTE..."
ssh "$REMOTE" "
  sudo systemctl stop daemon.service 2>/dev/null || true
  sleep 2
  if command -v uhubctl >/dev/null 2>&1; then
    echo '  Power-cycling display Arduino (uhubctl port 2 on Huasheng hub)...'
    sudo uhubctl -l 1-1 -a cycle -p 2 -f
    echo '  Waiting for device to reappear...'
    for i in 1 2 3 4 5 6 7 8 9 10; do
      [ -e $FP ] && break
      sleep 1
    done
  else
    sleep 2
  fi
  cd $REPO_DIR/Arduino
  SKETCH_PORT=\$(readlink -f $FP 2>/dev/null || echo $FP)
  [ -z \"\$SKETCH_PORT\" ] && SKETCH_PORT=$FP
  before=\$(ls -1 /dev/ttyACM* 2>/dev/null | sort -u)
  echo \"  Triggering bootloader (1200 baud on \$SKETCH_PORT)...\"
  if command -v python3 >/dev/null 2>&1; then
    python3 -c \"import serial,time; s=serial.Serial('\$SKETCH_PORT',1200); s.setDTR(False); s.close(); time.sleep(0.5)\" 2>/dev/null || true
  else
    stty -F \$SKETCH_PORT 1200 2>/dev/null || true
    sleep 0.5
  fi
  BOOT_PORT=
  for i in \$(seq 1 40); do
    [ -e \"\$SKETCH_PORT\" ] || break
    sleep 0.05
  done
  for i in \$(seq 1 100); do
    if [ -e \"\$SKETCH_PORT\" ]; then
      BOOT_PORT=\$SKETCH_PORT
      break
    fi
    after=\$(ls -1 /dev/ttyACM* 2>/dev/null | sort -u)
    new=\$(comm -13 <(echo \"\$before\") <(echo \"\$after\") 2>/dev/null | head -1)
    [ -n \"\$new\" ] && [ -e \"\$new\" ] && BOOT_PORT=\"\$new\" && break
    sleep 0.05
  done
  if [ -z \"\$BOOT_PORT\" ]; then
    echo '  Bootloader port not detected, falling back to sketch port (may fail)'
    BOOT_PORT=\$SKETCH_PORT
  else
    echo \"  Bootloader on \$BOOT_PORT\"
  fi
  make flash_display FLASH_PORT=\"\$BOOT_PORT\" SKIP_TRIGGER=1
  rc=\$?
  sudo systemctl start daemon.service 2>/dev/null || true
  exit \$rc
"

echo "Done."
