# Reusable narrative audio and interaction patterns

The initial reviewed library is embodied by *The Last Line* 1.1.0. Its clips
are deliberately small, handset-normalized, transcript-backed patterns that a
new story may copy and re-record without inheriting story logic:

| Pattern | Reference clip | Interaction contract |
| --- | --- | --- |
| Invitation | `incoming_line.wav` | Name the physical action that begins. |
| Choice | `choose_message.wav` | Speak every valid key and the repeat key. |
| Retry | `repeat_choice.wav` | Shorten the prompt without removing choices. |
| Accepted choice | `past_selected.wav` | Confirm input before any delay. |
| Connection | `connecting_line.wav` | Explain both online and timeout outcomes. |
| Optional discovery | `coin_secret.wav` | Reward hardware play and state how to return. |
| Interruption | `call_saved.wav` | Reassure the caller and name the resume action. |
| Ending | `past_delivered.wav` | State consequence and invite a return. |
| Return recognition | `welcome_back.wav` | Acknowledge anonymous persistent state. |
| Coda | `past_coda.wav` | Pay off the earlier choice without new setup. |

All reference WAVs are mono, 8 kHz, signed 16-bit PCM at -20 LUFS. The source
transcripts and deterministic regeneration command live in
`stories/last_line/regenerate_audio.sh`. New library entries require a
transcript, source/voice and license note, loudness/rate metadata, a VFD-only
fallback, and a scenario that exercises the associated interaction.

These are patterns, not mandatory prose. Authors should preserve the contract
while changing voice, language, and tone to suit the story bible and audience.
