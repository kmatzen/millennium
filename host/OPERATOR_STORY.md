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
- **Coins (never gate winning):** at the Operator a coin buys a **sharper hint**;
  in an era a coin buys **more line time** before drift (and **forces** the sealed era).
- **Reset:** on hang-up, with a ~30 s **grace** (re-lift resumes; later, or after a
  win, starts fresh). All state is in-memory for the session.
- **Tone & length:** warm & bittersweet, ~3–5 minutes.

## Legibility rules
- The Operator's **voice** states the goal and the next step; every line is echoed
  as VFD text so it works without audio.
- The **VFD HUD**: line 1 = current hint, line 2 = `… n/3`. Every line ≤ 20 chars
  (no scroll), so it's stable and readable.
- You can't hard-fail; every mistake is recoverable and the next action is clear.
- **Listen, don't speak:** in a key era `1=LISTEN` gives the piece; `2=SPEAK`
  tangles the line — recover by pressing `1`. Self-teaching, local, no penalty.

## Hardware → meaning
| Input | Role |
|---|---|
| Lift handset | Connect to the Operator (mission / resume / next hint) |
| Dial 4-digit **year** | Travel to that era |
| **1 / 2** in a key era | `1=LISTEN` (get piece) vs `2=SPEAK` (tangle) |
| **Coin** | Hub: sharper hint · Era: hold the line / force the sealed era |
| **Card** swipe | "Trunk-line pass" → opens the sealed era |
| Idle (**drift**) | Pulled back to the Operator (nothing lost) |
| **#** | Repeat the hint / back to the Operator |
| Hang up | End session (30 s grace, then fresh) |

## The number & the eras
Ruth's number is **36‑41‑55** (→ dial `364155`), in fixed positions A‑B‑C; find
pieces in any order. Year roles (see `operator_year_role`):

| Years | Role | Holds |
|---|---|---|
| 1950–1959 | **KEY A** | piece **36** — two girls, a kitchen radio |
| 1970–1989 | **KEY B** | piece **41** — the night she lost her nerve |
| 1995–1999 | **KEY C** (sealed) | piece **55** — a Christmas card; needs pass/coin |
| < 1950 | flavor `early` | "too early — she wasn't born" |
| 2000 | flavor `home` | "the frozen minute; only silence" |
| ≥ 2050 | flavor `future` | "the future is sealed" |
| other in range | flavor `other` | "not one of the years she held it" |
| outside 1900–2100 | `bad` | "no such year" (refused at the hub) |

## States (`plugins/time_operator.c`)
`DORMANT → HUB → TRAVEL → {FLAVOR | KEY} → REVEAL → … → READY → FINAL → WIN`
plus `KEY` sub-flags `locked` (sealed) and `tangled` (spoke). Timers:
`TRAVEL_SECS 2`, `REVEAL_SECS 3`, `FLAVOR_SECS 3`, `DRIFT_SECS 30`,
`EXTEND_SECS 30`, `GRACE_SECS 30`, final beats `3` then `4`.

## Decision tree
```
DORMANT ──lift──▶ [won OR idle>grace?] ─ yes ▶ reset + op_intro ▶ HUB
                                         └ no ▶ op_resume ───────▶ HUB
HUB (hint for lowest unfound piece; n/3)
  ├─ dial flavor year ─▶ FLAVOR ─(timeout/#)─▶ HUB
  ├─ dial key year (unfound) ─▶ TRAVEL ─▶ KEY
  │        ├─ 1 LISTEN ─▶ REVEAL (piece++) ─(timeout/#)─▶ HUB
  │        └─ 2 SPEAK  ─▶ tangled ──1 LISTEN──▶ REVEAL
  ├─ dial key year (already found) ─▶ FLAVOR "already heard" ─▶ HUB
  ├─ dial 1998 (sealed, unfound) ─▶ KEY locked ──card|coin──▶ KEY ─▶ …
  ├─ coin ─▶ sharper hint     │   # ─▶ repeat hint
  └─ 3/3 ─▶ READY ──dial 364155──▶ FINAL ─▶ WIN ─▶ (hang up) reset
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
- Scenario (`tests/test_operator_*.scenario`): intro, find-piece, flavor, tangle,
  sealed, full win, drift, grace, coin-hint.
- Live: `make operator-smoke` (web API) + `OPERATOR_PLAYTEST.md` (hands-on).
