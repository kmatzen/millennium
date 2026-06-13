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
# Pi address is DHCP and HAS changed before (.145 -> .115 -> .152). mDNS
# (raspberrypi.local) is FLAKY and often doesn't resolve, so the last-known IP
# is usually the faster path: 192.168.86.152 (as of 2026-06-13).
PI=matzen@192.168.86.152
KEY=$HOME/.ssh/id_ed25519_sk_anima_notouch
```

The working key is `id_ed25519_sk_anima_notouch` — a **YubiKey-backed** (`sk`)
key, so the YubiKey must be physically attached (the `notouch` variant needs no
button press, but the hardware must be present). The old `id_ed25519_claude_anima`
key was RETIRED (`.retired-20260608`) and no longer works. The key gives
passwordless sudo on the Pi.

If `192.168.86.152` is dead, find the Pi: `arp -a | grep -i 'b8:27\|dc:a6\|e4:5f\|d8:3a'`
(Raspberry Pi OUIs) or check the router's DHCP table.

GOTCHA (this environment's shell is **zsh**): do NOT stash the ssh command in a
variable and run it as `$SSH $PI '...'` — zsh does not word-split unquoted
parameters, so the whole `ssh -i ...` string is treated as one command name and
fails with "no such file or directory". Invoke `ssh`/`rsync` directly with the
flags inline, e.g.:
```bash
ssh -i "$KEY" -o IdentitiesOnly=yes -o BatchMode=yes -o ConnectTimeout=8 "$PI" 'whoami'
```
(The PQ-crypto "not using a post-quantum key exchange" warning the Pi prints is
harmless; filter it out of output if it's noisy.)

Pi layout: scratch build dir `~/millennium-pjsip/host` · installed binary
`/usr/local/bin/millennium-daemon` · systemd unit `daemon.service` · config
`/etc/millennium/daemon.conf` · web API on `:80`, metrics on `:8080`.

## Steps

1. **Test on the Mac first** (fast, no hardware): `cd host && make test`.

2. **Sync source to the Pi scratch dir:**
   ```bash
   rsync -az --delete --exclude '*.o' --exclude daemon --exclude simulator \
     --exclude unit_tests --exclude pjsip_smoke \
     -e "ssh -i $KEY -o IdentitiesOnly=yes -o BatchMode=yes -o ConnectTimeout=8" \
     host/ "$PI":millennium-pjsip/host/
   ```
   (Or, for released code already on `main`, `git pull` in a Pi clone instead.)

3. **Rebuild — `make clean` FIRST.** rsync preserves source mtimes, so a plain
   `make daemon` often prints "up to date" and silently keeps the OLD binary.
   The build takes ~40 s on the Pi Zero 2 W:
   ```bash
   ssh -i "$KEY" -o IdentitiesOnly=yes -o BatchMode=yes "$PI" \
     'cd ~/millennium-pjsip/host && make clean && make daemon 2>&1 | tail -15'
   ```
   GOTCHA: never `pkill -f "make daemon"` over SSH — the remote shell's own argv
   contains that string, so it self-matches and kills your command. Use a `[m]ake`
   bracket pattern or check the binary mtime instead.

   GOTCHA: the **Pi's GCC is 10.2.1, which is STRICTER than CI's GCC on some
   `-Werror` warnings** (e.g. `stringop-truncation`), so code that builds in CI
   and `make test` on the Mac (clang) can still fail `make daemon` here. If the
   build dies on a file you didn't touch, it's likely a latent warning the Pi
   toolchain newly flags — fix it (usually `snprintf` over `strncpy`) and land it
   on `main` so the repo stays buildable on the deployment target.

4. **Swap the binary and restart** (must stop first — can't overwrite a running
   binary). Keep a rollback copy:
   ```bash
   ssh -i "$KEY" -o IdentitiesOnly=yes -o BatchMode=yes "$PI" '
     sudo cp -a /usr/local/bin/millennium-daemon /usr/local/bin/millennium-daemon.prev.bak
     sudo systemctl stop daemon.service
     sudo cp ~/millennium-pjsip/host/daemon /usr/local/bin/millennium-daemon
     sudo systemctl start daemon.service'
   ```

5. **Verify:**
   ```bash
   ssh -i "$KEY" -o IdentitiesOnly=yes -o BatchMode=yes "$PI" \
     'systemctl is-active daemon.service; \
     echo NRestarts=$(systemctl show daemon.service -p NRestarts --value); \
     curl -s --max-time 4 http://127.0.0.1:80/api/state'
   ```
   Expect `active`, `NRestarts=0`, `"sip_registered":1`, empty `sip_last_error`.
   On failure: check `sip.*` keys in `/etc/millennium/daemon.conf` and
   `sudo journalctl -u daemon.service -n 50 --no-pager`.

## Rollback
```bash
ssh -i "$KEY" -o IdentitiesOnly=yes -o BatchMode=yes "$PI" \
  'sudo systemctl stop daemon.service; \
  sudo cp /usr/local/bin/millennium-daemon.prev.bak /usr/local/bin/millennium-daemon; \
  sudo systemctl start daemon.service'
```
A Baresip-era binary is also kept at `…/millennium-daemon.baresip-patched.bak`.

## First-time setup
For a fresh Pi, clone the repo and run `sudo make install` (installs the binary,
ALSA config, and the systemd unit) — see `host/SETUP.md`.
