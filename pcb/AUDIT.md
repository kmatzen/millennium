# Schematic and PCB Audit

This document captures audit findings for the phonev4 schematic and PCB design, plus recommendations. Run `python3 audit_schematic.py` in this directory to regenerate the machine-parsed report.

## Tools Available

1. **audit_schematic.py** — Python script that parses `phonev4.kicad_sch` (s-expression) and `phonev4.csv` to extract components, compare BOM vs schematic, check net labels, and flag documentation mismatches.

2. **kicad-cli** (command-line ERC/DRC):
   ```bash
   kicad-cli sch erc phonev4.kicad_sch -o erc_report.txt
   kicad-cli pcb drc phonev4.kicad_pcb -o drc_report.txt
   ```
   - **ERC**: Unconnected pins, undriven inputs, power pin issues.
   - **DRC**: Footprint library paths, silkscreen clipping, spacing.
   - Add `--exit-code-violations` to fail CI if violations exist.

3. **KiCad GUI** — Schematic → Inspect → ERC; PCB Editor → Inspect → DRC.

---

## Audit Findings Summary

| Category | Status | Action |
|----------|--------|--------|
| Power labels (5v, 12v) | Mismatch | Rename to 5V_MAIN, 12V_COIN per README |
| BOM vs schematic refs | D3/LED1, TP6/TP7 | Align references; update BOM for TP6=12V_COIN |
| Q1 part number | Si2319 vs Si2301 | Verify pin compatibility; update schematic or BOM |
| Missing footprints | D3, F1, TP1–TP5 | Assign footprints in schematic |
| PRTR package | SOT-143 in schem | BOM says SOT-23; PRTR5V0U2X has both — verify |
| Test points TP6/TP7 | Removed per docs | BOM still lists TX/RX; add TP6=12V_COIN, remove TP7 |

---

## Detailed Findings

### 1. Net Labels vs Documentation

- **Schematic**: Uses `5v`, `12v`, `gnd`
- **README**: Specifies `5V_MAIN`, `12V_COIN`, `gnd`
- **Action**: In KiCad schematic, use Find and Replace to rename:
  - `5v` → `5V_MAIN`
  - `12v` → `12V_COIN` (ensure XL6009 OUT+ is labeled)

### 2. BOM (phonev4.csv) Updates Required

- **LED1 vs D3**: Schematic uses D3 for the power LED. Either rename D3→LED1 in schematic, or update BOM to use D3.
- **TP6, TP7**: BOM lists TP1–TP7 (TX, RX). Per README, USB is via external hub — no TX/RX nets. Replace with TP6=12V_COIN. Remove TP7 or repurpose.
- **U1 footprint**: BOM shows "-" for U1 footprint; schematic has `misc_footprints:XL6009_module`. Confirm BOM row.

### 3. Part Discrepancies

- **Q1**: Schematic shows Si2319CDS, README/BOM specify Si2301. Both are P-ch MOSFETs. Check datasheets for pinout; update schematic symbol or BOM to match the actual part.
- **D1, D2 (PRTR5V0U2X)**: Schematic footprint SOT-143; BOM says SOT-23. PRTR5V0U2X is available in SOT-143 (4-pin) and possibly SOT-23 variants. Confirm which package is used and align.

### 4. Missing Footprints

| Ref | Value | BOM Footprint | Schematic | Action |
|-----|-------|---------------|-----------|--------|
| D3/LED1 | Green LED | LED_0805 | (none) | Assign LED_0805 or equivalent |
| F1 | 1A PTC | Fuse_1812 | (none) | Assign Fuse_1812 |
| TP1–TP5 | Test points | TestPoint | (none) | Assign test point footprint |
| TP6 | 12V_COIN | (add) | (add) | Add test point for 12V_COIN |

### 5. Wiring Verification (Manual)

Per README "Schematic Changes Required in KiCad":

- [ ] Q1 on incoming 5V rail, before F1 and U1
- [ ] F1 in series on incoming 5V
- [ ] U1 IN+ from 5V_MAIN, OUT+ to 12V_COIN, feeding J1 only
- [ ] D1, D2 clamp signal pins to 5V_MAIN and GND (not power rails)
- [ ] TDA2822 V+ from 5V_MAIN
- [ ] No TX/RX nets for Arduino–Pi path (USB via hub)

### 6. Connector Pinouts

Verify J1 (coin validator) pinout includes 12V_COIN and GND for the validator supply, plus TX, RX, RESET from display Arduino.

### 7. ERC / DRC Results (kicad-cli)

Running `kicad-cli sch erc` and `kicad-cli pcb drc` produces reports. Representative findings:

**ERC** (199 violations in last run):
- Unconnected pins on J2, J6, A1, A2, A3 (unused keypad/header/GPIO pins — many are intentional)
- `power_pin_not_driven` on A1 5V, GND; A2; U2 V+ — power symbols may need correct net assignment
- Review `erc_report.txt` and suppress expected violations via ERC exclusions where appropriate

**DRC** (27 violations):
- `lib_footprint_issues`: Footprint libraries (Connector_IDC, Capacitor_THT, etc.) not in default path — configure KiCad fp lib table or use project-local libs
- `silk_over_copper`: U1 pads — silkscreen clipped by solder mask; adjust if needed

---

## Running the Audit Script

```bash
cd pcb
python3 audit_schematic.py
```

Output includes: component table, net labels, BOM vs schematic comparison, documentation alignment, part value discrepancies, and missing footprints.

---

## Related Documentation

- `README.md` — Power distribution, protection circuits, BOM, schematic change checklist
- `HARDWARE.md` — Physical assembly, power topology, cable routing
