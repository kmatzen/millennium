#define _POSIX_C_SOURCE 200112L
#include "websocket.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

/* ── SHA-1 (RFC 3174) ──────────────────────────────────────────── */

static uint32_t sha1_rotl(uint32_t value, int bits) {
    return (value << bits) | (value >> (32 - bits));
}

static void sha1_hash(const uint8_t *msg, size_t msg_len, uint8_t digest[20]) {
    uint32_t h0 = 0x67452301;
    uint32_t h1 = 0xEFCDAB89;
    uint32_t h2 = 0x98BADCFE;
    uint32_t h3 = 0x10325476;
    uint32_t h4 = 0xC3D2E1F0;
    size_t padded_len, i;
    uint8_t *padded;
    uint64_t bit_len;

    bit_len = (uint64_t)msg_len * 8;
    padded_len = ((msg_len + 8) / 64 + 1) * 64;
    padded = (uint8_t *)calloc(padded_len, 1);
    if (!padded) return;
    memcpy(padded, msg, msg_len);
    padded[msg_len] = 0x80;

    for (i = 0; i < 8; i++) {
        padded[padded_len - 1 - i] = (uint8_t)(bit_len >> (i * 8));
    }

    for (i = 0; i < padded_len; i += 64) {
        uint32_t w[80];
        uint32_t a, b, c, d, e, f, k, temp;
        int t;

        for (t = 0; t < 16; t++) {
            w[t] = ((uint32_t)padded[i + t*4] << 24)
                 | ((uint32_t)padded[i + t*4+1] << 16)
                 | ((uint32_t)padded[i + t*4+2] << 8)
                 | ((uint32_t)padded[i + t*4+3]);
        }
        for (t = 16; t < 80; t++) {
            w[t] = sha1_rotl(w[t-3] ^ w[t-8] ^ w[t-14] ^ w[t-16], 1);
        }

        a = h0; b = h1; c = h2; d = h3; e = h4;

        for (t = 0; t < 80; t++) {
            if (t < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999;
            } else if (t < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (t < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            temp = sha1_rotl(a, 5) + f + e + k + w[t];
            e = d; d = c; c = sha1_rotl(b, 30); b = a; a = temp;
        }

        h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
    }

    free(padded);

    digest[0]  = (uint8_t)(h0 >> 24); digest[1]  = (uint8_t)(h0 >> 16);
    digest[2]  = (uint8_t)(h0 >> 8);  digest[3]  = (uint8_t)h0;
    digest[4]  = (uint8_t)(h1 >> 24); digest[5]  = (uint8_t)(h1 >> 16);
    digest[6]  = (uint8_t)(h1 >> 8);  digest[7]  = (uint8_t)h1;
    digest[8]  = (uint8_t)(h2 >> 24); digest[9]  = (uint8_t)(h2 >> 16);
    digest[10] = (uint8_t)(h2 >> 8);  digest[11] = (uint8_t)h2;
    digest[12] = (uint8_t)(h3 >> 24); digest[13] = (uint8_t)(h3 >> 16);
    digest[14] = (uint8_t)(h3 >> 8);  digest[15] = (uint8_t)h3;
    digest[16] = (uint8_t)(h4 >> 24); digest[17] = (uint8_t)(h4 >> 16);
    digest[18] = (uint8_t)(h4 >> 8);  digest[19] = (uint8_t)h4;
}

/* ── Base64 encoding ───────────────────────────────────────────── */

static const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void base64_encode(const uint8_t *in, size_t in_len, char *out, size_t out_size) {
    size_t i, o = 0;
    for (i = 0; i + 2 < in_len && o + 4 < out_size; i += 3) {
        out[o++] = b64_table[(in[i] >> 2) & 0x3F];
        out[o++] = b64_table[((in[i] & 0x03) << 4) | ((in[i+1] >> 4) & 0x0F)];
        out[o++] = b64_table[((in[i+1] & 0x0F) << 2) | ((in[i+2] >> 6) & 0x03)];
        out[o++] = b64_table[in[i+2] & 0x3F];
    }
    if (i < in_len && o + 4 < out_size) {
        out[o++] = b64_table[(in[i] >> 2) & 0x3F];
        if (i + 1 < in_len) {
            out[o++] = b64_table[((in[i] & 0x03) << 4) | ((in[i+1] >> 4) & 0x0F)];
            out[o++] = b64_table[((in[i+1] & 0x0F) << 2)];
        } else {
            out[o++] = b64_table[((in[i] & 0x03) << 4)];
            out[o++] = '=';
        }
        out[o++] = '=';
    }
    out[o] = '\0';
}

/* ── WebSocket API ─────────────────────────────────────────────── */

#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

int ws_compute_accept_key(const char *client_key, char *out, size_t out_size) {
    char concat[128];
    uint8_t digest[20];

    if (!client_key || !out || out_size < 29) return -1;

    snprintf(concat, sizeof(concat), "%s%s", client_key, WS_GUID);
    sha1_hash((const uint8_t *)concat, strlen(concat), digest);
    base64_encode(digest, 20, out, out_size);
    return 0;
}

uint8_t *ws_encode_text_frame(const char *payload, size_t payload_len, size_t *frame_len) {
    uint8_t *frame;
    size_t offset = 2;

    if (payload_len <= 125) {
        *frame_len = 2 + payload_len;
    } else if (payload_len <= 65535) {
        *frame_len = 4 + payload_len;
        offset = 4;
    } else {
        *frame_len = 10 + payload_len;
        offset = 10;
    }

    frame = (uint8_t *)malloc(*frame_len);
    if (!frame) return NULL;

    frame[0] = 0x81; /* FIN + text opcode */

    if (payload_len <= 125) {
        frame[1] = (uint8_t)payload_len;
    } else if (payload_len <= 65535) {
        frame[1] = 126;
        frame[2] = (uint8_t)(payload_len >> 8);
        frame[3] = (uint8_t)(payload_len & 0xFF);
    } else {
        frame[1] = 127;
        frame[2] = 0; frame[3] = 0; frame[4] = 0; frame[5] = 0;
        frame[6] = (uint8_t)((payload_len >> 24) & 0xFF);
        frame[7] = (uint8_t)((payload_len >> 16) & 0xFF);
        frame[8] = (uint8_t)((payload_len >> 8) & 0xFF);
        frame[9] = (uint8_t)(payload_len & 0xFF);
    }

    memcpy(frame + offset, payload, payload_len);
    return frame;
}

int ws_decode_frame(const uint8_t *data, size_t data_len,
                    uint8_t *payload_out, size_t *payload_len,
                    size_t *bytes_consumed) {
    int opcode;
    int masked;
    size_t plen, offset;
    uint8_t mask_key[4];
    size_t i;

    if (data_len < 2) return -1;

    opcode = data[0] & 0x0F;
    masked = (data[1] & 0x80) != 0;
    plen = data[1] & 0x7F;
    offset = 2;

    if (plen == 126) {
        if (data_len < 4) return -1;
        plen = ((size_t)data[2] << 8) | data[3];
        offset = 4;
    } else if (plen == 127) {
        if (data_len < 10) return -1;
        plen = ((size_t)data[6] << 24) | ((size_t)data[7] << 16)
             | ((size_t)data[8] << 8)  | data[9];
        offset = 10;
    }

    if (masked) {
        if (data_len < offset + 4) return -1;
        memcpy(mask_key, data + offset, 4);
        offset += 4;
    }

    if (data_len < offset + plen) return -1;

    for (i = 0; i < plen; i++) {
        payload_out[i] = masked ? (data[offset + i] ^ mask_key[i % 4])
                                : data[offset + i];
    }

    *payload_len = plen;
    *bytes_consumed = offset + plen;
    return opcode;
}

int ws_send_text(int fd, const char *message) {
    uint8_t *frame;
    size_t frame_len;
    ssize_t sent;

    if (!message) return -1;

    frame = ws_encode_text_frame(message, strlen(message), &frame_len);
    if (!frame) return -1;

    sent = send(fd, frame, frame_len, MSG_NOSIGNAL);
    free(frame);
    return (sent == (ssize_t)frame_len) ? 0 : -1;
}

int ws_send_close(int fd) {
    uint8_t frame[2];
    frame[0] = 0x88; /* FIN + close */
    frame[1] = 0x00; /* zero-length payload */
    return (send(fd, frame, 2, MSG_NOSIGNAL) == 2) ? 0 : -1;
}

int ws_send_pong(int fd, const uint8_t *payload, size_t payload_len) {
    uint8_t frame[128];
    if (payload_len > 125) payload_len = 125;
    frame[0] = 0x8A; /* FIN + pong */
    frame[1] = (uint8_t)payload_len;
    if (payload_len > 0 && payload) {
        memcpy(frame + 2, payload, payload_len);
    }
    return (send(fd, frame, 2 + payload_len, MSG_NOSIGNAL) == (ssize_t)(2 + payload_len)) ? 0 : -1;
}
