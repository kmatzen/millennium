# TVS Diode Schematic: D1, D2, D3 (P6KE6.8CA)

**Rationale:** P6KE6.8CA (600W bidirectional TVS, DO-15 THT) protects signal lines from ESD and back-EMF. D1, D2, D3 each clamp a signal to GND.

## Implemented State (THT)

- **D1, D2, D3:** P6KE6.8CA (600W bidirectional TVS, DO-15 THT)
- **Pin 1:** GND
- **Pin 2:** Protected signal (e.g. speaker_receiver+, speaker_front+)

Clamps transient voltage between signal and GND (~10.5V max). In stock at DigiKey (Littelfuse), Mouser (Taiwan Semiconductor).

### D2 (speaker_front+)

- **D2:** P6KE6.8CA
- Pin 1 → GND; Pin 2 → `speaker_front+` (TDA2822 channel A output → ringer)
- No VCC connection; TVS clamps signal to GND only.

### Net Reference

| Net                 | Nodes                                        |
|---------------------|-----------------------------------------------|
| coin_rx             | A2 MISO, J1 Pin_3                             |
| speaker_front+      | C_outA pin 2, J2 Pin_19, **D2 pin 2**         |
| speaker_receiver+   | C_outB pin 2, **D1 pin 2**, J4 pin 2          |
| gnd (TVS pin 1)     | D1, D2, D3 pin 1 → GND                        |
