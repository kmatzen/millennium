# phonev6 — Hand-assembly polarity sheet

Auto-generated from the schematic. For each part, verify the **silkscreen polarity mark** (bar, dot, plus, beveled corner, pin-1 chamfer) lines up with the **net listed at the marked pin** before reflow / iron.


## electrolytic cap

| Ref | Value | Package | Pin | Function | Net |
|-----|-------|---------|-----|----------|-----|
| `C7` | 100µF 16V | CP_Elec_6.3x5.4 | 1 | + (anode) | `5V_MAIN` |
|  | |  | 2 | − (cathode) | `GND` |
| `C11` | 100µF 16V | CP_Elec_6.3x5.4 | 1 | + (anode) | `Net-(U2-OUT1)` |
|  | |  | 2 | − (cathode) | `speaker_front+` |
| `C12` | 100µF 16V | CP_Elec_6.3x5.4 | 1 | + (anode) | `Net-(U2-OUT2)` |
|  | |  | 2 | − (cathode) | `speaker_receiver+` |

## LED

| Ref | Value | Package | Pin | Function | Net |
|-----|-------|---------|-----|----------|-----|
| `D4` | Red LED | LED_0805_2012Metric | 1 | K  (cathode, marked) | `GND` |
|  | |  | 2 | A  (anode) | `Net-(D4-A)` |
| `D8` | Green LED | LED_0805_2012Metric | 1 | K  (cathode, marked) | `GND` |
|  | |  | 2 | A  (anode) | `LED_3V3` |
| `D9` | Green LED | LED_0805_2012Metric | 1 | K  (cathode, marked) | `GND` |
|  | |  | 2 | A  (anode) | `LED_12V` |
| `D10` | Yellow LED | LED_0805_2012Metric | 1 | K  (cathode, marked) | `RESET1` |
|  | |  | 2 | A  (anode) | `LED_RST1` |
| `D11` | Yellow LED | LED_0805_2012Metric | 1 | K  (cathode, marked) | `RESET2` |
|  | |  | 2 | A  (anode) | `LED_RST2` |
| `D12` | Blue LED | LED_0805_2012Metric | 1 | K  (cathode, marked) | `sda` |
|  | |  | 2 | A  (anode) | `LED_SDA` |
| `D13` | Blue LED | LED_0805_2012Metric | 1 | K  (cathode, marked) | `scl` |
|  | |  | 2 | A  (anode) | `LED_SCL` |

## MOSFET (SOT-23)

| Ref | Value | Package | Pin | Function | Net |
|-----|-------|---------|-----|----------|-----|
| `Q1` | AO3401A | SOT-23 | 1 | G  (gate) | `GND` |
|  | |  | 2 | S  (source) | `Net-(Q1-S)` |
|  | |  | 3 | D  (drain) | `BOOST_IN+` |
