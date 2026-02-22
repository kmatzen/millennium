#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include <stddef.h>
#include <stdint.h>

/* WebSocket opcodes (RFC 6455 section 5.2) */
#define WS_OPCODE_TEXT   0x1
#define WS_OPCODE_BINARY 0x2
#define WS_OPCODE_CLOSE  0x8
#define WS_OPCODE_PING   0x9
#define WS_OPCODE_PONG   0xA

/*
 * Compute the Sec-WebSocket-Accept value for the given client key.
 * Writes a base64-encoded SHA-1 hash into `out` (must be >= 29 bytes).
 * Returns 0 on success.
 */
int ws_compute_accept_key(const char *client_key, char *out, size_t out_size);

/*
 * Build a WebSocket frame around a text payload.
 * Returns allocated buffer (caller must free) with the total frame length
 * written to *frame_len. Server frames are never masked.
 */
uint8_t *ws_encode_text_frame(const char *payload, size_t payload_len, size_t *frame_len);

/*
 * Decode a WebSocket frame from raw bytes.
 * Returns the opcode, writes the unmasked payload into `payload_out`
 * (must be >= data_len), and the payload length into *payload_len.
 * Returns -1 if the frame is incomplete or invalid.
 * *bytes_consumed is set to the total frame size read from `data`.
 */
int ws_decode_frame(const uint8_t *data, size_t data_len,
                    uint8_t *payload_out, size_t *payload_len,
                    size_t *bytes_consumed);

/*
 * Send a WebSocket text frame on a file descriptor.
 * Returns 0 on success, -1 on failure.
 */
int ws_send_text(int fd, const char *message);

/*
 * Send a WebSocket close frame.
 */
int ws_send_close(int fd);

/*
 * Send a WebSocket pong frame in response to a ping payload.
 */
int ws_send_pong(int fd, const uint8_t *payload, size_t payload_len);

#endif /* WEBSOCKET_H */
