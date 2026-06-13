#define _POSIX_C_SOURCE 200112L
#include "audio_tones.h"
#include "wav.h"
#include "logger.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

#ifdef __linux__
#include <alsa/asoundlib.h>
#define HAVE_ALSA 1
#else
#define HAVE_ALSA 0
#endif

#define SAMPLE_RATE   8000
#define AMPLITUDE     12000   /* ~37 % of int16 range — comfortable volume */
#define DTMF_DURATION_MS   150
#define COIN_DURATION_MS   200
#define COIN_AMPLITUDE     7500   /* ~4 dB below default; coin chime sits softer */
#define MAX_CLIP_BYTES     (8 * 1024 * 1024)  /* refuse to slurp huge files */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Tone description passed to the playback thread */
typedef struct {
    double freq1;          /* first frequency  (Hz), 0 = silence */
    double freq2;          /* second frequency (Hz), 0 = none    */
    int    on_ms;          /* on period (ms), 0 = continuous      */
    int    off_ms;         /* off period (ms), 0 = no cadence     */
    int    total_ms;       /* total duration (ms), 0 = until stop */
    int    amplitude;      /* peak amplitude (int16); 0 = default AMPLITUDE */
    const char *device;    /* ALSA PCM name; NULL = "default" (both channels) */
} tone_spec_t;

static pthread_t      tone_thread;
static int            tone_thread_running = 0;
static volatile int   tone_stop_flag = 0;
static pthread_mutex_t tone_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ── DTMF frequency table ─────────────────────────────────────── */

static int dtmf_freqs(char key, double *f1, double *f2) {
    /* Row frequencies */
    static const double row[] = { 697, 770, 852, 941 };
    /* Column frequencies */
    static const double col[] = { 1209, 1336, 1477 };
    int r = -1, c = -1;

    switch (key) {
        case '1': r=0; c=0; break;  case '2': r=0; c=1; break;
        case '3': r=0; c=2; break;  case '4': r=1; c=0; break;
        case '5': r=1; c=1; break;  case '6': r=1; c=2; break;
        case '7': r=2; c=0; break;  case '8': r=2; c=1; break;
        case '9': r=2; c=2; break;  case '*': r=3; c=0; break;
        case '0': r=3; c=1; break;  case '#': r=3; c=2; break;
        default: return -1;
    }
    *f1 = row[r];
    *f2 = col[c];
    return 0;
}

/* ── Tone playback thread (ALSA) ──────────────────────────────── */

#if HAVE_ALSA

static void *tone_thread_func(void *arg) {
    tone_spec_t spec = *(tone_spec_t *)arg;
    snd_pcm_t *pcm = NULL;
    int err;
    int16_t buf[SAMPLE_RATE / 10]; /* 100 ms buffer */
    int buf_frames = SAMPLE_RATE / 10;
    unsigned long sample_idx = 0;
    int elapsed_ms = 0;
    int cadence_pos = 0; /* ms into current on/off cycle */

    free(arg);

    err = snd_pcm_open(&pcm, spec.device ? spec.device : "default",
                       SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        logger_warnf_with_category("AudioTones", "Cannot open PCM: %s", snd_strerror(err));
        pthread_mutex_lock(&tone_mutex);
        tone_thread_running = 0;
        pthread_mutex_unlock(&tone_mutex);
        return NULL;
    }

    err = snd_pcm_set_params(pcm, SND_PCM_FORMAT_S16_LE,
                             SND_PCM_ACCESS_RW_INTERLEAVED,
                             1, SAMPLE_RATE, 1, 100000);
    if (err < 0) {
        logger_warnf_with_category("AudioTones", "Cannot set PCM params: %s", snd_strerror(err));
        snd_pcm_close(pcm);
        pthread_mutex_lock(&tone_mutex);
        tone_thread_running = 0;
        pthread_mutex_unlock(&tone_mutex);
        return NULL;
    }

    while (!tone_stop_flag) {
        int i;
        int silent = 0;
        snd_pcm_sframes_t frames;

        /* Check cadence: are we in the off period? */
        if (spec.on_ms > 0 && spec.off_ms > 0) {
            int cycle = spec.on_ms + spec.off_ms;
            silent = (cadence_pos % cycle) >= spec.on_ms;
        }

        for (i = 0; i < buf_frames; i++) {
            if (silent) {
                buf[i] = 0;
            } else {
                double t = (double)sample_idx / SAMPLE_RATE;
                double val = 0;
                if (spec.freq1 > 0) val += sin(2.0 * M_PI * spec.freq1 * t);
                if (spec.freq2 > 0) val += sin(2.0 * M_PI * spec.freq2 * t);
                buf[i] = (int16_t)(val * (spec.amplitude > 0 ? spec.amplitude : AMPLITUDE) / 2.0);
            }
            sample_idx++;
        }

        frames = snd_pcm_writei(pcm, buf, buf_frames);
        if (frames < 0) {
            frames = snd_pcm_recover(pcm, (int)frames, 0);
            if (frames < 0) break;
        }

        elapsed_ms += 100;
        cadence_pos += 100;

        if (spec.total_ms > 0 && elapsed_ms >= spec.total_ms) break;
    }

    snd_pcm_drain(pcm);
    snd_pcm_close(pcm);

    pthread_mutex_lock(&tone_mutex);
    tone_thread_running = 0;
    pthread_mutex_unlock(&tone_mutex);
    return NULL;
}

