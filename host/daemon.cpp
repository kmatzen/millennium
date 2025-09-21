extern "C" {
#include "config.h"
#include "daemon_state.h"
#include "logger.h"
#include "metrics.h"
#include "metrics_server.h"
}
extern "C" {
#include "health_monitor.h"
}
#include "web_server.h"
extern "C" {
#include "millennium_sdk.h"
#include "events.h"
#include "event_processor.h"
}
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

// Global state management - now using C struct
// DaemonState class replaced with daemon_state_data_t from daemon_state.h

// Forward declarations

// Global instances
std::atomic<bool> running(true);
daemon_state_data_t* daemon_state = nullptr;  // C pointer instead of unique_ptr
millennium_client_t* client = nullptr;  // C pointer instead of unique_ptr
metrics_server_t *metrics_server = nullptr;
std::unique_ptr<WebServer> web_server;
event_processor_t *event_processor = nullptr;

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
    if (daemon_state) {
        info.current_state = static_cast<int>(daemon_state->current_state);
        info.inserted_cents = daemon_state->inserted_cents;
        info.keypad_buffer = std::string(daemon_state->keypad_buffer);
        // Convert time_t to chrono time_point
        auto duration = std::chrono::seconds(daemon_state->last_activity);
        info.last_activity = std::chrono::steady_clock::time_point(duration);
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
config_data_t* config = config_get_instance();
health_monitor_t* health_monitor = health_monitor_get_instance();
// Metrics instance is now global g_metrics

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

std::string format_number(const char* buffer) {
    char filled[11] = "__________";  // 10 underscores + null terminator
    int len = strlen(buffer);
    for (int i = 0; i < len && i < 10; ++i) {
        filled[i] = buffer[i];
    }
    
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
    int cost_cents = config_get_call_cost_cents(config);
    std::ostringstream message;
    message << "Insert " << std::setfill('0') << std::setw(2)
            << (cost_cents - inserted) << " cents";
    logger_debug_with_category("Display", ("Generated message: " + message.str()).c_str());
    return message.str();
}

void check_and_call() {
    if (!client) {
        logger_error_with_category("Call", "Client is null, cannot initiate call");
        return;
    }
    
    int cost_cents = config_get_call_cost_cents(config);
    
    if (daemon_state_get_keypad_length(daemon_state) == 10 && 
        daemon_state->inserted_cents >= cost_cents &&
        daemon_state->current_state == DAEMON_STATE_IDLE_UP) {
        
        std::string number = std::string(daemon_state->keypad_buffer);
        
        logger_info_with_category("Call", ("Dialing number: " + number).c_str());
        metrics_increment_counter("calls_initiated", 1);
        
        line2 = "Calling";
        millennium_client_set_display(client, generateDisplayBytes().c_str());
        
        try {
            millennium_client_call(client, number.c_str());
            daemon_state->current_state = DAEMON_STATE_CALL_ACTIVE;
            daemon_state_update_activity(daemon_state);
        } catch (const std::exception& e) {
            logger_error_with_category("Call", ("Failed to initiate call: " + std::string(e.what())).c_str());
            metrics_increment_counter("call_errors", 1);
            
            line2 = "Call failed";
            millennium_client_set_display(client, generateDisplayBytes().c_str());
        }
    }
}

void handle_coin_event(coin_event_t *coin_event) {
    if (!coin_event) {
        logger_error_with_category("Coin", "Received null coin event");
        return;
    }
    
    if (!client) {
        logger_error_with_category("Coin", "Client is null, cannot handle coin event");
        return;
    }
    
    char *coin_code_str = coin_event_get_coin_code(coin_event);
    if (!coin_code_str) {
        logger_error_with_category("Coin", "Failed to get coin code");
        return;
    }
    
    int coin_value = 0;
    
    if (strcmp(coin_code_str, "COIN_6") == 0) {
        coin_value = 5;
    } else if (strcmp(coin_code_str, "COIN_7") == 0) {
        coin_value = 10;
    } else if (strcmp(coin_code_str, "COIN_8") == 0) {
        coin_value = 25;
    }
    
    if (coin_value > 0 && daemon_state->current_state == DAEMON_STATE_IDLE_UP) {
        daemon_state->inserted_cents += coin_value;
        daemon_state_update_activity(daemon_state);
        
        metrics_increment_counter("coins_inserted", 1);
        metrics_increment_counter("coins_value_cents", coin_value);
        
        std::string log_msg = "Coin inserted: " + std::string(coin_code_str) + ", value: " + std::to_string(coin_value) + 
                   " cents, total: " + std::to_string(daemon_state->inserted_cents) + " cents";
        logger_info_with_category("Coin", log_msg.c_str());
        
        line1 = format_number(daemon_state->keypad_buffer);
        line2 = generate_message(daemon_state->inserted_cents);
        millennium_client_set_display(client, generateDisplayBytes().c_str());
        
        // Check if we should initiate a call after coin insertion
        check_and_call();
    }
    
    free(coin_code_str);
}

void handle_call_state_event(call_state_event_t *call_state_event) {
    if (!call_state_event) {
        logger_error_with_category("Call", "Received null call state event");
        return;
    }
    
    if (!client) {
        logger_error_with_category("Call", "Client is null, cannot handle call state event");
        return;
    }
    
    if (call_state_event_get_state(call_state_event) == EVENT_CALL_STATE_INCOMING && 
        daemon_state->current_state == DAEMON_STATE_IDLE_DOWN) {
        
        logger_info_with_category("Call", "Incoming call received");
        metrics_increment_counter("calls_incoming", 1);
        
        line1 = "Call incoming...";
        millennium_client_set_display(client, generateDisplayBytes().c_str());
        
        daemon_state->current_state = DAEMON_STATE_CALL_INCOMING;
        daemon_state_update_activity(daemon_state);
        
        millennium_client_write_to_coin_validator(client, 'f');
        millennium_client_write_to_coin_validator(client, 'z');
    } else if (call_state_event_get_state(call_state_event) == EVENT_CALL_STATE_ACTIVE) {
        // Handle call established (when baresip reports CALL_ESTABLISHED)
        logger_info_with_category("Call", "Call established - audio should be working");
        metrics_increment_counter("calls_established", 1);
        
        line1 = "Call active";
        line2 = "Audio connected";
        millennium_client_set_display(client, generateDisplayBytes().c_str());
        
        daemon_state->current_state = DAEMON_STATE_CALL_ACTIVE;
        daemon_state_update_activity(daemon_state);
    }
}

void handle_hook_event(hook_state_change_event_t *hook_event) {
    if (!hook_event) {
        logger_error_with_category("Hook", "Received null hook event");
        return;
    }
    
    if (!client) {
        logger_error_with_category("Hook", "Client is null, cannot handle hook event");
        return;
    }
    
    if (hook_state_change_event_get_direction(hook_event) == 'U') {
        if (daemon_state->current_state == DAEMON_STATE_CALL_INCOMING) {
            logger_info_with_category("Call", "Call answered");
            metrics_increment_counter("calls_answered", 1);
            
            daemon_state->current_state = DAEMON_STATE_CALL_ACTIVE;
            daemon_state_update_activity(daemon_state);
            
            try {
                millennium_client_answer_call(client);
            } catch (const std::exception& e) {
                logger_error_with_category("Call", ("Failed to answer call: " + std::string(e.what())).c_str());
                metrics_increment_counter("call_answer_errors", 1);
            }
        } else if (daemon_state->current_state == DAEMON_STATE_IDLE_DOWN) {
            logger_info_with_category("Hook", "Hook lifted, transitioning to IDLE_UP");
            metrics_increment_counter("hook_lifted", 1);
            
            daemon_state->current_state = DAEMON_STATE_IDLE_UP;
            daemon_state_update_activity(daemon_state);
            
            millennium_client_write_to_coin_validator(client, 'a');
            daemon_state->inserted_cents = 0;
            daemon_state_clear_keypad(daemon_state);
            
            line2 = generate_message(daemon_state->inserted_cents);
            line1 = format_number(daemon_state->keypad_buffer);
            millennium_client_set_display(client, generateDisplayBytes().c_str());
        }
    } else if (hook_state_change_event_get_direction(hook_event) == 'D') {
        logger_info_with_category("Hook", "Hook down, call ended");
        metrics_increment_counter("hook_down", 1);
        
        if (daemon_state->current_state == DAEMON_STATE_CALL_ACTIVE) {
            metrics_increment_counter("calls_ended", 1);
        }
        
        try {
            millennium_client_hangup(client);
        } catch (const std::exception& e) {
            logger_error_with_category("Call", ("Failed to hangup call: " + std::string(e.what())).c_str());
            metrics_increment_counter("call_hangup_errors", 1);
        }
        
        daemon_state_clear_keypad(daemon_state);
        daemon_state->inserted_cents = 0;
        
        line2 = generate_message(daemon_state->inserted_cents);
        line1 = format_number(daemon_state->keypad_buffer);
        millennium_client_set_display(client, generateDisplayBytes().c_str());
        
        millennium_client_write_to_coin_validator(client, daemon_state->current_state == DAEMON_STATE_IDLE_UP ? 'f' : 'c');
        millennium_client_write_to_coin_validator(client, 'z');
        daemon_state->current_state = DAEMON_STATE_IDLE_DOWN;
        daemon_state_update_activity(daemon_state);
    }
}

void handle_keypad_event(keypad_event_t *keypad_event) {
    if (!keypad_event) {
        logger_error_with_category("Keypad", "Received null keypad event");
        return;
    }
    
    if (!client) {
        logger_error_with_category("Keypad", "Client is null, cannot handle keypad event");
        return;
    }
    
    if (daemon_state_get_keypad_length(daemon_state) < 10 && 
        daemon_state->current_state == DAEMON_STATE_IDLE_UP) {
        
        char key = keypad_event_get_key(keypad_event);
        if (std::isdigit(key)) {
            logger_debug_with_category("Keypad", ("Key pressed: " + std::string(1, key)).c_str());
            metrics_increment_counter("keypad_presses", 1);
            
            daemon_state_add_key(daemon_state, key);
            daemon_state_update_activity(daemon_state);
            
            line2 = generate_message(daemon_state->inserted_cents);
            line1 = format_number(daemon_state->keypad_buffer);
            millennium_client_set_display(client, generateDisplayBytes().c_str());
            
            // Check if we should initiate a call after keypad input
            check_and_call();
        }
    }
}




// Implementation of sendControlCommand (moved here to access display functions)
bool sendControlCommand(const std::string& action) {
    if (!daemon_state) {
        logger_error_with_category("Control", "Daemon state is null");
        return false;
    }
    
    if (!event_processor) {
        logger_error_with_category("Control", "Event processor is null");
        return false;
    }
    
    try {
        logger_info_with_category("Control", ("Received control command: " + action).c_str());
        std::cout << "[CONTROL] Received command: " << action << std::endl;
        
        // Parse command and arguments
        size_t colon_pos = action.find(':');
        std::string command = (colon_pos != std::string::npos) ? action.substr(0, colon_pos) : action;
        std::string arg = (colon_pos != std::string::npos) ? action.substr(colon_pos + 1) : "";
        if (command == "start_call") {
            // Simulate starting a call by changing state
            daemon_state->current_state = DAEMON_STATE_CALL_INCOMING;
            daemon_state_update_activity(daemon_state);
            metrics_set_gauge("current_state", static_cast<double>(daemon_state->current_state));
            metrics_increment_counter("calls_initiated", 1);
            logger_info_with_category("Control", "Call initiation requested via web portal");
            return true;
            
        } else if (command == "reset_system") {
            // Reset the system state
            daemon_state_reset(daemon_state);
            metrics_set_gauge("current_state", static_cast<double>(daemon_state->current_state));
            metrics_set_gauge("inserted_cents", 0.0);
            metrics_increment_counter("system_resets", 1);
            logger_info_with_category("Control", "System reset requested via web portal");
            return true;
            
        } else if (command == "emergency_stop") {
            // Emergency stop - set to invalid state and stop running
            daemon_state->current_state = DAEMON_STATE_INVALID;
            daemon_state_update_activity(daemon_state);
            metrics_set_gauge("current_state", static_cast<double>(daemon_state->current_state));
            metrics_increment_counter("emergency_stops", 1);
            logger_warn_with_category("Control", "Emergency stop activated via web portal");
            // Note: We don't actually stop the daemon, just set it to invalid state
            return true;
        } else if (command == "keypad_press") {
            // Extract key from argument and inject as keypad event
            std::string key = arg;
            if (std::isdigit(key[0])) {
                keypad_event_t *keypad_event = keypad_event_create(key[0]);
                if (keypad_event) {
                    event_processor_process_event(event_processor, (event_t *)keypad_event);
                    event_destroy((event_t *)keypad_event);
                }
                logger_info_with_category("Control", ("Keypad key '" + key + "' pressed via web portal").c_str());
                return true;
            } else {
                logger_warn_with_category("Control", ("Invalid keypad key: " + key).c_str());
                return false;
            }
        } else if (command == "keypad_clear") {
            // Only allow clear when handset is up (same as physical keypad logic)
            if (daemon_state->current_state == DAEMON_STATE_IDLE_UP) {
                daemon_state_clear_keypad(daemon_state);
                daemon_state_update_activity(daemon_state);
                metrics_increment_counter("keypad_clears", 1);
                logger_info_with_category("Control", "Keypad cleared via web portal");
                
                // Update physical display
                line1 = format_number(daemon_state->keypad_buffer);
                line2 = generate_message(daemon_state->inserted_cents);
                millennium_client_set_display(client, generateDisplayBytes().c_str());
                
                return true;
            } else {
                logger_warn_with_category("Control", "Keypad clear ignored - handset down");
                return false;
            }
        } else if (command == "keypad_backspace") {
            // Only allow backspace when handset is up and buffer is not empty
            if (daemon_state->current_state == DAEMON_STATE_IDLE_UP && daemon_state_get_keypad_length(daemon_state) > 0) {
                daemon_state_remove_last_key(daemon_state);
                daemon_state_update_activity(daemon_state);
                metrics_increment_counter("keypad_backspaces", 1);
                logger_info_with_category("Control", "Keypad backspace via web portal");
                
                // Update physical display
                line1 = format_number(daemon_state->keypad_buffer);
                line2 = generate_message(daemon_state->inserted_cents);
                millennium_client_set_display(client, generateDisplayBytes().c_str());
                
                return true;
            } else {
                logger_warn_with_category("Control", "Keypad backspace ignored - handset down or buffer empty");
                return false;
            }
        } else if (command == "coin_insert") {
            // Extract cents from argument and inject as coin event
            std::string cents_str = arg;
            logger_info_with_category("Control", ("Extracted cents string: '" + cents_str + "'").c_str());
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
                    logger_warn_with_category("Control", ("Invalid coin value: " + cents_str + "¢").c_str());
                    return false;
                }
                
                coin_event_t *coin_event = coin_event_create(coin_code);
                if (coin_event) {
                    event_processor_process_event(event_processor, (event_t *)coin_event);
                    event_destroy((event_t *)coin_event);
                }
                logger_info_with_category("Control", ("Coin inserted: " + cents_str + "¢ via web portal").c_str());
                std::cout << "[CONTROL] Coin inserted successfully: " << cents << "¢" << std::endl;
                
                return true;
            } catch (const std::exception& e) {
                logger_error_with_category("Control", ("Failed to parse cents: " + std::string(e.what())).c_str());
                std::cout << "[CONTROL] ERROR: Failed to parse cents: " << e.what() << std::endl;
                return false;
            }
        } else if (command == "coin_return") {
            daemon_state->inserted_cents = 0;
            daemon_state_update_activity(daemon_state);
            metrics_set_gauge("inserted_cents", 0.0);
            metrics_increment_counter("coin_returns", 1);
            logger_info_with_category("Control", "Coins returned via web portal");
            
            // Update physical display
            line1 = format_number(daemon_state->keypad_buffer);
            line2 = generate_message(daemon_state->inserted_cents);
            millennium_client_set_display(client, generateDisplayBytes().c_str());
            
            return true;
        } else if (command == "handset_up") {
            // Inject as hook event
            hook_state_change_event_t *hook_event = hook_state_change_event_create('U');
            if (hook_event) {
                event_processor_process_event(event_processor, (event_t *)hook_event);
                event_destroy((event_t *)hook_event);
            }
            logger_info_with_category("Control", "Handset lifted via web portal");
            return true;
        } else if (command == "handset_down") {
            // Inject as hook event
            hook_state_change_event_t *hook_event = hook_state_change_event_create('D');
            if (hook_event) {
                event_processor_process_event(event_processor, (event_t *)hook_event);
                event_destroy((event_t *)hook_event);
            }
            logger_info_with_category("Control", "Handset placed down via web portal");
            return true;
        }
    } catch (const std::exception& e) {
        logger_error_with_category("Control", ("Error executing control command: " + std::string(e.what())).c_str());
    }
    
    return false;
}

