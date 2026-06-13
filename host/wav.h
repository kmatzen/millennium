#ifndef WAV_H
#define WAV_H

#include <stddef.h>

/*
 * Minimal, dependency-free parser for canonical RIFF/WAVE (PCM) files.
 *
 * It validates the RIFF/WAVE container, walks the chunk list, and reports the
 * format fields plus the byte range of the PCM "data" chunk within the supplied
 * buffer. It does no I/O and pulls in nothing beyond <stddef.h>/<string.h>, so
 * it compiles and is unit-tested on every platform — the ALSA streaming layer
 * (audio_tones.c) is the only Linux-only consumer.
 *
 * Only the fields needed to play a clip are decoded; extension/LIST/fact chunks
 * are skipped. All offsets/lengths are bounds-checked against `len`.
 */
typedef struct {
    int    format;           /* WAVE_FORMAT tag; 1 = PCM */
    int    channels;
    int    sample_rate;      /* Hz */
    int    bits_per_sample;
    size_t data_offset;      /* byte offset of PCM samples within the buffer */
    size_t data_len;         /* number of PCM bytes available */
} wav_info_t;

/* Parse `data` (`len` bytes) into `*out`. Returns 0 on success, -1 if the
 * buffer is not a well-formed WAVE file with both a "fmt " and a "data" chunk.
 * On success `data_offset`/`data_len` always lie within [0, len]. */
int wav_parse(const unsigned char *data, size_t len, wav_info_t *out);

#endif /* WAV_H */
