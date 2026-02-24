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
- `DEVICE_TEST.md` — Run API tests against the real device (192.168.86.145)
- `daemon.conf.example` — Example configuration file (copy to `/etc/millennium/daemon.conf`)
- `asoundrc.example` — Example ALSA configuration for the USB audio device
- `systemd/daemon.service` — Systemd service for the daemon
- `plugins/` — Plugin implementations (classic_phone, fortune_teller, jukebox)
- `tests/` — Unit tests and scenario test files

## Dependencies

The following must be installed on the Raspberry Pi Zero 2 W:

1. **Baresip** and **libre**: Built from source for VoIP/SIP support.
   - [Baresip GitHub Repository](https://github.com/baresip/baresip)
   - [libre GitHub Repository](https://github.com/baresip/re)

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
make daemon        # Build the full daemon (requires Baresip/libre)
make test          # Build simulator + unit tests, run both
make clean         # Remove all build artifacts
sudo make install  # Install daemon binary, ALSA config, and systemd service
```

## Systemd Setup

```bash
sudo make install
```

This installs the daemon and enables the `daemon.service` unit. The service
runs system-wide but as your user (for `~/.baresip/`). Check status:

```bash
sudo systemctl status daemon.service
```
