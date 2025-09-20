#pragma once

// clang-format off
#include <re.h>
#include <baresip.h>
// clang-format on
#include <cstdint>
#include <memory>
#include <string>

namespace EventType {
constexpr char KEYPAD = 'K';
constexpr char CARD = 'C';
constexpr char COIN = 'V';
constexpr char COIN_UPLOAD_START = 'A';
constexpr char COIN_UPLOAD_END = 'B';
constexpr char COIN_VALIDATION_START = 'D';
constexpr char COIN_VALIDATION_END = 'F';
constexpr char EEPROM_ERROR = 'E';
constexpr char HOOK = 'H';
constexpr char CALL_STATE = '1';
} // namespace EventType

class Event {
public:
  virtual ~Event() = default;
  virtual std::string name() const = 0;
  virtual std::string repr() const = 0;
};

class KeypadEvent : public Event {
  char key_;

public:
  explicit KeypadEvent(char key) : key_(key) {}
  std::string name() const override { return "KeypadEvent"; }
  std::string repr() const override { return std::string(1, key_); }
  char get_key() const { return key_; }
};

class CardEvent : public Event {
  std::string card_number_;

public:
  explicit CardEvent(const std::string &card_number)
      : card_number_(card_number) {}
  std::string name() const override { return "CardEvent"; }
  std::string repr() const override { return card_number_; }
};

class CoinEvent : public Event {
  uint8_t code_;

public:
  explicit CoinEvent(uint8_t code) : code_(code) {}
  std::string name() const override { return "CoinEvent"; }
  std::string repr() const override { return coin_code(); }
  std::string coin_code() const;
};

class HookStateChangeEvent : public Event {
  char state_;

public:
  explicit HookStateChangeEvent(char state) : state_(state) {}
  std::string name() const override { return "HookStateChangeEvent"; }
  std::string repr() const override { return state_ == 'U' ? "Up" : "Down"; }
  char get_direction() const { return state_; }
};

class CoinEepromUploadStart : public Event {
public:
  std::string name() const override { return "CoinEepromUploadStart"; }
  std::string repr() const override { return {}; }
};

class CoinEepromUploadEnd : public Event {
public:
  std::string name() const override { return "CoinEepromUploadEnd"; }
  std::string repr() const override { return {}; }
};

class CoinEepromValidationStart : public Event {
public:
  std::string name() const override { return "CoinEepromValidationStart"; }
  std::string repr() const override { return {}; }
};

class CoinEepromValidationEnd : public Event {
public:
  std::string name() const override { return "CoinEepromValidationEnd"; }
  std::string repr() const override { return {}; }
};

class CoinEepromValidationError : public Event {
  uint8_t addr_;
  uint8_t expected_;
  uint8_t actual_;

public:
  CoinEepromValidationError(uint8_t addr, uint8_t expected, uint8_t actual)
      : addr_(addr), expected_(expected), actual_(actual) {}

  std::string name() const override { return "CoinEepromValidationError"; }
  std::string repr() const override {
    return "Addr: " + std::to_string(addr_) +
           ", Expected: " + std::to_string(expected_) +
           ", Actual: " + std::to_string(actual_);
  }
};

struct call;

class CallStateEvent : public Event {
public:
  enum State {
    INVALID = 0,
    CALL_INCOMING,
    CALL_ACTIVE,
  };

private:
  std::string state_;
  struct call *call_;
  enum State state_value_;

public:
  explicit CallStateEvent(const std::string &state, struct call *call, enum CallStateEvent::State state_value)
      : state_(state), call_(call), state_value_(state_value) {}
  ~CallStateEvent() override = default;
  std::string name() const override { return "CallStateEvent"; }
  std::string repr() const override;
  enum CallStateEvent::State get_state() const;
  struct call *get_call() const;
};
