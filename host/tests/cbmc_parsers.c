/*
 * CBMC memory-safety proofs for the daemon's untrusted-input parsers.
 *
 * Each verify_* entry runs the real (or verbatim-copied) parser on fully
 * NONDETERMINISTIC input, so a successful CBMC run proves no out-of-bounds
 * access / pointer error / overflow for ANY input up to the modeled size. Run
 * with --32 to match the Pi (armv7l), which catches 32-bit size_t overflows.
 *
 *   make cbmc-parsers
 *
 * Targets:
 *   verify_wav          real wav.c (parses untrusted WAV file bytes)
 *   verify_ws_frame     ws_decode_frame, copied from websocket.c (network input)
 *   verify_config_trim  config_trim, copied from config.c
 * (The ws/config_trim bodies are copied verbatim; keep in sync.)
 *
 * The /api/control JSON field extraction in web_server.c is intentionally NOT
 * here: it parses with strstr/strchr, and CBMC's models of those can't prove the
 * returned pointers stay within the body, yielding spurious failures. Its
 * destination copies are guarded (len < sizeof(dest)) and it's exercised by
 * tests/break_test.sh fuzzing.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "../wav.h"

size_t nondet_size(void);
int nondet_bool(void);

/* isspace() abstracted as a nondeterministic predicate: it still dereferences
 * the char (so CBMC checks that read is in bounds) but returns any classification,
 * so the memory-safety proof holds for ALL whitespace definitions. This also
 * dodges modeling libc's ctype internals. */
static int hns_is_space(char c) { (void)c; return nondet_bool(); }

/* ── 1. wav.c (real) ─────────────────────────────────────────────────── */
#define WAV_CAP 40
void verify_wav(void) {
    unsigned char buf[WAV_CAP];
    wav_info_t info;
    size_t len = nondet_size();
    __CPROVER_assume(len <= (size_t)WAV_CAP);
    wav_parse(buf, len, &info);
}

/* ── 2. ws_decode_frame (verbatim from websocket.c) ──────────────────── */
static int ws_decode_frame(const uint8_t *data, size_t data_len,
                    uint8_t *payload_out, size_t payload_out_size,
                    size_t *payload_len, size_t *bytes_consumed) {
    int opcode, masked;
    size_t plen, offset, i;
    uint8_t mask_key[4];
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
    if (plen > payload_out_size) return -1;
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

#define WS_DCAP 24
#define WS_POUT 8
void verify_ws_frame(void) {
    uint8_t data[WS_DCAP];
    uint8_t pout[WS_POUT];
    size_t data_len = nondet_size();
    size_t pout_size = nondet_size();
    size_t plen, consumed;
    __CPROVER_assume(data_len <= (size_t)WS_DCAP);
    __CPROVER_assume(pout_size <= (size_t)WS_POUT);
    ws_decode_frame(data, data_len, pout, pout_size, &plen, &consumed);
}

/* ── 3. config_trim (verbatim from config.c) ─────────────────────────── */
static char *config_trim(const char *str, char *result, size_t result_size) {
    const char *start;
    const char *end;
    size_t len;
    if (str == NULL || result == NULL || result_size == 0) {
        if (result != NULL && result_size > 0) result[0] = '\0';
        return result;
    }
    start = str;
    while (*start != '\0' && hns_is_space(*start)) start++;
    if (*start == '\0') { result[0] = '\0'; return result; }
    end = str + strlen(str) - 1;
    while (end > start && hns_is_space(*end)) end--;
    len = (size_t)(end - start + 1);
    if (len >= result_size) len = result_size - 1;
    strncpy(result, start, len);
    result[len] = '\0';
    return result;
}

#define TRIM_SCAP 12
#define TRIM_RCAP 8
void verify_config_trim(void) {
    char str[TRIM_SCAP];
    char result[TRIM_RCAP];
    size_t rsize = nondet_size();
    str[TRIM_SCAP - 1] = '\0';
    __CPROVER_assume(rsize <= (size_t)TRIM_RCAP);
    config_trim(str, result, rsize);
}
