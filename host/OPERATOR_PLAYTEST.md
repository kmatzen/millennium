# The Operator: "The Last Call" — physical playtest checklist

A hands-on pass on the real phone. The flow is verified in software (unit +
scenario tests, `make test`); this covers what needs **real hardware and your
ears**: the hook switch, the keypad (including its printed letters), and audio on
the earpiece. Full design: `OPERATOR_STORY.md`.

## The goal
Help the Operator finish a call frozen at 11:59 PM, 1999. **Work out Ruth's
seven-digit number** by solving three puzzles, then **dial it** — the call
connects and the clock turns to 2000. The Operator's voice carries each riddle;
the VFD shows a terse prompt and what you've typed. Press **`*`** for a hint,
**`#`** to hear the riddle again. Coins and cards are not used.

## Before you start
1. Activate it (dashboard, or):
   ```bash
   curl -s -X POST -H 'Content-Type: application/json' \
     -d '{"action":"activate_plugin","plugin":"The Operator"}' http://<pi>/api/control
   ```
2. The answers (for checking): puzzle 1 = **484** (HUG), puzzle 2 = **51**,
   puzzle 3 = **58** → her number is **4845158**. Re-activating resets the session.

Clips live in `/usr/local/share/millennium/audio/`; a missing one is a silent no-op.

## Checklist  (V = VFD, A = audio, H = hardware path)

- [ ] **Greeting / hook switch.** Lift the handset. V: `WORD BY THE PHONE` /
      `SPELL IT  *=HINT` · A: `op_intro` (states the mission + first riddle) ·
      H: lifting really transitions to IDLE_UP.
- [ ] **Puzzle 1 — keypad letters.** The riddle: the word scratched by the phone,
      what they did instead of saying sorry = **HUG**. Read the **letters printed
      on the keys** and dial `4 8 4`. V: `> 48_` as you type → `GOT IT` /
      `HER #: 484`. *(Confirm the keypad actually has letters; if not, note it —
      the hint spells the mapping.)*
- [ ] **Hint ladder.** On any puzzle, press `*` three times: the Operator gives a
      nudge → a method → the answer. V updates each press; A plays the hint.
- [ ] **Wrong answer.** Enter a wrong code. V: `NOT IT` / `* hint  # clue` ·
      A: `wrong`. You can retry freely (no hard fail).
- [ ] **Puzzle 2 — logic.** V: `HER AGE IN 1999`. From the spoken facts (Ruth 19
      at the mother's death in '59; sister 11 yrs younger; call froze '99) →
      **51**. Dial `5 1` → `HER #: 48451`.
- [ ] **Puzzle 3 — cipher.** V: `CROSSED: 7 0`. Subtract 2 ("the two of us"),
      wrapping: 7→5, 0→8 → **58**. Dial `5 8` → `HER #: 4845158`.
- [ ] **The last call.** V: `DIAL HER NUMBER` / `484 51 58`. Dial `4 8 4 5 1 5 8`.
      V: `...RINGING...` → `11:59 ... 12:00` → `12:00  2000` / `SHE IS FREE`,
      then the coda `THE LINE IS QUIET` / `hang up: she's home`
      · A: `final_connect` → `final_clock` → `win_free`.
- [ ] **`#` repeat.** Mid-puzzle, press `#` — the Operator restates the riddle and
      your entry clears.
- [ ] **Grace resume.** Solve one puzzle, hang up, lift again within ~30 s →
      resumes where you were. After ~30 s, or after a win → fresh from puzzle 1.
- [ ] **Hang up.** Handset down → `THE OPERATOR` / `Lift: the last call`, and any
      clip stops immediately.

## What to listen for (only judge-able here)
- Clip **clarity** through the earpiece at 8 kHz, and **volume vs the DTMF beeps**.
- Each clip **stops cleanly** on hang-up / on dialing the next digit.
- The riddles are **understandable by ear** — the VFD is only a terse reminder.

## If something's off
- No audio at all: usually the dmix IPC — see the `ipc_perm` note in
  `asoundrc.example`; clear stale SysV IPC (`ipcs`/`ipcrm`) or reboot.
- Wrong/no clip for a step: confirm the file exists (16-bit PCM WAV) in the clip dir.
- Unexpected state: re-activate the plugin to reset the session.
