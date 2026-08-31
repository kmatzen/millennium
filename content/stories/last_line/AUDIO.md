# The Last Line audio

The `media/` directory contains mono, 8 kHz, 16-bit PCM WAV prompts generated
with the macOS Samantha system voice and normalized to -20 LUFS for handset
playback. Source text is embedded in `regenerate_audio.sh`; every prompt
duplicates or expands the VFD instruction so spoken-instruction mode remains
independently usable.

Before redistribution outside this project, confirm that the target use is
consistent with the operating system voice license. A later recorded-voice
release can replace these files without changing story logic.