// Signal handler
void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        logger_info_with_category("Daemon", ("Received signal " + std::to_string(signal) + ", shutting down gracefully...").c_str());
        running = false;
    }
}

// Health check functions
health_status_t checkSerialConnection() {
    // This would check if the serial connection is working
    // For now, we'll return healthy
    return HEALTH_STATUS_HEALTHY;
}

health_status_t checkSipConnection() {
    // This would check if the SIP connection is active
    // For now, we'll return healthy
    return HEALTH_STATUS_HEALTHY;
}

health_status_t checkDaemonActivity() {
    if (!daemon_state) {
        return HEALTH_STATUS_CRITICAL;
    }
    
    time_t now = time(NULL);
    time_t time_since_activity = now - daemon_state->last_activity;
    
    if (time_since_activity > 3600) { // No activity for more than 1 hour
        return HEALTH_STATUS_WARNING;
    }
    
    return HEALTH_STATUS_HEALTHY;
}

// Metrics collection thread
void metrics_collection_thread() {
    while (running) {
        // Skip metrics collection during audio activity to save CPU
        if (daemon_state && daemon_state->current_state >= 2) {
            // During audio activity, sleep longer and skip metrics
            std::this_thread::sleep_for(std::chrono::seconds(30));
            continue;
        }
        
        // Update system metrics
        metrics_set_gauge("daemon_uptime_seconds", 
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        
        metrics_set_gauge("current_state", static_cast<double>(daemon_state->current_state));
        metrics_set_gauge("inserted_cents", static_cast<double>(daemon_state->inserted_cents));
        metrics_set_gauge("keypad_buffer_size", static_cast<double>(daemon_state_get_keypad_length(daemon_state)));
        
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
        
        if (!config_load_from_file(config, config_file.c_str())) {
            logger_warn_with_category("Config", ("Could not load config file: " + config_file + ", using environment variables").c_str());
            config_load_from_environment(config);
        }
        
        if (!config_validate(config)) {
            logger_error_with_category("Config", "Configuration validation failed");
            return 1;
        }
        
        // Setup logging
        logger_set_level(logger_parse_level(config_get_log_level(config)));
        if (config_get_log_to_file(config) && strlen(config_get_log_file(config)) > 0) {
	    std::cerr << "Logging to " << config_get_log_file(config) << std::endl;
            logger_set_log_file(config_get_log_file(config));
            logger_set_log_to_file(1);
        }
        
        logger_info_with_category("Daemon", "Starting Millennium Daemon");
        
        // Initialize daemon state
        daemon_state = (daemon_state_data_t*)malloc(sizeof(daemon_state_data_t));
        if (!daemon_state) {
            logger_error_with_category("Daemon", "Failed to allocate daemon state memory");
            return 1;
        }
        daemon_state_init(daemon_state);
        
        // Initialize metrics
        if (metrics_init() != 0) {
            logger_error_with_category("Daemon", "Failed to initialize metrics");
            return 1;
        }
        
        // Initialize client
        client = millennium_client_create();
        
        // Initialize health monitoring
        health_monitor_register_check("serial_connection", checkSerialConnection, 30);
        health_monitor_register_check("sip_connection", checkSipConnection, 60);
        health_monitor_register_check("daemon_activity", checkDaemonActivity, 120);
        health_monitor_start_monitoring();
        
        // Start metrics server if enabled
        if (config_get_metrics_server_enabled(config)) {
            metrics_server = metrics_server_create(config_get_metrics_server_port(config));
            if (metrics_server) {
                metrics_server_start(metrics_server);
            }
            logger_info_with_category("Daemon", ("Metrics server started on port " + 
                       std::to_string(config_get_metrics_server_port(config))).c_str());
        } else {
            logger_info_with_category("Daemon", "Metrics server disabled");
        }
        
        // Start web server if enabled
        if (config_get_web_server_enabled(config)) {
            web_server = std::make_unique<WebServer>(config_get_web_server_port(config));
            
            // Add the web portal as a static route
            std::ifstream portal_file("web_portal.html");
            if (portal_file.is_open()) {
                std::string portal_content((std::istreambuf_iterator<char>(portal_file)),
                                         std::istreambuf_iterator<char>());
                web_server->addStaticRoute("/", portal_content, "text/html");
                portal_file.close();
            } else {
                logger_warn_with_category("WebServer", "Could not load web_portal.html, serving basic interface");
                web_server->addStaticRoute("/", "<h1>Millennium System Portal</h1><p>Web portal not available</p>", "text/html");
            }
            
            web_server->start();
            logger_info_with_category("Daemon", ("Web server started on port " + 
                       std::to_string(config_get_web_server_port(config))).c_str());
        } else {
            logger_info_with_category("Daemon", "Web server disabled");
        }
        
        // Start metrics collection thread (disabled for single-core optimization)
        // std::thread metrics_thread(metrics_collection_thread);
        
        // Initialize display
        line1 = format_number(daemon_state->keypad_buffer);
        line2 = generate_message(daemon_state->inserted_cents);
        millennium_client_set_display(client, generateDisplayBytes().c_str());
        
        // Initialize event processor
        event_processor = event_processor_create();
        if (!event_processor) {
            logger_error_with_category("Control", "Failed to create event processor");
            return 1;
        }
        
        // Register event handlers
        event_processor_register_coin_handler(event_processor, handle_coin_event);
        event_processor_register_call_state_handler(event_processor, handle_call_state_event);
        event_processor_register_hook_handler(event_processor, handle_hook_event);
        event_processor_register_keypad_handler(event_processor, handle_keypad_event);
        
        logger_info_with_category("Daemon", "Daemon initialized successfully");
        
        // Main event loop
        int loop_count = 0;
        while (running) {
            try {
	    	millennium_client_update(client);
                
                event_t *event = (event_t *)millennium_client_next_event(client);
                if (event) {
                    event_processor_process_event(event_processor, event);
                    event_destroy(event);
                }
                
                // Update metrics in main loop (every 1000 loops = ~1 second)
                if (++loop_count % 1000 == 0) {
                    // Update system metrics (moved from separate thread)
                    metrics_set_gauge("daemon_uptime_seconds", 
                        std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::steady_clock::now() - daemon_start_time).count());
                    
                    if (daemon_state) {
                        metrics_set_gauge("current_state", static_cast<double>(daemon_state->current_state));
                        metrics_set_gauge("inserted_cents", static_cast<double>(daemon_state->inserted_cents));
                        metrics_set_gauge("keypad_buffer_size", static_cast<double>(daemon_state_get_keypad_length(daemon_state)));
                    }
                }
                
                // Log metrics summary every 10000 loops (about every 10 seconds) at DEBUG level
                if (loop_count % 10000 == 0) {
                    logger_debug_with_category("Metrics", "=== Metrics Summary ===");
                    
                    // Log current state gauge (simplified for C89 version)
                    double current_state = metrics_get_gauge("current_state");
                    logger_debug_with_category("Metrics", ("Current state: " + std::to_string(current_state)).c_str());
                }
            } catch (const std::exception& e) {
                logger_error_with_category("Daemon", ("Error in main loop: " + std::string(e.what())).c_str());
                metrics_increment_counter("main_loop_errors", 1);
                
                // Wait a bit before continuing to prevent rapid error loops
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
        
        // Cleanup
        logger_info_with_category("Daemon", "Shutting down daemon");
        
        // Stop metrics server
        if (metrics_server) {
            metrics_server_stop(metrics_server);
            metrics_server_destroy(metrics_server);
            metrics_server = nullptr;
        }
        
        // Stop metrics thread (disabled for single-core optimization)
        // metrics_thread.join();
        
        // Stop health monitoring
        health_monitor_stop_monitoring();
        
        // Cleanup client
        millennium_client_destroy(client);
        client = nullptr;
        
        // Cleanup metrics
        metrics_cleanup();
        
        // Cleanup event processor
        if (event_processor) {
            event_processor_destroy(event_processor);
            event_processor = nullptr;
        }
        
        // Cleanup daemon state
        if (daemon_state) {
            free(daemon_state);
            daemon_state = nullptr;
        }
        
        logger_info_with_category("Daemon", "Daemon shutdown complete");
        
    } catch (const std::exception& e) {
        logger_error_with_category("Daemon", ("Fatal error: " + std::string(e.what())).c_str());
        return 1;
    }
    
    return 0;
}
