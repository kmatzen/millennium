#include "millennium_sdk.h"
#include <baresip.h>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <linux/serial.h>
#include <re.h>
#include <sstream>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

#define DEBUG_MODULE ""
#define DEBUG_LEVEL 0
#include <re_dbg.h>

namespace {
constexpr int BAUD_RATE = B9600;
}

enum { ASYNC_WORKERS = 4 };

Logger::Level Logger::parseLevel(const std::string &level_str) {
  if (level_str == "DEBUG")
    return Logger::DEBUG;
  if (level_str == "INFO")
    return Logger::INFO;
  if (level_str == "WARN")
    return Logger::WARN;
  if (level_str == "ERROR")
    return Logger::ERROR;
  return Logger::INFO; // Default level
}

Logger::Level Logger::current_level_ = Logger::INFO;

static void ua_event_handler(enum ua_event ev, bevent *event, void *client) {
  Logger::log(Logger::INFO, "UA event: " + std::string(uag_event_str(ev)));
  if (client) {
    struct call *call = bevent_get_call(event);
    ((MillenniumClient *)client)
        ->createAndQueueEvent(std::make_shared<CallStateEvent>(ev, call));
  }
}

void list_audio_devices() {
  struct le *le;

  Logger::log(Logger::INFO, "--- Audio Sources ---");
  for (le = list_head(baresip_ausrcl()); le; le = le->next) {
    struct ausrc *ausrc = (struct ausrc *)le->data;
    if (ausrc && ausrc->name) {
      Logger::log(Logger::INFO, "Source: " + std::string(ausrc->name));
    }
  }

  Logger::log(Logger::INFO, "--- Audio Players ---");
  for (le = list_head(baresip_auplayl()); le; le = le->next) {
    struct auplay *auplay = (struct auplay *)le->data;
    if (auplay && auplay->name) {
      Logger::log(Logger::INFO, "Player: " + std::string(auplay->name));
    }
  }
}

MillenniumClient::MillenniumClient() : display_fd_(-1), is_open_(false) {
  const std::string display_device =
      "/dev/serial/by-id/usb-Arduino_LLC_Millennium_Beta-if00";

  lastUpdateTime_ = std::chrono::steady_clock::now();
  displayDirty_ = false;
  display_fd_ = open(display_device.c_str(), O_RDWR | O_NOCTTY);
  if (display_fd_ == -1) {
    Logger::log(Logger::ERROR, "Failed to open display device.");
    throw std::runtime_error("Failed to open display device.");
  }

  // Set display_fd_ to non-blocking mode
  int flags = fcntl(display_fd_, F_GETFL, 0);
  if (flags == -1) {
    Logger::log(Logger::ERROR, "Failed to get file descriptor flags: " +
                                   std::string(strerror(errno)));
    throw std::runtime_error("Failed to get file descriptor flags: " +
                             std::string(strerror(errno)));
  }

  if (fcntl(display_fd_, F_SETFL, flags | O_NONBLOCK) == -1) {
    Logger::log(Logger::ERROR, "Failed to set non-blocking mode: " +
                                   std::string(strerror(errno)));
    throw std::runtime_error("Failed to set non-blocking mode: " +
                             std::string(strerror(errno)));
  }

  struct termios options;
  tcgetattr(display_fd_, &options);
  cfsetispeed(&options, BAUD_RATE);
  cfsetospeed(&options, BAUD_RATE);
  options.c_cflag |= (CS8 | CLOCAL | CREAD);
  options.c_cflag &= ~(PARENB | CSTOPB | CRTSCTS);
  options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
  tcsetattr(display_fd_, TCSANOW, &options);

  Logger::log(Logger::DEBUG, "libre_init");
  int err = libre_init();
  if (err) {
    Logger::log(Logger::ERROR, "libre_init failed");
    throw std::runtime_error("libre_init failed");
  }

  re_thread_async_init(ASYNC_WORKERS);

  log_enable_debug(true);

  int dbg_level = DBG_DEBUG;
  enum dbg_flags dbg_flags = (enum dbg_flags)(DBG_ANSI | DBG_TIME);
  dbg_init(dbg_level, dbg_flags);

  Logger::log(Logger::DEBUG, "conf_configure");
  err = conf_configure();
  if (err) {
    Logger::log(Logger::ERROR, "conf_configure failed");
    throw std::runtime_error("conf_configure failed");
  }

  Logger::log(Logger::DEBUG, "baresip_init");
  if (baresip_init(conf_config()) != 0) {
    Logger::log(Logger::ERROR, "Failed to initialize Baresip.");
    throw std::runtime_error("Failed to initialize Baresip.");
  }

  play_set_path(baresip_player(), "/usr/local/share/baresip");

  err = ua_init("baresip v" BARESIP_VERSION " (" ARCH "/" OS ")", true, true,
                true);
  if (err) {
    Logger::log(Logger::ERROR, "ua_init failed");
    throw std::runtime_error("ua_init failed");
  }

  std::string sip_uri =
      "sip:+<phonenumber>@matzen-test.sip.twilio.com"
      ";auth_user=+1<phonenumber>;auth_pass=<yourpasswordgoeshere>;transport=tls";

  err = ua_alloc(&ua_, sip_uri.c_str());
  if (err) {
    Logger::log(Logger::ERROR, "Failed to allocate SIP account");
    throw std::runtime_error("ua_alloc failed");
  }

  err = ua_register(ua_);
  if (err) {
    Logger::log(Logger::ERROR, "Failed to register SIP account");
    throw std::runtime_error("ua_register failed");
  }

  Logger::log(Logger::INFO, "SIP account registered successfully");

  Logger::log(Logger::DEBUG, "conf_modules");
  err = conf_modules();
  if (err) {
    Logger::log(Logger::ERROR, "conf_modules failed");
    throw std::runtime_error("conf_modules failed");
  }

  bevent_register(ua_event_handler, this);

  thread_ = std::thread([]() { re_main(nullptr); });

  Logger::log(Logger::INFO, "MillenniumClient initialized successfully.");
  is_open_ = true;
}

