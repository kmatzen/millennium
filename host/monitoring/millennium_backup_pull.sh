#!/bin/bash
# Run on the backup host. Pull a fixed archive through the phone's restricted
# SSH key, commit it to Restic, apply retention, then acknowledge success back
# to the phone so its local monitor can measure off-device backup freshness.
set -euo pipefail

config_root="${MILLENNIUM_BACKUP_CONFIG_ROOT:-$HOME/.config/millennium-backup}"
repository="${MILLENNIUM_BACKUP_REPOSITORY:-$HOME/backups/millennium/phone-001-restic}"
phone_host="${MILLENNIUM_BACKUP_PHONE_HOST:-127.0.0.1}"
phone_port="${MILLENNIUM_BACKUP_PHONE_PORT:-22022}"
phone_user="${MILLENNIUM_BACKUP_PHONE_USER:-matzen}"
export RESTIC_PASSWORD_FILE="${RESTIC_PASSWORD_FILE:-$config_root/restic-password}"

ssh_args=(
    -T -p "$phone_port"
    -i "$config_root/pull-key"
    -o IdentitiesOnly=yes
    -o BatchMode=yes
    -o StrictHostKeyChecking=yes
    -o UserKnownHostsFile="$config_root/phone-known-hosts"
    "$phone_user@$phone_host"
)

ssh "${ssh_args[@]}" backup | \
    restic -r "$repository" backup --stdin --stdin-filename phone-001.tar \
        --tag millennium-phone --tag phone-001

restic -r "$repository" forget --tag phone-001 --keep-daily 14 \
    --keep-weekly 8 --keep-monthly 12 --prune

ssh "${ssh_args[@]}" ack
