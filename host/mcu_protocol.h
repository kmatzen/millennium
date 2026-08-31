#ifndef MCU_PROTOCOL_H
#define MCU_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#define MCU_PROTOCOL_SOF 0x7e
#define MCU_PROTOCOL_VERSION 2
#define MCU_PROTOCOL_MAX_PAYLOAD 240
#define MCU_PROTOCOL_MAX_FRAME (MCU_PROTOCOL_MAX_PAYLOAD + 7)

typedef enum {
    MCU_MSG_ACK = 0x01,
    MCU_MSG_HELLO = 0x02,
    MCU_CMD_DISPLAY = 0x10,
    MCU_CMD_COIN_CONTROL = 0x11,
    MCU_CMD_COIN_PROGRAM = 0x12,
    MCU_CMD_COIN_VERIFY = 0x13,
    MCU_CMD_KEEPALIVE = 0x14,
    MCU_CMD_IDENTITY = 0x15,
    MCU_EVT_KEY = 0x20,
    MCU_EVT_HOOK = 0x21,
    MCU_EVT_CARD = 0x22,
    MCU_EVT_COIN = 0x23,
    MCU_EVT_DIAGNOSTIC = 0x24,
    MCU_EVT_HEARTBEAT = 0x25,
    MCU_EVT_OPERATION = 0x26
} mcu_message_type_t;

typedef struct {
    uint8_t type;
    uint8_t sequence;
    uint8_t length;
    uint8_t payload[MCU_PROTOCOL_MAX_PAYLOAD];
} mcu_frame_t;

typedef struct {
    uint8_t bytes[MCU_PROTOCOL_MAX_FRAME];
    size_t used;
    size_t expected;
} mcu_decoder_t;

typedef struct {
    uint8_t initialized;
    uint8_t last_sequence;
} mcu_replay_guard_t;

uint16_t mcu_protocol_crc16(const uint8_t *data, size_t length);
size_t mcu_protocol_encode(uint8_t type, uint8_t sequence,
                           const uint8_t *payload, size_t payload_length,
                           uint8_t *output, size_t output_size);
void mcu_decoder_init(mcu_decoder_t *decoder);
int mcu_decoder_feed(mcu_decoder_t *decoder, uint8_t byte, mcu_frame_t *frame);
int mcu_replay_accept(mcu_replay_guard_t *guard, uint8_t sequence);
int mcu_message_is_critical(uint8_t type);

#endif
