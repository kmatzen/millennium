# The Operator: "The Last Call" — story & decision tree

The design reference for the `The Operator` plugin (`plugins/time_operator.c`).
Implementation follows this; where they drift, the code wins — fix one or the other.

## Concept
At 11:59 PM on New Year's Eve 1999 a woman lifted a payphone to call her
estranged sister **Ruth** and make peace — and the clock froze mid-ring. The
**Operator** has held that unfinished call ever since. The caller helps her
finish it: recover the three pieces of Ruth's number, each overheard at a moment
the woman almost dialed it across her life, then dial the whole number. The call
connects, the clock turns over to 2000, and the Operator is freed.

A treasure hunt for a phone number — the right goal for a *payphone*, because the
climax is literally placing the call.

## Agreed choices
- **Core:** goal-driven quest (find 3 number-pieces → place the call → time resumes).
- **Finding a year is a homing search, not a decade lookup.** The hub gives a
  **cryptic** clue (no decade named). A wrong-but-valid year answers with
  direction — `TOO EARLY` / `TOO LATE`, and `WARMER` (+ *a little earlier/later*)
  once within `WARM_BAND` years. You deduce the year from the clue + this feedback.
- **One real inference:** pieces A and C accept a small window (±2 yrs of 1955 / 1998);
  the middle piece **B demands the exact year (1978)** — the one genuine deduction.
- **The finale hides the answer:** `READY` shows `6 DIGITS`, a wrong attempt says
  `TRY AGAIN`. You assemble the three pieces you collected from memory.
- **Coins (never gate winning), the escape hatches:** at the hub a coin buys a
  **sharper hint** (names the decade); in an era a coin buys **more line time**
  before drift (and **forces** the sealed era); at `READY` a coin **reveals the
  assembled number**.
- **Reset:** on hang-up, with a ~30 s **grace** (re-lift resumes; later, or after a
  win, starts fresh). All state is in-memory for the session.
- **Tone & length:** warm & bittersweet, ~3–5 minutes.

## Legibility rules
- The Operator's **voice** states the goal and the next step; every line is echoed
  as VFD text so it works without audio.
- The **VFD HUD**: line 1 = current **cryptic** clue, line 2 = `… n/3`. Every line
  ≤ 20 chars (no scroll), so it's stable and readable.
- You can't hard-fail; every mistake is recoverable and the next action is clear —
  the directional/warm feedback always points you nearer.
- **Listen, don't speak:** in a key era `1=LISTEN` gives the piece; `2=SPEAK`
  tangles the line — recover by pressing `1`. Self-teaching, local, no penalty.
  After you've spoken once the `2=SPEAK` prompt is **retired** (it reads
  `press 1=LISTEN`) so it doesn't keep advertising a dead end.

## Hardware → meaning
| Input | Role |
|---|---|
| Lift handset | Connect to the Operator (mission / resume / next hint) |
| Dial 4-digit **year** | Travel to that era |
| **1 / 2** in a key era | `1=LISTEN` (get piece) vs `2=SPEAK` (tangle) |
| **Coin** | Hub: sharper hint (names decade) · Era: hold the line / force the sealed era · Ready: reveal the number |
| **Card** swipe | "Trunk-line pass" → opens the sealed era |
| Idle (**drift**) | Pulled back to the Operator (nothing lost) |
| **#** | Repeat the hint / back to the Operator |
| Hang up | End session (30 s grace, then fresh) |

## The number & the eras
Ruth's number is **36‑41‑55** (→ dial `364155`), in fixed positions A‑B‑C; find
pieces in any order. Each piece has a **target year** and an **arrival window**
(`op_target_year` / `op_in_window`):

