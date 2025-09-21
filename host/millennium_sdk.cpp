#include "millennium_sdk.h"
#include "events.h"
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <linux/serial.h>
#include <sstream>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

namespace {
constexpr int BAUD_RATE = B9600;
}

enum { ASYNC_WORKERS = BARESIP_ASYNC_WORKERS };

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

static void ua_event_handler(baresip_ua_event ev, struct bevent *event, void *client) {
  Logger::log(Logger::INFO, "UA event: " + std::string(baresip_uag_event_str(ev)));
  if (client) {
    struct call *call = baresip_bevent_get_call(event);
    if (call) {
      // For incoming calls, we need to set the UA pointer
      if (ev == BARESIP_UA_EVENT_CALL_INCOMING) {
        struct ua *ua = baresip_call_get_ua(call);
        if (ua) {
          ((MillenniumClient *)client)->setUA(ua);
        }
      }
    }

    call_state_t state_value;
    if (ev == BARESIP_UA_EVENT_CALL_INCOMING) {
      state_value = EVENT_CALL_STATE_INCOMING;
    } else if (ev == BARESIP_UA_EVENT_CALL_ESTABLISHED) {
      state_value = EVENT_CALL_STATE_ACTIVE;
    } else {
      state_value = EVENT_CALL_STATE_INVALID;
    }

    call_state_event_t *call_event = call_state_event_create(baresip_uag_event_str(ev), call, state_value);
    if (call_event) {
        ((MillenniumClient *)client)->createAndQueueEvent((event_t *)call_event);
    }
  }
}

void list_audio_devices() {
  struct le *le;

  Logger::log(Logger::INFO, "--- Audio Sources ---");
  for (le = baresip_ausrcl_head(); le; le = baresip_list_next(le)) {
    struct ausrc *ausrc = baresip_list_data(le);
    if (ausrc && baresip_ausrc_name(ausrc)) {
      Logger::log(Logger::INFO, "Source: " + std::string(baresip_ausrc_name(ausrc)));
    }
  }

  Logger::log(Logger::INFO, "--- Audio Players ---");
  for (le = baresip_auplayl_head(); le; le = baresip_list_next(le)) {
    struct auplay *auplay = baresip_auplay_data(le);
    if (auplay && baresip_auplay_name(auplay)) {
      Logger::log(Logger::INFO, "Player: " + std::string(baresip_auplay_name(auplay)));
    }
  }
}

MillenniumClient::MillenniumClient() : display_fd_(-1), is_open_(false), ua_(nullptr) {
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
  int err = baresip_libre_init();
  if (err) {
    Logger::log(Logger::ERROR, "libre_init failed");
    throw std::runtime_error("libre_init failed");
  }

  baresip_re_thread_async_init(ASYNC_WORKERS);

  baresip_log_enable_debug(true);

  int dbg_level = BARESIP_DBG_DEBUG;
  int dbg_flags = BARESIP_DBG_ANSI | BARESIP_DBG_TIME;
  baresip_dbg_init(dbg_level, dbg_flags);

  Logger::log(Logger::DEBUG, "conf_configure");
  err = baresip_conf_configure();
  if (err) {
    Logger::log(Logger::ERROR, "conf_configure failed");
    throw std::runtime_error("conf_configure failed");
  }

  Logger::log(Logger::DEBUG, "baresip_init_c");
  if (baresip_init_c(baresip_conf_config()) != 0) {
    Logger::log(Logger::ERROR, "Failed to initialize Baresip.");
    throw std::runtime_error("Failed to initialize Baresip.");
  }

  baresip_play_set_path(baresip_player_c(), "/usr/local/share/baresip");

  err = baresip_ua_init("baresip v2.0.0 (x86_64/linux)", true, true,
                true);
  if (err) {
    Logger::log(Logger::ERROR, "ua_init failed");
    throw std::runtime_error("ua_init failed");
  }

  Logger::log(Logger::DEBUG, "conf_modules");
  err = baresip_conf_modules();
  if (err) {
    Logger::log(Logger::ERROR, "conf_modules failed");
    throw std::runtime_error("conf_modules failed");
  }

  baresip_bevent_register(ua_event_handler, this);

  thread_ = std::thread([]() { baresip_re_main(nullptr); });

  Logger::log(Logger::INFO, "MillenniumClient initialized successfully.");
  is_open_ = true;
}

