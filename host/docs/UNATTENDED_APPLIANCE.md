# Unattended deployment: OTA and remote maintenance

The deployed phone is an appliance. Its owner should only need to provide power
and Wi-Fi; software updates and maintenance must not require access to the
owner's router or knowledge of Linux.

This document defines the production implementation. The daemon deliberately
has no `git pull` fallback: a missing worker or trust key causes a closed,
reported failure.

## Goals

- The phone makes outbound connections only. No public inbound port or home
  router configuration is required.
- Only a release signed by the Millennium release key can be installed.
- A release contains the host application and both Arduino firmwares.
- Configuration, SIP credentials, state, and logs survive every update.
- Power loss or a bad host build returns to the last known-good release.
- Arduino firmware is flashed only when its digest changes. A failed flash is
  recoverable remotely and never makes an unverified host release current.
- Maintenance SSH is available only over a private device-management network.

## Service names

| Purpose | Name |
| --- | --- |
| Release origin | `updates.kmatzen.com` |
| Management VPN endpoint | `maintenance.kmatzen.com` |

Both names may initially resolve to the same server, but they are separate
security boundaries and should remain separately configurable.

## Signed release format

The phone polls:

```text
https://updates.kmatzen.com/millennium/stable/manifest.json
https://updates.kmatzen.com/millennium/stable/manifest.json.sig
```

`manifest.json` is canonical JSON and contains no floating URLs:

```json
{
  "schema": 1,
  "channel": "stable",
  "key_id": "release-2026",
  "version": "0.4.0",
  "sequence": 4,
  "published_at": "2026-09-01T00:00:00Z",
  "minimum_sequence": 0,
  "bundle": {
    "url": "https://updates.kmatzen.com/millennium/releases/00000004-0.4.0/millennium-00000004-0.4.0.tar.gz",
    "sha256": "<64 lowercase hex characters>",
    "size": 1234567
  }
}
```

The detached signature is Ed25519. The offline release private key signs the
exact bytes of `manifest.json`; `key_id` selects one of the overlapping public
keys configured in `trusted_keys`. TLS protects privacy and availability,
while those pinned keys determine whether code is trusted.

`sequence` is monotonically increasing and is persisted by the device. The
updater rejects a lower sequence even if the release has a plausible version
number. `minimum_sequence` is signed compatibility metadata and may not exceed
the release sequence; it does not bypass the device's anti-rollback record.

The bundle contains:

```text
release.json
host/millennium-daemon
host/web_portal.html
arduino/keypad.hex
arduino/display.hex
arduino/pi_flash.sh
ota/millennium-ota
```

`release.json` repeats the version, sequence, target architecture, file sizes,
and SHA-256 digest of every payload. The updater verifies the architecture,
outer bundle digest, and all inner file digests before stopping the phone
service. Cross-builds pass `--architecture` explicitly to the release builder.

## Device layout and atomic host rollback

Mutable data is never stored in a release directory:

```text
/opt/millennium/releases/00000003-0.3.0/
/opt/millennium/releases/00000004-0.4.0/
/opt/millennium/current -> /opt/millennium/releases/00000004-0.4.0
/opt/millennium/previous -> /opt/millennium/releases/00000003-0.3.0
/etc/millennium/                 configuration and public keys
/var/lib/millennium/             state, installed sequence, firmware digests
/var/log/millennium/             logs
```

The systemd unit executes `/opt/millennium/current/host/millennium-daemon`.
Installation extracts to a new version directory, verifies it, and atomically
replaces the `current` symlink. It never runs `make install` and never overwrites
`/etc/millennium/daemon.conf`.

On first boot of a new release, a health-check service requires all of the
following within a bounded interval:

- `daemon.service` remains active;
- `GET http://127.0.0.1:8081/api/version` reports the expected version;
- `GET http://127.0.0.1:8081/api/health` returns valid JSON;
- both MCU serial devices reconnect and report the signed identities.

Until that succeeds, the release is *pending*. Failure atomically restores the
previous symlink and restarts the daemon. Success marks the sequence committed
and prunes all but the current and previous host releases.

