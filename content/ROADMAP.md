# Story and interaction roadmap

The infrastructure is deliberately boring; the experience should not be. The
phone's product direction is an ordinary object with an impossible memory: it
recognizes consequences, calls back when promised, and rewards physical
curiosity without becoming noisy or difficult to use.

## Now — make Operator 17 persistent

Status: **in development in content/runtime 2.1**.

- Give Operator 17 a stable voice, motives, limits, and memory of prior choices.
- Allow an ending to schedule a signed, local callback scene by wall-clock
  delay. Persist the due time across daemon restarts and deliver it only while
  the handset is down.
- Treat voicemail as narrative scenes: a missed callback leaves a visible
  message, lifting retrieves it, and the recording reflects the earlier choice.
- Keep callback delivery offline-capable. SIP or cloud data may enrich a call
  but must never be required to hear the promised message.
- Measure only aggregate callbacks scheduled, delivered, played, or abandoned.

*The House at the End of the Line* 2.1 is the first vertical slice: either
primary ending schedules an Operator 17 callback for the next day, and the
message names the consequence the caller created.

## Next — episodic lost calls

Build three linked 5–12 minute episodes rather than one monolithic story. Each
has a complete emotional arc, while recurring details gradually reveal why
Operator 17 maintains the line.

1. **The House at the End of the Line** — establish Mara, contradictory time,
   consequential choices, and the first callback.
2. **The Number That Rang Twice** — voicemail from two incompatible futures;
   preserving one message changes what the other caller remembers.
3. **Night Shift at Exchange 9** — the caller temporarily performs the
   Operator's job and learns that one “lost” call was hidden intentionally.

Each episode must support interruption, a missed-call path, a satisfying first
session, an optional physical discovery, and a later payoff.

## Then — deepen the physical fiction

- **Living number:** one canonical number or operator identity accumulates
  anonymous local history across episodes.
- **Scheduled incoming calls:** conservative ring frequency, quiet hours,
  missed-call behavior, and an owner-visible kill switch.
- **Voicemail mailbox:** preserve, erase, replay, or forward fictional messages;
  never record the real caller without explicit consent.
- **Hardware discoveries:** coins buy time or reveal evidence, provisioned
  tokens identify fictional correspondents, and hanging up can be a meaningful
  choice. Optional inputs never gate the main ending.
- **Calendar atmosphere:** local time, anniversaries, weather, or long absences
  can change texture while the essential path stays deterministic and offline.
- **Haunted idle mode:** rare, bounded fragments or a single ring, governed by
  quiet hours and a strict frequency ceiling so mystery never becomes nuisance.

## Authoring and operations

- Add an authenticated browser console for branch preview, media-in-context,
  callback inspection, anonymous state reset, signed content deployment, and
  rollback.
- Show privacy-preserving funnels for completion, abandonment, invalid input,
  prompt repetition, branch selection, callback delivery, and session duration.
- Add audience profiles (family, mild suspense, late-night) without duplicating
  firmware or weakening the default Everyone 10+ safeguards.
- Add an owner-safe diagnostic ritual activated by a protected token or key
  sequence. It should speak and display handset, keypad, coin, audio, SIP, MCU,
  OTA, tunnel, and backup results without exposing administrative controls.
- Deliver a sealed handoff packet: one-page owner guide, diagnostic token,
  recovery instructions, public-key fingerprints, and maintenance contact path.

## Reusable library

Maintain reviewed assets and interaction patterns for invitation, incoming
ring, connection, accepted choice, retry, escalating hint, silence, voicemail,
interruption, callback, ending, and return recognition. Every audio cue needs a
transcript, source/license, normalized handset loudness, and display fallback.

The reviewed set is catalogued in `AUDIO_LIBRARY.md`. New experiences should
reuse its interaction contracts while recording prose appropriate to their own
characters.

## Release gates

Every feature above remains incomplete until it has scenario coverage, signed
content validation, physical handset/audio verification, and two uncoached
first-time playtests. Scheduled behavior additionally requires restart,
clock-jump, missed-call, quiet-hour, and disable-switch tests.

The factory-installed signed story remains the offline fallback. Narrative
plugins must not depend on SIP, DNS, Cloudflare, or an external generative
service to reach a complete ending.
