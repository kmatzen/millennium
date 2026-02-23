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
| Power labels (5v, 12v) | ✓ Fixed | Renamed to 5V_MAIN, 12V_COIN |
| BOM vs schematic refs | ✓ Fixed | D3, TP1-TP5, U1; see phonev4.csv |
| Q1 part number | ✓ Doc | Si2319/Si2301 pin-compatible (G-S-D); either acceptable |
| Missing footprints | ✓ Fixed | THT assigned for D3, F1, TP1–TP5 |
| PRTR package | ✓ Fixed | PRTR5V0U2X is SOT-143 only; BOM/README updated |
| Test points | ✓ Fixed | TP1-TP5 only; no TX/RX on PCB |

---

## Detailed Findings

### 1. Net Labels vs Documentation

- **Status:** ✓ Fixed. Schematic and PCB use `5V_MAIN`, `12V_COIN`, `gnd`.

### 2. BOM (phonev4.csv)

- **Status:** ✓ Aligned. D3, TP1-TP5, F1/Fuse_Radial, R3/R_Axial, U1/XL6009_module.

### 3. Part Discrepancies

- **Q1**: Schematic Si2319CDS, BOM Si2301. Both P-ch, SOT-23, identical pinout (Gate-Source-Drain). Pin-compatible; use whichever is available.
- **D1, D2 (PRTR5V0U2X)**: ✓ Fixed. PRTR5V0U2X is SOT-143 only (Nexperia datasheet). BOM and README updated from SOT-23 to SOT-143.

### 4. Missing Footprints

- **Status:** ✓ Fixed. THT footprints assigned: D3 (LED_D5.0mm), F1 (Fuse_Radial_D10mm), TP1–TP5 (TestPoint_Loop).

### 5. Wiring Verification (Manual)

Per README "Schematic Changes Required in KiCad". Verify in KiCad: open schematic, highlight nets (Ctrl+click), trace power flow.

- [ ] Q1 on incoming 5V rail, before F1 and U1
- [ ] F1 in series on incoming 5V
- [ ] U1 IN+ from 5V_MAIN, OUT+ to 12V_COIN, feeding J1 only
- [ ] D1, D2 clamp signal pins to 5V_MAIN and GND (not power rails)
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

## Related Documentation

- `README.md` — Power distribution, protection circuits, BOM, schematic change checklist
- `HARDWARE.md` — Physical assembly, power topology, cable routing
