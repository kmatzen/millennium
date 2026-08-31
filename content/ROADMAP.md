# Story and interaction roadmap

## Release 1 — The Last Call

Polish the existing Operator experience as the primary first-time call: audible
invitation, three keypad puzzles with escalating hints, display feedback,
outgoing-call climax, interruption recovery, and a conclusive ending. Add a
persistent anonymous completion flag so a later visit receives a short coda.
Completion requires two uncoached first-time playtests on the physical phone.

## Release 2 — The Last Line

Use the data-authored sample as the compact content-update reference. Add voiced
prompts and sound cues, a real-time return branch, and optional coin and token
discoveries. Neither optional hardware path may block the two primary endings.
Ship and roll it back independently of the daemon using a signed content bundle.

## Reusable library

Maintain reviewed assets and patterns for: incoming ring, connection, accepted
choice, retry, escalating hint, silence/pause, interruption, callback, ending,
and return recognition. Each audio cue needs a transcript, source/license,
normalized handset loudness, and a display-only fallback.

The first reviewed set is catalogued in `AUDIO_LIBRARY.md` and backed by the
checked-in *The Last Line* 1.1.0 WAVs and scenario. Later experiences should
reuse the interaction contracts while re-recording prose appropriate to their
own voice.

## Operational fallback

The factory-installed *The Last Line* bundle is the offline fallback. Narrative
plugins must not depend on SIP, DNS, Cloudflare, or an external generative
service to reach a complete ending. Network features enhance a story only after
the local path is known healthy.
