# PCB Design

This directory contains the PCB design files created with [KiCad](https://www.kicad.org/).

## Versions

### phonev4 (current)

The active design. Simplified audio amplification and power supply compared to v2.

### phonev2 (archived)

The original design with dual LM358 op-amp audio circuits. Superseded by v4.

### Changes from v2 to v4

| Area          | v2                              | v4                                    |
|---------------|---------------------------------|---------------------------------------|
| Audio amp     | 2x LM358 dual op-amp           | 1x LM386 audio power amplifier       |
| Power supply  | Raw power connector (J8)        | XL6009 boost converter module (U1)    |
| Handset jack  | 4-pin header (J5, 4p4c)        | RJ9 jack (J4)                         |
| Audio jacks   | Pin headers for speaker/mic     | 3.5mm stereo jacks (J5 mic, J7 spk)  |
| Motor output  | J7 motor connector              | Removed                               |
| Passives      | 9 resistors, 17 capacitors      | 2 resistors, 9 capacitors             |
| Decoupling    | 11x 0.1µF axial caps            | 5x 0.1µF (4 SMD + 1 axial)           |

## Bill of Materials (phonev4)

| Ref   | Value / Part                  | Qty | Footprint                       | Notes                        |
|-------|-------------------------------|-----|----------------------------------|------------------------------|
| A1    | Arduino Micro (keypad)        | 1   | Socket headers                   | Millennium Alpha board       |
| A2    | Arduino Micro (display)       | 1   | Socket headers                   | Millennium Beta board        |
| A3    | Raspberry Pi Zero WH          | 1   | ADA3708 footprint                | Adafruit ADA3708             |
| U1    | XL6009 boost converter        | 1   | Module                           | 5V output                    |
| U2    | LM386 audio amplifier         | 1   | DIP-8                            | Speaker/ringer driver        |
| R1    | 10kΩ resistor                 | 1   | Axial                            | I2C pull-up (SDA)            |
| R2    | 10kΩ resistor                 | 1   | Axial                            | I2C pull-up (SCL)            |
| C1    | Electrolytic (axial, large)   | 1   | CP_Axial_L18mm_D10mm_P25mm      | LM386 output coupling        |
| C2    | Electrolytic (axial)          | 1   | CP_Axial_L11mm_D8mm_P15mm       | LM386 bypass                 |
| C3    | Electrolytic (axial)          | 1   | CP_Axial_L11mm_D8mm_P15mm       | Power filter                 |
| C4    | 0.1µF ceramic                 | 1   | 0603 SMD                         | LM386 decoupling             |
| C-*   | 0.1µF ceramic                 | 5   | SMD / small axial                | Decoupling (see below)       |
| J1    | Coin validator                | 1   | 2x5 pin header                   | 10-pin IDC                   |
| J2    | Keypad                        | 1   | 2x10 pin header                  | 20-pin IDC                   |
| J3    | VFD display                   | 1   | 2x13 pin header                  | 26-pin IDC                   |
| J4    | Handset (RJ9)                 | 1   | RJ9 jack                         | 4P4C handset connector       |
| J5    | Microphone                    | 1   | 3.5mm stereo jack                | Handset mic input            |
| J6    | Card reader                   | 1   | 2x7 pin header                   | 14-pin for magstripe reader  |
| J7    | Speaker                       | 1   | 3.5mm stereo jack                | Ringer/speaker output        |

### Decoupling Capacitors (C-*)

| Ref                | Location                |
|--------------------|-------------------------|
| C-arduino1         | Near keypad Arduino     |
| C-arduino-display1 | Near display Arduino    |
| C-raspi1           | Near Raspberry Pi       |
| C-coin1            | Near coin connector     |
| C-card1            | Near card reader        |

## Connector Pinouts

### J1 — Coin Validator (2x5)

See the coin validator documentation for the serial protocol. The PCB routes
SoftwareSerial (600 baud) from the display Arduino pins 14 (RX) and 23 (TX),
plus a reset line from pin 15.

### J2 — Keypad (2x10)

Routes to the keypad Arduino's row pins (6–9) and column pins (10–13, 18–20),
plus the hook switch pins (4, 5, 21). See `Arduino/PINOUT.md` for the complete
mapping.

### J3 — VFD Display (2x13)

8-bit parallel data bus (D0–D7) plus control signals (WR, AD, RD, CS, TEST,
RESET) routed from the display Arduino. See `Arduino/PINOUT.md` for the pin-
to-VFD-pin mapping with wire colors.

### J6 — Card Reader (2x7)

MagStripe reader connections routed to the keypad Arduino: CLS (pin 22),
RDT/data (pin 0), RCL/clock (pin 1).

## I2C Bus

The two Arduinos communicate over I2C (SDA/SCL). The PCB provides 10kΩ pull-up
resistors (R1, R2) to 5V on both lines.

## Power Distribution

The XL6009 boost converter (U1) provides regulated 5V from the phone line
voltage. Power is distributed to:

- Both Arduino Micro boards (5V pin)
- Raspberry Pi Zero WH (5V via GPIO header)
- VFD display (5V logic supply; VFD filament uses its own internal supply)
- LM386 amplifier
- Coin validator (5V)
- MagStripe reader (5V via Arduino)

## Files

| File              | Description                                  |
|-------------------|----------------------------------------------|
| `phonev4.kicad_pro` | KiCad project file (v4, current)           |
| `phonev4.kicad_sch` | Schematic                                  |
| `phonev4.kicad_pcb` | PCB layout                                 |
| `phonev4.kicad_prl` | KiCad preferences (local)                  |
| `phonev2.kicad_pro` | KiCad project file (v2, archived)          |
| `phonev2.kicad_sch` | Schematic (v2)                             |
| `phonev2.kicad_pcb` | PCB layout (v2)                            |
| `phonev2.kicad_prl` | KiCad preferences (v2)                     |
| `phonev4.csv`      | BOM export (currently from v2 — needs update)|
| `phone.kicad_sym`  | Custom symbol library                       |

## Manufacturing

The Gerber files were produced from the v4 PCB layout and uploaded to
[JLCPCB](https://jlcpcb.com) for fabrication.

To reproduce:
1. Open `phonev4.kicad_pro` in KiCad 7+
2. Use **Plot** (File → Plot) to export Gerber files
3. Use **Generate Drill Files** for drill data
4. Upload to your PCB fabricator

## Known Issues

1. **BOM CSV is stale**: `phonev4.csv` contains the v2 BOM, not v4. Regenerate
   from KiCad: **Tools → Edit Symbol Fields → Export**.

2. **No Gerber files in repo**: The exported manufacturing files are not version
   controlled. Consider adding them or documenting the exact plot settings.

3. **No assembly drawing**: There is no board silkscreen diagram or assembly
   guide showing component placement orientation (especially electrolytic cap
   polarity).

4. **I2C pull-ups**: R1 and R2 (10kΩ to 5V) provide I2C pull-ups. For the short
   bus length on-board this should be adequate, but if the bus is extended off-
   board, lower values (4.7kΩ) may be needed.

5. **LM386 gain**: The schematic should be checked to confirm whether the LM386
   is configured for default gain (20) or boosted gain (200 with cap on pins
   1–8). The gain setting affects speaker volume and distortion.
