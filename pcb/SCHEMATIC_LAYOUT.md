# Schematic Layout Guide (phonev4)

This document describes a proposed layout to isolate subsections for clarity. Rearrange in **KiCad Schematic Editor** by selecting groups and dragging to new positions. Connectivity is preserved when moving symbols in the GUI.

## Proposed Regions (mm coordinates)

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  TOP ROW (y ≈ 20–50)                                                         │
│                                                                              │
│  [Coin]          [Keypad]              [Display]          [Power]          │
│  x: 115–135      x: 65–175             x: 175–240          x: 250–300       │
│  J1, D2, C-coin1 A1, J2, J6            A2, J3              Q1, F1, U1, TP1  │
└─────────────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────────────┐
│  MIDDLE ROW (y ≈ 85–170)                                                     │
│                                                                              │
│  [I2C / Misc]   [Audio Amplifier]              [Audio Jacks]  [ESD]         │
│  x: 40–60       x: 95–150                      x: 215–260    x: 295–300     │
│  R1, R2         U2, C_inA, C_inB                J4, J5, J7    D1             │
│  TP4, TP5       C_outA, C_outB                                     │
│  C-arduino1     C_vcc, C_dec, C_ripple                               │
│  C-card1                                                                     │
└─────────────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────────────┐
│  BOTTOM ROW (y ≈ 185–245)                                                    │
│                                                                              │
│  [Raspberry Pi]                 [Power LED]     [GND / TP3]                   │
│  x: 35–95                       x: 115–125      x: 85–90                     │
│  A3, C-raspi1                   D3, R3          TP3                          │
│  TP2                                                                         │
└─────────────────────────────────────────────────────────────────────────────┘
```

## Subsection Details

### 1. Power (top-right, x: 250–300, y: 20–55)
- **Q1** — Reverse polarity FET
- **F1** — Fuse
- **U1** — XL6009 boost converter
- **TP1** — 5V_MAIN test point
- Place in order: input → Q1 → F1 → U1 (12V out). Keep 5V_MAIN and 12V_COIN labels clear.

### 2. Audio (center, x: 95–170, y: 125–165)
- **U2** — TDA2822M amplifier
- **C_inA, C_inB** — Input coupling caps
- **C_outA, C_outB** — Output coupling caps
- **C_vcc, C_dec, C_ripple** — Power bypass caps
- Group U2 with caps around it. Keep speaker_front_pre+, speaker_receiver_pre+ labels visible.

### 3. Audio Jacks (right of audio, x: 215–265, y: 85–145)
- **J4** — RJ9 handset
- **J5** — 3.5mm microphone
- **J7** — 3.5mm speaker/audio input
- **D1** — ESD protection (near J4)
- Cluster connectors; D1 clamps J4 signals.

### 4. Keypad (left, x: 65–100, y: 25–95)
- **A1** — Keypad Arduino
- **J2** — Keypad connector (2x10)
- **J6** — Card reader connector
- **C-arduino1, C-card1** — Decoupling (near A1)
- Place A1 central, connectors to the right.

### 5. Display (center-right, x: 175–240, y: 25–95)
- **A2** — Display Arduino
- **J3** — VFD display connector
- **C-arduino-display1** — Decoupling (near A2)
- Place A2 and J3 together.

### 6. Coin Validator (left of keypad, x: 115–135, y: 5–50)
- **J1** — Coin validator connector
- **D2** — ESD protection
- **C-coin1** — Decoupling
- Compact block; coin_tx, coin_rx labels connect to A2.

### 7. Raspberry Pi (bottom-left, x: 35–95, y: 190–220)
- **A3** — Pi Zero WH
- **C-raspi1** — Decoupling
- **TP2** — 3.3V test point
- Keep GPIO, 5V, GND labels clear.

### 8. Power LED (bottom center, x: 115–125, y: 215–230)
- **D3** — Power indicator LED
- **R3** — Current limit resistor
- Place on 5V_MAIN bus with short wires to GND.

### 9. I2C / Test Points
- **R1, R2** — I2C pull-ups (near I2C bus between A1 and A2)
- **TP4, TP5** — SDA, SCL test points
- **TP3** — GND test point (near power/GND area)

## Steps in KiCad

1. **Open** `phonev4.kicad_sch` in Schematic Editor.
2. **Select** all symbols in one subsection (Shift+click or drag box).
3. **Drag** the group to its target region.
4. **Re-route** any wires that cross or look messy (optional).
5. **Repeat** for each subsection.
6. **Run ERC** (Inspect → Electrical Rules Check) to verify connectivity.
7. **Update PCB** (File → Update PCB from Schematic) after layout changes.

## Notes

- Decoupling caps (C-arduino1, C-card1, etc.) stay near their powered device.
- Global labels (5V_MAIN, GND, coin_tx, etc.) keep nets connected across the sheet.
- Power symbols (#PWR01, etc.) move with their associated components.
- Leave space between subsections for labels and wire routing.