Before stopping the daemon, the worker writes an fsynced activation journal
containing the previous release and every firmware flash it is about to
attempt. `millennium-update-recover.service` runs before `daemon.service` on
boot. If power was lost during activation, it restores the previous host
symlink and reflashes attempted boards from that release before allowing the
daemon to start. The journal is removed only after rollback or a successful
health check.

## Arduino update transaction

The GPIO/Caterina mechanism in `Arduino/pi_flash.sh` remains the flashing
primitive. The OTA worker owns the transaction around it:

1. Verify both hex files before disrupting the daemon.
2. Compare each digest with `/var/lib/millennium/firmware/*.sha256`.
3. Stop `daemon.service` once.
4. Flash Alpha first and verify its application USB identity returns.
5. Flash Beta and verify its application USB identity returns.
6. Record a digest only after that board verifies and reconnects.
7. Activate and health-check the new host release.

The release retains the previous hex files. If a new sketch fails to reconnect,
the worker attempts one flash of the previous hex. Because the boards share the
same stock bootloader identity, they are always reset and flashed one at a time.

An update is deferred while a call is active. A file lock prevents concurrent
automatic, dashboard, and maintenance-triggered updates.

## Polling policy

Two systemd timers drive the device:

- `millennium-update-check.timer`: 15 minutes after boot, then every 6 hours
  with randomized delay. It downloads and verifies only the small manifest.
- `millennium-update-apply.timer`: checks once per hour whether a verified
  release is pending and the phone is idle.

Automatic installation is controlled by `/etc/millennium/ota.conf`:

```ini
channel=stable
manifest_url=https://updates.kmatzen.com/millennium/stable/manifest.json
automatic=true
install_window_start=02:00
install_window_end=05:00
```

The dashboard may request an immediate check or apply, but it invokes the same
privileged worker and cannot provide a URL, filesystem path, shell command, or
unsigned bundle.

## Remote maintenance network

Each phone receives a unique WireGuard private key generated on the phone. Its
public key is registered on the management server. The peer endpoint is
`maintenance.kmatzen.com:51820`, `PersistentKeepalive` is 25 seconds, and the
phone initiates all traffic through the owner's NAT.

The phone firewall permits SSH only on its WireGuard interface and optionally
on the local LAN during initial provisioning. Root login and password login are
disabled. The maintainer key is restricted to the `matzen` account, and sudo is
limited to the installed maintenance/update commands where practical.

The management server must not route arbitrary Internet traffic through a
phone. Give every device a stable address in a dedicated subnet, isolate peers
from each other, and keep an inventory mapping device ID, WireGuard public key,
address, hardware revision, and owner.

If outbound UDP is unavailable, an optional `autossh` unit may expose the
phone's port 22 on a loopback-only port at `maintenance.kmatzen.com` over TCP
443. The server-side key must be restricted with `permitlisten`, no PTY, no
agent forwarding, no command execution, and no access to other reverse
forwards. This is a fallback, not the default path.

## Bootstrap

Before handing over a phone:

1. Install the release-based systemd unit and OTA worker once over local SSH.
2. Provision only the OTA public key; keep the signing private key offline.
3. Generate the device WireGuard key locally and register its public half.
4. Confirm maintenance access through `maintenance.kmatzen.com` from outside
   the provisioning LAN.
5. Install a signed no-op release and prove the health/commit path.
6. Install a deliberately unhealthy test release and prove automatic rollback.
7. Reboot and power-cycle during extraction in a test environment and prove the
   current release remains bootable.
8. Record the recovery procedure and device identity before shipment.

## Server-side minimums

- Serve release files as immutable objects with correct content lengths.
- Publish the signature before atomically replacing the manifest, so a client
  never observes a new manifest without its signature.
- Keep signing separate from web serving; compromise of the web server alone
  must not authorize an update.
- Back up the WireGuard inventory and revoke a lost phone's peer immediately.
- Alert on phones that have not checked in, repeated signature failures,
  rollback, or a firmware recovery attempt.

Full operating-system image updates are a separate phase. Application and MCU
OTA should be proven first; unattended OS A/B updates require partition-level
boot selection and a boot-count watchdog rather than the application symlink
mechanism above.

