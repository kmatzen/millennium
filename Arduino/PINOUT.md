# Arduino Pinout Reference

This document maps every Arduino pin used by the firmware to its physical function and PCB connector.

## Board Assignments

| Board Name        | Ref | FQBN                            | USB Product Name  | Firmware         |
|-------------------|-----|---------------------------------|-------------------|------------------|
| Millennium Alpha  | A1  | `millennium:avr:millennium_alpha`  | Millennium Alpha  | `keypad.ino`     |
| Millennium Beta   | A2  | `millennium:avr:millennium_beta`   | Millennium Beta   | `display.ino`    |

Both are ATmega32U4-based (Arduino Micro clones) at 16 MHz. The custom board
definitions only change the USB VID/PID so the Pi can distinguish them via
`/dev/serial/by-id/`.

## Keypad Arduino (Millennium Alpha, A1)

| Arduino Pin | ATmega32U4 Pin | Function                     | Direction    | Notes                              |
|-------------|----------------|------------------------------|--------------|------------------------------------|
| 0           | PD2 (RXD1)     | MagStripe RDT (data)         | Input (ISR)  | Shared with HW UART RX — see note  |
| 1           | PD3 (TXD1)     | MagStripe RCL (clock)        | Input (ISR)  | Shared with HW UART TX — see note  |
| 4           | PD4            | Hook down sense              | Input pullup |                                    |
| 5           | PC6            | Hook up sense                | Input pullup |                                    |
| 6           | PD7            | Keypad row 3 (* 0 #)         | I/O          |                                    |
| 7           | PE6            | Keypad row 2 (7 8 9)         | I/O          |                                    |
| 8           | PB4            | Keypad row 1 (4 5 6)         | I/O          |                                    |
| 9           | PB5            | Keypad row 0 (1 2 3)         | I/O          |                                    |
| 10          | PB6            | Keypad col 0                 | I/O          |                                    |
| 11          | PB7            | Keypad col 1                 | I/O          |                                    |
| 12          | PD6            | Keypad col 2                 | I/O          |                                    |
| 13          | PC7            | Keypad col 3                 | I/O          | Extra columns (A-P keys)           |
| 18 (A0)     | PF7            | Keypad col 4                 | I/O          | Extra columns (A-P keys)           |
| 19 (A1)     | PF6            | Keypad col 5                 | I/O          | Extra columns (A-P keys)           |
| 20 (A2)     | PF5            | Keypad col 6                 | I/O          | Extra columns (A-P keys)           |
| 21 (A3)     | PF4            | Hook common pin              | Output       | Driven LOW to scan, HIGH at idle   |
| 22 (A4)     | PF1 (ADC1)     | MagStripe CLS (card present) | Input        |                                    |
| 2 (SDA)     | PD1            | I2C SDA (master)             | I/O          | To display Arduino                 |
| 3 (SCL)     | PD0            | I2C SCL (master)             | I/O          | To display Arduino                 |

### Notes

**Pins 0/1 (MagStripe) vs UART**: On the ATmega32U4, USB serial (`SerialUSB`)
uses the native USB peripheral, not the hardware UART on pins 0/1. So
`Serial1` (PD2/PD3) is free and there is no conflict between the MagStripe
interrupt pins and USB communication. This is correct.

**Hook switch technique**: Pin 21 is configured as OUTPUT in `setup()` and
driven LOW to scan the hook switch, then returned HIGH. A 50 ms debounce timer
prevents spurious events from switch bounce.

**4x7 keypad matrix**: Columns 3–6 map keys A–P which are not standard phone
keys. The daemon only processes `0`–`9`, `*`, `#`, so these extra keys are
silently ignored. They correspond to extra button positions on the Millennium
keypad PCB (volume, language, etc.).

## Display Arduino (Millennium Beta, A2)

| Arduino Pin | ATmega32U4 Pin | Function             | Direction | Notes                        |
|-------------|----------------|----------------------|-----------|------------------------------|
| 0 (RX)      | PD2 (RXD1)     | VFD RD (read strobe) | Output    |                              |
| 1 (TX)      | PD3 (TXD1)     | VFD AD (address)     | Output    |                              |
| 4           | PD4            | VFD WR (write strobe)| Output    |                              |
| 5           | PC6            | VFD D0               | Output    |                              |
| 6           | PD7            | VFD D1               | Output    |                              |
| 7           | PE6            | VFD D2               | Output    |                              |
| 8           | PB4            | VFD D3               | Output    |                              |
| 9           | PB5            | VFD D4               | Output    |                              |
| 10          | PB6            | VFD D5               | Output    |                              |
| 11          | PB7            | VFD D6               | Output    |                              |
| 12          | PD6            | VFD D7               | Output    |                              |
| 13          | PC7            | VFD RESET            | Output    |                              |
| 14 (MISO)   | PB3            | Coin validator RX    | Input     | SoftwareSerial at 600 baud   |
| 15          | PB1            | Coin validator RESET | Output    | Active-low reset             |
| 16          | PB2            | VFD TEST             | Output    |                              |
| 17 (SS)     | PB0            | VFD CS (chip select) | Output    |                              |
| 23          | N/A            | Coin validator TX    | Output    | SoftwareSerial (pin 23 is TX)|
| 2 (SDA)     | PD1            | I2C SDA (slave)      | I/O       | Slave address 0              |
| 3 (SCL)     | PD0            | I2C SCL (slave)      | I/O       |                              |
| USB         | Native USB     | SerialUSB to Pi      | I/O       | 9600 baud                    |

### VFD Pin Mapping (Noritake CU20026SCPB-T23A)

| VFD Pin | Signal | Arduino Pin | Wire Color |
|---------|--------|-------------|------------|
| 1       | D7     | 12          | gray       |
| 3       | D6     | 11          | white      |
| 5       | D5     | 10          | black      |
| 7       | D4     | 9           | brown      |
| 9       | D3     | 8           | red        |
| 11      | D2     | 7           | orange     |
| 13      | D1     | 6           | yellow     |
| 15      | D0     | 5           | green      |
| 17      | WR     | 4           | blue       |
| 19      | AD     | 0 (RD=1)    | violet     |
| 21      | RD     | 1 (AD=0)    | gray       |
| 23      | CS     | 17          | white      |
| 25      | TEST   | 16          | black      |
| 20      | RESET  | 13          | yellow     |

## I2C Bus

- **Master**: Keypad Arduino (A1) via `Wire.begin()`
- **Slave**: Display Arduino (A2) via `Wire.begin(8)`
- **Address**: 8 (`I2C_DISPLAY_ADDR` on both controllers)
- **Pull-ups**: External pull-ups required on SDA/SCL (check PCB)

### Protocol (Keypad → Display → Pi)

The keypad Arduino sends complete protocol-v2 frames over I2C. The display
Arduino's `receiveEvent` ISR forwards each byte unchanged over USB serial to the
Pi. Key, hook, credential, and diagnostic types are defined in
[`../docs/MCU_PROTOCOL.md`](../docs/MCU_PROTOCOL.md). The Wire buffer limits an
Alpha frame to 32 bytes and its credential payload to 24 bytes.

### Pi → Display Arduino (USB Serial)

All commands use the versioned frame described in `docs/MCU_PROTOCOL.md`.
Display, coin control, EEPROM program, and EEPROM verify are types `0x10`–`0x13`;
keepalive is `0x14`. Critical commands are acknowledged by sequence number and
replays are not executed twice.

### Coin Validator → Pi

When the coin validator sends a byte over SoftwareSerial, the display Arduino
wraps it as a `MCU_EVT_COIN` (`0x23`) frame and forwards it over USB serial.

## Custom Board Definitions

The board definitions are vendored in the repo (#256):
```
Arduino/hardware/millennium/avr/boards.txt
```

Key differences from stock Arduino Micro:

| Property       | Stock Micro    | Millennium Alpha  | Millennium Beta  |
|----------------|----------------|-------------------|------------------|
| USB PID (app)  | 0x0037         | 0x0045            | 0x0046           |
| USB PID (boot) | 0x8037         | 0x8045            | 0x8046           |
| Product name   | Arduino Micro  | Millennium Alpha  | Millennium Beta  |
| Bootloader     | Caterina-Micro | Caterina-Micro    | Caterina-Micro   |

The boards run **stock** Caterina bootloaders flashed at the factory. The old
hand-edited definitions named `Caterina-Micro-Millennium-Alpha.hex` / `-Beta.hex`
bootloaders that were never built; the vendored definitions drop those keys,
since burning a bootloader is not part of any workflow here.

The practical consequence is that the custom product name only applies while the
**sketch** is running. During the bootloader window both boards present as a
plain `Arduino Micro` — which is why `pi_flash.sh` flashes the
`usb-Arduino_LLC_Arduino_Micro-if00` path and resets only one board at a time
(#254).

If a board is ever bricked badly enough to need its bootloader reflashed, stock
`Caterina-Micro.hex` is the right image; the custom identity comes back as soon
as a sketch is uploaded.

## Known Issues and Recommendations

1. ~~**I2C address 0 is the general call address**~~. **Resolved** — display
   Arduino now uses `Wire.begin(8)` (`I2C_DISPLAY_ADDR`), and the keypad
   addresses it as `Wire.beginTransmission(8)`.

2. ~~**No hook switch debounce**~~. **Resolved** — `keypad.ino` now uses a 50 ms
   timer-based debounce (`DEBOUNCE_MS`) before reporting hook state changes.

3. ~~**`parseTrack2` has no bounds checking**~~. **Resolved** — all sentinel
   search loops use the `length` parameter as an upper bound.

4. ~~**`receiveEvent` ISR writes to `SerialUSB`**~~. **Resolved** — `receiveEvent`
   now writes to a lock-free ring buffer; `loop()` drains it to `SerialUSB`.

5. ~~**Blocking serial reads in `display.ino`**~~. **Resolved** — `waitForSerial()`
   implements a 2-second timeout (`SERIAL_TIMEOUT_MS`).

6. ~~**No watchdog timer**~~. **Resolved** — both sketches enable a 4-second
   watchdog (`WDTO_4S`) in `setup()` and pet it every `loop()` iteration (and
   inside the long EEPROM program/verify loops in `display.ino`).

7. ~~**Dead code**~~. **Resolved** — `#if 0` debug blocks and unused `vfdtest()`
   have been removed.

8. ~~**Coin EEPROM data is hardcoded**~~. **Resolved** — `coinEeprom[]` in
   `display.ino` now has a block comment documenting the structure: global config,
   per-coin-type acceptance windows (nickel, dime, quarter, dollar), calibration
   block, and hardware provenance (TRC-6500).