MillenniumClient::~MillenniumClient() { close(); }

void MillenniumClient::close() {
  if (is_open_) {
    ::close(display_fd_);
    baresip_ua_stop_all(true);
    baresip_ua_close();
    baresip_module_app_unload();
    baresip_conf_close();
    baresip_close_c();
    baresip_mod_close();
    baresip_re_thread_async_close();
    baresip_libre_close();
    thread_.join();
    is_open_ = false;
    Logger::log(Logger::INFO, "MillenniumClient closed.");
  }
}

void MillenniumClient::call(const std::string &number) {
  std::ostringstream target;
  target << "+1" << number; // Pass just the number, like the CLI does

  Logger::log(Logger::DEBUG, "ua: " + std::to_string((intptr_t)ua_));
  Logger::log(Logger::INFO, "Initiating call to: " + target.str());
  
  // Find the appropriate UA for this request URI (like the CLI does)
  struct ua *ua = baresip_ua_find_requri(target.str().c_str());
  
  if (!ua) {
    Logger::log(Logger::ERROR, "Could not find UA for: " + target.str());
    throw std::runtime_error("Could not find UA for call");
  }
  
  // Store the UA for later use in answer/hangup
  ua_ = ua;
  
  // Complete the URI (like the CLI does)
  char *uric = nullptr;
  int err = baresip_account_uri_complete_strdup(baresip_ua_account(ua), &uric, target.str().c_str());
  if (err != 0) {
    Logger::log(Logger::ERROR, "Failed to complete URI: " + target.str() + " " + std::to_string(err));
    throw std::runtime_error("Failed to complete URI");
  }
  
  Logger::log(Logger::INFO, "Using UA: " + std::string(baresip_account_aor(baresip_ua_account(ua))));
  Logger::log(Logger::INFO, "Completed URI: " + std::string(uric));
  
  // Make the call (like the CLI does)
  struct call *call = nullptr;
  err = baresip_ua_connect(ua, &call, nullptr, uric, BARESIP_VIDMODE_OFF);
  
  // Clean up the completed URI
  baresip_mem_deref(uric);
  
  if (err != 0) {
    Logger::log(Logger::ERROR, "Failed to initiate call to: " + target.str() +
                                   " " + std::to_string(err));
    throw std::runtime_error("Failed to initiate call");
  } else {
    Logger::log(Logger::INFO, "Calling: " + number);
  }
}

void MillenniumClient::answerCall() {
  if (!ua_) {
    Logger::log(Logger::ERROR, "Cannot answer call: UA is null");
    return;
  }
  
  // Find the current call for this UA
  struct call *call = baresip_ua_call(ua_);
  if (!call) {
    Logger::log(Logger::ERROR, "Cannot answer call: No active call found");
    return;
  }
  
  baresip_ua_answer(ua_, call, BARESIP_VIDMODE_OFF);
  Logger::log(Logger::INFO, "Call answered.");
}

