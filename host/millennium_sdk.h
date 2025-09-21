#pragma once

#include <cstdint>
#include <iostream>
#include <memory>
#include <queue>
#include <string>
#include <thread>
#include <vector>
extern "C" {
#include "events.h"
#include "baresip_interface.h"
}

// Forward declarations

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


class MillenniumClient {
  int display_fd_;
  bool is_open_;
  std::string input_buffer_;
  std::queue<event_t *> event_queue_;
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
  void createAndQueueEvent(event_t *event);
  void setDisplay(const std::string &message);
  void writeToCoinValidator(uint8_t data);
  void update();
  event_t *nextEvent();
  void close();

  void call(const std::string &number);
  void answerCall();
  void hangup();
  void setUA(struct ua *ua);
};
;