## Implementation and provisioning commands

Build the daemon and Arduino hex files, generate an offline signing key once,
then export its public half:

```bash
openssl genpkey -algorithm ED25519 -out millennium-release-private.pem
openssl pkey -in millennium-release-private.pem -pubout -out update-signing-key.pem
chmod 0600 millennium-release-private.pem
```

Keep the private file off the web server and off every phone. Build and publish
a release from the repository root:

```bash
python3 tools/build_ota_release.py \
  --sequence 4 --key-id release-2026 \
  --base-url https://updates.kmatzen.com/millennium \
  --private-key /offline/path/millennium-release-private.pem \
  --output-dir /tmp/millennium-0.4.0

sudo tools/publish_ota_release.sh \
  /tmp/millennium-0.4.0 /srv/www/updates.kmatzen.com/millennium

python3 tools/verify_ota_endpoint.py \
  --public-key /path/to/update-signing-key.pem
```

An nginx virtual-host template is provided at
`host/ota/server/nginx-updates.conf.example`. Public DNS must contain an A/AAAA
record for `updates.kmatzen.com`, and its TLS certificate must be valid before
provisioning a phone.

Bootstrap a phone once from its checkout. This preserves existing configuration
and installs the current build as the rollback-capable `bootstrap` release:

```bash
cd host
make daemon
sudo ./ota/install_ota.sh
sudo install -m 0644 /path/to/update-signing-key.pem \
  /etc/millennium/update-signing-key-release-2026.pem
# Add release-2026:/etc/millennium/update-signing-key-release-2026.pem
# to trusted_keys in /etc/millennium/ota.conf before publishing with that ID.
sudo systemctl start millennium-update-check.service
```

Provision the management server and phone:

```bash
# maintenance.kmatzen.com
sudo tools/provision_maintenance_server.sh

# phone; use the public key printed by the previous command
sudo host/ota/provision_maintenance.sh \
  --server-key SERVER_PUBLIC_KEY --address 10.77.0.2/24 \
  --maintainer-key /tmp/maintainer-key.pub

# maintenance server; use the device key printed by the phone
sudo tools/register_maintenance_device.sh phone-001 DEVICE_PUBLIC_KEY 10.77.0.2
ssh matzen@10.77.0.2
```

Where UDP WireGuard ingress is unavailable, the phone can instead maintain a
restricted outbound reverse SSH tunnel. The server key must be authorized with
`restrict,port-forwarding,permitlisten="127.0.0.1:22022"`; administrators then
connect to the server and use `ssh -p 22022 matzen@127.0.0.1`. The phone accepts
no new public inbound port and systemd automatically reconnects the tunnel.

`maintenance.kmatzen.com` must be a DNS-only A/AAAA record pointing at the
WireGuard server; an HTTP reverse proxy cannot proxy WireGuard UDP. Permit UDP
51820 at the server firewall. Administrators first SSH to the management server
and then to the phone's stable `10.77.0.x` address. Device provisioning installs
the maintainer's Ed25519 key, validates the effective sshd configuration, and
disables root, password, and keyboard-interactive SSH login before the phone is
handed over.

If monitoring and restricted backups still traverse the reverse tunnel but all
administrator keys are rejected, use the phone's local console. Put only the
approved public key on removable media, record its fingerprint on a separate
trusted display, and run:

```bash
sudo python3 /path/to/repair_maintenance_access.py \
  --user matzen --key-file /media/KEY.pub \
  --fingerprint SHA256:EXPECTED_FINGERPRINT
```

The repair refuses non-Ed25519 keys, multi-line input, and fingerprint
mismatches. It atomically appends rather than replacing existing keys, validates
`sshd`, and records evidence without storing the public-key body. Copy the
evidence into the phone's as-built record only after setting
`remote_login_verified` to true based on an actual external-network login.

The implemented device commands are `millennium-ota check`, `apply`,
`auto-apply`, `recover`, and `status`. The dashboard calls the same signed worker through
restricted systemd units; it cannot choose an alternate artifact or key.
