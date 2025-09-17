#include "config.h"
#include "logger.h"
#include "health_monitor.h"
#include "metrics.h"
#include "metrics_server.h"
#include "web_server.h"
#include "millennium_sdk.h"
#include "payphone_state_machine.h"
#include <atomic>
#include <csignal>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <typeindex>
#include <unordered_map>
#include <vector>
#include <thread>
#include <chrono>
#include <fstream>
#include <filesystem>

#define EVENT_CATEGORIES 3

// Global state management - now using PayPhoneStateMachine

// Forward declarations
class EventProcessor;

// Global instances
std::atomic<bool> running(true);
std::unique_ptr<PayPhoneStateMachine> state_machine;
std::unique_ptr<MillenniumClient> client;
std::unique_ptr<MetricsServer> metrics_server;
std::unique_ptr<WebServer> web_server;
std::unique_ptr<EventProcessor> event_processor;

// Daemon start time for uptime calculation
std::chrono::steady_clock::time_point daemon_start_time;

// Function to get daemon state info for web server
struct DaemonStateInfo {
    int current_state;
    int inserted_cents;
    std::string keypad_buffer;
    std::chrono::steady_clock::time_point last_activity;
};

DaemonStateInfo getDaemonStateInfo() {
    DaemonStateInfo info;
    if (state_machine) {
        info.current_state = static_cast<int>(state_machine->getCurrentState());
        const auto& state_data = state_machine->getStateData();
        info.inserted_cents = state_data.inserted_cents;
        info.keypad_buffer = std::string(state_data.keypad_buffer.begin(), state_data.keypad_buffer.end());
        info.last_activity = state_data.last_activity;
    } else {
        info.current_state = 0;
        info.inserted_cents = 0;
        info.keypad_buffer = "";
        info.last_activity = std::chrono::steady_clock::now();
    }
    return info;
}

std::chrono::steady_clock::time_point getDaemonStartTime() {
    return daemon_start_time;
}

// Forward declaration - will be implemented after function declarations
bool sendControlCommand(const std::string& action);
Config& config = Config::getInstance();
MillenniumLogger& logger = MillenniumLogger::getInstance();
HealthMonitor& health_monitor = HealthMonitor::getInstance();
Metrics& metrics = Metrics::getInstance();

// Display management
std::string line1;
std::string line2;

std::string generateDisplayBytes() {
    const size_t DISPLAY_WIDTH = 20;
    // const size_t TOTAL_CHARS = DISPLAY_WIDTH * 2; // Unused variable
    
    std::vector<uint8_t> bytes;
    
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
    bytes.push_back(0x0A);
    
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
    int cost_cents = config.getCallCostCents();
    std::ostringstream message;
    message << "Insert " << std::setfill('0') << std::setw(2)
            << (cost_cents - inserted) << " cents";
    logger.debug("Display", "Generated message: " + message.str());
    return message.str();
}

void handle_coin_event(const std::shared_ptr<CoinEvent> &coin_event) {
    if (!coin_event) {
        logger.error("Coin", "Received null coin event");
        return;
    }
    
    if (!client || !state_machine) {
        logger.error("Coin", "Client or state machine is null, cannot handle coin event");
        return;
    }
    
    const auto &code = coin_event->coin_code();
    int coin_value = 0;
    
    if (code == "COIN_6") {
        coin_value = 5;
    } else if (code == "COIN_7") {
        coin_value = 10;
    } else if (code == "COIN_8") {
        coin_value = 25;
    }
    
    if (coin_value > 0 && state_machine->getCurrentState() == PayPhoneStateMachine::State::IDLE_UP) {
        // Let the state machine handle the coin event
        state_machine->handleEvent(coin_event);
        
        metrics.incrementCounter("coins_inserted");
        metrics.incrementCounter("coins_value_cents", coin_value);
        
        // Display is now updated automatically by the state machine
    }
}

