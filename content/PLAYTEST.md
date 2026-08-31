# First-time caller playtest protocol and record

Run this protocol with at least two people who have never used the phone and
have not read its documentation. Use the physical production phone and the
exact signed content release being considered for handoff. Do not record
names, contact details, card data, audio, video, or exact speech.

## Before each participant

1. Restore the configured fallback experience, then activate Story Mode using
   the authenticated administrative path. Reset only the participant test
   profile; do not alter prompts, volume, or timing between participants.
2. Confirm the handset, keypad, display, coin input, audio, and offline fallback
   are healthy. Record the content version and as-built record below.
3. Say only: “This phone is ready for you. Use it however you think makes
   sense.” Do not mention the handset, keys, display, coins, story, or goal.
4. Start timing when the participant can first see and hear the phone. During
   the uncoached run, do not answer procedural questions or rescue a stalled
   participant. Record the observable point of hesitation instead.

## Part A — uncoached primary experience

The participant chooses all actions. Stop when they reach a primary ending,
explicitly abandon the experience, or remain inactive for five minutes after
the story's own recovery prompts. A pass requires all of the following:

- the caller lifts the handset and enters the story without coaching;
- every required action is discoverable from audio or display feedback;
- accepted and invalid inputs produce understandable feedback;
- the caller reaches either primary ending without observer intervention;
- audio is intelligible and the display is legible at the installed location;
- no secret interaction is required to complete the main path.

## Part B — controlled resilience scenarios

After Part A is complete, explain that the remaining actions are test
scenarios rather than part of the uncoached score. Start each scenario from a
fresh test profile unless the scenario explicitly requires persisted state.

| Scenario | Action | Required observable result |
| --- | --- | --- |
| Repeat | Press the documented repeat key at two prompts | Prompt repeats without losing progress or duplicating consequences |
| Invalid input | Press an unrelated key at a choice | Input is rejected or reprompted clearly; story remains completable |
| Timeout | Give no input at the first and final choices | Escalating/recovery prompt appears; no silent dead end |
| Interruption | Hang up before and after the final choice, then lift again | State resumes coherently or explicitly explains the restart |
| Return visit | Complete each primary choice and begin a later session | The matching persistent coda is reachable and contradicts no prior choice |
| Offline | Disable upstream network before starting | Signed local story or documented fallback remains usable |
| Optional input | Use a coin or provisioned test token when invited | Discovery is acknowledged but never blocks the primary ending |

Restore networking and the configured fallback experience after the session.
Do not mark a failed scenario complete merely because the simulator covers it.

## Participant record

Create one copy of this section per participant.

- Date and content version:
- Device/as-built record:
- Observer:
- Caller age range and prior familiarity with the phone:
- Started without coaching: yes / no
- Completed primary ending: yes / no
- Time to first action and total duration:
- Prompts repeated / invalid inputs / abandoned scene:
- Interruption, timeout, offline, and return paths exercised:
- Where the caller hesitated, became confused, or disengaged:
- Audio clarity, display legibility, and comfortable volume:
- Optional interaction discovered without blocking progress:
- Changes required before release:
- Retest result:

## Release decision

- Participant records included:
- Both uncoached primary runs passed: yes / no
- Every Part B scenario passed on physical hardware: yes / no
- Open defects and owners:
- Signed content release accepted for handoff: yes / no
