# Arduino Firmware

Two Arduino Micro boards (ATmega32U4) run the keypad/peripheral I/O:

| Board             | FQBN                            | Sketch            | Role                                    |
|-------------------|---------------------------------|-------------------|-----------------------------------------|
| Millennium Alpha  | `arduino:avr:millennium_alpha`  | `sketches/keypad` | 4x7 keypad, magstripe reader, hook switch |
| Millennium Beta   | `arduino:avr:millennium_beta`   | `sketches/display`| VFD display, coin validator, I2C→USB bridge |

See [PINOUT.md](PINOUT.md) for complete pin assignments, I2C protocol, and serial command reference.

## Pre-built Firmware

Pre-built hex files are checked in under `build/`:

```
build/keypad/keypad.ino.hex    # Flash to Millennium Alpha
build/display/display.ino.hex  # Flash to Millennium Beta
```

These can be flashed directly without compiling:

```bash
arduino-cli upload -p /dev/serial/by-id/usb-Arduino_LLC_Millennium_Alpha-if00 \
    --fqbn arduino:avr:millennium_alpha --input-dir ./build/keypad

arduino-cli upload -p /dev/serial/by-id/usb-Arduino_LLC_Millennium_Beta-if00 \
    --fqbn arduino:avr:millennium_beta --input-dir ./build/display
```

## Building from Source

### Prerequisites

- [arduino-cli](https://arduino.github.io/arduino-cli) in your `PATH`
  (or set `ARDUINO_CLI=/path/to/arduino-cli`)
- Arduino AVR core: `arduino-cli core install arduino:avr`
- The custom board definitions must be appended to the Arduino AVR
  `boards.txt` (see [Custom Board Definitions](#custom-board-definitions) below)

### Build and Flash

```bash
make build              # Compile both sketches → build/
make install            # Flash both to connected Arduinos
make install_keypad     # Flash keypad only
make install_display    # Flash display only
make clean              # Remove build artifacts
```

**When display firmware changes** (e.g. after merging a PR that touches `sketches/display/`):
flash the display Arduino so it stays in sync with the daemon. Always build with
`arduino:avr:millennium_beta` so the board keeps its "Millennium Beta" USB identity.

**Recommended: build on macOS, sync, flash on Pi**
```bash
./Arduino/deploy_display.sh [user@host]
# Builds locally, syncs (git pull on remote), prompts to put Arduino in bootloader mode,
# then flashes with avrdude. Default host: matzen@192.168.86.145 (see host/DEVICE_TEST.md)
```

The script triggers the bootloader via the 1200-baud trick (open port at 1200 baud, close — no physical reset needed), then flashes with avrdude. Install on Pi: `sudo apt install avrdude`. FLASH_PORT should be the display Arduino's serial port (e.g. `/dev/serial/by-id/usb-Arduino_LLC_Millennium_Beta-if00` or `/dev/ttyACM0`).

If build fails (arduino-cli.yaml has Linux paths): `BUILD_CONFIG=0 make build_display`. If hex not pushed: `VIA_SCP=1`.

If `arduino-cli` is not in your `PATH`:

```bash
make build ARDUINO_CLI=/home/matzen/bin/arduino-cli
```

### Verifying a Build

The build is reproducible — recompiling from the same source with the same
arduino-cli and AVR core version produces identical hex files.

## Custom Board Definitions

The two boards are Arduino Micro clones with custom USB product names and
PIDs so the Pi can distinguish them via `/dev/serial/by-id/`. The custom
entries must be appended to:

```
~/.arduino15/packages/arduino/hardware/avr/1.8.6/boards.txt
```

Key differences from stock Arduino Micro:

| Property         | Stock Micro    | Millennium Alpha   | Millennium Beta    |
|------------------|----------------|--------------------|--------------------|
| USB PID (app)    | 0x0037         | 0x0045             | 0x0046             |
| USB PID (boot)   | 0x8037         | 0x8045             | 0x8046             |
| Product name     | Arduino Micro  | Millennium Alpha   | Millennium Beta    |

The full board definition entries are documented in [PINOUT.md](PINOUT.md#custom-board-definitions).

### Custom Bootloaders

The `boards.txt` entries reference custom bootloader hex files
(`Caterina-Micro-Millennium-Alpha.hex`, `Caterina-Micro-Millennium-Beta.hex`)
that encode the custom VID/PID. These bootloader hex files were never built —
the boards currently run with stock Micro bootloaders that were already flashed
at the factory. The custom VID/PID only takes effect when the sketch is running
(not during the bootloader's 8-second window).

To build custom bootloaders (requires LUFA library and `avr-gcc`):

```bash
cd ~/.arduino15/packages/arduino/hardware/avr/1.8.6/bootloaders/caterina
make VID=0x2341 PID=0x8045 TARGET=Caterina-Micro-Millennium-Alpha
make VID=0x2341 PID=0x8046 TARGET=Caterina-Micro-Millennium-Beta
```

This requires the [LUFA 111009](https://github.com/abcminiuser/lufa) library
installed at the path referenced in the Makefile
(`../../../../../../LUFA/LUFA-111009` relative to the bootloader directory).

## Directory Structure

```
Arduino/
├── Makefile                    # Build and flash targets
├── arduino-cli.yaml            # arduino-cli configuration
├── PINOUT.md                   # Pin assignments and protocol reference
├── build/
│   ├── keypad/keypad.ino.hex   # Pre-built keypad firmware
│   └── display/display.ino.hex # Pre-built display firmware
├── sketches/
│   ├── keypad/keypad.ino       # Keypad Arduino source
│   └── display/display.ino     # Display Arduino source
└── libraries/
    ├── Keypad/                 # Keypad matrix library
    └── MagStripe/              # Magnetic stripe reader library
```