void handle_call_state_event(const std::shared_ptr<CallStateEvent> &call_state_event) {
    if (!call_state_event) {
        logger.error("Call", "Received null call state event");
        return;
    }
    
    if (!client || !state_machine) {
        logger.error("Call", "Client or state machine is null, cannot handle call state event");
        return;
    }
    
    // Let the state machine handle the call state event
    state_machine->handleEvent(call_state_event);
    
    if (call_state_event->get_state() == CALL_INCOMING) {
        logger.info("Call", "Incoming call received");
        metrics.incrementCounter("calls_incoming");
        
        client->writeToCoinValidator('f');
        client->writeToCoinValidator('z');
    } else if (call_state_event->get_state() == CALL_ACTIVE) {
        logger.info("Call", "Call established - audio should be working");
        metrics.incrementCounter("calls_established");
    }
}

void handle_hook_event(const std::shared_ptr<HookStateChangeEvent> &hook_event) {
    if (!hook_event) {
        logger.error("Hook", "Received null hook event");
        return;
    }
    
    if (!client || !state_machine) {
        logger.error("Hook", "Client or state machine is null, cannot handle hook event");
        return;
    }
    
    // Let the state machine handle the hook event (including call actions)
    state_machine->handleEvent(hook_event);
    
    // Handle coin validator commands based on hook direction
    if (hook_event->get_direction() == 'U') {
        if (state_machine->getCurrentState() == PayPhoneStateMachine::State::IDLE_UP) {
            logger.info("Hook", "Hook lifted, transitioning to IDLE_UP");
            metrics.incrementCounter("hook_lifted");
            client->writeToCoinValidator('a');
        }
    } else if (hook_event->get_direction() == 'D') {
        logger.info("Hook", "Hook down");
        metrics.incrementCounter("hook_down");
        
        // Send appropriate coin validator command
        client->writeToCoinValidator(state_machine->getCurrentState() == PayPhoneStateMachine::State::IDLE_UP ? 'f' : 'c');
        client->writeToCoinValidator('z');
    }
}

void handle_keypad_event(const std::shared_ptr<KeypadEvent> &keypad_event) {
    if (!keypad_event) {
        logger.error("Keypad", "Received null keypad event");
        return;
    }
    
    if (!client || !state_machine) {
        logger.error("Keypad", "Client or state machine is null, cannot handle keypad event");
        return;
    }
    
    // Let the state machine handle the keypad event
    state_machine->handleEvent(keypad_event);
    
    const auto& state_data = state_machine->getStateData();
    if (state_data.keypad_buffer.size() < 10 && 
        state_machine->getCurrentState() == PayPhoneStateMachine::State::IDLE_UP) {
        
        char key = keypad_event->get_key();
        if (std::isdigit(key)) {
            logger.debug("Keypad", "Key pressed: " + std::string(1, key));
            metrics.incrementCounter("keypad_presses");
            
            // Display is now updated automatically by the state machine
        }
    }
}

void check_and_call() {
    if (!client || !state_machine) {
        logger.error("Call", "Client or state machine is null, cannot initiate call");
        return;
    }
    
    const auto& state_data = state_machine->getStateData();
    
    if (state_data.keypad_buffer.size() == 10 && 
        state_data.inserted_cents >= 50 && // Use state machine's call cost
        state_machine->getCurrentState() == PayPhoneStateMachine::State::IDLE_UP) {
        
        std::string number = std::string(state_data.keypad_buffer.begin(), 
                                        state_data.keypad_buffer.end());
        
        logger.info("Call", "Dialing number: " + number);
        metrics.incrementCounter("calls_initiated");
        
        // Update display to show calling status
        line1 = "Calling " + number;
        line2 = "Connecting...";
        client->setDisplay(generateDisplayBytes());
        
        try {
            client->call(number);
            state_machine->transitionTo(PayPhoneStateMachine::State::CALL_ACTIVE);
        } catch (const std::exception& e) {
            logger.error("Call", "Failed to initiate call: " + std::string(e.what()));
            metrics.incrementCounter("call_errors");
            
            line1 = "Call failed";
            line2 = "Try again";
            client->setDisplay(generateDisplayBytes());
        }
    }
}

