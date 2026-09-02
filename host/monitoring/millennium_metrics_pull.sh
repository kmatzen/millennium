#!/bin/bash
# Pull the phone's fixed Prometheus textfile through its restricted reverse-SSH
# key and atomically expose it to anima's node_exporter textfile collector.
set -euo pipefail

config_root="${MILLENNIUM_BACKUP_CONFIG_ROOT:-$HOME/.config/millennium-backup}"
output="${MILLENNIUM_METRICS_OUTPUT:-$HOME/selfhosted/prometheus/node-exporter-textfile/millennium-phone.prom}"
phone_host="${MILLENNIUM_BACKUP_PHONE_HOST:-127.0.0.1}"
phone_port="${MILLENNIUM_BACKUP_PHONE_PORT:-22022}"
phone_user="${MILLENNIUM_BACKUP_PHONE_USER:-matzen}"
output_dir="$(dirname "$output")"
temporary="$(mktemp "$output_dir/.millennium-phone.XXXXXX")"
trap 'rm -f "$temporary"' EXIT

ssh -T -p "$phone_port" \
    -i "$config_root/pull-key" \
    -o IdentitiesOnly=yes -o BatchMode=yes -o StrictHostKeyChecking=yes \
    -o UserKnownHostsFile="$config_root/phone-known-hosts" \
    "$phone_user@$phone_host" metrics >"$temporary"

grep -q '^millennium_last_checkin_timestamp_seconds ' "$temporary"
chmod 0644 "$temporary"
mv -f "$temporary" "$output"
trap - EXIT
