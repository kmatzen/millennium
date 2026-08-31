#!/bin/bash
# Encrypted, deduplicated appliance backup. Credentials and repository location
# are supplied by /etc/millennium/backup.env, never command-line arguments.
set -euo pipefail

if [[ ! -r /etc/millennium/backup.env ]]; then
    echo "missing /etc/millennium/backup.env" >&2
    exit 1
fi

# shellcheck source=/dev/null
source /etc/millennium/backup.env
: "${RESTIC_REPOSITORY:?RESTIC_REPOSITORY is required}"
: "${RESTIC_PASSWORD_FILE:?RESTIC_PASSWORD_FILE is required}"
export RESTIC_CACHE_DIR=/var/lib/millennium/backup/cache

paths=(/etc/millennium /var/lib/millennium/ota)
# These reconstruct outbound maintenance after a failed system disk.  The
# canonical authorized key is stored under /etc/millennium by provisioning, so
# this root-only backup never needs permission to traverse a user's home.
[[ -f /etc/wireguard/millennium.conf ]] && paths+=(/etc/wireguard/millennium.conf)
[[ -f /etc/ssh/sshd_config.d/60-millennium-maintenance.conf ]] && \
    paths+=(/etc/ssh/sshd_config.d/60-millennium-maintenance.conf)
[[ -d /etc/cloudflared ]] && paths+=(/etc/cloudflared)

restic backup --one-file-system --tag millennium-phone "${paths[@]}"
restic forget --tag millennium-phone --keep-daily 14 --keep-weekly 8 \
    --keep-monthly 12 --prune
install -d -m 0700 /var/lib/millennium/backup
touch /var/lib/millennium/backup/last-success
