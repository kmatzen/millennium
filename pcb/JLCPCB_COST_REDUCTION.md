# JLCPCB Assembly Cost Reduction: Basic vs Extended Parts

JLCPCB charges **$3 per unique extended component** (Economic PCBA) or **$1.50** (Standard PCBA). Basic parts have **no loading fee** because they are pre-loaded on pick-and-place machines. This document identifies extended parts in the phonev6 design and recommends basic alternatives where available.

## Cost Summary

| Category | Current | Basic Alternative | Savings (Economic) |
|----------|---------|-------------------|---------------------|
| Q1 P-MOSFET | Si2319A (C545576) extended | **AO3401A (C15127)** basic | $3 |
| R1,R2 4.7kΩ | 0805 C17673 basic | (already basic) | — |
| R3,R4,R5 1kΩ | 0805 C11702 basic | (already basic) | — |
| 100nF caps | 0805/0603 basic | C14663, C1525 | — |
| 100µF caps | CP_Elec (SMD) | C15008 (1206 MLCC) or C16133 (tantalum) | Consider for decoupling |
| U2 Audio amp | PT2308-S (C115492) extended | No basic dual audio amp | — |
| D1–D3 TVS | P6KE6.8CA (D_SMB) extended | Check SMB TVS basic options | TBD |
| F1 Fuse | 1812 SMD | Check basic fuse options | TBD |
| D4 LED | LED_0603 SMD | Basic LED options exist | — |
| Connectors | IDC, RJ9, PJ-320, Arduino, Pi, XL6009 | Usually extended | — |

## Implemented Replacements

### Q1 → AO3401A (Basic)

**Replaced:** Si2319A (C545576) → **AO3401A (C15127)** [JLCPCB basic]

- Same SOT-23 package, compatible pinout (G-S-D)
- AO3401A: 30V, 4A, 47mΩ @ 10V — sufficient for 5V reverse polarity (F1 is 1A)
- Saves $3 per board (Economic assembly)

### D4 LED → Red LED C2286 (Basic)

**Replaced:** Green LED (C125094) → **Red LED (C2286)** [JLCPCB basic, 0603]

- Same LED_0603 footprint
- Red is standard for power indicators
- Saves $3 per board if green was extended

### R1, R2 (4.7kΩ) → C17673 (Basic)

**Updated:** LCSC C17673 for 4.7kΩ 0805 resistors [JLCPCB basic]

### R3, R4, R5 (1kΩ) → C17513 (Basic)

**Updated:** LCSC C17513 for 1kΩ 0805 resistors [JLCPCB basic]

## Parts Already Basic or SMD

The design already uses many cost-effective choices:
- **Resistors:** 0805 (4.7k, 1k) — basic part C17673, C11702
- **Capacitors:** 0805 100nF, 0402 — basic MLCCs available
- **LED:** 0603 SMD — basic LED options in JLCPCB library
- **Fuse:** 1812 SMD (not radial THT) — check JLCPCB for basic 1A fuse

## Likely Extended (No Easy Basic Replacements)

| Part | LCSC | Notes |
|------|------|-------|
| PT2308-S (U2) | C115492 | Dual audio amp SOIC-8; no basic dual audio amp in JLCPCB library |
| P6KE6.8CA (D1–D3) | — | TVS D_SMB; SMD TVS often extended |
| Arduino sockets, ADA3708, XL6009 | — | Connectors and modules are typically extended |
| IDC headers, RJ9, PJ-320 | — | Connectors usually extended |

## Verification Steps

1. **Upload BOM to JLCPCB** — Use KiCad's JLCPCB Fabrication Toolkit (File → Fabrication Outputs → JLCPCB) and upload the generated BOM. JLCPCB will flag each part as basic or extended and show the total loading fees.
2. **Check active basic parts list** — [lrks.github.io/jlcpcb-economic-parts/active.html](https://lrks.github.io/jlcpcb-economic-parts/active.html) (updated periodically).
3. **JLCPCB Part Search** — [jlcpcb.com/parts/componentsearch](https://jlcpcb.com/parts/componentsearch) — filter by "Basic" to find alternatives.

## Other Cost Savings (Non-Component)

### Assembly Service Level

- **Economic PCBA**: $3 per extended part loading fee. Best when you have few extended parts (as after our replacements).
- **Standard PCBA**: $1.50 per extended part. Compare total quote—with fewer extended parts, Economic is often cheaper.

### Panelization

- **50+ units:** Use "Panel by JLCPCB" to combine boards. Reduces the $0.46/board minimum assembly processing surcharge.
- **Suggested:** 150–200 mm panel, 2–6 boards per panel (V-cut or mouse-bites).

### Order Quantity

- **Volume discounts (PCB fabrication):** 50+ boards + area > 10 m² triggers FR4 discounts.
- **Assembly:** Per-board cost drops with quantity; fewer setup amortization per unit.

### Fabrication Defaults

- **Solder mask:** Green (default) — no upcharge. Red/blue/black/white cost more.
- **Lead time:** Standard is cheaper than expedited.
- **Surface finish:** ENIG costs more than HASL; use HASL if acceptable.

### Parts Already Basic

- **C49678** (100nF 0805): Used for C1–C6, C9, C10 — already basic.
- No further capacitor swaps needed unless changing footprints.

## References

- [JLCPCB: Basic vs Extended Parts](https://jlcpcb.com/help/answers/detail/578-How-to-identify-basic-and-extended-parts-in-JLCPCB-parts-library)
- [JLCPCB Assembly Pricing](https://jlcpcb.com/help/article/pcb-assembly-price)
- [Economic Parts List (Basic/Preferred Extended)](https://lrks.github.io/jlcpcb-economic-parts/active.html)
