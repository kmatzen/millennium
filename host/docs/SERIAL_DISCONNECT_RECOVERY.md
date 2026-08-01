# Serial/Display Disconnect During Call (#103)

When the display Arduino or serial link disconnects during a call (USB unplug, cable fault), here is the observed behavior.

## Detection

- `millennium_client_check_serial` runs periodically. If no read/write activity for `SERIAL_WATCHDOG_SECONDS` (60s), it sets `serial_healthy = 0`.
- On the next cycle, reconnect is attempted: `open_serial_port` closes the stale fd and opens the device again.

## What Continues

- **SIP call**: PJSIP (the PJSUA worker thread) runs separately and uses the network. The call continues; audio is independent of the serial link.
- **Plugin state**: Daemon state, plugin state (e.g. `classic_phone_data`), and display_manager state keep updating via `plugins_tick` / `display_manager_tick`. These run in the main loop regardless of serial.

## Display Re-sync on Reconnect

When `open_serial_port` succeeds:

1. `client->display_dirty = 1` is set if `client->display_message` exists.
2. `millennium_client_update` writes `display_message` to the display on the next cycle (throttled to ~33ms).
3. `display_message` is kept current by `display_manager_tick`, which is driven by the main loop. So it contains the latest "Call active | X:XX remaining" (or whatever the plugin last set) even while disconnected.

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
and that the backoff stays in range for an unbounded outage.

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
