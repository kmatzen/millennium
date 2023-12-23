#include "millennium_sdk.h"
#include <atomic>
#include <csignal>
#include <cstring> // for strcmp
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <typeindex>
#include <unordered_map>
#include <vector>

#define EVENT_CATEGORIES 3

const int COST_CENTS = 50;

struct call;

std::string line1;
std::string line2;

std::string generateDisplayBytes() {
  // Define the display dimensions
  const size_t DISPLAY_WIDTH = 20;
  const size_t TOTAL_CHARS =
      DISPLAY_WIDTH * 2; // Two lines of 20 characters each

  // Create a buffer to hold the output bytes
  std::vector<uint8_t> bytes;

  // Add control byte to clear the display (optional)
  //    bytes.push_back(0x0C); // Form Feed (FF) - Clears the display

  // Move the cursor back to the beginning after clearing
  //    bytes.push_back(0x11); // DC1 - Set to normal display mode (starts at
  //    position 0)

  // Truncate or pad line1 to fit DISPLAY_WIDTH
  std::string truncatedLine1 = line1.substr(0, DISPLAY_WIDTH);
  if (truncatedLine1.length() < DISPLAY_WIDTH) {
    truncatedLine1.append(DISPLAY_WIDTH - truncatedLine1.length(), ' ');
  }

  // Truncate or pad line2 to fit DISPLAY_WIDTH
  std::string truncatedLine2 = line2.substr(0, DISPLAY_WIDTH);
  if (truncatedLine2.length() < DISPLAY_WIDTH) {
    truncatedLine2.append(DISPLAY_WIDTH - truncatedLine2.length(), ' ');
  }

  // Add line1 characters to bytes
  for (char c : truncatedLine1) {
    bytes.push_back(static_cast<uint8_t>(c));
  }

  // Add line feed to move to the second line
  bytes.push_back(0x0A); // Line Feed (LF)

  // Add line2 characters to bytes
  for (char c : truncatedLine2) {
    bytes.push_back(static_cast<uint8_t>(c));
  }

  bytes.resize(std::min<size_t>(bytes.size(), 100));

  return std::string(bytes.begin(), bytes.end());
}

std::string format_number(const std::vector<char> &buffer) {
  std::vector<char> filled(10, '_');
  std::copy(buffer.begin(), buffer.end(), filled.begin());

  std::stringstream ss;
  ss << "(";
  for (int i = 0; i < 3; ++i)
    ss << filled[i];
  ss << ") ";
  for (int i = 3; i < 6; ++i)
    ss << filled[i];
  ss << "-";
  for (int i = 6; i < 10; ++i)
    ss << filled[i];
  return ss.str();
}

std::string generate_message(int inserted) {
  std::ostringstream message;
  message << "Insert " << std::setfill('0') << std::setw(2)
          << (COST_CENTS - inserted) << " cents";
  Logger::log(Logger::DEBUG, "Generated message: " + message.str());
  return message.str();
}

void handle_coin_event(const std::shared_ptr<CoinEvent> &coin_event,
                       int &inserted, const std::vector<char> &keypad_buffer,
                       MillenniumClient &client) {
  const auto &code = coin_event->coin_code();
  if (code == "COIN_6") {
    inserted += 5;
  } else if (code == "COIN_7") {
    inserted += 10;
  } else if (code == "COIN_8") {
    inserted += 25;
  }
  Logger::log(Logger::INFO, "Coin inserted: " + code + ", total inserted: " +
                                std::to_string(inserted));
  line1 = format_number(keypad_buffer);
  line2 = generate_message(inserted);
  client.setDisplay(generateDisplayBytes());
  Logger::log(Logger::DEBUG, "Line1 updated: " + line1);
}

void handle_call_state_event(
    const std::shared_ptr<CallStateEvent> &call_state_event, State &state,
    MillenniumClient &client) {
  if (call_state_event->get_state() == CALL_INCOMING && state == IDLE_DOWN) {
    Logger::log(Logger::INFO, "Incoming call received.");
    line1 = "Call incoming...";
    client.setDisplay(generateDisplayBytes());
    Logger::log(Logger::DEBUG, "Line1 updated: " + line1);
    state = CALL_INCOMING;
    client.writeToCoinValidator('f');
    client.writeToCoinValidator('z');
  }
}

void handle_hook_event(const std::shared_ptr<HookStateChangeEvent> &hook_event,
                       State &state, std::vector<char> &keypad_buffer,
                       int &inserted, MillenniumClient &client) {
  if (hook_event->get_direction() == 'U') {
    if (state == CALL_INCOMING) {
      Logger::log(Logger::INFO, "Call answered.");
      state = CALL_ACTIVE;
      client.answerCall();
    } else if (state == IDLE_DOWN) {
      Logger::log(Logger::INFO, "Hook lifted, transitioning to IDLE_UP.");
      state = IDLE_UP;
      client.writeToCoinValidator('a');
      inserted = 0;
      keypad_buffer.clear();
      line2 = generate_message(inserted);
      line1 = format_number(keypad_buffer);
      client.setDisplay(generateDisplayBytes());
      Logger::log(Logger::DEBUG, "Line1 updated: " + line1);
      Logger::log(Logger::DEBUG, "Line2 updated: " + line2);
    }
  } else if (hook_event->get_direction() == 'D') {
    Logger::log(Logger::INFO, "Hook down, call ended.");
    client.hangup();
    keypad_buffer.clear();
    inserted = 0;
    line2 = generate_message(inserted);
    line1 = format_number(keypad_buffer);
    client.setDisplay(generateDisplayBytes());
    Logger::log(Logger::DEBUG, "Line1 updated: " + line1);
    Logger::log(Logger::DEBUG, "Line2 updated: " + line2);

    client.writeToCoinValidator(state == IDLE_UP ? 'f' : 'c');
    client.writeToCoinValidator('z');
    state = IDLE_DOWN;
  }
}

