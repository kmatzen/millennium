#pragma once

#include <string>
#include <fstream>
#include <memory>
#include <mutex>
#include <chrono>
#include <sstream>
#include <vector>

class MillenniumLogger {
public:
    enum Level { 
        VERBOSE = 0, 
        DEBUG = 1, 
        INFO = 2, 
        WARN = 3, 
        ERROR = 4 
    };

    static MillenniumLogger& getInstance();
    
    // Configuration
    void setLevel(Level level);
    void setLogFile(const std::string& filename);
    void setLogToConsole(bool enable);
    void setLogToFile(bool enable);
    
    // Logging methods
    void log(Level level, const std::string& message);
    void log(Level level, const std::string& category, const std::string& message);
    
    // Convenience methods
    void verbose(const std::string& message) { log(VERBOSE, message); }
    void debug(const std::string& message) { log(DEBUG, message); }
    void info(const std::string& message) { log(INFO, message); }
    void warn(const std::string& message) { log(WARN, message); }
    void error(const std::string& message) { log(ERROR, message); }
    
    void verbose(const std::string& category, const std::string& message) { log(VERBOSE, category, message); }
    void debug(const std::string& category, const std::string& message) { log(DEBUG, category, message); }
    void info(const std::string& category, const std::string& message) { log(INFO, category, message); }
    void warn(const std::string& category, const std::string& message) { log(WARN, category, message); }
    void error(const std::string& category, const std::string& message) { log(ERROR, category, message); }
    
    // Utility methods
    static Level parseLevel(const std::string& level_str);
    static std::string levelToString(Level level);
    
    // In-memory log storage
    std::vector<std::string> getRecentLogs(int max_entries = 50) const;
    
    // Structured logging
    class LogEntry {
    public:
        LogEntry(MillenniumLogger& logger, Level level, const std::string& category = "");
        ~LogEntry();
        
        template<typename T>
        LogEntry& operator<<(const T& value) {
            stream_ << value;
            return *this;
        }
        
    private:
        MillenniumLogger& logger_;
        Level level_;
        std::string category_;
        std::ostringstream stream_;
    };
    
    // Macro for structured logging
    #define LOG_VERBOSE(category) MillenniumLogger::LogEntry(MillenniumLogger::getInstance(), MillenniumLogger::VERBOSE, category)
    #define LOG_DEBUG(category) MillenniumLogger::LogEntry(MillenniumLogger::getInstance(), MillenniumLogger::DEBUG, category)
    #define LOG_INFO(category) MillenniumLogger::LogEntry(MillenniumLogger::getInstance(), MillenniumLogger::INFO, category)
    #define LOG_WARN(category) MillenniumLogger::LogEntry(MillenniumLogger::getInstance(), MillenniumLogger::WARN, category)
    #define LOG_ERROR(category) MillenniumLogger::LogEntry(MillenniumLogger::getInstance(), MillenniumLogger::ERROR, category)

private:
    MillenniumLogger() = default;
    MillenniumLogger(const MillenniumLogger&) = delete;
    MillenniumLogger& operator=(const MillenniumLogger&) = delete;
    
    Level current_level_ = INFO;
    std::string log_file_;
    bool log_to_console_ = true;
    bool log_to_file_ = false;
    std::unique_ptr<std::ofstream> file_stream_;
    std::mutex log_mutex_;
    
    // In-memory log storage
    mutable std::mutex memory_logs_mutex_;
    std::vector<std::string> memory_logs_;
    static const size_t MAX_MEMORY_LOGS = 1000;
    
    void writeLog(Level level, const std::string& category, const std::string& message);
    std::string formatTimestamp() const;
    std::string formatLevel(Level level) const;
};
