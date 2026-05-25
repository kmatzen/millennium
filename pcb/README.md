# PCB Design

This directory contains the PCB design files created with [KiCad](https://www.kicad.org/).

## Design History

- **phonev2** (removed): Dual LM358 op-amps, raw power connector, pin headers for audio.
- **phonev4** (previous fabrication): Single LM386 power amplifier for ringer only,
  XL6009 boost converter, RJ9 and 3.5mm jacks. Had two design flaws: speaker
  output coupling (#42) and unamplified handset earpiece (#44).
- **phonev5 (current schematic)**: TDA2822M dual-channel amplifier replacing the
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
  │                                  TDA2822 OUT_A (pin 1) → C_outA → speaker_front+ → ringer
  │
  └─ Right channel → J7 ring → C_inB → TDA2822 IN_B (pin 4)
                                       TDA2822 OUT_B (pin 5) → C_outB → speaker_receiver+ → J4 RJ9 → handset
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
| 6   | V+         | `5V_MAIN`                    |
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
| U1    | XL6009 boost converter        | 1   | Module                           | 12V output (from 5V input), coin validator supply |
| U2    | TDA2822M                      | 1   | DIP-8                            | Dual audio amplifier             |
| R1    | 4.7kΩ resistor                | 1   | Axial                            | I2C pull-up (SDA)                |
| R2    | 4.7kΩ resistor                | 1   | Axial                            | I2C pull-up (SCL)                |
| R3    | 1kΩ resistor                  | 1   | Axial                            | Power LED current limit          |
| C_inA | 100nF ceramic                 | 1   | Axial L3.8mm THT                 | TDA2822 ch A input coupling      |
| C_inB | 100nF ceramic                 | 1   | Axial L3.8mm THT                 | TDA2822 ch B input coupling      |
| C_outA| 100µF 16V electrolytic        | 1   | Radial D8mm P3.8mm                | TDA2822 ch A output coupling     |
| C_outB| 100µF 16V electrolytic        | 1   | Radial D8mm P3.8mm                | TDA2822 ch B output coupling     |
| C_vcc | 100µF 16V electrolytic        | 1   | Radial D8mm P3.8mm                | TDA2822 Vcc bypass               |
| C_ripple | 4.7µF electrolytic         | 1   | Radial D5mm P2.0mm               | TDA2822 ripple rejection         |
| C_dec | 100nF ceramic                 | 1   | Axial L3.8mm THT                 | TDA2822 Vcc decoupling           |
| C-*   | 100nF ceramic                 | 5   | Axial L3.8mm THT                 | Decoupling (see below)           |
| D1–D3 | P6KE6.8CA                     | 3   | DO-15 (THT axial)               | TVS clamp signal lines to GND (600W bidirectional)    |
| Q1    | AO3401A P-ch MOSFET           | 1   | SOT-23 (SMD, JLCPCB basic C15127) | Reverse polarity protection      |
| F1    | 1A PTC fuse                   | 1   | Radial D10mm THT                 | Resettable overcurrent fuse      |
| D4    | Red LED (0603, C2286 basic)   | 1   | LED_0603 SMD                     | Power indicator                  |
| J1    | Coin validator                | 1   | 2x5 pin header                   | 10-pin IDC                      |
| J2    | Keypad                        | 1   | 2x10 pin header                  | 20-pin IDC                      |
| J3    | VFD display                   | 1   | 2x13 pin header                  | 26-pin IDC                      |
| J4    | Handset (RJ9)                 | 1   | RJ9 jack                         | 4P4C handset connector          |
| J5    | Microphone                    | 1   | 3.5mm stereo jack                | Handset mic input                |
| J6    | Card reader                   | 1   | 2x7 pin header                   | 14-pin for magstripe reader     |
| J7    | Speaker                       | 1   | 3.5mm stereo jack                | Stereo audio from USB card      |
| TP1-5 | Test points                   | 5   | Through-hole loop D2.5mm         | 5V_MAIN, Pi 3.3V, GND, SDA, SCL (see Test Points section) |

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

A P-channel MOSFET (AO3401A, SOT-23 SMD; JLCPCB basic C15127) on the incoming 5V rail protects
against accidental reverse-polarity connections. Q1 is placed on the power
input *before* distribution and *before* feeding U1 IN+. The MOSFET's body
diode conducts during normal operation with near-zero voltage drop, and blocks
current when polarity is reversed.

### ESD / TVS Protection (D1, D2, D3)

Three P6KE6.8CA TVS diodes (600W bidirectional, THT DO-15) protect signal
lines from ESD and transients. Each clamps between the protected signal and
GND—one pin to signal, one to GND. D1 protects J4 (handset); D2 protects
speaker_front+ (ringer output from TDA2822); D3 protects the third signal
line. These protect against back-EMF and ESD on external connectors.

### Overcurrent Protection (F1)

A 1A resettable PTC fuse (F1) is placed on the incoming 5V rail, in series
with Q1, before power distribution and before U1. It protects the entire board
(including the boost converter input) from downstream shorts. The fuse
self-resets when the fault is cleared.

### Power Indicator (D4, R3)

A red LED on the 5V_MAIN rail provides visual confirmation that the board is
powered, useful when debugging inside the payphone enclosure.

## Connector Pinouts

### J1 — Coin Validator (2x5)

The coin validator is powered from the 12V_COIN rail (U1 output). The PCB
routes SoftwareSerial (600 baud) from the display Arduino pins 14 (RX) and 23
(TX), plus a reset line from pin 15. See the coin validator documentation for
the serial protocol.

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

The board is powered from an external 5V supply. Power flows as follows:

1. **Incoming 5V** enters the board and passes through:
   - Q1 (reverse polarity protection)
   - F1 (PTC fuse)
2. **5V_MAIN rail** (after Q1 and F1) distributes to:
   - Both Arduino Micro boards (5V pin)
   - Raspberry Pi Zero WH (5V via GPIO header)
   - VFD display (5V logic supply; VFD filament uses its own internal supply)
   - TDA2822M amplifier (pin 6)
   - MagStripe reader (5V via Arduino)
   - U1 IN+ (boost converter input)

3. **12V_COIN rail**: U1 (XL6009) boosts 5V → 12V. XL6009 pins: IN+ = 5V_MAIN,
   IN- = GND, OUT- = GND, OUT+ = 12V_COIN. The 12V_COIN rail powers the coin
   validator only (J1). Nothing else on the board uses 12V.

## Test Points

| Label | Signal   | Purpose                           |
|-------|----------|-----------------------------------|
| TP1   | 5V_MAIN  | Main 5V power rail                |
| TP2   | 3.3V     | Pi's 3.3V output (if accessible)  |
| TP3   | GND      | Ground reference                  |
| TP4   | SDA      | I2C data (Arduino ↔ Arduino)      |
| TP5   | SCL      | I2C clock                         |

The board has five test points (TP1–TP5), matching the BOM. The 12V_COIN rail
exists on the board but has no dedicated test point.

**Note on USB connectivity**: Arduino ↔ Pi communication is via USB through an
external hub — there are no discrete USB serial TX/RX nets on the PCB, so
(despite older docs) there are no TX/RX test points.

## Schematic Audit

Run `python3 audit_schematic.py` to check component/BOM alignment, net labels, and documentation consistency. See `AUDIT.md` for findings and action items.

## Files

| File              | Description                                  |
|-------------------|----------------------------------------------|
| `phonev5.kicad_pro` | KiCad project file                         |
| `phonev5.kicad_sch` | Schematic (updated with TDA2822M)          |
| `phonev5.kicad_pcb` | PCB layout (needs re-layout for new parts) |
| `phonev5.kicad_prl` | KiCad preferences (local)                  |
| `phonev5.csv`      | Bill of materials (updated)                 |
| `phone.kicad_sym`  | Custom symbol library (xl6009, TDA2822M, P6KE6.8CA) |
| `footprints.pretty/` | Project footprint lib (F1: Fuse_Radial_D10.0mm_P5.00mm) |
| `fp-lib-table` | Footprint library table (includes project footprints) |
| `jlcpcb/production_files/` | Gerbers zip, BOM, CPL (JLCPCB upload) |
| `audit_schematic.py` | Python script to audit schematic vs BOM vs README |
| `AUDIT.md`         | Audit findings and action items             |
| `SCHEMATIC_D2_CHANGES.md` | TVS diode wiring (D1, D2, D3 P6KE6.8CA) and net reference |

## Manufacturing

See **[JLCPCB_WORKFLOW.md](JLCPCB_WORKFLOW.md)** for fabrication steps.

Quick summary:
1. Run `./pcb/scripts/export_jlcpcb.sh` to generate `jlcpcb/production_files/GERBER-phonev5.zip`
2. Use KiCad's JLCPCB Fabrication Toolkit plugin for BOM/CPL
3. Upload all three from `jlcpcb/production_files/` to JLCPCB

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

7. **TDA2822 V+ (pin 6)**: Connect to `5V_MAIN` via C_vcc and C_dec bypass caps.

8. **TDA2822 RIPPLE (pin 7)**: Connect via C_ripple (4.7µF) to `gnd`.

### Protection circuit wiring

1. **Q1 (reverse polarity)**: Place on the incoming 5V rail *before* distribution
   and *before* U1. Source to incoming 5V, drain to 5V_MAIN bus, gate to source
   (or appropriate pull-down for PMOS). Protects the entire board including U1
   input.

2. **F1 (fuse)**: Insert in series on the incoming 5V rail, before Q1 or between
   Q1 drain and 5V_MAIN distribution. Protects the 5V supply and boost converter
   input from overcurrent.

3. **D1, D2, D3 (TVS)**: P6KE6.8CA bidirectional TVS. Each: pin 1 → GND,
   pin 2 → protected signal (e.g. speaker_receiver+, speaker_front+, or
   other external signal). Protects signal lines, not power.

4. **D4 + R3**: Connect from 5V_MAIN through R3 (1kΩ) through D4 to GND.

### Test points

Add test point symbols connected to: 5V_MAIN, 3.3V, GND, SDA, SCL, 12V_COIN.
Do not add TX/RX test points for USB serial; Arduino ↔ Pi communication uses
USB through an external hub, so there are no discrete USB serial nets on the
PCB.

## Known PCB Errata

1. **Missing trace: Reset 2 (GPIO27 → Arduino Beta RST)** — The PCB is missing a
   trace connecting the Pi's GPIO27/GEN2 (pin 13) to the display Arduino's RST
   pin. Worked around with a bodge wire on the assembled board. Must be added as
   a proper trace in the next KiCad layout revision.

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

## Circuit Improvement Ideas

Ideas for future revisions, ordered by likely impact.

### High priority

1. **Bulk capacitor on 5V input** — Add a 470–1000 µF electrolytic (16–25V) on
   the 5V rail (after Q1/F1, before distribution). Smooths supply during Pi boot
   current spikes and audio peaks.

2. **Bulk capacitor on 12V_COIN** — Add 100–220 µF (25V) near J1 (coin validator).
   Coin validators draw current spikes when the motor runs; the bulk cap reduces
   rail droop.

3. **Series resistor on coin validator reset** — If the Arduino drives the reset
   line directly to J1, add a 100–220 Ω series resistor. Limits fault current and
   reduces coupling.

### Medium priority

4. **I2C series resistors** — Add 22–33 Ω series resistors on SDA/SCL at each
   Arduino I2C port. Dampens ringing if cable lengths increase or in noisy
   environments.

5. **XL6009 thermal** — The 5V→12V boost at ~50–100 mA can get warm in a confined
   enclosure. Use adequate copper around the module; consider a small heatsink if
   airflow is limited.

6. **Undervoltage handling** — The bulk input cap (item 1) helps. If the 5V supply
   is marginal during boot, consider explicit UVLO or reset sequencing for
   critical loads.

### Lower priority

7. **ESD on audio jacks** — J5 (mic) and J7 (speaker) are externally accessible.
   Consider PRTR5V0U2X or similar TVS on their signal pins for robustness.

8. **Power LED brightness** — If D4 is too bright in a dark payphone, increase
   R3 (e.g., 2–4.7 kΩ) for dimmer indication.

9. **TDA2822M gain flexibility** — Add DNP footprints for optional input
   attenuation resistors so gain can be tuned without a respin.

### Documentation / mechanical

10. **Connector keying** — Verify J1 (coin validator) is keyed to prevent reverse
    insertion. Document pin-1 orientation clearly.

11. **Pi power path** — Document whether the Pi receives 5V only from the GPIO
    header or also via USB; affects power sequencing and fault behavior.
