# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This project modernizes a Nortel Millennium public payphone by replacing its original electronics with a Raspberry Pi Zero 2 W, two Arduino Micros, and a VoIP software stack. The Pi runs a C daemon that handles coins, keypad, magstripe cards, VFD display, SIP calls, and a web dashboard — while the original hardware is fully retained.

## Development Environment

Development happens on a Mac laptop. Claude Code cannot run directly on the Raspberry Pi Zero 2 W (ARMv6l architecture is unsupported), so all code editing and local testing runs on the Mac. Deployment and hardware testing require SSH-ing into the Pi.

```bash
ssh matzen@192.168.86.145   # Pi fixed address on local network
```

`make test` (unit + scenario tests) runs locally on the Mac. `make daemon` and `sudo make install` must be run on the Pi over SSH.

## Commands

### Host Software (`host/`)

```bash
# Build the full daemon (requires PJSIP/libpjproject/ALSA — Pi only)
make daemon

# Build and run all tests (works on any platform, no PJSIP/ALSA needed)
make test

# Run unit tests only
make unit_tests && ./unit_tests

# Run scenario tests only
make simulator && ./simulator tests/<scenario>.scenario

# Install daemon, ALSA config, and systemd service on the Pi
sudo make install

# Run API tests against the live device
make device-test                        # fixed Pi address (192.168.86.145)
make api-test HOST=<ip>                 # arbitrary host

# Run break/fuzzing tests
make break-test
```

### Arduino (`Arduino/`)

```bash
make build            # compile both sketches (run on Mac)
make install          # flash both via arduino-cli (Arduino directly connected to Mac)
make install_keypad   # flash Alpha via arduino-cli (Mac)
make install_display  # flash Beta via arduino-cli (Mac)

# Deploy to Pi over SSH using GPIO reset (normal workflow):
make deploy           # flash both (keypad first, then display)
make deploy_display   # flash Beta using GPIO27 reset
make deploy_keypad    # flash Alpha using GPIO17 reset
# or run the scripts directly:
./Arduino/deploy_display.sh [matzen@192.168.86.145]
./Arduino/deploy_keypad.sh  [matzen@192.168.86.145]
```

The deploy scripts assert reset via GPIO (open-drain: drive low, release to input), wait for the stock Arduino Micro bootloader to enumerate at `/dev/serial/by-id/usb-Arduino_LLC_Arduino_Micro-if00`, then flash with `avrdude`. Requires `raspi-gpio` and `avrdude` on the Pi.

## Claude Code Skills (`.claude/skills/`)

Repeatable workflows are captured as project skills (auto-discovered by Claude Code):

| Skill | What it does |
|-------|--------------|
| `deploy-daemon` | Sync `host/` to the Pi, `make clean && make daemon`, swap the binary, restart, verify SIP registration (with rollback). |
| `deploy-arduino` | Compile and GPIO-reset-flash the Alpha/Beta Micros in place over SSH. |
| `new-plugin` | Scaffold a plugin against `plugin_sdk.h`, register it, wire the build, add a scenario test. |
| `phone-status` | Health-check the live phone: service state, SIP registration, phone state, recent errors. |

Note: the Pi's IP is DHCP and has changed before (.145 → .115 …), so the skills prefer the `raspberrypi.local` hostname and document how to find the current address.

## Architecture

### System Components

```
Raspberry Pi Zero 2 W
  ├─ GPIO17/GEN0 (pin 11) → Reset 1 → Arduino Alpha RST
  ├─ GPIO27/GEN2 (pin 13) → Reset 2 → Arduino Beta RST
  └─ USB Hub
       ├─ Arduino "Millennium Alpha" (keypad.ino) — keypad matrix, hook switch, magstripe reader
       │    └─ I2C (addr 8) → Arduino "Millennium Beta" (display.ino)
       ├─ Arduino "Millennium Beta" — USB-serial bridge, VFD display, coin validator (SoftwareSerial 600 baud)
       └─ C-Media USB Audio Adapter (hw:1,0) — stereo: left=ringer, right=earpiece
```

The custom PCB (`pcb/`) connects the Pi to phone hardware: TDA2822M audio amp, XL6009 boost converter (5V→12V for coin validator), TVS ESD protection, PTC fuse, and reverse-polarity protection.

### Serial Protocol (Arduino ↔ Pi, 9600 baud)

Arduino → Pi: `K<char>` keypress, `HU`/`HD` hook up/down, `C<16-char-PAN>` card swipe, `V<byte>` coin event

Pi → Arduino: `0x02+len+data` write VFD, `0x03+byte` coin validator, `0x04` program EEPROM, `0x05` verify EEPROM, `0x06` keepalive/watchdog reset

### Host Daemon (`host/`)

Single-process C daemon. Key files:

