# Writing a Millennium Plugin

A plugin turns the payphone into whatever you want — a phone, a game, an art
piece, a prank line. You write a few callbacks, register them, and the daemon
routes hardware events (coins, keys, hook, card, calls, timer ticks) to your
code. The active plugin can be switched live from the web dashboard and
survives restarts.

This guide builds a plugin start to finish. The companion reference is
[`plugin_sdk.h`](plugin_sdk.h); the canonical example is
[`plugins/number_guess.c`](plugins/number_guess.c).

## The model

The daemon owns the hardware and the state machine. Your plugin is a set of
**callbacks** it invokes when something happens:

| Callback | Fires when… |
|----------|-------------|
| `handle_activation` | your plugin becomes the active one (set up + draw your first screen) |
| `handle_coin(value, code)` | a coin is accepted (`value` in cents) |
| `handle_keypad(key)` | **any** key is pressed: `0`–`9`, `*`, `#`, `A`–`D`, in every state |
| `handle_hook(up, down)` | the handset is lifted (`up`) or hung up (`down`) |
| `handle_call_state(state)` | a SIP call changes state |
| `handle_card(number)` | a magstripe card is swiped |
| `handle_tick()` | every main-loop tick (~30 Hz) — use for animation/timeouts |

Any callback you don't need is `NULL`. Return `0` from the `int`-returning
callbacks.

Drive the phone back through the **SDK** (`plugin_sdk.h`): show text, play
tones, place calls, read state, manage the coin balance, log, and get random
numbers. Every SDK call is NULL-safe and degrades to a no-op when the hardware
isn't present, so your plugin runs unchanged on the Pi, in the simulator, and
in unit tests.

## A minimal plugin

```c
#define _POSIX_C_SOURCE 200112L
#include <string.h>
#include "../plugins.h"
#include "../plugin_sdk.h"

static void on_activate(void) {
    sdk_display("Hello!", "Press any key");
}

static int on_key(char key) {
    sdk_beep(key);                 /* DTMF feedback */
    sdk_displayf("You pressed %c", key);
    return 0;
}

void register_hello_plugin(void) {
    plugins_register("Hello", "A one-screen demo",
        /* coin  */ NULL,
        /* key   */ on_key,
        /* hook  */ NULL,
        /* call  */ NULL,
        /* card  */ NULL,
        /* activate */ on_activate,
        /* tick  */ NULL);
}
```

## Wiring it in (5 steps)

1. **Create** `plugins/hello.c` (as above).
2. **Declare** the registration function in `plugins.h`:
   ```c
   void register_hello_plugin(void);
   ```
3. **Call** it from `plugins_init()` in `plugins.c`:
   ```c
   register_hello_plugin();
   ```
4. **Build** it — add an object rule in `Makefile` and append
   `plugins/hello.o` to the `daemon`, `SIM_OBJS`, and `UNIT_TEST_OBJS` lists:
   ```make
   plugins/hello.o: plugins/hello.c plugins.h plugin_sdk.h
   	gcc plugins/hello.c -o plugins/hello.o -c $(CFLAGS)
   ```
5. **Run** `make test`. Your plugin now appears automatically in the dashboard
   and `GET /api/plugins` — there's no list to update.

(`MAX_PLUGINS` in `plugins.c` is the registry cap; raise it if you add many.)

## Patterns worth copying

- **Coin balance.** The daemon already credits the shared balance before your
  `handle_coin` runs. Read it with `sdk_balance()`, charge with
  `sdk_spend_balance(n)`, and refund/return with `sdk_clear_balance()`. Don't
  re-add the coin yourself.
- **Receiver state.** Gate behaviour on `sdk_receiver_is_up()`; show a
  "Lift receiver" splash when it's down (every built-in does this).
- **Non-blocking delays.** Never `sleep()`. Record a deadline
  (`sdk_now() + n`) and check it in `handle_tick()` — see the celebration
  delay in `number_guess.c` or the playback pacing in `simon.c`. Always read
  the clock with `sdk_now()` (or age a timestamp with `sdk_elapsed(t)`), never
  `time(NULL)` directly: `sdk_now()` honors the scenario simulator's
  advanceable clock, so `wait` advances time instantly instead of really
  sleeping. (Resolution is 1 second.)
