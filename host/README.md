# Host Software for Raspberry Pi Zero 2 W

This directory contains the software and configuration files needed to run the Millennium payphone daemon on a Raspberry Pi Zero 2 W.

**First-time setup?** See [SETUP.md](SETUP.md) for the full step-by-step guide.

## Overview

The host software includes:
- A daemon for managing the payphone — event processing, plugin system, display control, VoIP calling, web dashboard
- A systemd service file for running the daemon at startup
- An ALSA configuration for audio routing (mono channel splitting for handset/speaker)

## Files

- `Makefile` — Builds the daemon, simulator, and unit tests
- `DEVICE_TEST.md` — Run API tests against the live phone over the network
- `daemon.conf.example` — Example configuration file (copy to `/etc/millennium/daemon.conf`)
- `asoundrc.example` — Example ALSA configuration for the USB audio device
- `systemd/daemon.service` — Systemd service for the daemon
- `plugins/` — Plugin implementations (classic_phone, fortune_teller, jukebox, number_guess, simon, dial_a_joke, trivia)
- `PLUGIN_AUTHORING.md` / `PLUGIN_SYSTEM_SUMMARY.md` — Plugin authoring guide and system overview
- `tests/` — Unit tests and scenario test files

## Dependencies

The following must be installed on the Raspberry Pi Zero 2 W:

1. **PJSIP (pjproject)**: Built from source for VoIP/SIP support (PJSUA C API).
   - [pjproject GitHub Repository](https://github.com/pjsip/pjproject)
   - See [SETUP.md](SETUP.md) for build flags (ALSA + OpenSSL/TLS).

2. **ALSA development libraries**: For audio tone generation.
   ```bash
   sudo apt install libasound2-dev
   ```

## Audio Setup

Audio routing uses pure ALSA configuration (no PipeWire or WirePlumber needed). The USB audio device is a C-Media USB sound card with stereo output. The ALSA config splits this into mono channels so one channel drives the handset earpiece and the other drives the ringer/speaker.

1. Copy the ALSA configuration (or run `sudo make install`, which includes this):
   ```bash
   sudo cp host/asoundrc.example /etc/asound.conf
   ```

2. Verify the USB audio device is card 1:
   ```bash
   aplay -l
   ```
   If it's a different card number, edit `/etc/asound.conf` and change `"hw:1,0"` accordingly.

The config provides several PCM devices:
- `default` — Stereo output to the USB audio device (via dmix + softvol)
- `out_left_solo` — Mono output to left channel only (handset earpiece)
- `out_right_solo` — Mono output to right channel only (ringer speaker)
- `out_left_dup` / `out_right_dup` — Mono duplicated to both channels

## Build and Test

```bash
make daemon        # Build the full daemon (requires PJSIP/libpjproject)
make test          # Build simulator + unit tests, run both
make clean         # Remove all build artifacts
sudo make install  # Install daemon binary, ALSA config, and systemd service
```

## Command-line options

The daemon accepts a few flags (anything else is rejected with a usage message
and exit code 2):

```
Usage: millennium-daemon [options]

Options:
  -c, --config <path>   Read configuration from <path>
                        (default: /etc/millennium/daemon.conf)
  -v, --version         Print version and build info, then exit
  -h, --help            Print this help, then exit
```

`--version` prints the build's semantic version, git hash, and build time —
useful for confirming which build is actually installed after a deploy:

```bash
millennium-daemon --version
# millennium-daemon 0.3.0 (git 1f831a8, built 2026-06-11T12:00:00Z)
```

`--config` may appear anywhere on the command line and also accepts the
`--config=<path>` form. The systemd unit passes
`--config /etc/millennium/daemon.conf`.

## Systemd Setup

```bash
sudo make install
```

This installs the daemon and enables the `daemon.service` unit. SIP credentials
are read from `/etc/millennium/daemon.conf` (`sip.*`). Check status:

```bash
sudo systemctl status daemon.service
```
