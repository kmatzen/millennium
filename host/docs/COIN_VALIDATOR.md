# Coin Validator Commands (#110)

The daemon sends 1-byte commands to the Mars/MEI TRC-6500 coin validator via the Arduino display firmware (I2C → CMD_COIN_CTRL → UART to validator).

## Observed Usage

| Byte | Context | Purpose |
|------|---------|---------|
| `'a'` (0x61) | Hook up → IDLE_UP | Enable/accept coins |
| `'c'` (0x63) | Hook down, remote hangup, call ended | Cancel/reject coins |
| `'f'` (0x66) | Serial reconnect, call incoming | Re-init / enable validator |
| `'z'` (0x7a) | After `'c'` or `'f'` | Commit/execute (sequence terminator?) |

Note `'a'` travels alone — `daemon.c` sends no `'z'` after it. Only `'c'` and `'f'` are terminated.

## Serial Reconnect (#110, #239)

When the serial link recovers (`open_serial_port` succeeds), the SDK restores the
gate to whatever state the daemon last asked for, replaying that call site's exact
byte sequence. This mirrors how the same reconnect path re-sends the last display
contents (see `SERIAL_DISCONNECT_RECOVERY.md`).

The gate state is tracked in `client->coin_gate_cmd`, updated on every
`millennium_client_write_to_coin_validator` call; the replay decision lives in
`coin_gate.c` and is unit-tested:

| Tracked gate | Replayed on reconnect |
|---|---|
| `'a'` (handset up) | `'a'` |
| `'c'` (handset down) | `'c'` `'z'` |
| `'f'` (call incoming) | `'f'` `'z'` |
| nothing yet | `'c'` `'z'` |

Untracked defaults to reject, which is the fail-safe direction: an armed validator
with the handset on the hook physically swallows a coin that `handle_coin_event`
then refuses to credit, because it only credits in `IDLE_UP`.

**Previously (#239)**: this path sent a bare `'f'` unconditionally. That both
dropped the `'z'` every other `'f'` call site pairs with — so the re-init may never
have committed — and armed the validator regardless of hook state. Replaying the
tracked gate fixes both without introducing any byte sequence the daemon does not
already send elsewhere, so it needs no new assumption about what the TRC-6500 does
with an unterminated `'f'`.

**Still open**: whether `'z'` really is a commit/terminator is unconfirmed against
the hardware; the table above just preserves each call site's existing pairing.

**Risk (unchanged)**: a coin mid-accept or mid-reject when the link dropped is
still unaccounted for.

**Mitigation (future)**: a non-blocking delay (e.g. 2 s) before the replay would
let in-progress coins settle. Would require storing the reconnect timestamp and
replaying on a later `check_serial` call.
