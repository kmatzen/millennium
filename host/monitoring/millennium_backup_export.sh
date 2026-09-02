#!/bin/bash
# Fixed-purpose command used by anima's restricted pull-only SSH key. The
# forced command passes SSH_ORIGINAL_COMMAND through this dispatcher; no shell
# input or arbitrary path is accepted.
set -euo pipefail

case "${SSH_ORIGINAL_COMMAND:-}" in
backup)
    paths=(etc/millennium var/lib/millennium/ota var/lib/millennium/content)
    for optional in \
        etc/wireguard/millennium.conf \
        etc/ssh/sshd_config.d/60-millennium-maintenance.conf \
        etc/systemd/system/millennium-maintenance-tunnel.service; do
        [[ -e "/$optional" ]] && paths+=("$optional")
    done

    exec sudo -n /bin/tar --numeric-owner --xattrs --acls \
        --warning=no-file-changed -C / -cf - "${paths[@]}"
    ;;
ack)
    # Anima sends this only after Restic has committed and retention succeeds.
    sudo -n /usr/bin/install -d -m 0700 /var/lib/millennium/backup
    sudo -n /usr/bin/touch /var/lib/millennium/backup/last-success
    ;;
metrics)
    # Prometheus text format only; never expose daemon configuration or tokens.
    exec sudo -n /bin/cat \
        /var/lib/node_exporter/textfile_collector/millennium.prom
    ;;
*)
    echo "unsupported backup command" >&2
    exit 2
    ;;
esac
