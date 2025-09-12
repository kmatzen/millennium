#include "config.h"
#include "logger.h"
#include "health_monitor.h"
#include "metrics.h"
#include "millennium_sdk.h"
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

#define EVENT_CATEGORIES 3

// Global state management
class DaemonState {
public:
    enum State {
        INVALID = 0,
        IDLE_DOWN,
        IDLE_UP,
        CALL_INCOMING,
        CALL_ACTIVE,
    };
    
    State current_state = IDLE_DOWN;
    std::vector<char> keypad_buffer;
    int inserted_cents = 0;
    std::chrono::steady_clock::time_point last_activity;
    
    void reset() {
        current_state = IDLE_DOWN;
        keypad_buffer.clear();
        inserted_cents = 0;
        last_activity = std::chrono::steady_clock::now();
    }
    
    void updateActivity() {
        last_activity = std::chrono::steady_clock::now();
    }
};

// Global instances
std::atomic<bool> running(true);
std::unique_ptr<DaemonState> daemon_state;
std::unique_ptr<MillenniumClient> client;
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
    const auto &code = coin_event->coin_code();
    int coin_value = 0;
    
    if (code == "COIN_6") {
        coin_value = 5;
    } else if (code == "COIN_7") {
        coin_value = 10;
    } else if (code == "COIN_8") {
        coin_value = 25;
    }
    
    if (coin_value > 0 && daemon_state->current_state == DaemonState::IDLE_UP) {
        daemon_state->inserted_cents += coin_value;
        daemon_state->updateActivity();
        
        metrics.incrementCounter("coins_inserted");
        metrics.incrementCounter("coins_value_cents", coin_value);
        
        logger.info("Coin", "Coin inserted: " + code + ", value: " + std::to_string(coin_value) + 
                   " cents, total: " + std::to_string(daemon_state->inserted_cents) + " cents");
        
        line1 = format_number(daemon_state->keypad_buffer);
        line2 = generate_message(daemon_state->inserted_cents);
        client->setDisplay(generateDisplayBytes());
    }
}

void handle_call_state_event(const std::shared_ptr<CallStateEvent> &call_state_event) {
    if (call_state_event->get_state() == CALL_INCOMING && 
        daemon_state->current_state == DaemonState::IDLE_DOWN) {
        
        logger.info("Call", "Incoming call received");
        metrics.incrementCounter("calls_incoming");
        
        line1 = "Call incoming...";
        client->setDisplay(generateDisplayBytes());
        
        daemon_state->current_state = DaemonState::CALL_INCOMING;
        daemon_state->updateActivity();
        
        client->writeToCoinValidator('f');
        client->writeToCoinValidator('z');
    }
}

void handle_hook_event(const std::shared_ptr<HookStateChangeEvent> &hook_event) {
    if (hook_event->get_direction() == 'U') {
        if (daemon_state->current_state == DaemonState::CALL_INCOMING) {
            logger.info("Call", "Call answered");
            metrics.incrementCounter("calls_answered");
            
            daemon_state->current_state = DaemonState::CALL_ACTIVE;
            daemon_state->updateActivity();
            
            client->answerCall();
        } else if (daemon_state->current_state == DaemonState::IDLE_DOWN) {
            logger.info("Hook", "Hook lifted, transitioning to IDLE_UP");
            metrics.incrementCounter("hook_lifted");
            
            daemon_state->current_state = DaemonState::IDLE_UP;
            daemon_state->updateActivity();
            
            client->writeToCoinValidator('a');
            daemon_state->inserted_cents = 0;
            daemon_state->keypad_buffer.clear();
            
            line2 = generate_message(daemon_state->inserted_cents);
            line1 = format_number(daemon_state->keypad_buffer);
            client->setDisplay(generateDisplayBytes());
        }
    } else if (hook_event->get_direction() == 'D') {
        logger.info("Hook", "Hook down, call ended");
        metrics.incrementCounter("hook_down");
        
        if (daemon_state->current_state == DaemonState::CALL_ACTIVE) {
            metrics.incrementCounter("calls_ended");
        }
        
        client->hangup();
        daemon_state->keypad_buffer.clear();
        daemon_state->inserted_cents = 0;
        
        line2 = generate_message(daemon_state->inserted_cents);
        line1 = format_number(daemon_state->keypad_buffer);
        client->setDisplay(generateDisplayBytes());
        
        client->writeToCoinValidator(daemon_state->current_state == DaemonState::IDLE_UP ? 'f' : 'c');
        client->writeToCoinValidator('z');
        daemon_state->current_state = DaemonState::IDLE_DOWN;
        daemon_state->updateActivity();
    }
}

void handle_keypad_event(const std::shared_ptr<KeypadEvent> &keypad_event) {
    if (daemon_state->keypad_buffer.size() < 10 && 
        daemon_state->current_state == DaemonState::IDLE_UP) {
        
        char key = keypad_event->get_key();
        if (std::isdigit(key)) {
            logger.debug("Keypad", "Key pressed: " + std::string(1, key));
            metrics.incrementCounter("keypad_presses");
            
            daemon_state->keypad_buffer.push_back(key);
            daemon_state->updateActivity();
            
            line2 = generate_message(daemon_state->inserted_cents);
            line1 = format_number(daemon_state->keypad_buffer);
            client->setDisplay(generateDisplayBytes());
        }
    }
}