void MillenniumClient::hangup() {
  if (!ua_) {
    Logger::log(Logger::ERROR, "Cannot hangup call: UA is null");
    return;
  }
  
  // Find the current call for this UA
  struct call *call = baresip_ua_call(ua_);
  if (!call) {
    Logger::log(Logger::WARN, "Cannot hangup call: No active call found");
    return;
  }
  
  baresip_ua_hangup(ua_, call, 0, "Call terminated");
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
  case EVENT_TYPE_KEYPAD:
  case EVENT_TYPE_HOOK:
  case EVENT_TYPE_COIN:
    payload_length = 1;
    break;
  case EVENT_TYPE_CARD:
    payload_length = 16;
    break;
  case EVENT_TYPE_EEPROM_ERROR:
    payload_length = 3;
    break;
  case EVENT_TYPE_COIN_UPLOAD_START:
  case EVENT_TYPE_COIN_UPLOAD_END:
  case EVENT_TYPE_COIN_VALIDATION_START:
  case EVENT_TYPE_COIN_VALIDATION_END:
    payload_length = 0;
    break;
  }

  if (event_start + payload_length <= input_buffer_.length()) {
    return input_buffer_.substr(event_start + 1, payload_length);
  }
  return {};
}

void MillenniumClient::createAndQueueEvent(event_t *event) {
  if (event) {
    event_queue_.push(event);
  }
}

void MillenniumClient::createAndQueueEvent(char event_type,
                                           const std::string &payload) {
  Logger::log(Logger::DEBUG,
              "Creating event of type: " + std::string(1, event_type));
  if (event_type == EVENT_TYPE_KEYPAD) {
    keypad_event_t *event = keypad_event_create(payload[0]);
    if (event) event_queue_.push((event_t *)event);
  } else if (event_type == EVENT_TYPE_CARD) {
    card_event_t *event = card_event_create(payload.c_str());
    if (event) event_queue_.push((event_t *)event);
  } else if (event_type == EVENT_TYPE_COIN) {
    coin_event_t *event = coin_event_create(static_cast<uint8_t>(payload[0]));
    if (event) event_queue_.push((event_t *)event);
  } else if (event_type == EVENT_TYPE_HOOK) {
    hook_state_change_event_t *event = hook_state_change_event_create(payload[0]);
    if (event) event_queue_.push((event_t *)event);
  } else if (event_type == EVENT_TYPE_COIN_UPLOAD_START) {
    coin_eeprom_upload_start_t *event = coin_eeprom_upload_start_create();
    if (event) event_queue_.push((event_t *)event);
  } else if (event_type == EVENT_TYPE_COIN_UPLOAD_END) {
    coin_eeprom_upload_end_t *event = coin_eeprom_upload_end_create();
    if (event) event_queue_.push((event_t *)event);
  } else if (event_type == EVENT_TYPE_COIN_VALIDATION_START) {
    coin_eeprom_validation_start_t *event = coin_eeprom_validation_start_create();
    if (event) event_queue_.push((event_t *)event);
  } else if (event_type == EVENT_TYPE_COIN_VALIDATION_END) {
    coin_eeprom_validation_end_t *event = coin_eeprom_validation_end_create();
    if (event) event_queue_.push((event_t *)event);
  } else if (event_type == EVENT_TYPE_EEPROM_ERROR) {
    uint8_t addr = static_cast<uint8_t>(payload[0]);
    uint8_t expected = static_cast<uint8_t>(payload[1]);
    uint8_t actual = static_cast<uint8_t>(payload[2]);
    coin_eeprom_validation_error_t *event = coin_eeprom_validation_error_create(addr, expected, actual);
    if (event) event_queue_.push((event_t *)event);
  } else {
    Logger::log(Logger::WARN,
                "Unknown event type: " + std::string(1, event_type));
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

event_t *MillenniumClient::nextEvent() {
  if (!event_queue_.empty()) {
    event_t *event = event_queue_.front();
    event_queue_.pop();
    char *repr = event_get_repr(event);
    Logger::log(Logger::DEBUG,
                "Dequeued event: " + std::string(event_get_name(event)) + " " + std::string(repr ? repr : ""));
    if (repr) free(repr);
    return event;
  }
  return nullptr;
}


void MillenniumClient::setUA(struct ua *ua) {
  ua_ = ua;
  Logger::log(Logger::DEBUG, "UA set to: " + std::to_string((intptr_t)ua_));
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