MillenniumClient::~MillenniumClient() { close(); }

void MillenniumClient::close() {
  if (is_open_) {
    ::close(display_fd_);
    ua_stop_all(true);
    ua_close();
    module_app_unload();
    conf_close();
    baresip_close();
    mod_close();
    re_thread_async_close();
    libre_close();
    thread_.join();
    is_open_ = false;
    Logger::log(Logger::INFO, "MillenniumClient closed.");
  }
}

void MillenniumClient::setupSIP(const std::string &username,
                                const std::string &password,
                                const std::string &domain) {
  std::ostringstream uri;
  uri << "sip:" << username << "@" << domain;

  std::string account_line =
      uri.str() + ";auth_user=" + username + ";auth_pass=" + password;

  if (ua_alloc(nullptr, account_line.c_str()) != 0) {
    Logger::log(Logger::ERROR, "Failed to configure SIP account.");
    throw std::runtime_error("Failed to configure SIP account.");
  }

  Logger::log(Logger::INFO, "SIP account configured successfully.");
}

void MillenniumClient::call(const std::string &number) {
  std::ostringstream target;
  target << "sip:+1" << number << "@matzen-test.sip.twilio.com";

  Logger::log(Logger::DEBUG, "ua: " + std::to_string((intptr_t)ua_));
  int err =
      ua_connect(ua_, nullptr, nullptr, target.str().c_str(), VIDMODE_OFF);
  if (err != 0) {
    Logger::log(Logger::ERROR, "Failed to initiate call to: " + target.str() +
                                   " " + std::to_string(err));
    throw std::runtime_error("Failed to initiate call");
  } else {
    Logger::log(Logger::INFO, "Calling: " + number);
  }
}

void MillenniumClient::answerCall() {
  ua_answer(ua_, nullptr, VIDMODE_OFF);
  Logger::log(Logger::INFO, "Call answered.");
}

void MillenniumClient::hangup() {
  ua_hangup(ua_, nullptr, 0, "Call terminated");
  Logger::log(Logger::INFO, "Call terminated.");
}

void MillenniumClient::update() {
  char buffer[1024]; // Optimized buffer size
  ssize_t bytes_read;

  // Read directly from the file descriptor
  while ((bytes_read = read(display_fd_, buffer, sizeof(buffer))) > 0) {
    input_buffer_.append(buffer, bytes_read);
    processEventBuffer();
  }

  if (bytes_read == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
    // Log only if the error is not due to no data being available
    Logger::log(Logger::ERROR, "Error reading from display_fd_: " +
                                   std::string(strerror(errno)));
  }

  std::chrono::steady_clock::time_point currentTime =
      std::chrono::steady_clock::now();
  auto elapsedTime = std::chrono::duration_cast<std::chrono::milliseconds>(
      currentTime - lastUpdateTime_);
  if (displayDirty_ && elapsedTime.count() > 33) {
    writeToDisplay(displayMessage_);
    lastUpdateTime_ = currentTime;
    displayDirty_ = false;
  } else {
    if (displayDirty_) {
      Logger::log(Logger::INFO, "waiting");
    }
  }
}

