#include "health_monitor.h"
#include "logger.h"
#include <thread>
#include <algorithm>
#include <sys/statvfs.h>
#include <fstream>
#include <sstream>

HealthMonitor& HealthMonitor::getInstance() {
    static HealthMonitor instance;
    return instance;
}

void HealthMonitor::registerCheck(const std::string& name, 
                                 std::function<Status()> check_function,
                                 std::chrono::milliseconds interval) {
    std::lock_guard<std::mutex> lock(checks_mutex_);
    
    HealthCheck check;
    check.name = name;
    check.check_function = check_function;
    check.interval = interval;
    check.last_check = std::chrono::steady_clock::now();
    check.last_status = UNKNOWN;
    check.last_message = "Not yet checked";
    
    health_checks_[name] = check;
    
    MillenniumLogger::getInstance().info("HealthMonitor", "Registered health check: " + name);
}

void HealthMonitor::unregisterCheck(const std::string& name) {
    std::lock_guard<std::mutex> lock(checks_mutex_);
    
    auto it = health_checks_.find(name);
    if (it != health_checks_.end()) {
        health_checks_.erase(it);
        MillenniumLogger::getInstance().info("HealthMonitor", "Unregistered health check: " + name);
    }
}

HealthMonitor::Status HealthMonitor::getOverallStatus() const {
    std::lock_guard<std::mutex> lock(checks_mutex_);
    
    if (health_checks_.empty()) {
        return UNKNOWN;
    }
    
    Status overall_status = HEALTHY;
    
    for (const auto& pair : health_checks_) {
        const HealthCheck& check = pair.second;
        if (check.last_status > overall_status) {
            overall_status = check.last_status;
        }
    }
    
    return overall_status;
}

std::map<std::string, HealthMonitor::HealthCheck> HealthMonitor::getAllChecks() const {
    std::lock_guard<std::mutex> lock(checks_mutex_);
    return health_checks_;
}

HealthMonitor::HealthCheck HealthMonitor::getCheck(const std::string& name) const {
    std::lock_guard<std::mutex> lock(checks_mutex_);
    
    auto it = health_checks_.find(name);
    if (it != health_checks_.end()) {
        return it->second;
    }
    
    HealthCheck empty_check;
    empty_check.name = name;
    empty_check.last_status = UNKNOWN;
    empty_check.last_message = "Check not found";
    return empty_check;
}

void HealthMonitor::runCheck(const std::string& name) {
    std::lock_guard<std::mutex> lock(checks_mutex_);
    
    auto it = health_checks_.find(name);
    if (it == health_checks_.end()) {
        MillenniumLogger::getInstance().warn("HealthMonitor", "Health check not found: " + name);
        return;
    }
    
    HealthCheck& check = it->second;
    
    try {
        Status status = check.check_function();
        check.last_status = status;
        check.last_check = std::chrono::steady_clock::now();
        check.last_message = "Check completed successfully";
        
        updateStatistics(status);
        
        if (status != HEALTHY) {
            MillenniumLogger::getInstance().warn("HealthMonitor", 
                "Health check '" + name + "' returned status: " + statusToString(status));
        }
    } catch (const std::exception& e) {
        check.last_status = CRITICAL;
        check.last_check = std::chrono::steady_clock::now();
        check.last_message = "Check failed with exception: " + std::string(e.what());
        
        updateStatistics(CRITICAL);
        
        MillenniumLogger::getInstance().error("HealthMonitor", 
            "Health check '" + name + "' failed: " + e.what());
    }
}

void HealthMonitor::runAllChecks() {
    std::lock_guard<std::mutex> lock(checks_mutex_);
    
    for (auto& pair : health_checks_) {
        HealthCheck& check = pair.second;
        
        try {
            Status status = check.check_function();
            check.last_status = status;
            check.last_check = std::chrono::steady_clock::now();
            check.last_message = "Check completed successfully";
            
            updateStatistics(status);
        } catch (const std::exception& e) {
            check.last_status = CRITICAL;
            check.last_check = std::chrono::steady_clock::now();
            check.last_message = "Check failed with exception: " + std::string(e.what());
            
            updateStatistics(CRITICAL);
            
            MillenniumLogger::getInstance().error("HealthMonitor", 
                "Health check '" + check.name + "' failed: " + e.what());
        }
    }
}

void HealthMonitor::startMonitoring() {
    if (monitoring_active_.load()) {
        return;
    }
    
    monitoring_active_ = true;
    should_stop_ = false;
    statistics_.start_time = std::chrono::steady_clock::now();
    
    std::thread monitoring_thread(&HealthMonitor::monitoringLoop, this);
    monitoring_thread.detach();
    
    MillenniumLogger::getInstance().info("HealthMonitor", "Health monitoring started");
}

void HealthMonitor::stopMonitoring() {
    should_stop_ = true;
    monitoring_active_ = false;
    
    MillenniumLogger::getInstance().info("HealthMonitor", "Health monitoring stopped");
}

