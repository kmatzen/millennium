# The Operator — physical playtest checklist

A hands-on pass for the **The Operator** plugin on the real phone. Everything
here was verified in software (unit + scenario tests, and the web-API smoke
test); this checklist covers the parts that need **real hardware and your ears**:
the handset hook switch, the coin validator, the magstripe reader, and audio on
the earpiece.

## Before you start

1. Make it the active plugin (web dashboard, or):
   ```bash
   curl -s -X POST -H 'Content-Type: application/json' \
     -d '{"action":"activate_plugin","plugin":"The Operator"}' http://<pi>/api/control
   ```
2. Software pre-flight (optional but recommended) — confirms the state machine
   is healthy before you touch the phone:
   ```bash
   make operator-smoke OP_HOST=<pi>        # add RUN_DRIFT=1 for the slow drift leg
   ```
3. Re-activating the plugin resets session state (clears an armed paradox / the
   temporal pass). Do it between runs if you want a clean slate.

Audio clips live in `/usr/local/share/millennium/audio/` (see `AUDIO_CLIPS.md`).
A missing clip is a silent no-op, so anything you didn't install just won't speak.

## Checklist

Tick VFD (V), audio (A), and the hardware path (H) for each.

- [ ] **Greeting / hook switch.** Lift the handset.
      V: `TIME OPERATOR` / `DIAL A YEAR  #=BACK`  ·  A: `operator.wav` greeting
      ·  H: lifting really transitions to IDLE_UP (no API nudge).
- [ ] **Coin validator + fare.** With the handset up, drop a real coin.
      V: `CREDIT $0.25` (value matches the coin)  ·  A: coin chime
      ·  H: the validator credits the balance.
- [ ] **Travel to an era.** Dial `2 0 0 5` (fare 25¢ — insert a quarter first).
      V: `CONNECTING... / YEAR 2005`, then `STATIC YEARS`  ·  A: ringback, then
      `era_static.wav` ("Connecting. The static years…").
- [ ] **Era branches.** In the era press `1` (listen) then `#` (back).
      V: branch line ("…seven… nine…"), then back to the Operator.
- [ ] **Home, free.** Dial `2 0 0 0` (no fare).  V: `FROZEN MINUTE` · A: `era_frozen.wav`.
- [ ] **Insufficient funds.** Fresh lift, dial `2 0 0 5` with no coins.
      V: `NEED $0.25 / HAVE $0.00`. Now insert a quarter → it connects.
- [ ] **The 1999 drop.** Insert a quarter, dial `1 9 9 9`.
      V: `ALMOST 2000... / NEVER MIDNIGHT`, then `LINE DROPPED` → Operator
      ·  A: `drop.wav` ("…it always drops before midnight").
- [ ] **Sealed future refused.** Dial `2 0 8 0` with no pass.  V: `FUTURE SEALED`.
- [ ] **Temporal pass (magstripe).** Swipe any card.
      V: `TEMPORAL PASS / PASS ACCEPTED`  ·  H: the reader fires `handle_card`.
      Then dial `2 0 5 0` with enough coins (fare $1.50) → `SEALED FUTURE`.
- [ ] **Paradox.** Dial an era (e.g. `1 9 9 5`), in it press `2` (meddle →
      `TIMELINE BENT`), press `#`, then dial a *different* era (`2 0 0 5`).
      V: `LINE FAULT 19## / #=OPERATOR`  ·  A: `paradox.wav`.
- [ ] **Paradox survives a hang-up.** While armed, hang up and lift again, then
      dial a different era — still `LINE FAULT`. (Consequences persist for the
      session; re-activating the plugin is what clears it.)
- [ ] **Paradox repair.** From the fault press `#`, dial back to the source year
      (`1 9 9 5`), and press `1` (the opposite branch).  V: `TIMELINE / MENDED`.
- [ ] **Temporal drift.** In any era, touch nothing for ~30 s.
      V: snaps back with `TEMPORAL DRIFT / DIAL A YEAR`.
- [ ] **Hang up.** Place the handset down from anywhere.
      V: `THE OPERATOR / Lift to dial`  ·  A: any clip/tone stops immediately.

## What to listen for (audio judgement — only doable here)

- Clip **clarity** through the TDA2822M earpiece amp at 8 kHz narrowband.
- Clip **volume vs the tones** (dial/ring/busy/DTMF). If a clip is noticeably
  hotter or quieter than the tones, note it — clip levels can be normalized in
  the conversion step and redeployed without touching the daemon.
- No clipping/buzz, and that a clip **stops cleanly** on hang-up / dialing on.

## If something's off

- Wrong/no audio for one step: check the matching file exists and is 16-bit PCM
  WAV in the clip dir (`ls -la /usr/local/share/millennium/audio/`).
- Unexpected `LINE FAULT`: a paradox is still armed from a previous run —
  re-activate the plugin to reset.
- Coins not crediting / card not read / hook not switching: that's the Arduino
  serial path, not the plugin — see `phone-status` and `journalctl -u daemon`.
