# MCU wire protocol version 2

The Pi, display MCU (Beta), and keypad MCU (Alpha) use the same binary frame:

```text
0x7e | version | payload length | message type | sequence | payload | CRC16-BE
```

The length is the payload length (0–240 on the host, 0–100 on AVR). CRC-16/CCITT
uses polynomial `0x1021`, initial value `0xffff`, and covers every byte from
`version` through the end of the payload. A decoder ignores bytes until `0x7e`,
accepts partial reads, rejects unsupported versions and lengths, and resets
after a CRC failure. Payload bytes—including zero and `0x7e`—are unescaped
because length provides an unambiguous boundary.

## Negotiation and compatibility

On every serial open, the host sends `HELLO` (`0x02`) with `[minimum, maximum]`
protocol versions. Beta responds with its range. Version 2 is selected only
when the ranges overlap; otherwise the host marks the serial health gate failed.
OTA identity strings report `protocol=2`, and the signed manifest independently
attests both boards. Protocol 1 is not silently selected: rollback must restore
the matching protocol-1 host and both protocol-1 MCU images as one release.

## Types

- `0x01`: ACK (`acked sequence`, `status`; status 0 accepted, 1 busy)
- `0x02`: HELLO (`minimum version`, `maximum version`)
- `0x10`–`0x15`: display, coin control/program/verify, keepalive, identity commands
- `0x20`–`0x26`: key, hook, credential, coin, diagnostic, heartbeat, operation events

Alpha frames physical events over I2C; Beta forwards each complete frame to USB
without rewriting its type, sequence, payload, or CRC. The 32-byte AVR Wire
limit caps credential payloads at 24 bytes.

## Delivery and replay

Display and coin commands are critical. Beta ACKs an accepted sequence before
work begins, retains the last accepted sequence independently for the display
and coin subsystems, and ACKs a replay without executing it again. A busy
subsystem returns status 1. The host retries an unacknowledged frame after 500 ms
up to three times, then fails serial health and exports timeout metrics.

Long operations are cooperative state machines. The display deadline is five
seconds; coin control five seconds; EEPROM programming 45 seconds; verification
ten minutes with a two-second deadline per byte. Operation events include the
originating transaction sequence and completion/error code. The watchdog is
reset only after a complete main-loop pass, never inside a blocking operation.

MCUSR reset-cause bitmasks are captured in `.init3` before Arduino startup and
reported as diagnostics for both boards. The daemon exports reset counters and
the raw cause bitmasks for monitoring.