void HealthMonitor::monitoringLoop() {
    while (!should_stop_.load()) {
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        
        {
            std::lock_guard<std::mutex> lock(checks_mutex_);
            
            for (auto& pair : health_checks_) {
                HealthCheck& check = pair.second;
                
                if (now - check.last_check >= check.interval) {
                    try {
                        Status status = check.check_function();
                        check.last_status = status;
                        check.last_check = now;
                        check.last_message = "Check completed successfully";
                        
                        updateStatistics(status);
                    } catch (const std::exception& e) {
                        check.last_status = CRITICAL;
                        check.last_check = now;
                        check.last_message = "Check failed with exception: " + std::string(e.what());
                        
                        updateStatistics(CRITICAL);
                        
                        MillenniumLogger::getInstance().error("HealthMonitor", 
                            "Health check '" + check.name + "' failed: " + e.what());
                    }
                }
            }
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void HealthMonitor::updateStatistics(Status status) {
    statistics_.total_checks++;
    
    if (status == CRITICAL) {
        statistics_.failed_checks++;
    } else if (status == WARNING) {
        statistics_.warning_checks++;
    }
}

std::string HealthMonitor::statusToString(Status status) {
    switch (status) {
        case HEALTHY: return "HEALTHY";
        case WARNING: return "WARNING";
        case CRITICAL: return "CRITICAL";
        case UNKNOWN: return "UNKNOWN";
        default: return "UNKNOWN";
    }
}

HealthMonitor::Status HealthMonitor::stringToStatus(const std::string& status_str) {
    std::string upper = status_str;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
    
    if (upper == "HEALTHY") return HEALTHY;
    if (upper == "WARNING") return WARNING;
    if (upper == "CRITICAL") return CRITICAL;
    if (upper == "UNKNOWN") return UNKNOWN;
    
    return UNKNOWN;
}

// SystemHealthChecks implementation
HealthMonitor::Status SystemHealthChecks::checkSerialConnection() {
    // This would check if the serial connection to the Arduino is working
    // For now, we'll simulate a check
    return HealthMonitor::HEALTHY;
}

HealthMonitor::Status SystemHealthChecks::checkSipConnection() {
    // This would check if the SIP connection is active
    // For now, we'll simulate a check
    return HealthMonitor::HEALTHY;
}

HealthMonitor::Status SystemHealthChecks::checkMemoryUsage() {
    std::ifstream status_file("/proc/self/status");
    if (!status_file.is_open()) {
        return HealthMonitor::UNKNOWN;
    }
    
    std::string line;
    while (std::getline(status_file, line)) {
        if (line.substr(0, 6) == "VmRSS:") {
            std::istringstream iss(line);
            std::string key, value, unit;
            iss >> key >> value >> unit;
            
            long memory_kb = std::stol(value);
            long memory_mb = memory_kb / 1024;
            
            if (memory_mb > 500) { // More than 500MB
                return HealthMonitor::WARNING;
            } else if (memory_mb > 1000) { // More than 1GB
                return HealthMonitor::CRITICAL;
            }
            
            return HealthMonitor::HEALTHY;
        }
    }
    
    return HealthMonitor::UNKNOWN;
}

HealthMonitor::Status SystemHealthChecks::checkDiskSpace() {
    struct statvfs stat;
    if (statvfs("/", &stat) != 0) {
        return HealthMonitor::UNKNOWN;
    }
    
    unsigned long free_space = stat.f_bavail * stat.f_frsize;
    unsigned long total_space = stat.f_blocks * stat.f_frsize;
    
    double free_percentage = (double)free_space / total_space * 100.0;
    
    if (free_percentage < 5.0) {
        return HealthMonitor::CRITICAL;
    } else if (free_percentage < 10.0) {
        return HealthMonitor::WARNING;
    }
    
    return HealthMonitor::HEALTHY;
}

HealthMonitor::Status SystemHealthChecks::checkSystemLoad() {
    std::ifstream loadavg_file("/proc/loadavg");
    if (!loadavg_file.is_open()) {
        return HealthMonitor::UNKNOWN;
    }
    
    std::string line;
    if (std::getline(loadavg_file, line)) {
        std::istringstream iss(line);
        double load1, load5, load15;
        iss >> load1 >> load5 >> load15;
        
        // Get number of CPU cores
        std::ifstream cpuinfo("/proc/cpuinfo");
        int cpu_count = 0;
        std::string cpu_line;
        while (std::getline(cpuinfo, cpu_line)) {
            if (cpu_line.substr(0, 9) == "processor") {
                cpu_count++;
            }
        }
        
        if (cpu_count == 0) {
            cpu_count = 1; // Fallback
        }
        
        double load_percentage = (load1 / cpu_count) * 100.0;
        
        if (load_percentage > 90.0) {
            return HealthMonitor::CRITICAL;
        } else if (load_percentage > 70.0) {
            return HealthMonitor::WARNING;
        }
        
        return HealthMonitor::HEALTHY;
    }
    
    return HealthMonitor::UNKNOWN;
}
