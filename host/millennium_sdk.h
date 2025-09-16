#pragma once

// clang-format off
#include <re.h>
#include <baresip.h>
// clang-format on
#include <cstdint>
#include <iostream>
#include <memory>
#include <queue>
#include <string>
#include <thread>
#include <vector>

enum State {
  INVALID = 0,
  IDLE_DOWN,
  IDLE_UP,
  CALL_INCOMING,
  CALL_ACTIVE,
};

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

class Logger {
public:
  enum Level { VERBOSE, DEBUG, INFO, WARN, ERROR };

  static Logger::Level parseLevel(const std::string &level_str);
  static void setLevel(Level level) { current_level_ = level; }

  static void log(Level level, const std::string &message) {
    if (level >= current_level_) {
      std::cerr << levelToString(level) << ": " << message << std::endl;
    }
  }

private:
  static Level current_level_;

  static std::string levelToString(Level level) {
    switch (level) {
    case VERBOSE:
      return "VERBOSE";
    case DEBUG:
      return "DEBUG";
    case INFO:
      return "INFO";
    case WARN:
      return "WARN";
    case ERROR:
      return "ERROR";
    default:
      return "UNKNOWN";
    }
  }
};

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

class CallStateEvent : public Event {
  enum ua_event state_;
  struct call *call_;

public:
  explicit CallStateEvent(enum ua_event state, struct call *call)
      : state_(state), call_(call) {}
  ~CallStateEvent() override = default;
  std::string name() const override { return "CallStateEvent"; }
  std::string repr() const override;
  enum State get_state() const;
  struct call *get_call() const;
};

class MillenniumClient {
  int display_fd_;
  bool is_open_;
  std::string input_buffer_;
  std::queue<std::shared_ptr<Event>> event_queue_;
  std::thread thread_;
  std::string displayMessage_;
  bool displayDirty_;
  std::chrono::steady_clock::time_point lastUpdateTime_;
  struct ua *ua_;

  void writeCommand(uint8_t command, const std::vector<uint8_t> &data);
  void processEventBuffer();
  std::string extractPayload(char event_type, size_t event_start) const;
  void writeToDisplay(const std::string &message);

public:
  MillenniumClient();
  ~MillenniumClient();

  void createAndQueueEvent(char event_type, const std::string &payload);
  void createAndQueueEvent(const std::shared_ptr<Event> &event);
  void setDisplay(const std::string &message);
  void writeToCoinValidator(uint8_t data);
  void update();
  std::shared_ptr<Event> nextEvent();
  void close();

  void call(const std::string &number);
  void answerCall();
  void hangup();
};
;
