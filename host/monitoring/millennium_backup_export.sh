#!/bin/bash
# Fixed-output backup command used by anima's restricted pull-only SSH key.
set -euo pipefail

paths=(etc/millennium var/lib/millennium/ota var/lib/millennium/content)
for optional in \
    etc/wireguard/millennium.conf \
    etc/ssh/sshd_config.d/60-millennium-maintenance.conf \
    etc/systemd/system/millennium-maintenance-tunnel.service; do
    [[ -e "/$optional" ]] && paths+=("$optional")
done

exec sudo -n /bin/tar --numeric-owner --xattrs --acls \
    --warning=no-file-changed -C / -cf - "${paths[@]}"