- **Config.** Read per-plugin settings from `daemon.conf` with
  `config_get_int/string/bool(config_get_instance(), "your.key", default)`.
  Exposing a "forced" value (e.g. `guess.secret`) makes scenario tests
  deterministic.
- **Display width.** The VFD is two 20-char lines (`DISPLAY_WIDTH`). A line at
  or under 20 chars is shown statically; a longer line auto-scrolls. The
  pipeline scrolls up to `DISPLAY_MAX_TEXT_LEN`-1 (255) chars per line; a line
  longer than that is silently truncated, so keep every display string within
  that budget — and ideally within `2 × DISPLAY_WIDTH` (40) so a two-line
  message fits without scrolling. The unit-test guardrail
  `test_plugin_display_lines_fit` enforces the no-truncation budget across the
  built-in plugins by round-tripping their static strings through the display;
  if you add a content table, expose it the way `trivia_display_strings` does
  and add it to that test.

- **Recorded audio.** Beyond the synthesised tones, `sdk_play_clip("name")`
  plays a recorded WAV clip (`<audio.clip_dir>/name.wav`) on the earpiece. It
  shares the tone channel (one sound at a time, stopped by `sdk_stop_audio()`),
  and a missing file is a harmless no-op — so clips are optional and can be
  layered after a fallback tone. See [`AUDIO_CLIPS.md`](AUDIO_CLIPS.md) for the
  format and authoring workflow.

## Testing

**Scenario tests** drive a plugin end-to-end through the simulator. Create
`tests/test_hello.scenario`:

```
activate_plugin Hello
assert_display Hello
key 5
assert_display You pressed 5
```

Useful scenario commands: `activate_plugin <name>`, `hook_up`/`hook_down`,
`coin <cents>`, `key <0-9*#A-D>`, `keys <digits>`, `card <number>`,
`wait <seconds>` (advances time and ticks), `tick [n]` (ticks without waiting),
`config <key> <value>`, `assert_display <text>`, `assert_state <name>`,
`assert_clip <text>` (the last clip a plugin requested via `sdk_play_clip`),
`print`. All `tests/*.scenario` files run under `make test`.

**Unit tests** (`tests/unit_tests.c`) cover pure logic. Keep game rules in
small, side-effect-free functions (e.g. `number_guess_compare`) and assert on
them directly.

## Reference

See [`plugin_sdk.h`](plugin_sdk.h) for the full API: time (`sdk_now`,
`sdk_elapsed`), display (`sdk_display`, `sdk_displayf`), audio (`sdk_beep`,
`sdk_coin_chime`, `sdk_dial_tone`, …), calls (`sdk_call`, `sdk_answer`,
`sdk_hangup`, `sdk_send_dtmf`), state (`sdk_state`, `sdk_receiver_is_up`,
`sdk_keypad`), balance (`sdk_balance`, `sdk_spend_balance`, …), logging
(`sdk_log`, `sdk_logf`), and randomness (`sdk_rand_below`, `sdk_rand_range`,
`sdk_rand_choice`).

## Session teardown on plugin switch

There is deliberately **no `handle_deactivation` callback**. When the active
plugin is switched away from, it is simply frozen — it stops receiving events
and ticks, and its static session struct keeps whatever was in it.

The daemon releases the resources *it* owns before activating the incoming
plugin (`sdk_release_session`, called from `plugins_activate`): a call in
progress is hung up, continuous audio is stopped, the keypad is cleared, and
the state drops to the idle state matching the physical handset.

Two consequences for plugin authors:

- **You do not need to clean up on the way out.** Do not rely on being told;
  you will not be.
- **Do your session reset in `handle_activation`, not on exit.** That is the
  only hook that runs, and it runs on every activation — boot, a restore from
  persistence, and every deliberate switch. `plugins/classic_phone.c`'s
  `classic_phone_on_activation` is the reference: it re-reads the coin balance
  from `sdk_balance()` and zeroes its own per-session flags.

The coin balance is deliberately **not** cleared on a switch — an operator
changing plugins should not swallow the customer's money — so read it fresh in
`handle_activation` rather than assuming it is zero.

If your plugin ever holds a resource the daemon cannot see, say a file handle
or an external connection, that is the case this design does not cover; raise
it rather than leaking it.
