# Unused Pins (phonev4)

Pins reported as unconnected by ERC. Many are intentional (unused keypad/display
header pins, Raspberry Pi GPIO, etc.). Run `kicad-cli sch erc phonev4.kicad_sch`
to regenerate.

## A1 — Keypad Arduino (Millennium Alpha)

| Pin | Name | Notes |
|-----|------|-------|
| 3V3 | 3.3V | Regulator output; NC (power rail available if needed) |
| A5 | A5 | Unused analog input |
| AREF | AREF | Reference voltage, optional |
| RST1 | RESET | Reset button (optional) |
| RST2 | RESET | Reset header (optional) |

## A2 — Display Arduino (Millennium Beta)

| Pin | Name | Notes |
|-----|------|-------|
| 3V3 | 3.3V | Regulator output, unused |
| A0 | A0 | Unused analog input |
| A1 | A1 | Unused analog input |
| A2 | A2 | Unused analog input |
| A3 | A3 | Unused analog input |
| AREF | AREF | Reference voltage, optional |
| RST1 | RESET | Reset button (optional) |
| RST2 | RESET | Reset header (optional) |

## A3 — Raspberry Pi Zero WH

| Pin | Name | Notes |
|-----|------|-------|
| 1 | 3V3[1] | Pi 3.3V (power) |
| 3 | GPIO2/SDA | I2C SDA (may conflict with Arduino I2C) |
| 5 | GPIO3/SCL | I2C SCL |
| 7 | GPIO4/GPCKL0 | Unused GPIO |
| 8 | TXD0/GPIO14 | UART TX (USB used for Arduino↔Pi) |
| 10 | RXD0/GPIO15 | UART RX |
| 11 | GPIO17/GEN0 | Unused GPIO |
| 12 | GPIO18 | Unused GPIO |
| 13 | GPIO27/GEN2 | Unused GPIO |
| 15 | GPIO22/GEN3 | Unused GPIO |
| 16 | GEN4/GPIO23 | Unused GPIO |
| 17 | 3V3[2] | Pi 3.3V (power) |
| 18 | GEN5/GPIO24 | Unused GPIO |
| 22 | GEN/6GPIO25 | Unused GPIO |
| 24 | ~{CE0}/GPIO8 | Unused GPIO |
| 26 | ~{CE1}/~{GPIO7} | Unused GPIO |
| 27 | ID_SD | EEPROM (usually NC) |
| 28 | ID_SC | EEPROM (usually NC) |
| 29 | GPIO5 | Unused GPIO |
| 31 | GPIO6 | Unused GPIO |
| 32 | GPIO12 | Unused GPIO |
| 33 | GPIO13 | Unused GPIO |
| 35 | GPIO19 | Unused GPIO |
| 36 | GPIO16 | Unused GPIO |
| 38 | GPIO20 | Unused GPIO |
| 40 | GPIO21 | Unused GPIO |

## D2 — ESD protection (PRTR5V0U2X, J1)

All pins used. Pin 2 (I/O1) on coin_rx; pin 3 (I/O2) on coin_tx.

## J2 — Keypad (2x10)

| Pin | Notes |
|-----|-------|
| 15, 16, 17, 18 | Unused keypad matrix pins |

## J3 — VFD Display (2x13)

| Pin | Notes |
|-----|-------|
| 4, 6, 8, 10, 12, 14, 16, 18, 22, 24 | Unused display header pins |

## J6 — Card Reader (2x7)

| Pin | Notes |
|-----|-------|
| 1, 3, 5, 7, 8, 10, 13, 14 | Unused MagStripe reader pins |

## U2 — TDA2822M

| Pin | Name | Notes |
|-----|------|-------|
| 8 | NC | Not connected (per datasheet) |
