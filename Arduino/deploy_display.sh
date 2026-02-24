#!/usr/bin/env bash
# Sync repo and flash display Arduino firmware on the remote device (Pi).
# Uses millennium_beta FQBN so the display Arduino keeps its "Millennium Beta" USB identity.
# Usage: ./deploy_display.sh [user@host]
#   Default host from DEVICE_TEST.md: matzen@192.168.86.145

set -e

REMOTE="${1:-matzen@192.168.86.145}"
REPO_DIR="${REPO_DIR:-millennium}"
BRANCH="${BRANCH:-}"

echo "Deploying display firmware (Millennium Beta) to $REMOTE (repo: $REPO_DIR)"
echo "  Step 1: Syncing repo (git fetch + checkout + pull)..."
if [ -n "$BRANCH" ]; then
  ssh "$REMOTE" "cd $REPO_DIR && git fetch origin && git checkout $BRANCH && git pull"
else
  ssh "$REMOTE" "cd $REPO_DIR && git pull"
fi

echo "  Step 2: Building and flashing display Arduino (arduino:avr:millennium_beta)..."
ssh "$REMOTE" "cd $REPO_DIR/Arduino && make install_display"

echo "Done."
