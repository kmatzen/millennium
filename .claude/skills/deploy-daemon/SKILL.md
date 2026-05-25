---
name: deploy-daemon
description: Build and deploy the Millennium host C daemon to the Raspberry Pi over SSH, then verify it came up and registered with SIP. Use when asked to deploy/install/push the daemon, ship host/ changes to the phone, or restart the live daemon with new code.
---

# Deploy the host daemon to the Pi

The daemon is edited and unit-tested on the Mac but **builds and runs only on
the Pi** (it links PJSIP/libpjproject + ALSA, which aren't on the Mac). This
skill syncs the source, rebuilds, swaps the binary, restarts the service, and
verifies SIP registration.

## Connection

```bash
# Pi address is DHCP and HAS changed before (.145 -> .115 after a board swap).
# Prefer the hostname; fall back to finding the current IP.
PI=matzen@raspberrypi.local
SSH="ssh -i $HOME/.ssh/id_ed25519_claude_anima -o IdentitiesOnly=yes -o BatchMode=yes -o ConnectTimeout=8"
```

If `raspberrypi.local` doesn't resolve and the last-known IP is dead, find the
Pi: `arp -a | grep -i 'b8:27\|dc:a6\|e4:5f\|d8:3a'` (Raspberry Pi OUIs) or
check the router's DHCP table. `~/.ssh/id_ed25519_claude_anima` is the
non-interactive key in this environment (passwordless sudo on the Pi).

Pi layout: scratch build dir `~/millennium-pjsip/host` · installed binary
`/usr/local/bin/millennium-daemon` · systemd unit `daemon.service` · config
`/etc/millennium/daemon.conf` · web API on `:8081`, metrics on `:8080`.

## Steps

1. **Test on the Mac first** (fast, no hardware): `cd host && make test`.

2. **Sync source to the Pi scratch dir:**
   ```bash
   rsync -az --delete --exclude '*.o' --exclude daemon --exclude simulator \
     --exclude unit_tests --exclude pjsip_smoke \
     -e "$SSH" host/ $PI:millennium-pjsip/host/
   ```
   (Or, for released code already on `main`, `git pull` in a Pi clone instead.)

3. **Rebuild — `make clean` FIRST.** rsync preserves source mtimes, so a plain
   `make daemon` often prints "up to date" and silently keeps the OLD binary:
   ```bash
   $SSH $PI 'cd ~/millennium-pjsip/host && make clean && make daemon'
   ```
   GOTCHA: never `pkill -f "make daemon"` over SSH — the remote shell's own argv
   contains that string, so it self-matches and kills your command. Use a `[m]ake`
   bracket pattern or check the binary mtime instead.

4. **Swap the binary and restart** (must stop first — can't overwrite a running
   binary). Keep a rollback copy:
   ```bash
   $SSH $PI '
     sudo cp -a /usr/local/bin/millennium-daemon /usr/local/bin/millennium-daemon.prev.bak
     sudo systemctl stop daemon.service
     sudo cp ~/millennium-pjsip/host/daemon /usr/local/bin/millennium-daemon
     sudo systemctl start daemon.service'
   ```

5. **Verify:**
   ```bash
   $SSH $PI 'systemctl is-active daemon.service; \
     echo NRestarts=$(systemctl show daemon.service -p NRestarts --value); \
     curl -s --max-time 4 http://127.0.0.1:8081/api/state'
   ```
   Expect `active`, `NRestarts=0`, `"sip_registered":1`, empty `sip_last_error`.
   On failure: check `sip.*` keys in `/etc/millennium/daemon.conf` and
   `sudo journalctl -u daemon.service -n 50 --no-pager`.

## Rollback
```bash
$SSH $PI 'sudo systemctl stop daemon.service; \
  sudo cp /usr/local/bin/millennium-daemon.prev.bak /usr/local/bin/millennium-daemon; \
  sudo systemctl start daemon.service'
```
A Baresip-era binary is also kept at `…/millennium-daemon.baresip-patched.bak`.

## First-time setup
For a fresh Pi, clone the repo and run `sudo make install` (installs the binary,
ALSA config, and the systemd unit) — see `host/SETUP.md`.
