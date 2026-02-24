# Serial/Display Disconnect During Call (#103)

When the display Arduino or serial link disconnects during a call (USB unplug, cable fault), here is the observed behavior.

## Detection

- `millennium_client_check_serial` runs periodically. If no read/write activity for `SERIAL_WATCHDOG_SECONDS` (60s), it sets `serial_healthy = 0`.
- On the next cycle, reconnect is attempted: `open_serial_port` closes the stale fd and opens the device again.

## What Continues

- **SIP call**: Baresip runs in a separate thread and uses the network. The call continues; audio is independent of the serial link.
- **Plugin state**: Daemon state, plugin state (e.g. `classic_phone_data`), and display_manager state keep updating via `plugins_tick` / `display_manager_tick`. These run in the main loop regardless of serial.

## Display Re-sync on Reconnect

When `open_serial_port` succeeds:

1. `client->display_dirty = 1` is set if `client->display_message` exists.
2. `millennium_client_update` writes `display_message` to the display on the next cycle (throttled to ~33ms).
3. `display_message` is kept current by `display_manager_tick`, which is driven by the main loop. So it contains the latest "Call active | X:XX remaining" (or whatever the plugin last set) even while disconnected.

**Result**: After reconnect, the display shows the correct state (e.g. "Call active | 2:15 remaining") because the SDK re-sends the last display content.

## Buffered Updates While Disconnected

While the serial link is down:

- `millennium_client_write_to_display` still updates `client->display_message` and sets `display_dirty = 1`.
- The actual `write()` to the fd may fail (broken link), but the in-memory `display_message` is updated.
- When reconnect succeeds, we have the latest content and re-send it.

## Testing

A scenario test that unplugs the serial mid-call is difficult without hardware. Manual testing: start a call, unplug USB, wait for "Serial link recovered" in logs, verify the display shows current call state.
