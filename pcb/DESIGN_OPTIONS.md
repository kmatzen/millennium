# PCB Design Options: Bus Breakout & Direct UART

Design exploration for phonev5+ revisions.

## 1. Bus Breakout Connector (I/O on Separate Board)

**Goal:** Put J1–J7 (coin, keypad, VFD, handset, mic, card, speaker) on a separate I/O board, connected to the main board via a bus.

### Signal Count

| Connector | Pins | Notes |
|-----------|------|-------|
| J1 Coin validator | 10 | 12V, GND, TX, RX, RESET, + unused |
| J2 Keypad | 20 | 14 used (rows 0–3, cols 0–6, hook) |
| J3 VFD display | 26 | 14 used (D0–D7, WR, AD, RD, CS, TEST, RESET) |
| J4 Handset RJ9 | 4 | mic+, speaker+, GND |
| J5 Mic | 3 | sig, shield, switch |
| J6 Card reader | 14 | 3 used (CLS, RDT, RCL) |
| J7 Speaker | 3 | stereo L/R, GND |
| **Subtotal I/O** | **~80** | |
| Power (5V, GND, 12V_COIN) | 3 | |
| I2C (SDA, SCL) | 2 | Keypad ↔ Display |
| **Total** | **~85** | |

### Bus Connector Options

| Option | Connector | Pins | Cost | Notes |
|--------|-----------|------|------|-------|
| A | 2× 2×20 IDC ribbon | 80 | Low | Two 40-pin ribbons (keypad/VFD/coin on one, audio/card/handset on other) |
| B | 1× 2×25 IDC | 50 | Low | Single ribbon; need to reduce pins or use two boards |
| C | FFC/FPC (flex) | 40–60 | Medium | Single cable, smaller footprint |
| D | Board-to-board (e.g. Samtec) | 60–80 | Higher | More robust, good for modular layout |

**Recommended:** Option A — two 2×20 (40-pin) IDC connectors. Split logically:
- **Bus A:** Keypad (J2), VFD (J3), Card (J6) → ~50 signals → use two rows of a 2×20 or a 2×25
- **Bus B:** Coin (J1), Handset (J4), Mic (J5), Speaker (J7), power, I2C → ~25 signals → one 2×20

Or a single 2×25 (50-pin) if all critical signals fit; some keypad/VFD pins could be dropped if not all keys/features are used.

### Main Board vs I/O Board

**Main board (with bus connector):**
- Arduino Micro ×2 (A1, A2)
- Raspberry Pi
- TDA2822M, XL6009
- Power input (Q1, F1)
- **Bus connector** (replaces direct J1–J7 footprint)
- USB hub connection (unchanged)

**I/O board (daughter / remote):**
- J1, J2, J3, J4, J5, J6, J7
- Bus connector (mates to main)
- Optional: local ESD/TVS on long lines (D1–D3 could move here)
- Routing: straight pass-through from bus pins to connector pins

### KiCad Implementation

1. Create a new schematic sheet for "I/O Board" with J1–J7 and a bus connector symbol.
2. On the main schematic, replace J1–J7 with a single "J_BUS" connector that carries all nets.
3. Define the bus pinout in a table (e.g. `BUS_PINOUT.md`) so both boards stay in sync.
4. For a two-board design, either:
   - One project with two PCB files (main + I/O), or
   - One project, one PCB with a clearly defined "split line" for a future mechanical break.

---

## 2. Direct UART from Pi (Bypass USB for Arduino ↔ Pi)

**Goal:** Connect Pi UART (GPIO 14/15) directly to Arduino serial, avoiding USB hub and `/dev/ttyACM*`.

### Current Architecture

- **Pi ↔ Display Arduino:** USB CDC (SerialUSB) at 9600 baud
- **Pi ↔ Keypad Arduino:** Indirect — keypad → display (I2C) → display → Pi (USB)
- **Pi UART:** GPIO 14 (TX), GPIO 15 (RX) exist on the Pi header but are **unconnected** on phonev4

### Challenge: Arduino Pin Usage

| Arduino | HW UART (pins 0/1) | USB |
|---------|--------------------|-----|
| Keypad (A1) | MagStripe RDT, RCL | Not used for Pi |
| Display (A2) | VFD RD, AD | **Used for Pi** |

The display Arduino uses its only hardware UART (Serial1) for the VFD. Pi communication is over SerialUSB (native USB). So we **cannot** simply wire Pi UART to Arduino pins 0/1 on the display without reallocating VFD pins.

### Options for Direct UART

#### Option 2a: Repin Display Arduino

- Move VFD RD and AD to two other GPIO pins (e.g. 18, 19).
- Use pins 0 (RX) and 1 (TX) for Pi UART.
- **Level shifting required:** Pi = 3.3V, Arduino Micro = 5V. Use bidirectional shifter (e.g. TXS0108E, BSS138, or 74LVC245).
- **Firmware:** Use `Serial1` for Pi instead of `SerialUSB`. Daemon would open `/dev/ttyS0` or `/dev/serial0` instead of `/dev/serial/by-id/...`.

**Pros:** One UART link, no USB for display
**Cons:** Pin shuffle, level shifter, firmware changes, lose USB identity (no by-id)

#### Option 2b: Add USB-UART Bridge on Pi

- Add a USB-UART IC (e.g. CH340, CP2102) on the PCB, fed from Pi 3.3V.
- Connect its UART side to the display Arduino’s Serial1 (with level shifting).
- Pi talks to the bridge as `/dev/ttyUSB0`. Same idea as "direct" UART but still USB from the Pi’s perspective.

**Pros:** No change to Arduino pinout
**Cons:** Extra part, still USB on the Pi side, just different topology

#### Option 2c: Keep USB, Improve Topology

- Use a Pi 4/5 with multiple USB ports → no hub, simpler wiring.
- Or keep Pi Zero + hub, but this doesn’t address "direct from Pi."

#### Option 2d: Pi UART ↔ Keypad Only

- Keypad doesn’t talk to the Pi directly today; everything goes through the display.
- To use Pi UART, we’d need the keypad to talk to the Pi over UART instead of I2C→display→USB. That’s a protocol/architecture change.

**Conclusion:** Direct Pi UART is possible if we repin the display (Option 2a) and add level shifters. It removes USB for that link and avoids hub/ enumeration issues, at the cost of pin changes, new parts, and firmware updates.

---

## Summary

| Question | Feasible? | Approach |
|----------|-----------|----------|
| Bus breakout for I/O? | **Yes** | Add a multi-pin bus (e.g. 2×20 or 2×25 IDC) to the main board; create an I/O daughter board with J1–J7; document pinout. |
| Pi UART for Arduino? | **Yes, with changes** | Repin VFD RD/AD, use Serial1 for Pi, add 3.3V↔5V level shifter, update daemon to use `/dev/ttyS0`. |

If you want to proceed, the next step is a concrete pinout table for the bus connector and (if desired) a proposed VFD repin + UART schematic fragment.