class EventProcessor {
public:
    EventProcessor() {
        setup_dispatcher();
    }
    
    void process_event(const std::shared_ptr<Event> &event) {
        if (!state_machine) {
            logger.error("EventProcessor", "State machine is null, cannot process event");
            return;
        }
        
        try {
            // Let the state machine handle the event
            state_machine->handleEvent(event);
            metrics.incrementCounter("events_processed");
        } catch (const std::exception& e) {
            logger.error("EventProcessor", "Error processing event: " + std::string(e.what()));
            metrics.incrementCounter("event_errors");
        }
        
        // Always check side effects after processing any event
        check_side_effects();
    }
    
private:
    void check_side_effects() {
        // Check if we should initiate a call
        check_and_call();
        
        // Could add other side effects here in the future:
        // - Update display
        // - Send notifications
        // - Log state changes
        // - etc.
    }
    
private:
    std::unordered_map<std::type_index, std::function<void(const std::shared_ptr<Event> &)>> dispatcher;
    
    void setup_dispatcher() {
        dispatcher[typeid(CoinEvent)] = [&](const std::shared_ptr<Event> &e) {
            auto coin_event = std::dynamic_pointer_cast<CoinEvent>(e);
            handle_coin_event(coin_event);
        };
        
        dispatcher[typeid(CallStateEvent)] = [&](const std::shared_ptr<Event> &e) {
            auto call_state_event = std::dynamic_pointer_cast<CallStateEvent>(e);
            handle_call_state_event(call_state_event);
        };
        
        dispatcher[typeid(HookStateChangeEvent)] = [&](const std::shared_ptr<Event> &e) {
            auto hook_event = std::dynamic_pointer_cast<HookStateChangeEvent>(e);
            handle_hook_event(hook_event);
        };
        
        dispatcher[typeid(KeypadEvent)] = [&](const std::shared_ptr<Event> &e) {
            auto keypad_event = std::dynamic_pointer_cast<KeypadEvent>(e);
            handle_keypad_event(keypad_event);
        };
    }
};