static void start_tone(const tone_spec_t *spec) {
    tone_spec_t *arg;

    audio_tones_stop();

    arg = (tone_spec_t *)malloc(sizeof(tone_spec_t));
    if (!arg) return;
    *arg = *spec;

    pthread_mutex_lock(&tone_mutex);
    tone_stop_flag = 0;
    tone_thread_running = 1;
    pthread_mutex_unlock(&tone_mutex);

    if (pthread_create(&tone_thread, NULL, tone_thread_func, arg) != 0) {
        free(arg);
        pthread_mutex_lock(&tone_mutex);
        tone_thread_running = 0;
        pthread_mutex_unlock(&tone_mutex);
        logger_warn_with_category("AudioTones", "Failed to create tone thread");
    }
}

#else /* !HAVE_ALSA */

static void start_tone(const tone_spec_t *spec) {
    (void)spec;
}

#endif /* HAVE_ALSA */

/* ── Public API ────────────────────────────────────────────────── */

void audio_tones_init(void) {
    logger_info_with_category("AudioTones", "Audio tone subsystem initialized");
}

void audio_tones_cleanup(void) {
    audio_tones_stop();
    logger_info_with_category("AudioTones", "Audio tone subsystem cleaned up");
}

/* Feedback tones heard in the handset ride the earpiece (right) channel, which
 * is tuned for call audio; only the incoming-call ring uses the loudspeaker. */
#define EARPIECE_PCM   "out_right_solo"

void audio_tones_play_dial_tone(void) {
    tone_spec_t s = { 350.0, 440.0, 0, 0, 0, 0, EARPIECE_PCM };
    logger_debug_with_category("AudioTones", "Playing dial tone");
    start_tone(&s);
}

void audio_tones_play_dtmf(char key) {
    tone_spec_t s;
    memset(&s, 0, sizeof(s));
    if (dtmf_freqs(key, &s.freq1, &s.freq2) != 0) return;
    s.total_ms = DTMF_DURATION_MS;
    s.device = EARPIECE_PCM;
    logger_debugf_with_category("AudioTones", "Playing DTMF for key %c", key);
    start_tone(&s);
}

void audio_tones_play_ringback(void) {
    tone_spec_t s = { 440.0, 480.0, 2000, 4000, 0, 0, EARPIECE_PCM };
    logger_debug_with_category("AudioTones", "Playing ringback");
    start_tone(&s);
}

void audio_tones_play_ring(void) {
    /* Incoming-call ring stays on the loudspeaker (both channels). */
    tone_spec_t s = { 440.0, 480.0, 2000, 4000, 0, 0, NULL };
    logger_debug_with_category("AudioTones", "Playing ring (incoming)");
    start_tone(&s);
}

void audio_tones_play_busy_tone(void) {
    tone_spec_t s = { 480.0, 620.0, 500, 500, 0, 0, EARPIECE_PCM };
    logger_debug_with_category("AudioTones", "Playing busy tone");
    start_tone(&s);
}

void audio_tones_play_coin_tone(void) {
    tone_spec_t s = { 1700.0, 2200.0, 0, 0, COIN_DURATION_MS, COIN_AMPLITUDE, EARPIECE_PCM };
    logger_debug_with_category("AudioTones", "Playing coin tone");
    start_tone(&s);
}

/* ── Recorded-clip playback (WAV → ALSA) ──────────────────────────── */

#if HAVE_ALSA

typedef struct {
    unsigned char *data;   /* whole file, owned by the thread */
    wav_info_t     info;
} clip_arg_t;

