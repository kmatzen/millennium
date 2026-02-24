# Coin Validator Commands (#110)

The daemon sends 1-byte commands to the Mars/MEI TRC-6500 coin validator via the Arduino display firmware (I2C → CMD_COIN_CTRL → UART to validator).

## Observed Usage

| Byte | Context | Purpose |
|------|---------|---------|
| `'a'` (0x61) | Hook up → IDLE_UP | Enable/accept coins |
| `'c'` (0x63) | Hook down, remote hangup, call ended | Cancel/reject coins |
| `'f'` (0x66) | Serial reconnect, call incoming | Re-init / enable validator |
| `'z'` (0x7a) | After `'c'` or `'f'` | Commit/execute (sequence terminator?) |

## Serial Reconnect (#110)

When the serial link recovers (`open_serial_port` succeeds), the SDK immediately sends `'f'` to re-init the validator. This may cause problems if:

- A coin was mid-accept or mid-reject when the link dropped
- The validator is in an uncertain state after a brief disconnect

**Risk**: Double-credit, lost coin, or validator stuck in weird state.

**Mitigation (future)**: A non-blocking delay (e.g. 2 s) before sending `'f'` would let in-progress coins settle. Would require storing reconnect timestamp and sending `'f'` on a later `check_serial` call.