// Implementation of sendControlCommand (moved here to access display functions)
bool sendControlCommand(const std::string& action) {
    if (!state_machine) {
        MillenniumLogger::getInstance().error("Control", "State machine is null");
        return false;
    }
    
    if (!event_processor) {
        MillenniumLogger::getInstance().error("Control", "Event processor is null");
        return false;
    }
    
    try {
        MillenniumLogger::getInstance().info("Control", "Received control command: " + action);
        std::cout << "[CONTROL] Received command: " << action << std::endl;
        
        // Parse command and arguments
        size_t colon_pos = action.find(':');
        std::string command = (colon_pos != std::string::npos) ? action.substr(0, colon_pos) : action;
        std::string arg = (colon_pos != std::string::npos) ? action.substr(colon_pos + 1) : "";
        if (command == "start_call") {
            // Simulate starting a call by changing state
            state_machine->transitionTo(PayPhoneStateMachine::State::CALL_INCOMING);
            Metrics::getInstance().setGauge("current_state", static_cast<double>(state_machine->getCurrentState()));
            Metrics::getInstance().incrementCounter("calls_initiated");
            MillenniumLogger::getInstance().info("Control", "Call initiation requested via web portal");
            return true;
            
        } else if (command == "reset_system") {
            // Reset the system state
            state_machine->reset();
            Metrics::getInstance().setGauge("current_state", static_cast<double>(state_machine->getCurrentState()));
            Metrics::getInstance().setGauge("inserted_cents", 0.0);
            Metrics::getInstance().incrementCounter("system_resets");
            MillenniumLogger::getInstance().info("Control", "System reset requested via web portal");
            return true;
            
        } else if (command == "emergency_stop") {
            // Emergency stop - set to invalid state and stop running
            state_machine->transitionTo(PayPhoneStateMachine::State::INVALID);
            Metrics::getInstance().setGauge("current_state", static_cast<double>(state_machine->getCurrentState()));
            Metrics::getInstance().incrementCounter("emergency_stops");
            MillenniumLogger::getInstance().warn("Control", "Emergency stop activated via web portal");
            // Note: We don't actually stop the daemon, just set it to invalid state
            return true;
        } else if (command == "keypad_press") {
            // Extract key from argument and inject as keypad event
            std::string key = arg;
            if (std::isdigit(key[0])) {
                auto keypad_event = std::make_shared<KeypadEvent>(key[0]);
                event_processor->process_event(keypad_event);
                MillenniumLogger::getInstance().info("Control", "Keypad key '" + key + "' pressed via web portal");
                return true;
            } else {
                MillenniumLogger::getInstance().warn("Control", "Invalid keypad key: " + key);
                return false;
            }
        } else if (command == "keypad_clear") {
            // Only allow clear when handset is up (same as physical keypad logic)
            if (state_machine->getCurrentState() == PayPhoneStateMachine::State::IDLE_UP) {
                auto& state_data = state_machine->getStateData();
                state_data.keypad_buffer.clear();
                state_data.updateActivity();
                Metrics::getInstance().incrementCounter("keypad_clears");
                MillenniumLogger::getInstance().info("Control", "Keypad cleared via web portal");
                
                // Display is now updated automatically by the state machine
                
                return true;
            } else {
                MillenniumLogger::getInstance().warn("Control", "Keypad clear ignored - handset down");
                return false;
            }
        } else if (command == "keypad_backspace") {
            // Only allow backspace when handset is up and buffer is not empty
            auto& state_data = state_machine->getStateData();
            if (state_machine->getCurrentState() == PayPhoneStateMachine::State::IDLE_UP && !state_data.keypad_buffer.empty()) {
                state_data.keypad_buffer.pop_back();
                state_data.updateActivity();
                Metrics::getInstance().incrementCounter("keypad_backspaces");
                MillenniumLogger::getInstance().info("Control", "Keypad backspace via web portal");
                
                // Display is now updated automatically by the state machine
                
                return true;
            } else {
                MillenniumLogger::getInstance().warn("Control", "Keypad backspace ignored - handset down or buffer empty");
                return false;
            }
        } else if (command == "coin_insert") {
            // Extract cents from argument and inject as coin event
            std::string cents_str = arg;
            MillenniumLogger::getInstance().info("Control", "Extracted cents string: '" + cents_str + "'");
            std::cout << "[CONTROL] Extracted cents: '" << cents_str << "'" << std::endl;
            
            try {
                int cents = std::stoi(cents_str);
                
                // Map cents to coin codes (same as physical coin reader)
                uint8_t coin_code;
                if (cents == 5) {
                    coin_code = 0x36; // COIN_6
                } else if (cents == 10) {
                    coin_code = 0x37; // COIN_7
                } else if (cents == 25) {
                    coin_code = 0x38; // COIN_8
                } else {
                    MillenniumLogger::getInstance().warn("Control", "Invalid coin value: " + cents_str + "¢");
                    return false;
                }
                
                auto coin_event = std::make_shared<CoinEvent>(coin_code);
                event_processor->process_event(coin_event);
                MillenniumLogger::getInstance().info("Control", "Coin inserted: " + cents_str + "¢ via web portal");
                std::cout << "[CONTROL] Coin inserted successfully: " << cents << "¢" << std::endl;
                
                return true;
            } catch (const std::exception& e) {
                MillenniumLogger::getInstance().error("Control", "Failed to parse cents: " + std::string(e.what()));
                std::cout << "[CONTROL] ERROR: Failed to parse cents: " << e.what() << std::endl;
                return false;
            }
        } else if (command == "coin_return") {
            auto& state_data = state_machine->getStateData();
            state_data.inserted_cents = 0;
            state_data.updateActivity();
            Metrics::getInstance().setGauge("inserted_cents", 0.0);
            Metrics::getInstance().incrementCounter("coin_returns");
            MillenniumLogger::getInstance().info("Control", "Coins returned via web portal");
            
            // Update physical display
            line1 = format_number(state_data.keypad_buffer);
            line2 = generate_message(state_data.inserted_cents);
            client->setDisplay(generateDisplayBytes());
            
            return true;
        } else if (command == "handset_up") {
            // Inject as hook event
            auto hook_event = std::make_shared<HookStateChangeEvent>('U');
            event_processor->process_event(hook_event);
            MillenniumLogger::getInstance().info("Control", "Handset lifted via web portal");
            return true;
        } else if (command == "handset_down") {
            // Inject as hook event
            auto hook_event = std::make_shared<HookStateChangeEvent>('D');
            event_processor->process_event(hook_event);
            MillenniumLogger::getInstance().info("Control", "Handset placed down via web portal");
            return true;
        }
    } catch (const std::exception& e) {
        MillenniumLogger::getInstance().error("Control", "Error executing control command: " + std::string(e.what()));
    }
    
    return false;
}

