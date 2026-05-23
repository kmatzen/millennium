# Millennium Plugin System

The Millennium daemon is a pluggable experimentation platform: the active
**plugin** decides how the phone behaves, and you can switch plugins live from
the web dashboard (or REST API) — no restart, and the choice persists across
reboots.

> Writing your own plugin? See **[PLUGIN_AUTHORING.md](PLUGIN_AUTHORING.md)**
> for a step-by-step guide, and **[plugin_sdk.h](plugin_sdk.h)** for the API.

## How it works

The daemon owns the hardware and the state machine and routes events to the
active plugin's callbacks (`plugins.h: plugin_t`):

| Callback | Fires on |
|----------|----------|
| `handle_activation` | becoming the active plugin |
| `handle_coin` | a coin is accepted |
| `handle_keypad` | any key (`0`–`9`, `*`, `#`, `A`–`D`) in any state |
| `handle_hook` | handset lifted / hung up |
| `handle_call_state` | a SIP call changes state |
| `handle_card` | a magstripe card is swiped |
| `handle_tick` | every main-loop tick (~30 Hz) — animation/timeouts |

Unused callbacks are `NULL`. Plugins drive the hardware back through the
**Plugin SDK** (`plugin_sdk.h`): display, audio tones, calls, phone state,
coin balance, logging, and randomness. Every SDK call is NULL-safe and no-ops
when hardware is absent, so plugins run unchanged on the Pi, in the scenario
simulator, and in unit tests.

The registry is enumerated dynamically (`plugins_get_count` / `plugins_get_info`
/ `plugins_to_json`), so a newly registered plugin appears in the dashboard and
`GET /api/plugins` automatically — there is no hard-coded list.

## Built-in plugins

| Plugin | Coins? | What it does |
|--------|--------|--------------|
| **Classic Phone** | yes | Traditional payphone: dial 10 digits, VoIP call via Baresip, coin return on failure. Free/emergency numbers configurable. |
| **Fortune Teller** | 25¢ | Pick a category (1–5), receive a mystical fortune. |
| **Jukebox** | 25¢ | Choose a song (1–9); WAV playback via ALSA. `*` stop, `#` menu. |
| **Number Guess** | configurable | Hi-Lo guessing game: find the hidden number 1–99 with higher/lower hints. |
| **Simon** | free | Memory game: repeat a growing tone sequence (keys 1–4). |
| **Dial-A-Joke** | free | Press a key for a joke; setup then a timed punchline. |
| **Trivia** | free | True/False quiz (press 1 or 2); 3 questions, then a score. |

Game plugins read optional per-plugin settings from `daemon.conf` (see
`daemon.conf.example`); several expose a "forced" value (e.g. `guess.secret`,
`simon.seq`, `joke.index`, `trivia.start`) so scenario tests are deterministic.

## Switching plugins

From the dashboard (`http://<pi>:8081`) use the **Plugins** panel, or via REST:

```bash
curl -X POST http://<pi>:8081/api/control \
  -H 'Content-Type: application/json' \
  -d '{"action":"activate_plugin","plugin":"Trivia"}'
```

The **Play** panel on the dashboard injects coins, key presses, and hook
events, and the VFD is mirrored live — so you can play any plugin from a
browser.

## Testing

- **Scenario tests** (`tests/*.scenario`, run via the simulator under
  `make test`) drive plugins end-to-end. The `activate_plugin <name>` command
  selects a plugin; `key` accepts the full keypad.
- **Unit tests** (`tests/unit_tests.c`) cover the registry, JSON enumeration,
  and pure plugin logic.

All plugins ship with tests; `make test` runs the full suite on any platform
(no Baresip/ALSA needed).
