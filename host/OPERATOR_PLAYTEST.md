# The Operator: "The Last Call" — physical playtest checklist

A hands-on pass on the real phone. The flow is verified in software (unit +
scenario tests, and `make operator-smoke`); this covers what needs **real
hardware and your ears**: the hook switch, the coin validator, the magstripe
reader, and audio on the earpiece. Full script/tree: `OPERATOR_STORY.md`.

## The goal (what the caller is doing)
Help the Operator finish a phone call frozen at 11:59 PM, 1999. Recover the
**three pieces of Ruth's number** from three key years, then **dial the whole
number** — the call connects and the clock turns to 2000. The Operator's voice
states the goal and the next step; the VFD shows `PIECES n/3` and the current hint.

## Before you start
1. Activate it (dashboard, or):
   ```bash
   curl -s -X POST -H 'Content-Type: application/json' \
     -d '{"action":"activate_plugin","plugin":"The Operator"}' http://<pi>/api/control
   ```
2. Software pre-flight: `make operator-smoke OP_HOST=<pi>` (add `RUN_DRIFT=1` for the slow leg).
3. Number/years (current tuning): pieces **36 / 41 / 55** → dial **364155** to win.
   Target years: **1955** (A, ±2), **1978** (B, **exact**), **1998** (C ±2, sealed).
   The hub no longer names the decade — clues are cryptic and a wrong year answers
   `TOO EARLY`/`TOO LATE`/`WARMER`. The finale **hides** the number (`6 DIGITS`);
   a coin at `READY` reveals it. Re-activating resets the session.

Clips live in `/usr/local/share/millennium/audio/`; a missing one is a silent no-op.

## Checklist  (V = VFD, A = audio, H = hardware path)

- [ ] **Greeting / hook switch.** Lift the handset.
      V: `TWO GIRLS, A RADIO` / `DIAL A YEAR  0/3`  ·  A: `op_intro` (states the mission)
      ·  H: lifting really transitions to IDLE_UP (no API nudge).
- [ ] **Homing feedback.** Dial `1 9 5 0`. V: `WARMER` / `A LITTLE LATER`. Dial `1 9 5 8`:
      `WARMER` / `A LITTLE EARLIER`. Dial `1 9 2 5`: `TOO EARLY`. (Deduce, then land it.)
- [ ] **Piece A.** Dial `1 9 5 5`. V: connecting → `1955 TWO SISTERS` / `1=LISTEN 2=SPEAK`.
      Press `1`. V: `PIECE A: 36` / `PIECES 1/3` · A: `era1_arrive`, then `era1_listen`.
- [ ] **Listen, don't speak (+ retire).** In a key era press `2`. V: `LINE TANGLED` · A: `px_tangle`.
      Press `1` to recover the piece. Thereafter the prompt reads `press 1=LISTEN` (SPEAK retired).
- [ ] **Piece B — the exact year.** Dial `1 9 7 7` → `WARMER`/`A LITTLE LATER`; only
      `1 9 7 8` opens the era → `1=LISTEN` → `1` → `PIECE B: 41` / `2/3`.
- [ ] **Sealed final year.** Dial `1 9 9 8`. V: `1998: SEALED` / `SWIPE PASS / COIN`.
- [ ] **Temporal pass (magstripe).** Swipe any card.
      V: `1998 A CARD` / `1=LISTEN 2=SPEAK`  ·  H: the reader fires `handle_card`.
      Press `1` → `PIECE C: 55` / `3/3`.  (A real coin also forces it open — try both.)
- [ ] **Coin validator — hints & time.** At the Operator, drop a coin → sharper hint
      naming the decade (`TRY THE 1950s` …) · A: `op_hint_*`. In an era, a coin → A:
      `coin_hold` (holds the line). ·  H: the validator registers the coin.
- [ ] **The last call.** With `3/3`: V: `DIAL HER NUMBER` / `6 DIGITS` (number hidden —
      a coin here reveals `36 41 55`). Dial `3 6 4 1 5 5`.
      V: `...RINGING...` → `11:59 ... 12:00` → `12:00  2000` / `SHE IS FREE`,
      then after a beat the coda `THE LINE IS QUIET` / `hang up: she's home`
      ·  A: `final_connect` → `final_clock` → `win_free`.
- [ ] **Temporal drift.** In an era, touch nothing ~30 s. V: pulled back to the Operator
      (`DIAL A YEAR`) · A: `drift_back`. (A coin in the era buys ~30 s more first.)
- [ ] **Grace reset.** Hang up, then lift again within ~30 s → resumes with progress
      (A: `op_resume`). Lift after ~30 s, or after a win → fresh story `0/3` (A: `op_intro`).
- [ ] **Hang up.** Handset down from anywhere → `THE OPERATOR` / `Lift: the last call`,
      and any clip/tone stops immediately.

## What to listen for (only judge-able here)
- Clip **clarity** through the TDA2822M earpiece amp at 8 kHz narrowband.
- Clip **volume vs the tones** (ringback/DTMF/coin chime). If clips are noticeably
  hotter/quieter, note it — levels can be normalized in `regen_clips.sh` and redeployed.
- Each clip **stops cleanly** on hang-up / dialing on. Pacing of the win sequence feels right.

## If something's off
- Wrong/no audio for a step: confirm the file exists, 16-bit PCM WAV, in the clip dir.
- Unexpected state: re-activate the plugin to reset the session.
- Coins/card/hook not registering: that's the Arduino serial path, not the plugin —
  see `phone-status` and `journalctl -u daemon`.
