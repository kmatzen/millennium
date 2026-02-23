# PCB Design

This directory contains the PCB design files created with [KiCad](https://www.kicad.org/).

## Design History

- **phonev2** (removed): Dual LM358 op-amps, raw power connector, pin headers for audio.
- **phonev4** (previous fabrication): Single LM386 power amplifier for ringer only,
  XL6009 boost converter, RJ9 and 3.5mm jacks. Had two design flaws: speaker
  output coupling (#42) and unamplified handset earpiece (#44).
- **phonev4 (current schematic)**: TDA2822M dual-channel amplifier replacing the
  LM386, fixing both audio design flaws. Adds reverse polarity protection, ESD
  protection, power LED, fuse, test points, and improved I2C pull-ups.

## Audio Circuit (TDA2822M)

The TDA2822M is a dual low-voltage audio power amplifier in a DIP-8 package
(same footprint as the LM386 it replaces). It provides two independent amplified
channels, solving both the speaker coupling and quiet handset issues.

### Signal Paths

```
USB audio card (stereo 3.5mm)
  ├─ Left channel → J7 tip → C_inA → TDA2822 IN_A (pin 3)
  │                                    → OUT_A (pin 1) → C_outA → speaker_front+ → ringer
  │
  └─ Right channel → J7 ring → C_inB → TDA2822 IN_B (pin 4)
                                        → OUT_B (pin 5) → C_outB → speaker_receiver+ → J4 RJ9 → handset
```

Each channel has its own input coupling capacitor (100nF) and output coupling
capacitor (100µF). The channels are electrically isolated inside the TDA2822M,
eliminating the crosstalk present in the v4 LM386 design.

### TDA2822M Pin Connections

| Pin | Function   | Net                          |
|-----|------------|------------------------------|
| 1   | OUT_A      | → C_outA → `speaker_front+`  |
| 2   | GND        | `gnd`                        |
| 3   | IN_A       | `speaker_front_pre+` via C_inA |
| 4   | IN_B       | `speaker_receiver_pre+` via C_inB |
| 5   | OUT_B      | → C_outB → `speaker_receiver+` |
| 6   | V+         | `5v`                         |
| 7   | RIPPLE     | → C_ripple (4.7µF) to GND   |
| 8   | NC         | Not connected                |

### Component Values

| Ref       | Value     | Purpose                              |
|-----------|-----------|--------------------------------------|
| C_inA     | 100nF     | Input coupling, channel A (ringer)   |
| C_inB     | 100nF     | Input coupling, channel B (handset)  |
| C_outA    | 100µF 16V | Output coupling, channel A (ringer)  |
| C_outB    | 100µF 16V | Output coupling, channel B (handset) |
| C_vcc     | 100µF 16V | Vcc bypass                           |
| C_ripple  | 4.7µF     | Ripple rejection (pin 7)             |
| C_dec     | 100nF     | Vcc decoupling                       |

## Bill of Materials

| Ref   | Value / Part                  | Qty | Footprint                       | Notes                            |
|-------|-------------------------------|-----|----------------------------------|----------------------------------|
| A1    | Arduino Micro (keypad)        | 1   | Socket headers                   | Millennium Alpha board           |
| A2    | Arduino Micro (display)       | 1   | Socket headers                   | Millennium Beta board            |
| A3    | Raspberry Pi Zero WH          | 1   | ADA3708 footprint                | Adafruit ADA3708                 |
| U1    | XL6009 boost converter        | 1   | Module                           | 5V output                        |
| U2    | TDA2822M                      | 1   | DIP-8                            | Dual audio amplifier             |
| R1    | 4.7kΩ resistor                | 1   | Axial                            | I2C pull-up (SDA)                |
| R2    | 4.7kΩ resistor                | 1   | Axial                            | I2C pull-up (SCL)                |
| R3    | 1kΩ resistor                  | 1   | 0805 SMD                         | Power LED current limit          |
| C_inA | 100nF ceramic                 | 1   | SMD / small axial                | TDA2822 ch A input coupling      |
| C_inB | 100nF ceramic                 | 1   | SMD / small axial                | TDA2822 ch B input coupling      |
| C_outA| 100µF 16V electrolytic        | 1   | Radial D8mm                      | TDA2822 ch A output coupling     |
| C_outB| 100µF 16V electrolytic        | 1   | Radial D8mm                      | TDA2822 ch B output coupling     |
| C_vcc | 100µF 16V electrolytic        | 1   | Radial D8mm                      | TDA2822 Vcc bypass               |
| C_ripple | 4.7µF electrolytic         | 1   | Radial D5mm                      | TDA2822 ripple rejection         |
| C_dec | 100nF ceramic                 | 1   | 0603 SMD                         | TDA2822 Vcc decoupling           |
| C-*   | 100nF ceramic                 | 5   | SMD / small axial                | Decoupling (see below)           |
| D1    | PRTR5V0U2X                    | 1   | SOT-23                           | ESD protection on J4 (RJ9)      |
| D2    | PRTR5V0U2X                    | 1   | SOT-23                           | ESD protection on J1 (coin)     |
| Q1    | Si2301 P-ch MOSFET            | 1   | SOT-23                           | Reverse polarity protection      |
| F1    | 1A PTC fuse                   | 1   | 1812 SMD                         | Resettable overcurrent fuse      |
| LED1  | Green LED                     | 1   | 0805 SMD                         | Power indicator                  |
| J1    | Coin validator                | 1   | 2x5 pin header                   | 10-pin IDC                      |
| J2    | Keypad                        | 1   | 2x10 pin header                  | 20-pin IDC                      |
| J3    | VFD display                   | 1   | 2x13 pin header                  | 26-pin IDC                      |
| J4    | Handset (RJ9)                 | 1   | RJ9 jack                         | 4P4C handset connector          |
| J5    | Microphone                    | 1   | 3.5mm stereo jack                | Handset mic input                |
| J6    | Card reader                   | 1   | 2x7 pin header                   | 14-pin for magstripe reader     |
| J7    | Speaker                       | 1   | 3.5mm stereo jack                | Stereo audio from USB card      |
| TP1-7 | Test points                   | 7   | Through-hole pad                 | 5V, 3.3V, GND, SDA, SCL, TX, RX |

### Decoupling Capacitors (C-*)

| Ref                | Location                |
|--------------------|-------------------------|
| C-arduino1         | Near keypad Arduino     |
| C-arduino-display1 | Near display Arduino    |
| C-raspi1           | Near Raspberry Pi       |
| C-coin1            | Near coin connector     |
| C-card1            | Near card reader        |

## Protection Circuits

### Reverse Polarity Protection (Q1)

A P-channel MOSFET (Si2301 or equivalent) on the power input protects against
accidental reverse-polarity connections. The MOSFET's body diode conducts during
normal operation with near-zero voltage drop, and blocks current when polarity
is reversed.

### ESD Protection (D1, D2)

TVS diode arrays (PRTR5V0U2X) on the RJ9 handset connector (J4) and coin
validator header (J1) clamp ESD transients that could enter through externally
accessible connectors. These protect the Arduino I/O pins from static discharge
through the handset or coin slot.

### Overcurrent Protection (F1)

A 1A resettable PTC fuse on the 5V output of the boost converter limits current
in case of a downstream short. The fuse self-resets when the fault is cleared.

### Power Indicator (LED1, R3)

A green LED on the 5V rail provides visual confirmation that the board is powered,
useful when debugging inside the payphone enclosure.

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

### J4 — Handset (RJ9)

| Pin | Signal              |
|-----|---------------------|
| 1   | `mic+`              |
| 2   | `speaker_receiver+` |
| 3   | `gnd`               |
| 4   | `gnd`               |

### J6 — Card Reader (2x7)

MagStripe reader connections routed to the keypad Arduino: CLS (pin 22),
RDT/data (pin 0), RCL/clock (pin 1).

### J7 — Speaker/Audio (3.5mm stereo)

| Contact | Signal                 | Destination             |
|---------|------------------------|-------------------------|
| Tip     | `speaker_receiver_pre+`| → TDA2822 ch B → handset |
| Ring    | `speaker_front_pre+`   | → TDA2822 ch A → ringer  |
| Sleeve  | `gnd`                  | Ground                   |

## I2C Bus

The two Arduinos communicate over I2C (SDA/SCL). The PCB provides 4.7kΩ pull-up
resistors (R1, R2) to 5V on both lines — the standard recommended value for
100kHz I2C.

## Power Distribution

The XL6009 boost converter (U1) provides regulated 5V from the phone line
voltage. Power flows through Q1 (reverse polarity protection) and F1 (fuse),
then distributes to:

- Both Arduino Micro boards (5V pin)
- Raspberry Pi Zero WH (5V via GPIO header)
- VFD display (5V logic supply; VFD filament uses its own internal supply)
- TDA2822M amplifier
- Coin validator (5V)
- MagStripe reader (5V via Arduino)

## Test Points

| Label | Signal | Purpose                           |
|-------|--------|-----------------------------------|
| TP1   | 5V     | Main power rail                   |
| TP2   | 3.3V   | Pi's 3.3V output (if accessible)  |
| TP3   | GND    | Ground reference                  |
| TP4   | SDA    | I2C data                          |
| TP5   | SCL    | I2C clock                         |
| TP6   | TX     | USB serial TX (display → Pi)      |
| TP7   | RX     | USB serial RX (Pi → display)      |

## Files

| File              | Description                                  |
|-------------------|----------------------------------------------|
| `phonev4.kicad_pro` | KiCad project file                         |
| `phonev4.kicad_sch` | Schematic (updated with TDA2822M)          |
| `phonev4.kicad_pcb` | PCB layout (needs re-layout for new parts) |
| `phonev4.kicad_prl` | KiCad preferences (local)                  |
| `phonev4.csv`      | Bill of materials (updated)                 |
| `phone.kicad_sym`  | Custom symbol library (xl6009, TDA2822M)   |
| `gerbers/`         | Gerber files from previous v4 fabrication   |

## Manufacturing

The Gerber files in `gerbers/` correspond to the **previous** v4 fabrication
(with LM386). After completing the PCB layout for the updated schematic, new
Gerber files will need to be generated.

To regenerate from KiCad:
1. Open `phonev4.kicad_pro` in KiCad 7+
2. Open the schematic and run **Annotate** to assign references to new parts
3. Run **ERC** (Electrical Rules Check) to verify connectivity
4. Open the PCB editor and run **Update PCB from Schematic**
5. Place and route the new components
6. Use **Plot** (File → Plot) to export Gerber files
7. Use **Generate Drill Files** for drill data

## Schematic Changes Required in KiCad

The schematic file has been updated with the TDA2822M symbol and updated
component values. The following wiring changes need to be completed in KiCad's
schematic editor:

### Audio path rewiring

1. **J7 pin 1 (tip)**: Change net from `speaker_receiver+` to
   `speaker_receiver_pre+`. This is the pre-amplified handset signal that
   now feeds into the TDA2822 channel B input.

2. **TDA2822 IN_A (pin 3)**: Connect to `speaker_front_pre+` via input
   coupling cap C_inA.

3. **TDA2822 IN_B (pin 4)**: Connect to `speaker_receiver_pre+` via input
   coupling cap C_inB.

4. **TDA2822 OUT_A (pin 1)**: Connect via output coupling cap C_outA to
   `speaker_front+` (ringer speaker).

5. **TDA2822 OUT_B (pin 5)**: Connect via output coupling cap C_outB to
   `speaker_receiver+` (handset earpiece via J4).

6. **TDA2822 GND (pin 2)**: Connect to `gnd`.

7. **TDA2822 V+ (pin 6)**: Connect to `5v` via C_vcc and C_dec bypass caps.

8. **TDA2822 RIPPLE (pin 7)**: Connect via C_ripple (4.7µF) to `gnd`.

### Protection circuit wiring

1. **Q1 (reverse polarity)**: Insert between XL6009 OUT+ and the 5V
   distribution bus. Gate to OUT+, source to OUT+, drain to 5V bus.

2. **F1 (fuse)**: Insert in series between Q1 drain and the 5V bus.

3. **D1 (ESD on J4)**: Connect across J4 signal pins to GND/5V.

4. **D2 (ESD on J1)**: Connect across J1 signal pins to GND/5V.

5. **LED1 + R3**: Connect from 5V through R3 (1kΩ) through LED1 to GND.

### Test points

Add test point symbols connected to: 5V, 3.3V, GND, SDA, SCL, TX (USB serial),
RX (USB serial).

## Known Issues (resolved by this revision)

1. ~~**Speaker output coupling (#42)**~~ — **Fixed**: TDA2822M provides fully
   isolated dual channels. No more crosstalk between handset and ringer.

2. ~~**Handset earpiece too quiet (#44)**~~ — **Fixed**: Both channels are now
   amplified by the TDA2822M. The handset earpiece is no longer driven at
   unamplified line level.

## Remaining Considerations

1. **PCB layout**: The updated schematic requires a new PCB layout. The TDA2822M
   uses the same DIP-8 footprint as the LM386, but additional capacitors and
   protection components need board space.

2. **ALSA config update**: The ALSA `softvol` max_dB setting in
   `host/asoundrc.example` may need adjustment since the TDA2822M provides
   hardware amplification. Software gain can likely be reduced.

3. **Gain tuning**: The TDA2822M's default gain may need adjustment for
   comfortable volume. If too loud, input attenuation resistors can be added.
   If too quiet, the coupling cap values can be increased.

4. **Mounting holes**: Consider adding M3 mounting holes at board corners for
   mechanical support inside the payphone enclosure.
