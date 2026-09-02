#include "mcu_protocol.h"

#include <string.h>

uint16_t mcu_protocol_crc16(const uint8_t *data, size_t length) {
    uint16_t crc = 0xffff;
    size_t i;
    int bit;
    if (!data && length) return 0;
    for (i = 0; i < length; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (bit = 0; bit < 8; bit++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

size_t mcu_protocol_encode(uint8_t type, uint8_t sequence,
                           const uint8_t *payload, size_t payload_length,
                           uint8_t *output, size_t output_size) {
    uint16_t crc;
    size_t total = payload_length + 7;
    if (!output || payload_length > MCU_PROTOCOL_MAX_PAYLOAD || output_size < total ||
            (payload_length && !payload)) return 0;
    output[0] = MCU_PROTOCOL_SOF;
    output[1] = MCU_PROTOCOL_VERSION;
    output[2] = (uint8_t)payload_length;
    output[3] = type;
    output[4] = sequence;
    if (payload_length) memcpy(output + 5, payload, payload_length);
    crc = mcu_protocol_crc16(output + 1, payload_length + 4);
    output[5 + payload_length] = (uint8_t)(crc >> 8);
    output[6 + payload_length] = (uint8_t)crc;
    return total;
}

void mcu_decoder_init(mcu_decoder_t *decoder) {
    if (decoder) memset(decoder, 0, sizeof(*decoder));
}

int mcu_decoder_feed(mcu_decoder_t *decoder, uint8_t byte, mcu_frame_t *frame) {
    size_t payload_length;
    uint16_t expected_crc;
    uint16_t actual_crc;
    if (!decoder || !frame) return -1;
    if (decoder->used == 0) {
        if (byte != MCU_PROTOCOL_SOF) return 0;
        decoder->bytes[decoder->used++] = byte;
        return 0;
    }
    if (decoder->used >= sizeof(decoder->bytes)) {
        mcu_decoder_init(decoder);
        return -1;
    }
    decoder->bytes[decoder->used++] = byte;
    if (decoder->used == 3) {
        if (decoder->bytes[1] != MCU_PROTOCOL_VERSION ||
                decoder->bytes[2] > MCU_PROTOCOL_MAX_PAYLOAD) {
            mcu_decoder_init(decoder);
            return -1;
        }
        decoder->expected = (size_t)decoder->bytes[2] + 7;
    }
    if (!decoder->expected || decoder->used < decoder->expected) return 0;
    payload_length = decoder->bytes[2];
    expected_crc = ((uint16_t)decoder->bytes[5 + payload_length] << 8) |
                   decoder->bytes[6 + payload_length];
    actual_crc = mcu_protocol_crc16(decoder->bytes + 1, payload_length + 4);
    if (actual_crc != expected_crc) {
        mcu_decoder_init(decoder);
        return -1;
    }
    frame->length = (uint8_t)payload_length;
    frame->type = decoder->bytes[3];
    frame->sequence = decoder->bytes[4];
    if (payload_length) memcpy(frame->payload, decoder->bytes + 5, payload_length);
    mcu_decoder_init(decoder);
    return 1;
}

int mcu_replay_accept(mcu_replay_guard_t *guard, uint8_t sequence) {
    uint8_t distance;
    if (!guard) return 0;
    if (!guard->initialized) {
        guard->initialized = 1;
        guard->last_sequence = sequence;
        return 1;
    }
    distance = (uint8_t)(sequence - guard->last_sequence);
    if (distance == 0 || distance >= 128) return 0;
    guard->last_sequence = sequence;
    return 1;
}

int mcu_message_is_critical(uint8_t type) {
    return type == MCU_CMD_DISPLAY || type == MCU_CMD_COIN_CONTROL ||
           type == MCU_CMD_COIN_PROGRAM || type == MCU_CMD_COIN_VERIFY;
}
