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
