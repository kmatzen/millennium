# Serial/Display Disconnect During Call (#103)

When the display Arduino or serial link disconnects during a call (USB unplug, cable fault), here is the observed behavior.

## Detection

- `millennium_client_check_serial` runs periodically. If no read/write activity for `SERIAL_WATCHDOG_SECONDS` (60s), it sets `serial_healthy = 0`.
- On the next cycle, reconnect is attempted: `open_serial_port` closes the stale fd and opens the device again.

## What Continues

- **SIP call**: PJSIP (the PJSUA worker thread) runs separately and uses the network. The call continues; audio is independent of the serial link.
- **Plugin state**: Daemon state, plugin state (e.g. `classic_phone_data`), and display_manager state keep updating via `plugins_tick` / `display_manager_tick`. These run in the main loop regardless of serial.
- **Web API / metrics**: served by their own threads over the network. They keep working while the phone hardware is unreachable — see the escape hatch below.

## What Is Lost (#103)

**Every** phone input is lost for the duration, not just the display. The hook
switch, keypad and magstripe reader are on the *keypad* Arduino (Alpha), which has
no USB path of its own — it reaches the Pi as Alpha → I2C → Beta → USB. The coin
validator hangs off Beta's `SoftwareSerial`. So dropping Beta's USB link severs
all five channels at once.

Mid-call, the consequence is that **hanging up the handset does not end the call.**
The daemon never sees the `HD` event, so it never calls `millennium_client_hangup`.
The call then runs until the remote party hangs up, or a plugin's own tick-driven
timeout fires — `classic_phone` has `call.timeout_seconds` (default 300 s) and
`plugins_handle_tick` keeps running through the outage, but plugins without a
timeout (e.g. The Operator) will not end it at all.

Coins inserted during the outage are lost: if the gate was left open (`'a'`), the
mech still accepts them, but the `V` event cannot reach the daemon to be credited.

### Escape hatch

The web API is served over the network, independent of the serial link, so
`POST /api/control {"action":"handset_down"}` ends a call even while the phone
hardware is unreachable. This is the way out of the case above, and it is how the
verification run below was terminated.

### Caveat: hook state can go stale

Alpha emits `HU`/`HD` only on a debounced *transition*, so a hook change that
happens during the outage is lost permanently — the daemon's idea of the hook, and
therefore the coin gate replayed on reconnect (see `COIN_VALIDATOR.md`), can both
be wrong until the handset next moves. Related: #232.

## Verified Mid-Call (#103)

Confirmed on the phone with a live call up, by unbinding Beta from `cdc_acm` for
80 s and rebinding:

```
T+0    current_state 4 (CALL_ACTIVE), sip_registered 1
       ... watchdog fires at 61s, reconnect attempts 1-4 fail ...
T+80   current_state 4, sip_registered 1        <- call survived untouched
       reconnect attempt 5 -> Serial port reopened successfully
       Writing to coin validator: 97            <- gate replayed ('a', handset up)
       Writing message to display: Call active  <- display re-synced
T+120  current_state 4, health HEALTHY
```

The call was unaffected throughout and the display returned to the correct
call state without intervention.

## MCU Readiness and State Re-sync on Reconnect

When `open_serial_port` succeeds:

1. Opening Beta's Arduino Micro serial port resets the board. The daemon keeps
   the descriptor open for a 10-second readiness window and retries protocol
   `HELLO` once per second while the bootloader and sketch start.
2. Coin and display commands remain deferred until Beta answers `HELLO`. This
   prevents an early command from being lost and, critically, prevents a short
   negotiation timeout from closing and reopening the port in a reset loop.
3. Once the protocol is ready, the daemon replays the desired coin-gate state
   and marks the current display message dirty.
4. `millennium_client_update` writes that display message on the next cycle
   (throttled to ~33ms). `display_manager_tick` keeps it current while the link
   is unavailable.

**Result**: After reconnect, the display shows the correct state (e.g. "Call active | 2:15 remaining") because the SDK re-sends the last display content.

## Coin Gate Re-sync on Reconnect (#239)

The same reconnect path restores the coin validator's gate the same way it restores
the display: by replaying the last command the daemon sent (`'a'`, `'c'` `'z'`, or
`'f'` `'z'`), tracked in `client->coin_gate_cmd`. If the link died before the daemon
ever gated the validator, the reconnect rejects coins rather than accepting them.
See `COIN_VALIDATOR.md`.

## Buffered Updates While Disconnected

While the serial link is down:

- `millennium_client_write_to_display` still updates `client->display_message` and sets `display_dirty = 1`.
- The actual `write()` to the fd may fail (broken link), but the in-memory `display_message` is updated.
- When reconnect succeeds, we have the latest content and re-send it.

## Recovery Policy (#247)

The decision `millennium_client_check_serial` makes each pass — idle, keepalive,
declare dead, or retry the reopen — lives in `serial_recovery.c` as pure logic:

```c
serial_action_t serial_recovery_next_action(const serial_link_state_t *state);
int             serial_recovery_backoff_seconds(int reconnect_attempts);
```

`check_serial` samples the link into a `serial_link_state_t`, asks for the verdict,
and carries it out. Marking the link dead makes a retry due immediately, so it
re-asks and reconnects in the same pass.

The split exists because `check_serial` itself is stubbed out by **both**
`simulator.c` and `tests/unit_tests.c` — it touches real fds — so its policy had
no coverage at all, which is how #247 shipped. The pure half is unit-tested on any
platform, including the specific #247 state: fd closed while the health flag is
still set must resolve to `MARK_DEAD`, never `NONE`.

## Testing

Policy: covered by the `Serial Recovery` unit suite (`make test`), including the
keepalive/watchdog boundaries, that retries never give up while the link is down,
that the backoff stays in range for an unbounded outage, and that an Arduino boot
holds one descriptor open while retrying `HELLO` instead of repeatedly resetting
the board.

Execution: still needs hardware. Unbinding the display Arduino from its driver on
the Pi drives a real disconnect without touching the cable:

```bash
echo 1-1.3:1.0 | sudo tee /sys/bus/usb/drivers/cdc_acm/unbind   # ttyACM1 = Beta
sleep 75                                                        # past the 60s watchdog
echo 1-1.3:1.0 | sudo tee /sys/bus/usb/drivers/cdc_acm/bind
```

Expect retries backing off 2/4/8/16 s, `Serial port reopened successfully`, the
coin gate replayed (see `COIN_VALIDATOR.md`), and `/api/health` returning to
`HEALTHY` on its own. The `by-id` symlink is what makes the rebind transparent —
the port may come back as a different `ttyACM*`.
