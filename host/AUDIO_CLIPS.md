# Recorded Audio Clips (`sdk_play_clip`)

Plugins can play short recorded clips — voice lines, ambience, stingers —
through the same audio path that drives the dial/ringback/busy tones. This is
the Phase-2 audio capability for "The Operator" (and any other plugin), built so
that **clips are optional**: a plugin always degrades gracefully to its tones
and on-screen text when no files are installed.

## Feasibility (why this was straightforward)

`audio_tones.c` already owns a single background thread that streams
`S16_LE` PCM to a named ALSA PCM device, with one set of stop/join/`is_playing`
controls and the rule "starting a sound stops the previous one." Clip playback
reuses all of that: instead of synthesising sine samples, the thread streams the
PCM payload of a WAV file. The only genuinely new, error-prone code — parsing
the RIFF/WAVE container — lives in a pure, platform-independent module
(`wav.c`) that is unit-tested on every platform, while the ALSA streaming stays
in the Linux-only `audio_tones.c`.

## Authoring clips

- Format: **16-bit PCM WAV** (`WAVE_FORMAT_PCM`), mono or stereo.
- Sample rate: **8 kHz mono is strongly preferred.** Call audio on this device
  is 8 kHz narrowband and high-quality resampling crackles on the single-core
  CPU, so author at 8 kHz to avoid on-the-fly conversion. Other rates play (ALSA
  resamples via the `plug` device) but may not sound clean.
- Keep them short (a few seconds). The loader refuses files over 8 MB.
- Name them `<clip>.wav` and drop them in the clip directory
  (`audio.clip_dir`, default `/usr/local/share/millennium/audio`).

Example, producing a compliant clip with ffmpeg:

```sh
ffmpeg -i voice.mp3 -ac 1 -ar 8000 -sample_fmt s16 operator.wav
scp operator.wav matzen@raspberrypi.local:/usr/local/share/millennium/audio/
```

## Reproducible clips (manifest + regen script)

The clips are **not in git** (binary, and easy to regenerate), so the source of
truth is [`audio/clips.manifest`](audio/clips.manifest): one line per clip,
`<clip_name> <text>`, plus the voice/model in its header. The current set is
"The Operator" narration in ElevenLabs' *Sarah* voice.

[`audio/regen_clips.sh`](audio/regen_clips.sh) rebuilds every clip from the
manifest — synthesize via ElevenLabs TTS, convert to the format above, write to
`audio/out/`, and optionally deploy to the Pi. Use this to restore the clips
after an SD reflash or to re-voice them (edit the manifest, or set `VOICE_ID`).

```sh
export ELEVENLABS_API_KEY=sk_...        # a TTS-capable key; never commit it
make regen-clips                        # build into host/audio/out/
make regen-clips DEPLOY=1               # build + scp/install onto the Pi
# voice/host overrides: VOICE_ID=... PI_HOST=user@ip ./audio/regen_clips.sh --deploy
```

To re-voice, change the `Voice:` line + `VOICE_ID`. Free ElevenLabs accounts
can't use "library" voices via the API (HTTP 402); current default voices like
Sarah/Brian/George work.

## Using clips from a plugin

```c
sdk_play_clip("operator");   /* plays <clip_dir>/operator.wav */
```

- The name is restricted to letters, digits, `_` and `-` (no path separators),
  so it can't escape the clip directory.
- Playing a clip interrupts whatever sound is currently playing, exactly like
  the tone functions, and `sdk_stop_audio()` stops it.
- A **missing or unsupported file is a no-op that leaves the current sound
  playing.** That lets you layer a clip over a fallback tone:

  ```c
  sdk_busy_tone();          /* always heard */
  sdk_play_clip("drop");    /* replaces the busy tone only if drop.wav exists */
  ```

"The Operator" uses this for an Operator greeting on lift (`operator`), per-era
ambience on arrival (`era_wires`, `era_golden`, `era_space`, `era_analog`,
`era_frozen`, `era_static`, `era_sealed`), the 1999 line-drop (`drop`), and the
paradox sting (`paradox`). The recordings themselves aren't committed, but their
text is — all 10 are defined in `audio/clips.manifest` and rebuilt by
`make regen-clips` (see above). Leave them out and the experience still runs on
tones + text.

## Testing

`wav.c` is covered by unit tests (`test_wav_parse_*`). Scenario tests assert the
*request* for a clip with `assert_clip <substring>` (the simulator records the
last path a plugin asked to play); actual ALSA playback is exercised on the Pi.

## Not yet done (future work)

- **Looping ambience.** Clips currently play once through. A loop flag on the
  clip thread would let era ambience sustain under the dialogue.
- **Mixing.** Only one sound plays at a time (clip *or* tone), inherited from
  the tone subsystem. Simultaneous ambience + DTMF would need a mixer.
- **Phase 3 (live AI Operator).** Piping handset audio to STT → LLM → TTS so the
  Operator improvises is a separate, much larger effort (network + speech stack)
  and is out of scope here.