void MillenniumClient::processEventBuffer() {
  while (!input_buffer_.empty()) {
    Logger::log(Logger::DEBUG, "Buffer: " + input_buffer_);
    size_t event_start = input_buffer_.find_first_of("@KCVABDEFH");
    if (event_start == std::string::npos)
      return;

    char event_type = input_buffer_[event_start];
    std::string payload = extractPayload(event_type, event_start);
    Logger::log(Logger::DEBUG, "Event type: " + std::string(1, event_type));
    Logger::log(Logger::DEBUG, "Payload: " + payload);

    createAndQueueEvent(event_type, payload);
    input_buffer_.erase(0, event_start + payload.size() + 1);
  }
}

std::string MillenniumClient::extractPayload(char event_type,
                                             size_t event_start) const {
  size_t payload_length = 0;
  switch (event_type) {
  case EventType::KEYPAD:
  case EventType::HOOK:
  case EventType::COIN:
    payload_length = 1;
    break;
  case EventType::CARD:
    payload_length = 16;
    break;
  case EventType::EEPROM_ERROR:
    payload_length = 3;
    break;
  case EventType::COIN_UPLOAD_START:
  case EventType::COIN_UPLOAD_END:
  case EventType::COIN_VALIDATION_START:
  case EventType::COIN_VALIDATION_END:
    payload_length = 0;
    break;
  }

  if (event_start + payload_length <= input_buffer_.length()) {
    return input_buffer_.substr(event_start + 1, payload_length);
  }
  return {};
}

void MillenniumClient::createAndQueueEvent(
    const std::shared_ptr<Event> &event) {
  event_queue_.push(event);
}

void MillenniumClient::createAndQueueEvent(char event_type,
                                           const std::string &payload) {
  Logger::log(Logger::DEBUG,
              "Creating event of type: " + std::string(1, event_type));
  if (event_type == EventType::KEYPAD) {
    event_queue_.push(std::make_shared<KeypadEvent>(payload[0]));
  } else if (event_type == EventType::CARD) {
    event_queue_.push(std::make_shared<CardEvent>(payload));
  } else if (event_type == EventType::COIN) {
    event_queue_.push(
        std::make_shared<CoinEvent>(static_cast<uint8_t>(payload[0])));
  } else if (event_type == EventType::HOOK) {
    event_queue_.push(std::make_shared<HookStateChangeEvent>(payload[0]));
  } else if (event_type == EventType::COIN_UPLOAD_START) {
    event_queue_.push(std::make_shared<CoinEepromUploadStart>());
  } else if (event_type == EventType::COIN_UPLOAD_END) {
    event_queue_.push(std::make_shared<CoinEepromUploadEnd>());
  } else if (event_type == EventType::COIN_VALIDATION_START) {
    event_queue_.push(std::make_shared<CoinEepromValidationStart>());
  } else if (event_type == EventType::COIN_VALIDATION_END) {
    event_queue_.push(std::make_shared<CoinEepromValidationEnd>());
  } else if (event_type == EventType::EEPROM_ERROR) {
    uint8_t addr = static_cast<uint8_t>(payload[0]);
    uint8_t expected = static_cast<uint8_t>(payload[1]);
    uint8_t actual = static_cast<uint8_t>(payload[2]);
    event_queue_.push(
        std::make_shared<CoinEepromValidationError>(addr, expected, actual));
  } else {
    Logger::log(Logger::WARN,
                "Unknown event type: " + std::string(1, event_type));
  }
}

std::string CoinEvent::coin_code() const {
  switch (code_) {
  case 0x30:
    return "INVALID_COIN";
  case 0x31:
    return "COIN_1";
  case 0x32:
    return "COIN_2";
  case 0x33:
    return "COIN_3";
  case 0x34:
    return "COIN_4";
  case 0x35:
    return "COIN_5";
  case 0x36:
    return "COIN_6";
  case 0x37:
    return "COIN_7";
  case 0x38:
    return "COIN_8";
  default:
    return "UNKNOWN_COIN";
  }
}

