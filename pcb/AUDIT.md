# Schematic and PCB Audit

This document captures audit findings for the phonev6 schematic and PCB design, plus recommendations. Run `python3 audit_schematic.py` in this directory to regenerate the machine-parsed report.

## Tools Available

1. **audit_schematic.py** — Python script that parses `phonev6.kicad_sch` (s-expression) and `phonev6.csv` to extract components, compare BOM vs schematic, check net labels, and flag documentation mismatches.

2. **kicad-cli** (command-line ERC/DRC):
   ```bash
   kicad-cli sch erc phonev6.kicad_sch -o erc_report.txt
   kicad-cli pcb drc phonev6.kicad_pcb -o drc_report.txt
   ```
   - **ERC**: Unconnected pins, undriven inputs, power pin issues.
   - **DRC**: Footprint library paths, silkscreen clipping, spacing.
   - Add `--exit-code-violations` to fail CI if violations exist.

3. **KiCad GUI** — Schematic → Inspect → ERC; PCB Editor → Inspect → DRC.

---

## Audit Findings Summary

| Category | Status | Action |
|----------|--------|--------|
| Power labels (5v, 12v) | ✓ Fixed | Renamed to 5V_MAIN, 12V_COIN |
| BOM vs schematic refs | ✓ Fixed | D1–D4, TP1–TP5, U1; see phonev6.csv |
| Q1 part number | ✓ Doc | IRF9540N (TO-220 THT), G-S-D pinout for Si2319 drop-in |
| Missing footprints | ✓ Fixed | THT assigned for D4, F1, TP1–TP5 |
| TVS diodes | ✓ Fixed | D1, D2, D3 P6KE6.8CA DO-15; D4 Green LED 5mm |
| Test points | ✓ Fixed | TP1–TP5 only; no TX/RX on PCB |

---

## Detailed Findings

### 1. Net Labels vs Documentation

- **Status:** ✓ Fixed. Schematic and PCB use `5V_MAIN`, `12V_COIN`, `gnd`.

### 2. BOM (phonev6.csv)

- **Status:** ✓ Aligned. D1–D4, TP1–TP5, F1/Fuse_Radial, R3/R_Axial, U1/XL6009_module.

### 3. Part Discrepancies

- **Q1**: IRF9540N, TO-220 THT, G-S-D pinout (drop-in replacement for Si2319/Si2301 SOT-23).
- **D1, D2, D3 (P6KE6.8CA)**: ✓ All three TVS diodes use P6KE6.8CA (600W bidirectional, DO-15 THT). Each clamps signal to GND.
- **D4 (Green LED)**: Power indicator LED, 5mm THT.

### 4. Missing Footprints

- **Status:** ✓ Fixed. THT footprints assigned: D4 (LED_D5.0mm), F1 (footprints:Fuse_Radial_D10.0mm_P5.00mm), TP1–TP5 (TestPoint_Loop).

### 5. Wiring Verification (Manual)

Per README "Schematic Changes Required in KiCad". Verify in KiCad: open schematic, highlight nets (Ctrl+click), trace power flow.

- [ ] Q1 on incoming 5V rail, before F1 and U1
- [ ] F1 in series on incoming 5V
- [ ] U1 IN+ from 5V_MAIN, OUT+ to 12V_COIN, feeding J1 only
- [ ] D1, D2, D3 clamp signal pins to GND (not power rails)
- [ ] D2: P6KE6.8CA pin 1 → GND, pin 2 → speaker_front+ (see SCHEMATIC_D2_CHANGES.md)
- [ ] TDA2822 V+ from 5V_MAIN
- [ ] No TX/RX nets for Arduino–Pi path (USB via hub)

**How to verify:** In KiCad schematic, use Edit → Find to locate each component. Inspect wire connectivity; net names (5V_MAIN, 12V_COIN, gnd) appear on wires. Run "Update PCB from Schematic" to sync; DRC will flag any copper issues.

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

## Capacitor Package Audit (see #83)

Aligned BOM, README, schematic with Digi-Key availability research:

| Ref | Value | Footprint | Notes |
|-----|-------|-----------|-------|
| C_outA, C_outB, C_vcc | 100µF 16V | CP_Radial_D8.0mm_P3.80mm | P2mm uncommon for 8mm; P3.8mm matches PCB, Digi-Key stock |
| C_ripple | 4.7µF | CP_Radial_D5.0mm_P2.00mm | Standard 5×11mm; readily available |
| C_inA, C_inB, C_dec, C-* | 100nF | C_Axial_L3.8mm_D2.6mm_P7.50mm | KEMET C1046 etc.; THT available |

**Digi-Key research:** 100µF 16V radial — common parts (Panasonic EEU-FC) use 6.3mm/2.5mm; 8mm parts typically 3.5–5mm pitch. Schematic had P2.00mm (not in KiCad lib); PCB uses P3.80mm. Restored P3.80mm for availability.

---

## Related Documentation

- `README.md` — Power distribution, protection circuits, BOM, schematic change checklist
- `HARDWARE.md` — Physical assembly, power topology, cable routing
