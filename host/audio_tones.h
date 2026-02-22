#ifndef AUDIO_TONES_H
#define AUDIO_TONES_H

/*
 * Audio tone generator for payphone feedback sounds.
 * Uses ALSA on Linux; no-ops on other platforms.
 */

/* Initialize the tone subsystem (opens ALSA device). Call once at startup. */
void audio_tones_init(void);

/* Shut down and release resources. */
void audio_tones_cleanup(void);

/* Continuous dial tone (350 Hz + 440 Hz). Plays until audio_tones_stop(). */
void audio_tones_play_dial_tone(void);

/* Single DTMF key tone (~150 ms, auto-stops). */
void audio_tones_play_dtmf(char key);

/* Cadenced ringback (440 Hz + 480 Hz, 2 s on / 4 s off). Plays until stop. */
void audio_tones_play_ringback(void);

/* Cadenced busy tone (480 Hz + 620 Hz, 0.5 s on / 0.5 s off). Until stop. */
void audio_tones_play_busy_tone(void);

/* Short coin-deposit chime (~200 ms, auto-stops). */
void audio_tones_play_coin_tone(void);

/* Stop whatever tone is currently playing. */
void audio_tones_stop(void);

/* Returns 1 if a tone is currently being generated. */
int audio_tones_is_playing(void);

#endif /* AUDIO_TONES_H */