static void *clip_thread_func(void *arg) {
    clip_arg_t *c = (clip_arg_t *)arg;
    snd_pcm_t *pcm = NULL;
    const unsigned char *p = c->data + c->info.data_offset;
    size_t remaining = c->info.data_len;
    int frame_bytes = c->info.channels * (c->info.bits_per_sample / 8);
    int err;

    err = snd_pcm_open(&pcm, EARPIECE_PCM, SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        logger_warnf_with_category("AudioTones", "Cannot open PCM for clip: %s",
                                   snd_strerror(err));
        pcm = NULL;
    } else {
        err = snd_pcm_set_params(pcm, SND_PCM_FORMAT_S16_LE,
                                 SND_PCM_ACCESS_RW_INTERLEAVED,
                                 (unsigned)c->info.channels,
                                 (unsigned)c->info.sample_rate, 1, 200000);
        if (err < 0) {
            logger_warnf_with_category("AudioTones", "Cannot set clip params: %s",
                                       snd_strerror(err));
            snd_pcm_close(pcm);
            pcm = NULL;
        }
    }

    while (pcm && !tone_stop_flag && frame_bytes > 0 &&
           remaining >= (size_t)frame_bytes) {
        snd_pcm_uframes_t want = 1024;
        size_t avail = remaining / (size_t)frame_bytes;
        snd_pcm_sframes_t frames;
        if ((size_t)want > avail) want = (snd_pcm_uframes_t)avail;
        frames = snd_pcm_writei(pcm, p, want);
        if (frames < 0) {
            frames = snd_pcm_recover(pcm, (int)frames, 0);
            if (frames < 0) break;
            continue;
        }
        p += (size_t)frames * (size_t)frame_bytes;
        remaining -= (size_t)frames * (size_t)frame_bytes;
    }

    if (pcm) {
        snd_pcm_drain(pcm);
        snd_pcm_close(pcm);
    }

    free(c->data);
    free(c);

    pthread_mutex_lock(&tone_mutex);
    tone_thread_running = 0;
    pthread_mutex_unlock(&tone_mutex);
    return NULL;
}

void audio_tones_play_clip(const char *path) {
    FILE *f;
    long sz;
    size_t rd;
    unsigned char *buf;
    clip_arg_t *arg;
    wav_info_t info;

    if (!path) return;

    f = fopen(path, "rb");
    if (!f) {
        logger_debugf_with_category("AudioTones", "Clip not found: %s", path);
        return;  /* leave any current sound playing as a fallback */
    }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return; }
    sz = ftell(f);
    if (sz <= 0 || sz > MAX_CLIP_BYTES) { fclose(f); return; }
    rewind(f);

    buf = (unsigned char *)malloc((size_t)sz);
    if (!buf) { fclose(f); return; }
    rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (rd != (size_t)sz) { free(buf); return; }

    if (wav_parse(buf, (size_t)sz, &info) != 0 || info.format != 1 ||
        info.bits_per_sample != 16 || info.channels < 1 || info.channels > 2 ||
        info.sample_rate <= 0 || info.data_len == 0) {
        logger_warnf_with_category("AudioTones",
                                   "Unsupported clip (need 16-bit PCM WAV): %s",
                                   path);
        free(buf);
        return;
    }

    audio_tones_stop();   /* one sound at a time */

    arg = (clip_arg_t *)malloc(sizeof(clip_arg_t));
    if (!arg) { free(buf); return; }
    arg->data = buf;
    arg->info = info;

    pthread_mutex_lock(&tone_mutex);
    tone_stop_flag = 0;
    tone_thread_running = 1;
    pthread_mutex_unlock(&tone_mutex);

    if (pthread_create(&tone_thread, NULL, clip_thread_func, arg) != 0) {
        free(buf);
        free(arg);
        pthread_mutex_lock(&tone_mutex);
        tone_thread_running = 0;
        pthread_mutex_unlock(&tone_mutex);
        logger_warn_with_category("AudioTones", "Failed to create clip thread");
    }
}

#else /* !HAVE_ALSA */

void audio_tones_play_clip(const char *path) { (void)path; }

#endif /* HAVE_ALSA */

void audio_tones_stop(void) {
    pthread_mutex_lock(&tone_mutex);
    if (tone_thread_running) {
        tone_stop_flag = 1;
        pthread_mutex_unlock(&tone_mutex);
        pthread_join(tone_thread, NULL);
        pthread_mutex_lock(&tone_mutex);
        tone_thread_running = 0;
        tone_stop_flag = 0;
    }
    pthread_mutex_unlock(&tone_mutex);
}

int audio_tones_is_playing(void) {
    int playing;
    pthread_mutex_lock(&tone_mutex);
    playing = tone_thread_running;
    pthread_mutex_unlock(&tone_mutex);
    return playing;
}
