# The House at the End of the Line audio

The `media/` directory contains mono, 8 kHz, 16-bit PCM WAVs normalized to
-20 LUFS for handset playback. ElevenLabs voices distinguish Daniel as
Operator 17, Jessica as young Mara, Matilda as adult Mara, and Brian as Mara's
father. The uncanny line uses Jessica again with additional processing so its
identity becomes audible before the reveal. Generated beds add
mechanical ringing, telephone noise, rain, floorboards, and distant fire.

Source transcripts, voice IDs, sound-design prompts, mixing, filtering, and
normalization live in `regenerate_audio.sh`. Set `ELEVENLABS_API_KEY` only in
the process environment; never add a credential to this directory. Voice IDs
can be overridden with the role-specific environment variables documented in
the script. Essential choices remain legible on the VFD and are spoken aloud.