void check_and_call() {
    int cost_cents = config.getCallCostCents();
    
    if (daemon_state->keypad_buffer.size() == 10 && 
        daemon_state->inserted_cents >= cost_cents &&
        daemon_state->current_state == DaemonState::IDLE_UP) {
        
        std::string number = std::string(daemon_state->keypad_buffer.begin(), 
                                        daemon_state->keypad_buffer.end());
        
        logger.info("Call", "Dialing number: " + number);
        metrics.incrementCounter("calls_initiated");
        
        line2 = "Calling";
        client->setDisplay(generateDisplayBytes());
        
        try {
            client->call(number);
            daemon_state->current_state = DaemonState::CALL_ACTIVE;
            daemon_state->updateActivity();
        } catch (const std::exception& e) {
            logger.error("Call", "Failed to initiate call: " + std::string(e.what()));
            metrics.incrementCounter("call_errors");
            
            line2 = "Call failed";
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
        auto it = dispatcher.find(typeid(*event));
        if (it != dispatcher.end()) {
            try {
                it->second(event);
                metrics.incrementCounter("events_processed");
            } catch (const std::exception& e) {
                logger.error("EventProcessor", "Error processing event: " + std::string(e.what()));
                metrics.incrementCounter("event_errors");
            }
        } else {
            logger.warn("EventProcessor", "Unhandled event type: " + event->name());
            metrics.incrementCounter("unhandled_events");
        }
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
    if (!daemon_state) {
        return HealthMonitor::CRITICAL;
    }
    
    auto now = std::chrono::steady_clock::now();
    auto time_since_activity = std::chrono::duration_cast<std::chrono::minutes>(
        now - daemon_state->last_activity);
    
    if (time_since_activity.count() > 60) { // No activity for more than 1 hour
        return HealthMonitor::WARNING;
    }
    
    return HealthMonitor::HEALTHY;
}

// Metrics collection thread
void metrics_collection_thread() {
    while (running) {
        // Update system metrics
        metrics.setGauge("daemon_uptime_seconds", 
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        
        metrics.setGauge("current_state", static_cast<double>(daemon_state->current_state));
        metrics.setGauge("inserted_cents", static_cast<double>(daemon_state->inserted_cents));
        metrics.setGauge("keypad_buffer_size", static_cast<double>(daemon_state->keypad_buffer.size()));
        
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }
}

int main(int argc, char *argv[]) {
    try {
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
            logger.setLogFile(config.getLogFile());
            logger.setLogToFile(true);
        }
        
        logger.info("Daemon", "Starting Millennium Daemon");
        
        // Initialize daemon state
        daemon_state = std::make_unique<DaemonState>();
        daemon_state->reset();
        
        // Initialize client
        client = std::make_unique<MillenniumClient>();
        
        // Setup SIP if configured
        if (!config.getSipUsername().empty()) {
            client->setupSIP(config.getSipUsername(), 
                           config.getSipPassword(), 
                           config.getSipDomain());
        }
        
        // Initialize health monitoring
        health_monitor.registerCheck("serial_connection", checkSerialConnection, std::chrono::seconds(30));
        health_monitor.registerCheck("sip_connection", checkSipConnection, std::chrono::seconds(60));
        health_monitor.registerCheck("daemon_activity", checkDaemonActivity, std::chrono::seconds(120));
        health_monitor.startMonitoring();
        
        // Start metrics collection thread
        std::thread metrics_thread(metrics_collection_thread);
        
        // Initialize display
        line1 = format_number(daemon_state->keypad_buffer);
        line2 = generate_message(daemon_state->inserted_cents);
        client->setDisplay(generateDisplayBytes());
        
        // Initialize event processor
        EventProcessor processor;
        
        logger.info("Daemon", "Daemon initialized successfully");
        
        // Main event loop
        int loop_count = 0;
        while (running) {
            try {
                client->update();
                auto event = client->nextEvent();
                if (event) {
                    processor.process_event(event);
                    check_and_call();
                }
                
                // Log metrics every 1000 loops (about every 1 second)
                if (++loop_count % 1000 == 0) {
                    logger.info("Metrics", "=== Current Statistics ===");
                    
                    // Log counters
                    auto counters = metrics.getAllCounters();
                    for (const auto& pair : counters) {
                        if (pair.second > 0) { // Only log non-zero counters
                            logger.info("Metrics", "Counter " + pair.first + ": " + std::to_string(pair.second));
                        }
                    }
                    
                    // Log gauges
                    auto gauges = metrics.getAllGauges();
                    for (const auto& pair : gauges) {
                        logger.info("Metrics", "Gauge " + pair.first + ": " + std::to_string(pair.second));
                    }
                }
                
                // Small delay to prevent busy waiting
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                
            } catch (const std::exception& e) {
                logger.error("Daemon", "Error in main loop: " + std::string(e.what()));
                metrics.incrementCounter("main_loop_errors");
                
                // Wait a bit before continuing to prevent rapid error loops
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
        
        // Cleanup
        logger.info("Daemon", "Shutting down daemon");
        
        // Stop metrics thread
        metrics_thread.join();
        
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
