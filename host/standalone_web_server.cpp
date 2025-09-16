#include "web_server.h"
#include "config.h"
#include "logger.h"
#include "metrics.h"
#include "health_monitor.h"
#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>

// Mock daemon state for demonstration
struct MockDaemonState {
    int current_state = 1; // IDLE_UP
    int inserted_cents = 0;
    std::string keypad_buffer = "";
    std::chrono::steady_clock::time_point last_activity;
    
    MockDaemonState() {
        last_activity = std::chrono::steady_clock::now();
    }
    
    void updateActivity() {
        last_activity = std::chrono::steady_clock::now();
    }
};

MockDaemonState mock_daemon_state;

// Define DaemonStateInfo structure for standalone version
struct DaemonStateInfo {
    int current_state;
    int inserted_cents;
    std::string keypad_buffer;
    std::chrono::steady_clock::time_point last_activity;
};

// Mock function to get daemon state info
DaemonStateInfo getDaemonStateInfo() {
    DaemonStateInfo info;
    info.current_state = mock_daemon_state.current_state;
    info.inserted_cents = mock_daemon_state.inserted_cents;
    info.keypad_buffer = mock_daemon_state.keypad_buffer;
    info.last_activity = mock_daemon_state.last_activity;
    return info;
}

// Mock function to get daemon start time
std::chrono::steady_clock::time_point getDaemonStartTime() {
    static std::chrono::steady_clock::time_point start_time = std::chrono::steady_clock::now();
    return start_time;
}

// Mock function to send control commands
bool sendControlCommand(const std::string& action) {
    if (action == "start_call") {
        mock_daemon_state.current_state = 3; // CALL_INCOMING
        mock_daemon_state.updateActivity();
        std::cout << "Mock: Call initiation requested" << std::endl;
        return true;
    } else if (action == "reset_system") {
        mock_daemon_state.current_state = 1; // IDLE_UP
        mock_daemon_state.inserted_cents = 0;
        mock_daemon_state.keypad_buffer = "";
        mock_daemon_state.updateActivity();
        std::cout << "Mock: System reset requested" << std::endl;
        return true;
    } else if (action == "emergency_stop") {
        mock_daemon_state.current_state = 0; // INVALID
        mock_daemon_state.updateActivity();
        std::cout << "Mock: Emergency stop activated" << std::endl;
        return true;
    } else if (action.substr(0, 12) == "keypad_press") {
        std::string key = action.substr(13);
        mock_daemon_state.keypad_buffer += key;
        mock_daemon_state.updateActivity();
        std::cout << "Mock: Keypad key '" << key << "' pressed" << std::endl;
        return true;
    } else if (action == "keypad_clear") {
        mock_daemon_state.keypad_buffer = "";
        mock_daemon_state.updateActivity();
        std::cout << "Mock: Keypad cleared" << std::endl;
        return true;
    } else if (action == "keypad_backspace") {
        if (!mock_daemon_state.keypad_buffer.empty()) {
            mock_daemon_state.keypad_buffer.pop_back();
            mock_daemon_state.updateActivity();
        }
        std::cout << "Mock: Keypad backspace" << std::endl;
        return true;
    } else if (action.substr(0, 10) == "coin_insert") {
        std::string cents_str = action.substr(11);
        int cents = std::stoi(cents_str);
        mock_daemon_state.inserted_cents += cents;
        mock_daemon_state.updateActivity();
        std::cout << "Mock: Coin inserted: " << cents << "¢" << std::endl;
        return true;
    } else if (action == "coin_return") {
        mock_daemon_state.inserted_cents = 0;
        mock_daemon_state.updateActivity();
        std::cout << "Mock: Coins returned" << std::endl;
        return true;
    } else if (action == "handset_up") {
        if (mock_daemon_state.current_state == 1) { // IDLE_DOWN
            mock_daemon_state.current_state = 2; // IDLE_UP
            mock_daemon_state.updateActivity();
        }
        std::cout << "Mock: Handset lifted" << std::endl;
        return true;
    } else if (action == "handset_down") {
        if (mock_daemon_state.current_state == 2) { // IDLE_UP
            mock_daemon_state.current_state = 1; // IDLE_DOWN
            mock_daemon_state.updateActivity();
        }
        std::cout << "Mock: Handset placed down" << std::endl;
        return true;
    }
    return false;
}

