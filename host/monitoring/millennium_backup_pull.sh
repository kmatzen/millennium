#!/bin/bash
# Run on the backup host. Pull a fixed archive through the phone's restricted
# SSH key, commit it to Restic, apply retention, then acknowledge success back
# to the phone so its local monitor can measure off-device backup freshness.
set -euo pipefail

config_root="${MILLENNIUM_BACKUP_CONFIG_ROOT:-$HOME/.config/millennium-backup}"
repository="${MILLENNIUM_BACKUP_REPOSITORY:-$HOME/backups/millennium/phone-001-restic}"
phone_host="${MILLENNIUM_BACKUP_PHONE_HOST:-127.0.0.1}"
phone_port="${MILLENNIUM_BACKUP_PHONE_PORT:-22022}"
phone_user="${MILLENNIUM_BACKUP_PHONE_USER:-millennium}"
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

run_tag="millennium-phone-run-$(date -u +%Y%m%dT%H%M%SZ)-$$"
phone_ok=1

forget_run_snapshot() {
    # A producer can fail after Restic has observed EOF and committed the
    # partial stdin object. The per-run tag makes cleanup exact and safe even
    # if another backup is running concurrently.
    snapshot_ids=$(restic -r "$repository" snapshots --json --tag "$run_tag" \
        2>/dev/null | python3 -c \
        'import json,sys; print(" ".join(v["id"] for v in json.load(sys.stdin)))') \
        || return 0
    for snapshot_id in $snapshot_ids; do
        restic -r "$repository" forget "$snapshot_id" >/dev/null 2>&1 || true
    done
}

if ! ssh "${ssh_args[@]}" backup | \
    restic -r "$repository" backup --stdin --stdin-filename phone-001.tar \
        --tag millennium-phone --tag phone-001 --tag "$run_tag"
then
    echo "ERROR: phone backup export failed; removing this run's snapshot" >&2
    forget_run_snapshot
    phone_ok=0
else
    snapshot_id=$(restic -r "$repository" snapshots --latest 1 --json \
        --tag "$run_tag" | python3 -c \
        'import json,sys; v=json.load(sys.stdin); assert len(v)==1 and v[0].get("id"); print(v[0]["id"])')
    if ! restic -r "$repository" dump "$snapshot_id" phone-001.tar | \
        tar -tf - >/dev/null
    then
        echo "ERROR: phone backup restore-stream validation failed" >&2
        forget_run_snapshot
        phone_ok=0
    fi
fi

server_backup="${MILLENNIUM_SERVER_BACKUP_COMMAND:-$HOME/.local/bin/millennium-server-state-backup}"
if [[ ! -x "$server_backup" ]]; then
    echo "ERROR: required Millennium server-state backup command is missing" >&2
    exit 1
fi
server_ok=1
if ! "$server_backup" \
        --repository "$repository" --password-file "$RESTIC_PASSWORD_FILE" \
        --server-id anima \
        --evidence "$config_root/server-state-last.json"
then
    server_ok=0
fi

if (( phone_ok )); then
    restic -r "$repository" forget --tag phone-001 --keep-daily 14 \
        --keep-weekly 8 --keep-monthly 12 --prune
    if ! ssh "${ssh_args[@]}" ack; then
        phone_ok=0
    fi
fi

if (( ! phone_ok || ! server_ok )); then
    exit 1
fi