void MillenniumClient::setDisplay(const std::string &message) {
  if (message == displayMessage_) {
    return;
  }
  displayDirty_ = true;
  displayMessage_ = message;
}

void MillenniumClient::writeToDisplay(const std::string &message) {
  Logger::log(Logger::DEBUG, "Writing message to display: " + message);

  // Step 1: Write the command
  writeCommand(0x02, {static_cast<uint8_t>(message.size())});

  // Step 2: Write the message in a loop to ensure all bytes are written
  size_t total_bytes_written = 0;
  size_t message_length = message.size();
  const char *data_ptr = message.c_str();

  while (total_bytes_written < message_length) {
    ssize_t bytes_written = write(display_fd_, data_ptr + total_bytes_written,
                                  message_length - total_bytes_written);

    if (bytes_written > 0) {
      total_bytes_written += bytes_written;
    } else if (bytes_written == -1) {
      if (errno == EINTR) {
        // Interrupted by a signal, retry
        continue;
      } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // Non-blocking mode: handle or wait before retrying
        usleep(200);
        continue;
      } else {
        // Log other errors and exit
        Logger::log(Logger::ERROR, "Error writing to display: " +
                                       std::string(strerror(errno)));
        return;
      }
    }
  }

  Logger::log(Logger::DEBUG, "Successfully wrote " +
                                 std::to_string(total_bytes_written) +
                                 " bytes to display.");
}

void MillenniumClient::writeToCoinValidator(uint8_t data) {
  Logger::log(Logger::DEBUG,
              "Writing to coin validator: " + std::to_string(data));

  // Step 1: Write the command
  writeCommand(0x03, {data});

  Logger::log(Logger::DEBUG, "Successfully wrote command to coin validator: " +
                                 std::to_string(data));
}

std::shared_ptr<Event> MillenniumClient::nextEvent() {
  if (!event_queue_.empty()) {
    auto event = event_queue_.front();
    event_queue_.pop();
    Logger::log(Logger::DEBUG,
                "Dequeued event: " + event->name() + " " + event->repr());
    return event;
  }
  return nullptr;
}

std::string CallStateEvent::repr() const {
  return std::string(uag_event_str(state_));
}

struct call *CallStateEvent::get_call() const {
  return call_;
}

enum State CallStateEvent::get_state() const {
  switch (state_) {
  case UA_EVENT_CALL_INCOMING:
    return CALL_INCOMING;
  case UA_EVENT_CALL_ESTABLISHED:
    return CALL_ACTIVE;
  default:
    return INVALID;
  }
}

void MillenniumClient::writeCommand(uint8_t command,
                                    const std::vector<uint8_t> &data) {
  Logger::log(Logger::DEBUG,
              "Writing command to display: " + std::to_string(command));

  // Step 1: Write the command
  while (true) {
    ssize_t bytes_written = write(display_fd_, &command, 1);
    if (bytes_written == 1) {
      break;
    } else if (bytes_written == -1) {
      if (errno == EINTR) {
        continue;
      } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
        usleep(200);
        continue;
      } else {
        Logger::log(Logger::ERROR,
                    "Failed to write command: " + std::to_string(command) +
                        ", error: " +
                        (bytes_written == -1 ? std::string(strerror(errno))
                                             : "partial write"));
        throw std::runtime_error(
            "Failed to write command: " + std::to_string(command) +
            ", error: " +
            (bytes_written == -1 ? std::string(strerror(errno))
                                 : "partial write"));
      }
    }
  }

  // Step 2: Write the data if it exists
  if (!data.empty()) {
    size_t total_bytes_written = 0;
    size_t data_size = data.size();
    const uint8_t *data_ptr = data.data();

    while (total_bytes_written < data_size) {
      ssize_t result = write(display_fd_, data_ptr + total_bytes_written,
                             data_size - total_bytes_written);

      if (result > 0) {
        total_bytes_written += result;
      } else if (result == -1) {
        if (errno == EINTR) {
          // Interrupted by a signal, retry
          continue;
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
          // Non-blocking mode: handle or log and retry
          usleep(200);
          continue;
        } else {
          Logger::log(Logger::ERROR, "Error writing data to display, error: " +
                                         std::string(strerror(errno)));
          throw std::runtime_error("Error writing data to display, error: " +
                                   std::string(strerror(errno)));
        }
      }
    }

    Logger::log(Logger::DEBUG, "Successfully wrote " +
                                   std::to_string(total_bytes_written) +
                                   " bytes of data to display.");
  }
}
