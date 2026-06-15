# The Operator: "The Last Call" — design reference

The reference for the `The Operator` plugin (`plugins/time_operator.c`). The code
wins where they drift — fix one or the other.

## Concept
A call has rung on this line since **11:59 PM, New Year's Eve 1999** — a woman
calling her estranged sister **Ruth** to make peace, frozen mid-ring. The
Operator can still connect it, but Ruth's **seven-digit number** is lost,
"scattered in the things she told me while she waited." The caller **works the
number out by solving three puzzles**, dials it, and the clock turns to 2000.

The right shape for a *payphone*: the climax is literally placing the call, and
the puzzles all run on the keypad you're already holding.

## Principles
- **Real puzzles, not guess-and-check.** Each answer is *deduced*; the story the
  Operator narrates **is the puzzle input** (relevant content, not decoration).
- **Hard by default, never a dead end.** A three-step **hint ladder** (press `*`)
  escalates to the answer, so a stuck caller is never permanently blocked.
- **Nothing gated on flaky hardware.** Only the keypad (digits, `*`, `#`), the
  display, and the earpiece. No coin/validator or magstripe is required for
  progress (coins are ignored by this plugin).

## The number — 484 · 51 · 58 → `4845158`
The three puzzle answers concatenate to Ruth's number (`ANS1 ANS2 ANS3`).

| # | Type | Heard (audio carries the full clue) | Answer | The insight |
|---|------|------|--------|-------------|
| 1 | keypad letters | "the word scratched by the phone — what they did instead of saying sorry. Spell it." | **484** | HUG on the keypad (4‑8‑4) |
| 2 | logic | "Ruth was 19 when their mother passed, in '59; her sister came 11 years after Ruth; the call froze in '99 — how old was she?" | **51** | Ruth b.1940 → sister b.1951 → 1999−1951 (the mother's death is a red herring) |
| 3 | cipher | "the last two she shifted by *the two of us*; the crossed line shows `7 0` — bring them back." | **58** | subtract 2, wrapping mod 10: 7→5, 0→8 |

The VFD shows only a **terse prompt** (≤20 chars/line); the **audio** delivers the
full riddle. The physical keypad's printed letters make puzzle 1 fair.

## Hint ladder (press `*`)
Each puzzle has three escalating hints (nudge → method → answer), e.g. puzzle 1:
`LOOK AT THE KEYS` → `3 LETTERS / not 'sorry' but...` → `H U G / = 4 8 4`.
`#` repeats the spoken riddle and clears the current entry.

## Flow / states (`plugins/time_operator.c`)
`DORMANT → PUZZLE(0→1→2; each: enter answer · auto-checks at length · * hint · #
repeat · wrong→NOT IT, retry) → SOLVED(brief) → DIAL → FINAL → WIN`.
Lift resumes mid-session within `GRACE_SECS`; a win (or a longer absence) starts
fresh. Timers: `SOLVED_SECS 3`, `FINAL_CONNECT_SECS 8`, `FINAL_CLOCK_SECS 6`,
`WIN_HOLD_SECS 8` (then the quiet coda), `ENTRY_TIMEOUT_SECS 8`, `GRACE_SECS 30`.

## Hardware → meaning
| Input | Role |
|---|---|
| Lift handset | Connect to the Operator (mission / resume) |
| **Digits** | Enter the current puzzle's answer (auto-checks at the right length) |
| **`*`** | Next hint |
| **`#`** | Repeat the spoken riddle / clear entry |
| Hang up | End session (30 s grace, then fresh) |
| Coin / card | **Ignored** — nothing is gated on them |

## Pure helpers (unit-tested)
`operator_keypad_digit(c)` (2=ABC…9=WXYZ) and `operator_shift_down(d,key)`
(mod-10) — they also self-check the puzzle answers (HUG→484, 7/0−2→5/8).

## Clips (`audio/clips.manifest`, `make regen-clips`)
`op_intro` (mission + riddle 1), `puz1/puz2/puz3` (riddles), `solved`, `wrong`,
`op_ready`, and the reused `final_connect` / `final_clock` / `win_free`. Missing
clips are silent no-ops; the game still reads on the VFD.

## Tests
- Unit (`tests/unit_tests.c`): `operator_keypad_digit`, `operator_shift_down`,
  plus the display-line-budget guardrail.
- Scenario (`tests/test_operator_*.scenario`): intro, full win, hint ladder,
  wrong-answer recovery, grace resume.
- Live: play it on the phone; see `OPERATOR_PLAYTEST.md`.
