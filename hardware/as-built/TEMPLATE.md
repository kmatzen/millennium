# Millennium phone as-built record

## Identity

- Device ID: REQUIRED
- Stable hostname: REQUIRED
- Enclosure/asset serial: REQUIRED
- Raspberry Pi serial: REQUIRED
- Keypad MCU USB serial: REQUIRED
- Display MCU USB serial: REQUIRED
- Owner/location: REQUIRED (private inventory reference is acceptable)
- Record date and operator: REQUIRED

## Installed provenance

- Source commit/tag: REQUIRED
- PCB revision: REQUIRED
- Schematic SHA-256: REQUIRED
- PCB layout SHA-256: REQUIRED
- BOM SHA-256: REQUIRED
- Gerber archive SHA-256: REQUIRED
- Keypad HEX SHA-256 and reported identity: REQUIRED
- Display HEX SHA-256 and reported identity: REQUIRED
- Host binary SHA-256 and `--version`: REQUIRED
- OTA trusted key IDs and public-key fingerprints: REQUIRED
- Content release identity and manifest digest: REQUIRED

## Physical construction

- Wiring deviations/bodge wires: REQUIRED
- Audio interface and ALSA profile: REQUIRED
- Power supplies and ratings: REQUIRED
- Assembly photo filenames and SHA-256 hashes: REQUIRED

## Electrical and recovery validation

| Test | Instrument/load | Minimum voltage | Result | Evidence/date |
| --- | --- | ---: | --- | --- |
| Cold boot | REQUIRED | REQUIRED | REQUIRED | REQUIRED |
| Ringer/audio peak | REQUIRED | REQUIRED | REQUIRED | REQUIRED |
| Coin validator operation | REQUIRED | REQUIRED | REQUIRED | REQUIRED |
| Controlled brownout | REQUIRED | REQUIRED | REQUIRED | REQUIRED |
| Arbitrary idle power loss | REQUIRED | REQUIRED | REQUIRED | REQUIRED |
| OTA download interruption | REQUIRED | REQUIRED | REQUIRED | REQUIRED |
| MCU flash interruption/recovery | REQUIRED | REQUIRED | REQUIRED | REQUIRED |
| Host activation interruption/rollback | REQUIRED | REQUIRED | REQUIRED | REQUIRED |

Acceptance requires no unexplained reset, corrupt persistent state, stuck coin
gate, unrecoverable MCU, or release that cannot return to a signed healthy
version.
