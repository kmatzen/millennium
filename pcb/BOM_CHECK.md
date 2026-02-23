# BOM vs Schematic Final Check

Comparison of schematic values/footprints vs BOM (phonev4.csv) and README desired values.

## Value Comparison (Schematic vs BOM Designation)

| Ref | Schematic Value | BOM / README Desired | Status |
|-----|-----------------|----------------------|--------|
| R1, R2 | 4.7k | 4.7k | ✓ |
| R3 | 1kΩ | 1kΩ | ✓ |
| C_outA | 100µF 16V | 100uF 16V | ✓ |
| C_outB | 100µF 16V | 100uF 16V | ✓ |
| C_vcc | 100µF 16V | 100uF 16V | ✓ |
| C_inA | 100nF | 100nF | ✓ |
| C_inB | 100nF | 100nF | ✓ |
| C_dec | 100nF | 100nF | ✓ |
| C-* (5×) | 0.1u | 100nF | ✓ (0.1µF = 100nF) |
| C_ripple | 4.7µF | 4.7uF | ✓ |
| F1 | 1A | 1A PTC | ✓ |
| D3 | Green LED | Green LED | ✓ |
| D1, D2 | PRTR5V0U2X | PRTR5V0U2X | ✓ |
| Q1 | Si2319CDS | Si2301 | ✓ Doc'd equivalent (pin-compatible) |
| U2 | TDA2822M | TDA2822M | ✓ |

**All schematic values match desired BOM/README values.**

## Footprint Comparison (Schematic vs BOM)

| Ref | Schematic Footprint | BOM Footprint | Status |
|-----|---------------------|---------------|--------|
| C_outA, C_outB, C_vcc | CP_Radial_D8.0mm_P3.80mm | CP_Radial_D8.0mm_P3.80mm | ✓ |
| C_ripple | CP_Radial_D5.0mm_P2.00mm | CP_Radial_D5.0mm_P2.00mm | ✓ |
| C_inA, C_inB | C_Axial_L3.8mm_D2.6mm_P7.50mm | C_Axial_L3.8mm_D2.6mm_P7.50mm | ✓ |
| C_dec | C_Axial_L3.8mm_D2.6mm_P7.50mm | C_Axial_L3.8mm_D2.6mm_P7.50mm | ✓ |
| C-* (5×) | C_Axial_L3.8mm_D2.6mm_P7.50mm | C_Axial_L3.8mm_D2.6mm_P7.50mm | ✓ |
| D1, D2 | SOT-143 | SOT-143 | ✓ |
| D3 | LED_D5.0mm | LED_D5.0mm | ✓ |
| F1 | Fuse_Radial_D10.0mm_P5.00mm | Fuse_Radial_D10.0mm_P5.00mm | ✓ |
| Q1 | SOT-23 | SOT-23 | ✓ |
| TP1-TP5 | TestPoint_Loop_D2.50mm_Drill1.0mm | TestPoint_Loop_D2.50mm_Drill1.0mm | ✓ |

## Action Items

None. BOM aligned with schematic.
