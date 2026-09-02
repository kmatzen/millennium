# Millennium story bible

## Promise and tone

The phone should feel like an ordinary object with an impossible memory. It is
warm, mysterious, humane, and lightly uncanny—not hostile, humiliating, or
needlessly obscure. Every interaction should reward curiosity. The caller is a
participant whose physical actions matter, never merely an audience member.

The recurring world is a network of lost calls maintained by **The Operator**.
Calls can arrive from another time, but the phone cannot change public history;
it can preserve a message, repair a private relationship, or reveal a small
truth. The line remembers choices, not personal identity. Secrets add optional
context and alternative endings but never block the main experience.

## Interaction vocabulary

- A ringing display or audible ring is an invitation; lifting answers it.
- Digits choose, solve, or dial. Copy must show the available digits.
- `*` repeats the current prompt everywhere.
- `#` confirms or advances only when the display says so.
- Coins and purpose-built tokens unlock optional discoveries; they cannot gate
  the primary ending.
- Hanging up is always safe. A later lift resumes coherently or explains that a
  new call has begun.
- Every accepted action receives immediate audio or display acknowledgement.

Prompts use at most 20 characters per VFD line, plain language, and one decision
at a time. Default response windows are at least 12 seconds. Important spoken
instructions must also have sufficient display context for a caller with
hearing loss; experiences intended for callers with low vision enable spoken
instructions and provide audio for every actionable scene.

## Characters and continuity

- **Operator 17** is patient, precise, and quietly invested in completing lost
  connections. They remember only choices the line actually observed, never
  flatter the caller with invented familiarity, and leave concise voicemails
  when a promised callback is missed. Their private conflict is whether every
  lost connection deserves to be completed; some calls were hidden for a
  reason. Help becomes progressively clearer without judgment, while certainty
  becomes progressively less trustworthy.
- **The Caller on the line** changes by episode. Their wants must be concrete,
  emotionally legible, and resolvable within one short session.
- Returning callers may be recognized only through anonymous local story state.
  Stories must not imply surveillance or claim knowledge the phone did not earn.

## Review standards

Every release must pass `storytool validate` and `storytool explore`, be played
through in preview, and be installed from its signed package on a test root.
Reviewers confirm that:

1. The invitation and next action are discoverable without owner coaching.
2. Every input, timeout, interruption, repeat, offline, and return path is
   coherent and has no trap or infinite loop.
3. Audio is intelligible through the handset and essential information is not
   carried by audio alone unless spoken-instruction mode supplies an equivalent.
4. Secrets are optional, puzzles have escalating hints, and endings acknowledge
   the caller's consequential choices.
5. Content avoids personal data, cruelty, discriminatory stereotypes, sexual
   content, graphic violence, self-harm instructions, real emergency claims,
   monetized chance, and impersonation of a real living person.
6. Analytics remain aggregate counts and durations. Story state contains only
   content-defined integers and never card data, phone numbers, names, or speech.

Default rating is **Everyone 10+ / mild suspense**. A deployment owner must
approve any stricter rating and display a warning before the call begins.
