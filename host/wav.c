#include "wav.h"
#include <string.h>

static unsigned read_u16(const unsigned char *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static unsigned long read_u32(const unsigned char *p) {
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) |
           ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

int wav_parse(const unsigned char *data, size_t len, wav_info_t *out) {
    size_t pos;
    int have_fmt = 0;

    if (!data || !out || len < 12) return -1;
    if (memcmp(data, "RIFF", 4) != 0) return -1;
    if (memcmp(data + 8, "WAVE", 4) != 0) return -1;

    memset(out, 0, sizeof(*out));

    pos = 12;
    while (pos + 8 <= len) {
        const unsigned char *ck = data + pos;
        unsigned long cksize = read_u32(ck + 4);
        size_t body = pos + 8;

        if (cksize > len - body) {
            /* A declared size past the buffer is only tolerable for the final
             * data chunk (some writers leave it short of the real payload);
             * clamp it there and reject anything else as malformed. */
            if (memcmp(ck, "data", 4) == 0) {
                cksize = (unsigned long)(len - body);
            } else {
                return -1;
            }
        }

        if (memcmp(ck, "fmt ", 4) == 0) {
            if (cksize < 16) return -1;
            out->format          = (int)read_u16(ck + 8);
            out->channels        = (int)read_u16(ck + 10);
            out->sample_rate     = (int)read_u32(ck + 12);
            out->bits_per_sample = (int)read_u16(ck + 22);
            have_fmt = 1;
        } else if (memcmp(ck, "data", 4) == 0) {
            if (!have_fmt) return -1;
            out->data_offset = body;
            out->data_len = (size_t)cksize;
            return 0;
        }

        /* Chunks are word-aligned: a body of odd length carries a pad byte. */
        pos = body + cksize + (cksize & 1);
    }

    return -1;
}
