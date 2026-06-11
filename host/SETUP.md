# Raspberry Pi Setup Guide

Step-by-step instructions for setting up the Millennium payphone daemon on a
Raspberry Pi Zero 2 W from a fresh Raspbian install.

## Prerequisites

- Raspberry Pi Zero 2 W with Raspbian 11 (Bullseye) or later
- SSH access to the Pi
- The custom PCB with both Arduinos flashed and the USB audio card connected
- A SIP account (e.g., from Twilio, VoIP.ms, or a local Asterisk server)

## 1. System packages

```bash
sudo apt update
sudo apt install build-essential git libasound2-dev libssl-dev cmake
```

## 2. User setup

The daemon runs as a system-wide systemd service. Add your user to the
`dialout` and `audio` groups (needed for serial ports and ALSA):

```bash
sudo usermod -aG dialout,audio $USER
```

Log out and back in for group changes to take effect.

## 3. Build PJSIP (pjproject — the VoIP stack)

The daemon embeds **PJSIP** via its PJSUA C API (`libpjproject`). Build it with
ALSA and TLS support:

```bash
cd ~
git clone https://github.com/pjsip/pjproject.git
cd pjproject
# A recent 2.x release; pin a tag you've tested, e.g.:
git checkout 2.15.1
./configure --enable-shared --disable-video \
            --with-ssl                # OpenSSL for TLS (Twilio needs it)
make dep && make -j$(nproc)
sudo make install
sudo ldconfig
```

Notes:
- `pkg-config --modversion libpjproject` should now succeed (the daemon's
  Makefile uses `pkg-config --cflags/--libs libpjproject`).
- PJSIP auto-detects ALSA; G.711 (PCMU/PCMA) is built in, which is all Twilio
  needs. Opus is optional.
- If `pkg-config` can't find it, ensure `/usr/local/lib/pkgconfig` is on
  `PKG_CONFIG_PATH`.

Smoke-test the PJSIP integration before wiring up real credentials — this
brings PJSUA up and down without a SIP peer or the phone hardware:

```bash
make pjsip-smoke && ./pjsip_smoke   # expect "RESULT: PASS"
```

## 4. Configure the SIP account

There is **no separate accounts file** — SIP credentials live in
`/etc/millennium/daemon.conf` (`make install` copies `daemon.conf.example`).
Set:

```
sip.id_uri=sip:+1XXXXXXXXXX@your-provider.sip.example.com
sip.registrar=sip:your-provider.sip.example.com
sip.realm=*
sip.username=+1XXXXXXXXXX
sip.password=YOUR_PASSWORD
sip.transport=tls                 # Twilio requires TLS
sip.stun_server=stun.l.google.com:19302
```

Leave `sip.id_uri`/`sip.registrar` empty to run the phone as a games console
with VoIP disabled. Because credentials are read by the daemon itself, it no
longer needs to run as a particular user or own a `~/.baresip` directory.

### Audio routing

The handset audio is split by the TDA2822M (left = ringer, right = earpiece).
The daemon's own tone generator (`audio_tones.c`) drives the ringer/dial/busy
tones on the left channel; PJSIP only carries the live **call** audio. Route
PJSIP's mono call audio to the earpiece via `/etc/asound.conf` (installed from
`asoundrc.example`) so the ALSA `default` device maps capture to the mic and
playback to the right channel. If needed, pin specific device IDs with
`sip.snd_capture_dev` / `sip.snd_playback_dev` (the daemon logs available
device IDs at startup).

## 5. Clone and build the daemon

```bash
cd ~
git clone https://github.com/kmatzen/millennium.git
cd millennium/host
make daemon
```

## 6. ALSA audio configuration

The USB audio card needs a custom ALSA config to split stereo into independent
mono channels for the handset earpiece and ringer speaker. From the `host/`
directory (where you just ran `make daemon`):

```bash
sudo cp asoundrc.example /etc/asound.conf
```

Verify the USB audio card is detected as card 1:

```bash
aplay -l
```

If it shows up as a different card number, edit `/etc/asound.conf` and change
`"hw:1,0"` to match.

Test audio output:

```bash
speaker-test -D out_left_solo -c1 -twav   # should play through ringer
speaker-test -D out_right_solo -c1 -twav  # should play through handset
```

## 7. Configure the daemon

```bash
sudo mkdir -p /etc/millennium
sudo cp daemon.conf.example /etc/millennium/daemon.conf
```

Edit `/etc/millennium/daemon.conf`. The key settings to verify:

```
hardware.display_device=/dev/serial/by-id/usb-Arduino_LLC_Millennium_Beta-if00
hardware.baud_rate=9600
call.cost_cents=50
call.timeout_seconds=300
web_server.enabled=true
web_server.port=80
```

The `hardware.display_device` path depends on the Arduino's USB product name.
With the custom board definitions flashed, the display Arduino appears as
`Millennium_Beta`. Verify with:

```bash
ls /dev/serial/by-id/
```

## 8. Create the log and state directories

```bash
sudo mkdir -p /var/log/millennium
sudo chown $USER:$USER /var/log/millennium
sudo mkdir -p /var/lib/millennium
sudo chown $USER:$USER /var/lib/millennium
```

## 9. Install the systemd service

**Production** (recommended): Use `make install` for a system-wide service that
starts at boot:

```bash
cd ~/millennium/host
make install
```

This installs the daemon to `/usr/local/bin/millennium-daemon`, creates
`/etc/systemd/system/daemon.service` (with a user override), copies
`daemon.conf.example` to `/etc/millennium/daemon.conf`, and starts the service.
Check status:

```bash
sudo systemctl status daemon.service
```