void handle_keypad_event(const std::shared_ptr<KeypadEvent> &keypad_event,
                         std::vector<char> &keypad_buffer, int inserted,
                         MillenniumClient &client) {
  if (keypad_buffer.size() < 10) {
    char key = keypad_event->get_key();
    if (std::isdigit(key)) {
      Logger::log(Logger::DEBUG, "Key pressed: " + std::string(1, key));
      keypad_buffer.push_back(key);
      line2 = generate_message(inserted);
      line1 = format_number(keypad_buffer);
      client.setDisplay(generateDisplayBytes());
      Logger::log(Logger::DEBUG, "Line1 updated: " + line1);
      Logger::log(Logger::DEBUG, "Line2 updated: " + line2);
    }
  }
}

void check_and_call(State &state, const std::vector<char> &keypad_buffer,
                    int inserted, MillenniumClient &client) {
  if (keypad_buffer.size() == 10 && inserted >= COST_CENTS &&
      state == IDLE_UP) {
    Logger::log(Logger::INFO, "Dialing number.");
    line2 = "Calling";
    Logger::log(Logger::DEBUG, "Line2 updated: " + line2);
    client.setDisplay(generateDisplayBytes());
    client.call(std::string(keypad_buffer.begin(), keypad_buffer.end()));
    state = CALL_ACTIVE;
  }
}

class EventProcessor {
public:
  EventProcessor(MillenniumClient &client, State &state,
                 std::vector<char> &keypad_buffer, int &inserted)
      : client(client), state(state), keypad_buffer(keypad_buffer),
        inserted(inserted) {
    setup_dispatcher();
  }

  void process_event(const std::shared_ptr<Event> &event) {
    auto it = dispatcher.find(typeid(*event));
    if (it != dispatcher.end()) {
      it->second(event);
    } else {
      Logger::log(Logger::WARN, "Unhandled event type.");
    }
  }

private:
  MillenniumClient &client;
  State &state;
  std::vector<char> &keypad_buffer;
  int &inserted;

  std::unordered_map<std::type_index,
                     std::function<void(const std::shared_ptr<Event> &)>>
      dispatcher;

  void setup_dispatcher() {
    dispatcher[typeid(CoinEvent)] = [&](const std::shared_ptr<Event> &e) {
      auto coin_event = std::dynamic_pointer_cast<CoinEvent>(e);
      if (state == IDLE_UP) {
        handle_coin_event(coin_event, inserted, keypad_buffer, client);
      }
    };
    dispatcher[typeid(CallStateEvent)] = [&](const std::shared_ptr<Event> &e) {
      auto call_state_event = std::dynamic_pointer_cast<CallStateEvent>(e);
      handle_call_state_event(call_state_event, state, client);
    };
    dispatcher[typeid(HookStateChangeEvent)] =
        [&](const std::shared_ptr<Event> &e) {
          auto hook_event = std::dynamic_pointer_cast<HookStateChangeEvent>(e);
          handle_hook_event(hook_event, state, keypad_buffer, inserted, client);
        };
    dispatcher[typeid(KeypadEvent)] = [&](const std::shared_ptr<Event> &e) {
      auto keypad_event = std::dynamic_pointer_cast<KeypadEvent>(e);
      if (state == IDLE_UP) {
        handle_keypad_event(keypad_event, keypad_buffer, inserted, client);
      }
    };
  }
};

// Atomic flag to indicate whether the loop should continue
std::atomic<bool> running(true);

// Signal handler to catch Ctrl+C and update the running flag
void signal_handler(int signal) {
  if (signal == SIGINT) {
    std::cout << "\nExiting gracefully...\n";
    running = false;
  }
}

int main(int argc, char *argv[]) {
  std::signal(SIGINT, signal_handler);

  if (argc > 1) {
    Logger::setLevel(Logger::parseLevel(argv[1]));
  }

  State state = IDLE_DOWN;
  std::vector<char> keypad_buffer;
  int inserted = 0;
  MillenniumClient client;
  EventProcessor processor(client, state, keypad_buffer, inserted);

  line1 = format_number(keypad_buffer);
  line2 = generate_message(inserted);
  client.setDisplay(generateDisplayBytes());
  while (running) {
    client.update();
    auto event = client.nextEvent();
    if (event) {
      try {
        processor.process_event(event);
        check_and_call(state, keypad_buffer, inserted, client);
      } catch (const std::exception &exc) {
        Logger::log(Logger::ERROR, exc.what());
      }
    }
  }

  return 0;
}
