#pragma once

#include <string>
#include <map>
#include <chrono>
#include <mutex>
#include <atomic>
#include <functional>

class HealthMonitor {
public:
    enum Status {
        HEALTHY = 0,
        WARNING = 1,
        CRITICAL = 2,
        UNKNOWN = 3
    };
    
    struct HealthCheck {
        std::string name;
        std::function<Status()> check_function;
        std::chrono::milliseconds interval;
        std::chrono::steady_clock::time_point last_check;
        Status last_status = UNKNOWN;
        std::string last_message;
    };
    
    static HealthMonitor& getInstance();
    
    // Health check management
    void registerCheck(const std::string& name, 
                      std::function<Status()> check_function,
                      std::chrono::milliseconds interval = std::chrono::seconds(30));
    
    void unregisterCheck(const std::string& name);
    
    // Status reporting
    Status getOverallStatus() const;
    std::map<std::string, HealthCheck> getAllChecks() const;
    HealthCheck getCheck(const std::string& name) const;
    
    // Manual check execution
    void runCheck(const std::string& name);
    void runAllChecks();
    
    // Monitoring control
    void startMonitoring();
    void stopMonitoring();
    bool isMonitoring() const { return monitoring_active_.load(); }
    
    // Statistics
    struct Statistics {
        std::chrono::steady_clock::time_point start_time;
        std::atomic<uint64_t> total_checks{0};
        std::atomic<uint64_t> failed_checks{0};
        std::atomic<uint64_t> warning_checks{0};
        
        // Copy constructor for atomic members
        Statistics() = default;
        Statistics(const Statistics& other) 
            : start_time(other.start_time)
            , total_checks(other.total_checks.load())
            , failed_checks(other.failed_checks.load())
            , warning_checks(other.warning_checks.load()) {}
    };
    
    Statistics getStatistics() const { return statistics_; }
    
    // Utility methods
    static std::string statusToString(Status status);
    static Status stringToStatus(const std::string& status_str);

private:
    HealthMonitor() = default;
    HealthMonitor(const HealthMonitor&) = delete;
    HealthMonitor& operator=(const HealthMonitor&) = delete;
    
    mutable std::mutex checks_mutex_;
    std::map<std::string, HealthCheck> health_checks_;
    std::atomic<bool> monitoring_active_{false};
    std::atomic<bool> should_stop_{false};
    Statistics statistics_;
    
    void monitoringLoop();
    void updateStatistics(Status status);
};

// Predefined health checks
class SystemHealthChecks {
public:
    static HealthMonitor::Status checkSerialConnection();
    static HealthMonitor::Status checkSipConnection();
    static HealthMonitor::Status checkMemoryUsage();
    static HealthMonitor::Status checkDiskSpace();
    static HealthMonitor::Status checkSystemLoad();
};
