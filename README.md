# Millennium Project

The **Millennium Project** breathes new life into the Nortel Millennium public telephone by replacing much of its original electronics with modern hardware and software. The project integrates a Raspberry Pi Zero 2 W, two Arduinos, and a VoIP software stack while retaining all the original hardware, including the display, keypad, coin acceptor, magstripe reader, handset, and ringer speaker.

## Table of Contents

1. [Overview](#overview)
2. [Architecture](#architecture)
3. [Features](#features)
4. [Directory Structure](#directory-structure)
5. [Setup](#setup)
6. [Configuration](#configuration)
7. [Development](#development)
8. [Resources](#resources)
9. [License](#license)

---

## Overview

This project reimagines the functionality of the Nortel Millennium telephone by combining modern hardware and software while preserving its iconic hardware features:
- **Raspberry Pi Zero 2 W**: Manages the system, runs the VoIP stack, and interfaces with the original hardware.
- **Arduinos**: Control the keypad, display, and other peripherals.
- **Custom PCB**: Consolidates the connections between components.
- **Host Software**: Includes a daemon, plugin system, web dashboard, and ALSA audio configuration.
- **Preserved Hardware**: The original display, keypad, coin acceptor, magstripe reader, handset, and ringer speaker are retained and functional.

<img src="IMG_5137.JPG" alt="photo of modified phone" style="height:400px;">

---

## Architecture

```
┌─────────────────────────────────────────────────────┐
│                 Raspberry Pi Zero 2 W               │
│                                                     │
│  ┌──────────┐  ┌──────────┐  ┌───────────────────┐ │
│  │  Daemon   │  │ Baresip  │  │   Web Dashboard   │ │
│  │          │──│  (VoIP)  │  │   :8081            │ │
│  │ Plugins: │  └──────────┘  │ - Phone state      │ │
│  │ - Phone  │                │ - Plugin switching  │ │
│  │ - Fortune│  ┌──────────┐  │ - OTA updates      │ │
│  │ - Jukebox│  │  ALSA    │  │ - Health/metrics   │ │
│  │          │  │  Audio   │  └───────────────────┘ │
│  └────┬─────┘  └──────────┘                         │
│       │ USB Serial (9600 baud)                      │
└───────┼─────────────────────────────────────────────┘
        │
┌───────┴──────────────────────────────────────────┐
│              Custom PCB (phonev4)                 │
│                                                  │
│  ┌─────────────────┐    I2C    ┌───────────────┐ │
│  │ Display Arduino  │◄────────│ Keypad Arduino │ │
│  │ (millennium_beta)│         │(millennium_alpha)│
│  │                  │         │                 │ │
│  │ ► VFD Display    │         │ ► 4x7 Keypad   │ │
│  │   (parallel 8b)  │         │ ► MagStripe    │ │
│  │ ► Coin Validator  │         │ ► Hook Switch  │ │
│  │   (SoftSerial)   │         │                 │ │
│  └──────────────────┘         └─────────────────┘ │
└──────────────────────────────────────────────────┘
```

**Data flow**: Keypad/hook/card events originate on the Keypad Arduino, are sent via I2C to the Display Arduino, which forwards them to the Raspberry Pi over USB serial. The daemon processes events through an event processor, dispatches them to the active plugin, and sends display updates back down the same path. VoIP calls are handled by Baresip with audio routed through ALSA (dmix + route plugins for mono channel splitting).

---

## Features

- **Pay phone operation**: Insert coins, dial numbers, make VoIP calls — authentic payphone experience
- **Plugin system**: Swap between Classic Phone, Fortune Teller, and Jukebox modes at runtime
- **Emergency calls**: Dial 911, 311, or 0 without coins (configurable free numbers)
- **Magstripe card support**: Swipe a registered card for free calling or admin access
- **Web dashboard**: Real-time phone state, plugin switching, health monitoring via WebSocket
- **OTA updates**: Check for and apply updates from the web dashboard (git pull, build, restart)
- **Audio tones**: DTMF, dial tone, busy tone, coin tone, ringback via ALSA
- **Idle timeout**: Automatically resets phone state after configurable inactivity period
- **State persistence**: Saves and restores coin balance and plugin state across restarts
- **Health monitoring**: Tracks serial connection, SIP registration, and daemon activity
- **Metrics collection**: Counters for calls, coins, keypresses, card swipes
- **Version tracking**: Build version and git hash displayed on dashboard
- **Scenario testing**: Simulator-based integration tests with simulated time

---

## Directory Structure

- **`Arduino/`**: Arduino sketches for the keypad and display microcontrollers, plus a Makefile for building and flashing.
- **`case/`**: 3D model files (`.blend` and `.stl`) for a custom enclosure.
- **`pcb/`**: KiCad schematic, PCB layout, BOM, and Gerber files for the custom PCB (phonev4).
- **`host/`**: Raspberry Pi software:
  - `daemon.c` — Main daemon loop and event routing
  - `plugins/` — Plugin implementations (classic_phone, fortune_teller, jukebox)
  - `web_server.c` — HTTP server with dashboard and REST API
  - `audio_tones.c` — Tone generation (DTMF, dial tone, etc.)
  - `updater.c` — OTA update checker and applier
  - `simulator.c` — Test runner with scenario file support and simulated time
  - `tests/` — Unit tests and scenario test files
  - `systemd/` — Service file for the daemon
  - `daemon.conf.example` — Example configuration file
  - `asoundrc.example` — ALSA audio configuration for mono channel splitting

---

## Setup

### 1. Arduino Firmware
Navigate to the `Arduino/` directory and follow the instructions in its [README](Arduino/README.md) to build and flash firmware to the microcontrollers.

### 2. PCB
The PCB files are in the `pcb/` directory. Upload the Gerber files to a PCB fabricator like JLCPCB for manufacturing. See the [pcb README](pcb/README.md) for details.

### 3. Host Software
Follow the [setup guide](host/SETUP.md) for full step-by-step instructions, or use the quick install:

```bash
cd host
make daemon        # Build the daemon
sudo make install  # Install to /usr/local/bin and set up systemd
```

See the [host README](host/README.md) for dependency installation (Baresip, ALSA) and audio configuration.

### 4. Configuration
Copy and edit the configuration file:

```bash
sudo cp host/daemon.conf.example /etc/millennium/daemon.conf
sudo nano /etc/millennium/daemon.conf
```

See [Configuration](#configuration) below for all available options.

---

## Configuration

The daemon reads configuration from `/etc/millennium/daemon.conf`. See `host/daemon.conf.example` for a complete annotated example. Key settings:

| Key | Default | Description |
|-----|---------|-------------|
| `hardware.display_device` | `/dev/serial/by-id/usb-Arduino_LLC_Millennium_Beta-if00` | Serial device for display Arduino |
| `hardware.baud_rate` | `9600` | Serial baud rate |
| `call.cost_cents` | `50` | Cost per call in cents |
| `call.timeout_seconds` | `300` | Maximum call duration |
| `call.free_numbers` | `911,311,0` | Comma-separated numbers that bypass coin requirement |
| `call.idle_timeout_seconds` | `60` | Seconds of inactivity before phone resets |
| `card.enabled` | `true` | Enable magstripe card support |
| `card.free_cards` | *(empty)* | Comma-separated card numbers for free calling |
| `card.admin_cards` | *(empty)* | Comma-separated card numbers for admin access |
| `web_server.enabled` | `true` | Enable the web dashboard |
| `web_server.port` | `8081` | Web dashboard port |
| `system.source_dir` | `/home/matzen/millennium` | Source directory for OTA updates |

---

## Development

### Building and testing

```bash
cd host
make test          # Build simulator + unit tests, run both
make daemon        # Build the full daemon (requires Baresip/libre on the Pi)
make clean         # Remove all build artifacts
```

### Running tests locally

Tests run on any platform (macOS, Linux) without Baresip or ALSA — the simulator stubs out hardware dependencies:

```bash
cd host
make test
```

This runs:
- **Unit tests**: Config, daemon state, plugins, emergency numbers, card config, updater
- **Scenario tests**: Full integration tests (basic call, timeout, emergency, card swipe, hook lifecycle, state persistence, display scrolling, idle timeout)

### Adding a plugin

1. Create `host/plugins/your_plugin.c`
2. Implement handler functions for the events you care about (coin, keypad, hook, call state, card)
3. Register with `plugins_register()` in a `register_your_plugin()` function
4. Call the registration function from `plugins_init()` in `plugins.c`
5. Add the `.o` file to the Makefile build targets

### Coding style

- C89 for most files (`-std=c89`) — no mixed declarations/code
- C99 for files that need it (`-std=c99`) — simulator, jukebox (block-scoped declarations)
- `-Wall -Wextra` — all warnings enabled, treat them seriously
- No unnecessary comments — code should be self-documenting

---

## Resources

This project draws on several resources for understanding and interfacing with the Nortel Millennium telephone's hardware:

1. **General Millennium Payphone Documentation**
   The [Millennium Payphone Wiki](https://wiki.muc.ccc.de/millennium:start) provides extensive documentation on the phone's hardware and protocols.
   - **Coin Validator Protocol**: Used the documented higher-level protocol to interface with the coin validator.
   - **EEPROM Data**: Referenced for configuring the coin validator to accept Canadian currency.

2. **Display Documentation**
   A detailed discussion on the Noritake CU20026SCPB-T23A VFD display can be found in this [HardForum thread](https://hardforum.com/threads/noritake-cu20026scpb-t23a-20-x-2-vfd-modulel.1132806/). This was essential for understanding and controlling the display.

3. **Device Pinouts**
   The [New Fire Millennium Payphone Page](http://www.newfire.org/payphones/millennium/) includes pinout diagrams for various components, which were critical when developing the Arduino sketches.

4. **Millennium Phone Source**
   The Millennium phone used in this project was sourced from [Ballard Reuse](https://ballardreuse.com/) in Seattle, a store specializing in salvaged and reusable materials.

---

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE) for details.