| File | Role |
|------|------|
| `daemon.c` | Main event loop, all event handlers, `main()` |
| `millennium_sdk.c` | Serial I/O, event queue, VoIP call wrappers (via `pjsip_interface.c`) |
| `pjsip_interface.c` / `pjsip_interface.h` | PJSIP (PJSUA C API) wrapper: register/call/answer/hangup/DTMF. Header has plain declarations only (so `millennium_sdk.c` compiles on the Mac); the `.c` is built only on the Pi |
| `daemon_state.c` | Phone state machine (5 states), keypad buffer |
| `event_processor.c` | Routes queued events to handlers |
| `plugins.c` + `plugins/` | Plugin registry; built-ins: Classic Phone, Fortune Teller, Jukebox, Number Guess, Simon, Dial-A-Joke, Trivia |
| `plugin_sdk.c` / `plugin_sdk.h` | Friendly facade for plugin authors (display, audio, calls, state, balance, logging, RNG) — see `host/PLUGIN_AUTHORING.md` |
| `display_manager.c` | VFD abstraction with auto-scrolling for lines >20 chars |
| `audio_tones.c` | ALSA tone generator: dial tone, DTMF, ringback, busy, coin chime |
| `web_server.c` | HTTP+WebSocket server on port 80; REST API + real-time events |
| `metrics.c` / `metrics_server.c` | Prometheus/JSON metrics on port 8080 |
| `health_monitor.c` | Background checks: serial connection, SIP registration |
| `config.c` | `key=value` config file parser with environment variable fallback |
| `state_persistence.c` | Save/restore coin balance and active plugin across restarts |
| `clock_source.c` | Clock seam (`mclock_now()`): real `time()` in production, an advanceable clock under the simulator. Plugins read it via `sdk_now()` |
| `simulator.c` | Test harness: stubs hardware, drives the `clock_source` clock so scenario tests advance time instantly on any platform (no real sleeping, no Linux-only `--wrap` trick) |
| `tests/unit_tests.c` | Unit tests (config, state transitions, emergency detection, plugin registry) |

### State Machine

Five states: `INVALID`, `IDLE_DOWN`, `IDLE_UP`, `CALL_INCOMING`, `CALL_ACTIVE`

```
IDLE_DOWN ↔ IDLE_UP ↔ CALL_INCOMING → CALL_ACTIVE → IDLE_DOWN
```

State is protected by `daemon_state_mutex`; the `running_mutex` guards the main loop.

### Plugin System

Plugins register a `plugin_t` struct with function pointers for: `handle_coin`, `handle_keypad`, `handle_hook`, `handle_call_state`, `handle_card`, `handle_activation`, `handle_tick`. The active plugin can be switched at runtime via `POST /api/control` with `{"action":"activate_plugin","plugin":"<name>"}` (internally dispatched as the command string `activate_plugin:<name>`) and persists across restarts. The active plugin receives **every** keypress (digits, `*`, `#`, and matrix letters A–D) in all states, so plugins can use the full keypad.

The web dashboard and `GET /api/plugins` enumerate the registry **dynamically** (`plugins_to_json`), so a newly registered plugin appears automatically — no hard-coded list to update.

New plugins should be written against `plugin_sdk.h`, which wraps the hardware/subsystems behind a small, NULL-safe, no-op-on-missing-hardware API. The same plugin code then runs unchanged on the Pi, in the scenario simulator, and in unit tests. `plugins/number_guess.c` is the canonical example. See `host/PLUGIN_AUTHORING.md` for a step-by-step guide.

### Web API

Base URL: `http://<pi>` (port 80)

- `GET /api/state` — phone state, coins, keypad, SIP status
- `GET /api/health` — health-check JSON; returns HTTP **200** when serving (overall HEALTHY/WARNING) and **503** when not (CRITICAL/UNKNOWN), so liveness/readiness probes can key off the status code alone
- `GET /api/metrics` — Prometheus or JSON metrics
- `POST /api/control` — inject events (coin, keypad, hook, plugin activation)
- `GET /ws` — WebSocket real-time state broadcasts

### Audio

Pure ALSA (`libasound`), no PipeWire. Left channel → ringer (TDA2822M ch A), right channel → earpiece (TDA2822M ch B). Audio tone functions compile as no-ops on macOS so tests run locally.

## Coding Conventions

- **C89** across the whole codebase, compiled with `-std=gnu89` (the C89 language plus the GNU extensions the ALSA/PJSIP system headers require, e.g. `inline`; strict `-std=c89` can't parse those headers). `-Wall -Wextra -Wdeclaration-after-statement -Werror` on all targets — warnings are errors, and `-Wdeclaration-after-statement` keeps the source free of C99-style mixed declarations; no separate linter configured
- No external dependencies beyond ALSA, pthread, PJSIP (libpjproject), and libc
- No C++ in the active daemon targets (`.cpp` files in `host/` are legacy/unused)

## Configuration

Config file: `/etc/millennium/daemon.conf` (key=value). See `host/daemon.conf.example` for all options. Key settings: `hardware.display_device`, `call.cost_cents`, `call.free_numbers`, `card.enabled`, `web_server.port` (80, via `CAP_NET_BIND_SERVICE`), `metrics_server.port` (8080), `persistence.state_file`.

## PCB Design (`pcb/`)

KiCad project (`phonev6`) — the fabricated and installed revision (SMD migration of phonev5, gated by a `make verify` guardrail suite). Older revisions (v2/v4/v5) are removed; see `pcb/README.md` for the design history, BOM, and connector pinouts, and `pcb/JLCPCB_WORKFLOW.md` for fabrication steps.
