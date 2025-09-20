extern "C" {
#include "config.h"
}
#include "logger.h"
#include <atomic>
#include <csignal>
#include <iostream>
#include <thread>
#include <chrono>
#include <map>
#include <mutex>

// Global state
std::atomic<bool> running(true);
config_data_t* config = config_get_instance();
MillenniumLogger& logger = MillenniumLogger::getInstance();

// Signal handler
void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        logger.info("Daemon", "Received signal " + std::to_string(signal) + ", shutting down gracefully...");
        running = false;
    }
}

// Simple metrics tracking
class SimpleMetrics {
public:
    static SimpleMetrics& getInstance() {
        static SimpleMetrics instance;
        return instance;
    }
    
    void incrementCounter(const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex_);
        counters_[name]++;
    }
    
    void setGauge(const std::string& name, double value) {
        std::lock_guard<std::mutex> lock(mutex_);
        gauges_[name] = value;
    }
    
    void printStats() {
        std::lock_guard<std::mutex> lock(mutex_);
        logger.info("Metrics", "=== Current Statistics ===");
        for (const auto& pair : counters_) {
            logger.info("Metrics", "Counter " + pair.first + ": " + std::to_string(pair.second));
        }
        for (const auto& pair : gauges_) {
            logger.info("Metrics", "Gauge " + pair.first + ": " + std::to_string(pair.second));
        }
    }
    
private:
    std::mutex mutex_;
    std::map<std::string, uint64_t> counters_;
    std::map<std::string, double> gauges_;
};

// Simple health check
class SimpleHealthMonitor {
public:
    static SimpleHealthMonitor& getInstance() {
        static SimpleHealthMonitor instance;
        return instance;
    }
    
    void startMonitoring() {
        if (monitoring_active_) return;
        
        monitoring_active_ = true;
        std::thread monitoring_thread([this]() {
            while (running) {
                checkSystemHealth();
                std::this_thread::sleep_for(std::chrono::seconds(30));
            }
        });
        monitoring_thread.detach();
        
        logger.info("HealthMonitor", "Health monitoring started");
    }
    
    void stopMonitoring() {
        monitoring_active_ = false;
        logger.info("HealthMonitor", "Health monitoring stopped");
    }
    
private:
    bool monitoring_active_ = false;
    
    void checkSystemHealth() {
        // Simple health check - just log that we're alive
        logger.debug("HealthMonitor", "System health check - OK");
    }
};

// Simulated phone state
enum class PhoneState {
    IDLE_DOWN,
    IDLE_UP,
    CALL_INCOMING,
    CALL_ACTIVE
};

class PhoneSimulator {
public:
    PhoneSimulator() : state_(PhoneState::IDLE_DOWN) {}
    
    void simulateActivity() {
        static int counter = 0;
        counter++;
        
        switch (counter % 10) {
            case 0:
                simulateHookLift();
                break;
            case 3:
                simulateCoinInsert();
                break;
            case 6:
                simulateKeypadPress();
                break;
            case 9:
                simulateHookDown();
                break;
        }
    }
    
private:
    PhoneState state_;
    int inserted_cents_ = 0;
    std::string keypad_buffer_;
    
    void simulateHookLift() {
        if (state_ == PhoneState::IDLE_DOWN) {
            state_ = PhoneState::IDLE_UP;
            logger.info("PhoneSim", "Hook lifted - ready for calls");
            SimpleMetrics::getInstance().incrementCounter("hook_lifted");
        }
    }
    
    void simulateCoinInsert() {
        if (state_ == PhoneState::IDLE_UP) {
            inserted_cents_ += 25;
            logger.info("PhoneSim", "Coin inserted - total: " + std::to_string(inserted_cents_) + " cents");
            SimpleMetrics::getInstance().incrementCounter("coins_inserted");
            SimpleMetrics::getInstance().setGauge("inserted_cents", inserted_cents_);
        }
    }
    
    void simulateKeypadPress() {
        if (state_ == PhoneState::IDLE_UP && keypad_buffer_.length() < 10) {
            keypad_buffer_ += "1";
            logger.debug("PhoneSim", "Key pressed - buffer: " + keypad_buffer_);
            SimpleMetrics::getInstance().incrementCounter("keypad_presses");
        }
    }
    
    void simulateHookDown() {
        if (state_ == PhoneState::IDLE_UP) {
            state_ = PhoneState::IDLE_DOWN;
            logger.info("PhoneSim", "Hook down - call ended");
            SimpleMetrics::getInstance().incrementCounter("hook_down");
            inserted_cents_ = 0;
            keypad_buffer_.clear();
        }
    }
};

int main(int argc, char *argv[]) {
    try {
        // Setup signal handlers
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);
        
        // Load configuration
        std::string config_file = "daemon.conf.example";
        if (argc > 2 && std::string(argv[1]) == "--config") {
            config_file = argv[2];
        }
        
        if (!config_load_from_file(config, config_file.c_str())) {
            logger.warn("Config", "Could not load config file: " + config_file + ", using environment variables");
            config_load_from_environment(config);
        }
        
        if (!config_validate(config)) {
            logger.error("Config", "Configuration validation failed");
            return 1;
        }
        
        // Setup logging
        logger.setLevel(MillenniumLogger::parseLevel(config_get_log_level(config)));
        if (config_get_log_to_file(config) && strlen(config_get_log_file(config)) > 0) {
            logger.setLogFile(config_get_log_file(config));
            logger.setLogToFile(true);
        }
        
        logger.info("Daemon", "Starting Millennium Daemon (Simple Version)");
        logger.info("Daemon", "Configuration loaded successfully");
        logger.info("Daemon", "Call cost: " + std::to_string(config_get_call_cost_cents(config)) + " cents");
        
        // Initialize components
        SimpleHealthMonitor& health_monitor = SimpleHealthMonitor::getInstance();
        SimpleMetrics& metrics = SimpleMetrics::getInstance();
        PhoneSimulator phone_sim;
        
        // Start health monitoring
        health_monitor.startMonitoring();
        
        logger.info("Daemon", "Daemon initialized successfully");
        
        // Main event loop
        int loop_count = 0;
        while (running) {
            try {
                // Simulate phone activity
                phone_sim.simulateActivity();
                
                // Print stats every 50 loops
                if (++loop_count % 50 == 0) {
                    metrics.printStats();
                }
                
                // Small delay to prevent busy waiting
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                
            } catch (const std::exception& e) {
                logger.error("Daemon", "Error in main loop: " + std::string(e.what()));
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
        
        // Cleanup
        logger.info("Daemon", "Shutting down daemon");
        health_monitor.stopMonitoring();
        metrics.printStats();
        
        logger.info("Daemon", "Daemon shutdown complete");
        
    } catch (const std::exception& e) {
        logger.error("Daemon", "Fatal error: " + std::string(e.what()));
        return 1;
    }
    
    return 0;
}