// Mock metrics for demonstration
void setupMockMetrics() {
    Metrics& metrics = Metrics::getInstance();
    
    // Add some sample metrics
    metrics.incrementCounter("calls_made", 5);
    metrics.incrementCounter("calls_received", 3);
    metrics.incrementCounter("keypad_presses", 12);
    metrics.setGauge("current_state", 1.0);
    metrics.setGauge("inserted_cents", 0.0);
    metrics.observeHistogram("call_duration", 45.2);
    metrics.observeHistogram("call_duration", 67.8);
    metrics.observeHistogram("call_duration", 23.1);
}

// Mock health checks
void setupMockHealthChecks() {
    HealthMonitor& health_monitor = HealthMonitor::getInstance();
    
    // Add mock health checks
    health_monitor.registerCheck("serial_connection", []() {
        return HealthMonitor::HEALTHY;
    }, std::chrono::seconds(30));
    
    health_monitor.registerCheck("sip_connection", []() {
        return HealthMonitor::HEALTHY;
    }, std::chrono::seconds(60));
    
    health_monitor.registerCheck("daemon_activity", []() {
        return HealthMonitor::HEALTHY;
    }, std::chrono::seconds(120));
    
    health_monitor.startMonitoring();
}

// Simulate some activity
void simulateActivity() {
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        
        // Simulate some state changes
        static int counter = 0;
        counter++;
        
        if (counter % 3 == 0) {
            mock_daemon_state.current_state = (mock_daemon_state.current_state + 1) % 5;
            mock_daemon_state.last_activity = std::chrono::steady_clock::now();
            
            Metrics& metrics = Metrics::getInstance();
            metrics.setGauge("current_state", static_cast<double>(mock_daemon_state.current_state));
        }
        
        if (counter % 5 == 0) {
            mock_daemon_state.inserted_cents += 25;
            mock_daemon_state.keypad_buffer += std::to_string(counter % 10);
            mock_daemon_state.last_activity = std::chrono::steady_clock::now();
            
            Metrics& metrics = Metrics::getInstance();
            metrics.setGauge("inserted_cents", static_cast<double>(mock_daemon_state.inserted_cents));
            metrics.incrementCounter("keypad_presses");
        }
    }
}

int main(int argc, char* argv[]) {
    std::cout << "Starting Millennium Web Portal (Standalone Demo)" << std::endl;
    
    // Initialize configuration
    Config& config = Config::getInstance();
    config.loadFromEnvironment();
    
    // Initialize logging
    MillenniumLogger& logger = MillenniumLogger::getInstance();
    logger.setLevel(MillenniumLogger::INFO);
    logger.info("Standalone", "Starting standalone web server demo");
    
    // Setup mock data
    setupMockMetrics();
    setupMockHealthChecks();
    
    // Create and start web server
    WebServer web_server(8081);
    
    // Add the web portal as a static route
    std::ifstream portal_file("web_portal.html");
    if (portal_file.is_open()) {
        std::string portal_content((std::istreambuf_iterator<char>(portal_file)),
                                 std::istreambuf_iterator<char>());
        web_server.addStaticRoute("/", portal_content, "text/html");
        portal_file.close();
        logger.info("Standalone", "Loaded web portal from web_portal.html");
    } else {
        logger.warn("Standalone", "Could not load web_portal.html, serving basic interface");
        web_server.addStaticRoute("/", "<h1>Millennium System Portal</h1><p>Web portal demo running</p>", "text/html");
    }
    
    web_server.start();
    
    if (!web_server.isRunning()) {
        logger.error("Standalone", "Failed to start web server");
        return 1;
    }
    
    logger.info("Standalone", "Web server started successfully on port 8081");
    logger.info("Standalone", "Access the portal at: http://localhost:8081");
    
    // Start activity simulation in background
    std::thread activity_thread(simulateActivity);
    activity_thread.detach();
    
    // Keep running
    std::cout << "Web portal is running at http://localhost:8081" << std::endl;
    std::cout << "Press Ctrl+C to stop" << std::endl;
    
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    return 0;
}
