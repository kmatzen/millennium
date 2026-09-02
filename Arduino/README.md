# Arduino Firmware

Host and MCU communication uses the CRC-framed, negotiated protocol documented
in [`../docs/MCU_PROTOCOL.md`](../docs/MCU_PROTOCOL.md). Both sketches report
their role, release version, source build ID, protocol version, self-test result,
and previous AVR reset cause for OTA attestation and monitoring.

Two Arduino Micro boards (ATmega32U4) run the keypad/peripheral I/O:

| Board             | FQBN                            | Sketch             | Role                                        |
|-------------------|---------------------------------|--------------------|---------------------------------------------|
| Millennium Alpha  | `millennium:avr:millennium_alpha`  | `sketches/keypad`  | 4x7 keypad, magstripe reader, hook switch   |
| Millennium Beta   | `millennium:avr:millennium_beta`   | `sketches/display` | VFD display, coin validator, I2C→USB bridge |

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
    --fqbn millennium:avr:millennium_alpha --input-dir ./build/keypad

arduino-cli upload -p /dev/serial/by-id/usb-Arduino_LLC_Millennium_Beta-if00 \
    --fqbn millennium:avr:millennium_beta --input-dir ./build/display
```

## Building from Source

### Prerequisites

- [arduino-cli](https://arduino.github.io/arduino-cli) in your `PATH`
  (or set `ARDUINO_CLI=/path/to/arduino-cli`)
- Arduino AVR core: `arduino-cli core install arduino:avr`

That is the whole setup. The Millennium board definitions live in this repo
under `hardware/millennium/avr/` and the Makefile points `arduino-cli` at them,
so no machine-specific configuration is needed (#256).

### Build and Flash

```bash
make build              # Compile both sketches → build/
make install            # Flash both to connected Arduinos
make install_keypad     # Flash keypad only
make install_display    # Flash display only
make clean              # Remove build artifacts
```

**Recommended: build on macOS, deploy to Pi via GPIO reset**

The Pi has direct GPIO reset connections to both Arduinos:
- GPIO17/GEN0 (pin 11) → Arduino Alpha RST
- GPIO27/GEN2 (pin 13) → Arduino Beta RST

The deploy scripts assert reset via GPIO (open-drain: drive low, release to input), wait for
the sketch's port to disappear, then wait for the **bootloader's** port and flash that with
`avrdude`. Those are two different paths: a reset board runs stock Caterina, which enumerates
as `usb-Arduino_LLC_Arduino_Micro-if00`, not under the sketch's custom name. Pointing avrdude
at the sketch path instead reaches the running firmware and fails with
`butterfly_recv(): programmer is not responding` (#254). An external reset holds the
bootloader open for several seconds, so there is no tight window to hit. Requires
`raspi-gpio` and `avrdude` on the Pi.

```bash
./Arduino/deploy_display.sh [user@host]   # flash Beta (display)
./Arduino/deploy_keypad.sh  [user@host]   # flash Alpha (keypad)
# Default host: matzen@millennium-phone.local

# Or via make:
make deploy_display
make deploy_keypad
make deploy          # flash both (keypad first, then display)
```

If hex not pushed: `VIA_SCP=1`.

If `arduino-cli` is not in your `PATH`:

```bash
make build ARDUINO_CLI=/home/matzen/bin/arduino-cli
```

### Verifying a Build

The build is reproducible — recompiling from the same source with the same
arduino-cli and AVR core version produces identical hex files.

## Custom Board Definitions

The two boards are Arduino Micro clones with custom USB product names and
PIDs so the Pi can distinguish them via `/dev/serial/by-id/`. Building with
stock `arduino:avr:micro` is **not** a substitute — it would produce two
indistinguishable "Arduino Micro" ports, and both `pi_flash.sh` and the
daemon's `hardware.display_device` key off those names.

The definitions are vendored in this repo (#256):

```
hardware/millennium/avr/boards.txt     # millennium_alpha / millennium_beta
hardware/millennium/avr/platform.txt   # references arduino:avr
```

They reference the installed AVR core for toolchain, core and variant rather
than duplicating it, so nothing here pins a core version. Verified byte-identical
output across AVR core 1.8.6 and 1.8.8, and across a Pi and a Mac.

They previously existed only as hand-appended entries in the AVR core's own
`boards.txt` on one SD card, where any `arduino-cli core upgrade` would have
silently erased them.

Key differences from stock Arduino Micro:

| Property         | Stock Micro    | Millennium Alpha   | Millennium Beta    |
|------------------|----------------|--------------------|--------------------|
| USB PID (app)    | 0x0037         | 0x0045             | 0x0046             |
| USB PID (boot)   | 0x8037         | 0x8045             | 0x8046             |
| Product name     | Arduino Micro  | Millennium Alpha   | Millennium Beta    |

The full entries are in [`hardware/millennium/avr/boards.txt`](hardware/millennium/avr/boards.txt).

### Bootloaders

The boards run **stock** Micro Caterina bootloaders, flashed at the factory. The
custom VID/PID only takes effect once the sketch is running; during the
bootloader window both boards look like a plain Arduino Micro. That is why
`pi_flash.sh` flashes the `usb-Arduino_LLC_Arduino_Micro-if00` path, and why it
resets only one board at a time — two boards in the bootloader at once would
contend for that single path with no way to tell them apart (#254).

The vendored `boards.txt` deliberately omits the `bootloader.*` keys the old
hand-edited entries carried. They referenced
`Caterina-Micro-Millennium-Alpha.hex` / `-Beta.hex`, which were never built, and
burning a bootloader is not part of any workflow here.

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
├── hardware/millennium/avr/    # vendored board definitions (#256)
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
