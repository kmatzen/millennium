# Story and interaction roadmap

## Release 1 — The Last Call

Polish the existing Operator experience as the primary first-time call: audible
invitation, three keypad puzzles with escalating hints, display feedback,
outgoing-call climax, interruption recovery, and a conclusive ending. Add a
persistent anonymous completion flag so a later visit receives a short coda.
Completion requires two uncoached first-time playtests on the physical phone.

## Release 2 — The House at the End of the Line

Replace the abstract sample with a concrete dramatic mystery: a child calling
from the night her house burned, contradictory evidence, a consequential
instruction sent across the line, and a return call that reveals what the
player caused. Coins and tokens expose optional evidence without blocking the
two primary endings. Ship and roll it back independently of the daemon using a
signed content bundle.

## Reusable library

Maintain reviewed assets and patterns for: incoming ring, connection, accepted
choice, retry, escalating hint, silence/pause, interruption, callback, ending,
and return recognition. Each audio cue needs a transcript, source/license,
normalized handset loudness, and a display-only fallback.

The first reviewed set is catalogued in `AUDIO_LIBRARY.md` and backed by the
checked-in *The House at the End of the Line* 2.0.0 WAVs and scenario. Later experiences should
reuse the interaction contracts while re-recording prose appropriate to their
own voice.

## Operational fallback

The factory-installed *The House at the End of the Line* bundle is the offline fallback. Narrative
plugins must not depend on SIP, DNS, Cloudflare, or an external generative
service to reach a complete ending. Network features enhance a story only after
the local path is known healthy.