// Signal handler
void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        logger.info("Daemon", "Received signal " + std::to_string(signal) + ", shutting down gracefully...");
        running = false;
    }
}

// Health check functions
HealthMonitor::Status checkSerialConnection() {
    // This would check if the serial connection is working
    // For now, we'll return healthy
    return HealthMonitor::HEALTHY;
}

HealthMonitor::Status checkSipConnection() {
    // This would check if the SIP connection is active
    // For now, we'll return healthy
    return HealthMonitor::HEALTHY;
}

HealthMonitor::Status checkDaemonActivity() {
    if (!state_machine) {
        return HealthMonitor::CRITICAL;
    }
    
    auto now = std::chrono::steady_clock::now();
    const auto& state_data = state_machine->getStateData();
    auto time_since_activity = std::chrono::duration_cast<std::chrono::minutes>(
        now - state_data.last_activity);
    
    if (time_since_activity.count() > 60) { // No activity for more than 1 hour
        return HealthMonitor::WARNING;
    }
    
    return HealthMonitor::HEALTHY;
}

// Metrics collection thread
void metrics_collection_thread() {
    while (running) {
        // Skip metrics collection during audio activity to save CPU
        if (state_machine && state_machine->isInCall()) {
            // During audio activity, sleep longer and skip metrics
            std::this_thread::sleep_for(std::chrono::seconds(30));
            continue;
        }
        
        // Update system metrics
        metrics.setGauge("daemon_uptime_seconds", 
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        
        if (state_machine) {
            metrics.setGauge("current_state", static_cast<double>(state_machine->getCurrentState()));
            const auto& state_data = state_machine->getStateData();
            metrics.setGauge("inserted_cents", static_cast<double>(state_data.inserted_cents));
            metrics.setGauge("keypad_buffer_size", static_cast<double>(state_data.keypad_buffer.size()));
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }
}

int main(int argc, char *argv[]) {
    try {
        // Record daemon start time for uptime calculation
        daemon_start_time = std::chrono::steady_clock::now();
        
        // Setup signal handlers
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);
        
        // Load configuration
        std::string config_file = "/etc/millennium/daemon.conf";
        if (argc > 2 && std::string(argv[1]) == "--config") {
            config_file = argv[2];
        }
        
        if (!config.loadFromFile(config_file)) {
            logger.warn("Config", "Could not load config file: " + config_file + ", using environment variables");
            config.loadFromEnvironment();
        }
        
        if (!config.validate()) {
            logger.error("Config", "Configuration validation failed");
            return 1;
        }
        
        // Setup logging
        logger.setLevel(MillenniumLogger::parseLevel(config.getLogLevel()));
        if (config.getLogToFile() && !config.getLogFile().empty()) {
	    std::cerr << "Logging to " << config.getLogFile() << std::endl;
            logger.setLogFile(config.getLogFile());
            logger.setLogToFile(true);
        }
        
        logger.info("Daemon", "Starting Millennium Daemon");
        
        // Initialize state machine
        state_machine = std::make_unique<PayPhoneStateMachine>();
        state_machine->setCallCostCents(config.getCallCostCents());
        
        // Set up state transition callback to handle display updates
        state_machine->setStateTransitionCallback([](PayPhoneStateMachine::State /*from*/, 
                                                    PayPhoneStateMachine::State /*to*/, 
                                                    const PayPhoneStateMachine::StateData& data) {
            if (client) {
                line1 = data.display_line1;
                line2 = data.display_line2;
                client->setDisplay(generateDisplayBytes());
            }
        });
        
        // Set up display update callback for real-time updates (keypad, coins, etc.)
        state_machine->setDisplayUpdateCallback([](const PayPhoneStateMachine::StateData& data) {
            if (client) {
                line1 = data.display_line1;
                line2 = data.display_line2;
                logger.debug("Display", "Real-time update - Line1: '" + line1 + "', Line2: '" + line2 + "'");
                client->setDisplay(generateDisplayBytes());
            }
        });
        
        // Set up call action callbacks
        state_machine->setAnswerCallCallback([]() {
            if (client) {
                try {
                    client->answerCall();
                    logger.info("Call", "Call answered via state machine");
                    metrics.incrementCounter("calls_answered");
                } catch (const std::exception& e) {
                    logger.error("Call", "Failed to answer call: " + std::string(e.what()));
                    metrics.incrementCounter("call_answer_errors");
                }
            }
        });
        
        state_machine->setHangupCallCallback([]() {
            if (client) {
                try {
                    client->hangup();
                    logger.info("Call", "Call hung up via state machine");
                    metrics.incrementCounter("calls_ended");
                } catch (const std::exception& e) {
                    logger.error("Call", "Failed to hangup call: " + std::string(e.what()));
                    metrics.incrementCounter("call_hangup_errors");
                }
            }
        });
        
        // Initialize client
        client = std::make_unique<MillenniumClient>();
        
        // Initialize health monitoring
        health_monitor.registerCheck("serial_connection", checkSerialConnection, std::chrono::seconds(30));
        health_monitor.registerCheck("sip_connection", checkSipConnection, std::chrono::seconds(60));
        health_monitor.registerCheck("daemon_activity", checkDaemonActivity, std::chrono::seconds(120));
        health_monitor.startMonitoring();
        
        // Start metrics server if enabled
        if (config.getMetricsServerEnabled()) {
            metrics_server = std::make_unique<MetricsServer>(config.getMetricsServerPort());
            metrics_server->start();
            logger.info("Daemon", "Metrics server started on port " + 
                       std::to_string(config.getMetricsServerPort()));
        } else {
            logger.info("Daemon", "Metrics server disabled");
        }
        
        // Start web server if enabled
        if (config.getWebServerEnabled()) {
            web_server = std::make_unique<WebServer>(config.getWebServerPort());
            
            // Add the web portal as a static route
            std::ifstream portal_file("web_portal.html");
            if (portal_file.is_open()) {
                std::string portal_content((std::istreambuf_iterator<char>(portal_file)),
                                         std::istreambuf_iterator<char>());
                web_server->addStaticRoute("/", portal_content, "text/html");
                portal_file.close();
            } else {
                logger.warn("WebServer", "Could not load web_portal.html, serving basic interface");
                web_server->addStaticRoute("/", "<h1>Millennium System Portal</h1><p>Web portal not available</p>", "text/html");
            }
            
            web_server->start();
            logger.info("Daemon", "Web server started on port " + 
                       std::to_string(config.getWebServerPort()));
        } else {
            logger.info("Daemon", "Web server disabled");
        }
        
        // Start metrics collection thread (disabled for single-core optimization)
        // std::thread metrics_thread(metrics_collection_thread);
        
        // Initialize display with state machine data
        const auto& state_data = state_machine->getStateData();
        line1 = state_data.display_line1;
        line2 = state_data.display_line2;
        client->setDisplay(generateDisplayBytes());
        
        // Initialize event processor
        event_processor = std::make_unique<EventProcessor>();
        
        logger.info("Daemon", "Daemon initialized successfully");
        
        // Main event loop with proper sleep intervals
        auto last_metrics_update = std::chrono::steady_clock::now();
        auto last_debug_log = std::chrono::steady_clock::now();
        
        while (running) {
            try {
                // Update client and process events
                client->update();
                
                auto event = client->nextEvent();
                if (event) {
                    event_processor->process_event(event);
                    // check_and_call() is now called automatically by EventProcessor
                }
                
                // Update metrics every second
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::seconds>(now - last_metrics_update).count() >= 1) {
                    metrics.setGauge("daemon_uptime_seconds", 
                        std::chrono::duration_cast<std::chrono::seconds>(
                            now - daemon_start_time).count());
                    
                    if (state_machine) {
                        metrics.setGauge("current_state", static_cast<double>(state_machine->getCurrentState()));
                        const auto& state_data = state_machine->getStateData();
                        metrics.setGauge("inserted_cents", static_cast<double>(state_data.inserted_cents));
                        metrics.setGauge("keypad_buffer_size", static_cast<double>(state_data.keypad_buffer.size()));
                    }
                    last_metrics_update = now;
                }
                
                // Log metrics summary every 10 seconds at DEBUG level
                if (std::chrono::duration_cast<std::chrono::seconds>(now - last_debug_log).count() >= 10) {
                    logger.debug("Metrics", "=== Metrics Summary ===");
                    
                    // Log only significant counters (non-zero)
                    auto counters = metrics.getAllCounters();
                    int active_counters = 0;
                    for (const auto& pair : counters) {
                        if (pair.second > 0) {
                            active_counters++;
                        }
                    }
                    
                    if (active_counters > 0) {
                        logger.debug("Metrics", "Active counters: " + std::to_string(active_counters));
                    }
                    
                    // Log current state gauge
                    auto gauges = metrics.getAllGauges();
                    auto state_it = gauges.find("current_state");
                    if (state_it != gauges.end()) {
                        logger.debug("Metrics", "Current state: " + std::to_string(state_it->second));
                    }
                    last_debug_log = now;
                }
                
                // Sleep to prevent busy waiting - adjust based on system state
                if (state_machine && state_machine->isInCall()) {
                    // During active calls, sleep longer to reduce CPU usage
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                } else {
                    // Normal operation - shorter sleep for responsiveness
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                
            } catch (const std::exception& e) {
                logger.error("Daemon", "Error in main loop: " + std::string(e.what()));
                metrics.incrementCounter("main_loop_errors");
                
                // Wait longer on errors to prevent rapid error loops
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
        }
        
        // Cleanup
        logger.info("Daemon", "Shutting down daemon");
        
        // Stop metrics server
        if (metrics_server) {
            metrics_server->stop();
        }
        
        // Stop metrics thread (disabled for single-core optimization)
        // metrics_thread.join();
        
        // Stop health monitoring
        health_monitor.stopMonitoring();
        
        // Cleanup client
        client.reset();
        
        logger.info("Daemon", "Daemon shutdown complete");
        
    } catch (const std::exception& e) {
        logger.error("Daemon", "Fatal error: " + std::string(e.what()));
        return 1;
    }
    
    return 0;
}
