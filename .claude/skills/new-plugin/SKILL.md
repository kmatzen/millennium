---
name: new-plugin
description: Scaffold a new Millennium experience plugin against the plugin_sdk facade, register it, wire it into the build, and add a scenario test. Use when asked to add a new plugin, game, or interactive experience to the phone.
---

# Add a new plugin

Plugins are the unit of "fun" on this platform — each is a `plugin_t` of event
handlers (`handle_coin`, `handle_keypad`, `handle_hook`, `handle_call_state`,
`handle_card`, `handle_activation`, `handle_tick`). Write new plugins against
**`plugin_sdk.h`** (NULL-safe, no-op-on-missing-hardware) so the same code runs
on the Pi, in the scenario simulator, and in unit tests. Canonical example:
`host/plugins/number_guess.c`. Full guide: `host/PLUGIN_AUTHORING.md`.

The registry is enumerated **dynamically** — once registered, a plugin appears
in `GET /api/plugins` and the dashboard automatically (no list to hand-edit
there). Activate at runtime: `POST /api/control` with
`{"action":"activate_plugin:<Name>"}`.

## Steps (working in `host/`)

1. **Copy the template:** `cp plugins/number_guess.c plugins/<name>.c`.
   Replace the game logic; build the `plugin_t` and call
   `plugins_register("<Display Name>", "<description>", &handlers)` inside
   `register_<name>_plugin(void)`. Use `plugin_sdk` calls for display/audio/
   calls/balance/RNG — don't touch hardware directly. C89 unless you need C99
   (then add to the C99 set like `jukebox`).

2. **Declare the registrar** in `plugins.h` (alongside lines ~71–77):
   ```c
   void register_<name>_plugin(void);
   ```

3. **Call it** in `plugins_init()` in `plugins.c` (with the other
   `register_*_plugin();` calls).

4. **Wire the build** in `host/Makefile` — add the object rule:
   ```make
   plugins/<name>.o: plugins/<name>.c plugins.h plugin_sdk.h config.h
   	gcc plugins/<name>.c -o plugins/<name>.o -c $(CFLAGS)
   ```
   then add `plugins/<name>.o` to **all** of: the `daemon:` deps line **and** its
   `gcc` link line, `SIM_OBJS`, and `UNIT_TEST_OBJS`. (Missing one = link error
   or the plugin silently absent from tests.)

5. **Add a scenario test** `tests/test_<name>.scenario` (see
   `tests/test_number_guess.scenario`). Force any RNG/cost via `config <key>
   <value>` so feedback is deterministic, then drive `activate_plugin <Name>`,
   `hook_up`, `coin`, `keypress`, and assert with `assert_display` /
   `assert_state`.

6. **Build & test on the Mac:**
   ```bash
   make test          # unit + all scenario tests
   ```
   Iterate until green.

7. **Deploy** to the phone with the `deploy-daemon` skill, then activate it via
   the dashboard or `POST /api/control`.

## Checklist
- [ ] `plugins/<name>.c` written against `plugin_sdk.h`
- [ ] prototype in `plugins.h`, call in `plugins_init()`
- [ ] object rule + added to daemon link, `SIM_OBJS`, `UNIT_TEST_OBJS`
- [ ] `tests/test_<name>.scenario` added
- [ ] `make test` green