**Development** (running from repo, no `make install`): Use the dev service:

```bash
mkdir -p ~/.config/systemd/user
cp systemd/daemon-dev.service ~/.config/systemd/user/daemon.service
systemctl --user daemon-reload
systemctl --user enable daemon.service
systemctl --user start daemon.service
```

Ensure `/etc/millennium/daemon.conf` exists: `sudo cp daemon.conf.example /etc/millennium/daemon.conf`.

The web dashboard should now be accessible at `http://<pi-ip>:80`.

## 10. Disable unused services (optional cleanup)

If you previously had PipeWire/WirePlumber installed, mask the leftover services:

```bash
systemctl --user mask pipewire.service
systemctl --user mask wireplumber.service
systemctl --user disable remap-sink.service
systemctl --user disable filter-chain.service
systemctl --user disable audio-mux.service
```

## Verifying the setup

1. **Serial connection**: The daemon log should show "Serial port opened" on
   startup. If not, check the `display_device` path and that the Arduino is
   connected.

2. **VoIP registration**: Check the daemon log for SIP registration status.
   If registration fails, verify your `sip.*` credentials in
   `/etc/millennium/daemon.conf` and network connectivity.

3. **Audio**: Lift the handset and dial a number. You should hear dial tone
   through the handset earpiece. If no audio, verify the ALSA config and USB
   audio card detection.

4. **Web dashboard**: Open `http://<pi-ip>:80` in a browser. You should see
   the phone state, active plugin, and health status.

5. **API integration test**: From the Pi (or any machine that can reach it):
   ```bash
   cd ~/millennium/host && make api-test
   # Or from another machine: HOST=<pi-ip> ./tests/api_test.sh
   ```

## Updating

To update the daemon after pulling new code:

```bash
cd ~/millennium/host
git pull
make clean && make daemon
sudo make install
```

Or use the OTA update feature from the web dashboard if the `system.source_dir`
config points to the local repo. If using the dev service (systemctl --user),
use `systemctl --user restart daemon.service` instead.

## Troubleshooting

### Config, epoll, or state file errors

If you see:
- **Could not load config file** — Copy the config and ensure the path exists:
  ```bash
  sudo mkdir -p /etc/millennium
  sudo cp host/daemon.conf.example /etc/millennium/daemon.conf
  ```
  Update the systemd unit to use `--config /etc/millennium/daemon.conf`.

- **`pkg-config` can't find libpjproject** — Ensure PJSIP installed to
  `/usr/local` and `PKG_CONFIG_PATH=/usr/local/lib/pkgconfig`, then `sudo ldconfig`.
  Verify with `pkg-config --modversion libpjproject`.

- **Unit file daemon.service is masked** — Unmask before enabling: `sudo systemctl unmask daemon.service`, then run `make install` again.

- **No such file or directory** (for millennium-daemon) — You're running from the repo without `make install`. Use the dev service:
  ```bash
  cp systemd/daemon-dev.service ~/.config/systemd/user/daemon.service
  systemctl --user daemon-reload
  systemctl --user restart daemon.service
  ```

- **Failed to open state file for writing** — Create the state directory and make it writable:
  ```bash
  sudo mkdir -p /var/lib/millennium
  sudo chown $USER:$USER /var/lib/millennium
  ```
  Or set `MILLENNIUM_STATE_FILE=/home/$USER/.local/state/millennium` in the environment.

### Serial port not found

If the daemon log shows a serial open error:

1. Check the Arduino is connected: `ls /dev/serial/by-id/`
2. Verify your user is in the `dialout` group: `groups`
3. If the device path differs from the config, update `hardware.display_device`
   in `/etc/millennium/daemon.conf`

### No audio output

1. Verify the USB audio card is detected: `aplay -l`
2. Check the card number matches the ALSA config: look for `"hw:1,0"` in
   `/etc/asound.conf` and update if the card number differs
3. Test each channel independently:
   ```bash
   speaker-test -D out_left_solo -c1 -twav
   speaker-test -D out_right_solo -c1 -twav
   ```
4. Check ALSA mixer levels: `alsamixer -c 1`

### SIP registration fails

1. Check network connectivity: `ping -c3 8.8.8.8`
2. Verify the `sip.*` credentials in `/etc/millennium/daemon.conf`
3. Check the daemon log for PJSIP/SIP error messages:
   ```bash
   journalctl -u daemon.service --no-pager -n 100 | grep -iE 'reg|sip|403'
   ```
4. Check the web dashboard `/api/state` — it shows `sip_registered` (1=ok, -1=failed)
   and `sip_last_error` with the provider's message.
5. **Twilio 403 Forbidden**: Common causes:
   - Wrong `sip.username` or `sip.password` in `/etc/millennium/daemon.conf`
   - Credentials List username/password in Twilio Console don't match
   - SIP domain (e.g. `matzen-test.sip.twilio.com`) not set up or mismatched
   - Verify in Twilio Console → Voice → SIP Trunking → Credential List

### Daemon crashes or restarts repeatedly

1. Check the log for the crash reason:
   ```bash
   sudo journalctl -u daemon.service --no-pager -n 100
   ```
2. Check available disk space: `df -h` — the log file can grow if
   `logging.to_file=true`
3. Check memory: `free -h` — the Pi Zero 2 W has limited RAM

### VFD display shows garbage or nothing

1. Check the serial baud rate matches the config (should be 9600)
2. The Display Arduino may need a reset — unplug and replug the USB cable
3. Check I2C communication between the Arduinos:
   ```bash
   i2cdetect -y 1
   ```
   You should see address `08` (display Arduino) on the bus
