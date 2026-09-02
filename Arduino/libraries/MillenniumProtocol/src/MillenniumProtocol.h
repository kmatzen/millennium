#ifndef MILLENNIUM_PROTOCOL_H
#define MILLENNIUM_PROTOCOL_H

#include <Arduino.h>

#define MILLENNIUM_PROTOCOL_SOF 0x7e
#define MILLENNIUM_PROTOCOL_VERSION 2
#define MILLENNIUM_PROTOCOL_STRINGIFY_INNER(value) #value
#define MILLENNIUM_PROTOCOL_STRINGIFY(value) MILLENNIUM_PROTOCOL_STRINGIFY_INNER(value)
#define MILLENNIUM_PROTOCOL_VERSION_STRING MILLENNIUM_PROTOCOL_STRINGIFY(MILLENNIUM_PROTOCOL_VERSION)
#define MILLENNIUM_PROTOCOL_MAX_PAYLOAD 100
#define MILLENNIUM_PROTOCOL_MAX_FRAME 107

enum MillenniumMessageType {
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
};

struct MillenniumFrame {
  uint8_t type;
  uint8_t sequence;
  uint8_t length;
  uint8_t payload[MILLENNIUM_PROTOCOL_MAX_PAYLOAD];
};

class MillenniumDecoder {
 public:
  MillenniumDecoder() { reset(); }
  void reset() { used_ = 0; expected_ = 0; }
  bool active() const { return used_ != 0; }
  int feed(uint8_t byte, MillenniumFrame &frame) {
    if (used_ == 0) {
      if (byte != MILLENNIUM_PROTOCOL_SOF) return 0;
      bytes_[used_++] = byte;
      return 0;
    }
    if (used_ >= sizeof(bytes_)) { reset(); return -1; }
    bytes_[used_++] = byte;
    if (used_ == 3) {
      if (bytes_[1] != MILLENNIUM_PROTOCOL_VERSION ||
          bytes_[2] > MILLENNIUM_PROTOCOL_MAX_PAYLOAD) { reset(); return -1; }
      expected_ = bytes_[2] + 7;
    }
    if (!expected_ || used_ < expected_) return 0;
    uint8_t length = bytes_[2];
    uint16_t expected = ((uint16_t)bytes_[5 + length] << 8) | bytes_[6 + length];
    uint16_t actual = crc16(bytes_ + 1, length + 4);
    if (expected != actual) { reset(); return -1; }
    frame.length = length;
    frame.type = bytes_[3];
    frame.sequence = bytes_[4];
    if (length) memcpy(frame.payload, bytes_ + 5, length);
    reset();
    return 1;
  }

 private:
  uint8_t bytes_[MILLENNIUM_PROTOCOL_MAX_FRAME];
  uint8_t used_;
  uint8_t expected_;

 public:
  static uint16_t crc16(const uint8_t *data, size_t length) {
    uint16_t crc = 0xffff;
    for (size_t i = 0; i < length; i++) {
      crc ^= (uint16_t)data[i] << 8;
      for (uint8_t bit = 0; bit < 8; bit++)
        crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
  }
};

static inline size_t millenniumEncode(uint8_t type, uint8_t sequence,
                                      const uint8_t *payload, uint8_t length,
                                      uint8_t *output, size_t capacity) {
  size_t total = (size_t)length + 7;
  if (!output || length > MILLENNIUM_PROTOCOL_MAX_PAYLOAD || capacity < total ||
      (length && !payload)) return 0;
  output[0] = MILLENNIUM_PROTOCOL_SOF;
  output[1] = MILLENNIUM_PROTOCOL_VERSION;
  output[2] = length;
  output[3] = type;
  output[4] = sequence;
  if (length) memcpy(output + 5, payload, length);
  uint16_t crc = MillenniumDecoder::crc16(output + 1, length + 4);
  output[5 + length] = highByte(crc);
  output[6 + length] = lowByte(crc);
  return total;
}

#endif
