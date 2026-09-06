# Appliance monitoring and backup

`millennium-monitor.timer` samples the loopback health, state, and metrics APIs
once per minute and writes a Prometheus node-exporter textfile. It also checks
the daemon and maintenance-tunnel units, signed-OTA status, serial disconnects,
MCU reset reports, disk space, recent kernel filesystem errors, reboot time,
update-site certificate lifetime, and backup freshness. A failed check makes
the oneshot unit fail as well as emitting its metric, so `OnFailure=` or the
system journal can be used even without Prometheus.

Install `alerts.yml` on the monitoring server and scrape the phone's node
exporter through the authenticated maintenance path. The `last_checkin` alert
must be evaluated off-device; a dead phone cannot alert about itself.

`millennium-hil-smoke.timer` runs nightly against the installed hardware. It
requires both stable MCU device paths, framed-protocol negotiation, matching
host/release versions, healthy daemon and maintenance services, and a
non-failed OTA state. Its atomic JSON result is monitored for failure and
staleness. The test is intentionally non-destructive: it does not place calls,
accept coins, or rewrite story state.

## Physical interruption evidence

`millennium-physical-interruption` creates a durable checkpoint before an
operator deliberately interrupts power or networking. It records boot ID,
active host/content links, installed sequence, firmware digests, OTA state,
service state, and the full HIL result. Only one scenario can be armed at once:

```bash
sudo millennium-physical-interruption arm --scenario idle_power_loss
# Perform exactly the printed physical action, then restore power.
sudo millennium-physical-interruption status
```

The boot service automatically reconciles reboot-required scenarios. For
network-only and measured-load scenarios, run
`sudo millennium-physical-interruption reconcile` after restoring the normal
condition. Evidence is retained under
`/var/lib/millennium/physical-tests/evidence/` and failed attempts never
overwrite one another.

The harness does not mark a physical test accepted. It records
`physical_observation_required: true`; the operator must still document the
instrument/load, minimum voltage where applicable, observed behavior, and use
`tools/as_built_record.py record-test`. An expected OTA rollback counts as
system recovery only when the prior host release is restored and, for an MCU
flash interruption, both prior firmware digests are restored.

### Isolated OTA-download interruption

Use `tools/physical_ota_download_probe.py` when the production manifest is
already current and a destructive reinstallation would add risk without
improving the network evidence. The probe downloads the unchanged production
manifest and detached signature, validates both through the installed OTA
worker and trust store, then downloads and hashes the referenced bundle. It
never calls the install or activation commands.

For a Pi with an independent Wi-Fi maintenance path, connect its Ethernet
adapter and the operator laptop to an isolated router. Run an SSH TLS byte
forward on the laptop and put `tools/rate_limited_tcp_relay.py` between that
forward and the Pi. The rate limit belongs on the Pi-facing relay—not in the
downloader—so upstream or kernel buffers cannot complete the transfer after
the cable is removed. The relay binds only to the isolated laptop address and
the probe overrides DNS in-process; neither `/etc/hosts`, firewall rules, nor
the published manifest are changed.

Arm `ota_download_interruption`, begin the paced probe, and confirm its JSON
reports `phase: bundle-download` before physically removing only the RJ45
cable. Valid interruption evidence requires all of the following:

- the direct Ethernet address becomes unreachable;
- the probe terminates with `phase: interrupted` before receiving the signed
  bundle's declared byte count;
- the independent maintenance tunnel remains active;
- reconnecting RJ45 restores the direct address without a reboot;
- a subsequent probe validates the full bundle size and SHA-256; and
- harness reconciliation passes the full HIL snapshot without changing the
  active release, content target, installed sequence, or firmware identities.

A transfer that completes from buffered bytes after link loss is diagnostic
only and must not be entered into the as-built acceptance record.

## Encrypted backup

Install `restic`, copy `backup.env.example` to
`/etc/millennium/backup.env`, create the referenced password file with mode
`0600`, initialize the remote repository, and run:

```sh
sudo systemctl start millennium-backup.service
sudo systemctl status millennium-backup.service
sudo restic -r "$RESTIC_REPOSITORY" snapshots --tag millennium-phone
```

Only after that successful first run should the timer be enabled. The backup
contains daemon configuration and credentials, signed OTA state/artifacts, the
canonical authorized-maintainer public key, the device WireGuard identity,
the SSH hardening drop-in, and Cloudflare maintenance configuration when
present. Restic encrypts all content
before it leaves the phone and applies daily, weekly, and monthly retention.
Keep the repository credential and restic password in the offline recovery
record; losing both copies makes the backup intentionally unrecoverable.

Quarterly, restore the newest snapshot into an empty temporary directory on a
different machine and record the snapshot ID, date, and verification result.
An untested repository is not considered a backup.

### Pull-based backup host

For a phone reachable only through its reverse maintenance tunnel, install
`millennium_backup_pull.sh` on the backup host. The phone's authorized key must
be restricted to `millennium-backup-export`. The pull script can request only
`backup` and `ack`: it streams the fixed archive into Restic, applies retention,
and acknowledges the phone only after both operations succeed. That acknowledgement
updates `/var/lib/millennium/backup/last-success`, which is the freshness stamp
consumed by `millennium-monitor`.

Install `millennium_metrics_pull.sh` and its user timer on the same host to pull
the monitor's fixed Prometheus textfile through that restricted key. The export
command cannot read arbitrary files, and the pull validates the check-in metric
before atomically replacing anima's node-exporter textfile. This avoids a stale
LAN address and continues working wherever the phone's reverse tunnel connects.

The same nightly service must also run `millennium_server_state_backup.py`.
That helper streams an allowlisted archive directly into the existing encrypted
Restic repository—no plaintext archive is staged—and immediately streams the
saved archive back through `tar -t` to prove every required path is restorable.
It covers the update origin, doormand configuration, maintenance systemd units
and scripts, authorized public keys, and an installed copy of the recovery
instructions. The service refuses to acknowledge overall backup success if a
required path is missing or restore verification fails. Its latest evidence is
`~/.config/millennium-backup/server-state-last.json`.

Each phone stream carries a unique per-run Restic tag. If SSH fails after
Restic observes EOF, the wrapper resolves only snapshots with that run tag and
forgets their explicit IDs, preventing an empty or truncated stdin snapshot
from appearing valid. A successful phone snapshot is dumped back through
`tar -t` before retention or acknowledgement. The independent server-state
backup still runs when the phone is unavailable, but the combined service
correctly remains failed until both required legs succeed.

Install the backup-host components and recovery documentation with permissions
that keep the Restic credential private:

```bash
install -m 0755 host/monitoring/millennium_backup_pull.sh \
  ~/.local/bin/millennium-backup-pull
install -m 0755 host/monitoring/millennium_server_state_backup.py \
  ~/.local/bin/millennium-server-state-backup
install -d -m 0700 ~/.local/share/millennium-recovery
install -m 0600 host/docs/UNATTENDED_APPLIANCE.md \
  host/docs/SIGNING_KEY_LIFECYCLE.md ~/.local/share/millennium-recovery/
install -m 0644 host/monitoring/millennium-backup-pull.service \
  host/monitoring/millennium-backup-pull.timer ~/.config/systemd/user/
systemctl --user daemon-reload
systemctl --user enable --now millennium-backup-pull.timer
systemctl --user start millennium-backup-pull.service
```