| Year(s) | Role | Holds |
|---|---|---|
| 1955 ±2 (1953–1957) | **KEY A** | piece **36** — two girls, a kitchen radio |
| **1978 exact** | **KEY B** | piece **41** — the night she lost her nerve (the inference) |
| 1998 ±2 (1996–2000\*) | **KEY C** (sealed) | piece **55** — a Christmas card; needs pass/coin |
| 2000 | flavor `home` | "the frozen minute; only silence" (\*checked before C) |
| ≥ 2050 | flavor `future` | "the future is sealed" |
| any other 1900–2100 | **nudge** | `TOO EARLY`/`TOO LATE`, or `WARMER` within `WARM_BAND` (6 yrs) of the current target |
| outside 1900–2100 | `bad` | "no such year" (refused at the hub) |
| an already-found era | flavor `heard` | "already heard" |

`operator_year_role` (the unit-tested classifier) still reports the historical
decade roles (A/B/C/early/home/future/other) for reference; **arrival** is decided
by `op_in_window` (the narrow windows above), not by that classifier.

## States (`plugins/time_operator.c`)
`DORMANT → HUB → TRAVEL → {FLAVOR | KEY} → REVEAL → … → READY → FINAL → WIN`
plus `KEY` sub-flags `locked` (sealed) and `tangled` (spoke), and session flag
`spoke_once` (retires the SPEAK prompt). Timers are sized to let each beat's voice
clip finish (so the story isn't cut off mid-line, which paces a first session
toward ~5 min): `TRAVEL_SECS 2`, `REVEAL_SECS 6`, `FLAVOR_SECS 5`, `DRIFT_SECS 30`,
`EXTEND_SECS 30`, `GRACE_SECS 30`, final beats `8` then `6`, then `WIN_HOLD_SECS 8`
before the quiet coda. Tuning knob: `WARM_BAND 6`.

## Decision tree
```
DORMANT ──lift──▶ [won OR idle>grace?] ─ yes ▶ reset + op_intro ▶ HUB
                                         └ no ▶ op_resume ───────▶ HUB
HUB (cryptic clue for lowest unfound piece; n/3)
  ├─ dial year off-target ─▶ FLAVOR nudge (TOO EARLY/LATE/WARMER) ─(timeout/#)─▶ HUB
  ├─ dial year in a piece window (unfound) ─▶ TRAVEL ─▶ KEY
  │        ├─ 1 LISTEN ─▶ REVEAL (piece++) ─(timeout/#)─▶ HUB
  │        └─ 2 SPEAK  ─▶ tangled (SPEAK retired) ──1 LISTEN──▶ REVEAL
  ├─ dial a window already found ─▶ FLAVOR "already heard" ─▶ HUB
  ├─ dial 1998-window (sealed, unfound) ─▶ KEY locked ──card|coin──▶ KEY ─▶ …
  ├─ coin ─▶ sharper hint (decade)   │   # ─▶ repeat hint
  └─ 3/3 ─▶ READY (6 DIGITS) ──dial 364155──▶ FINAL ─▶ WIN ─(hold)─▶ quiet coda ─▶ (hang up) reset
                              └─ wrong ─▶ TRY AGAIN   coin ─▶ reveal number
KEY ── idle ──▶ drift_back ─▶ HUB        ── coin ──▶ hold the line (timer +)
ANY ── hang up ──▶ DORMANT (30 s grace; a WIN forces a fresh next session)
```

## Clips
All voice lines (IDs + exact text + voice) are the manifest:
`audio/clips.manifest`, rebuilt by `make regen-clips` (see `AUDIO_CLIPS.md`).
Groups: `op_*` (hub), `era{1,2,3}_{arrive,listen}` + `era3_sealed`, `flv_*`,
`px_tangle` / `drift_back` / `coin_hold`, `final_connect` / `final_clock` /
`win_free`. Missing clips are silent no-ops; the experience still reads on the VFD.

## Tests
- Unit (`tests/unit_tests.c`): `parse_year`, `operator_year_role`,
  `operator_target_piece`, plus the display-line-budget guardrail.
- Scenario (`tests/test_operator_*.scenario`): intro, find-piece, flavor, tangle
  (+ SPEAK retire), homing (directional/warm + exact-year B), sealed, full win
  (hidden number, coin-reveal, win coda), drift, grace, coin-hint.
- Live: `make operator-smoke` (web API) + `OPERATOR_PLAYTEST.md` (hands-on).
