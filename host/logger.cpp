#include "logger.h"
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <cctype>

MillenniumLogger& MillenniumLogger::getInstance() {
    static MillenniumLogger instance;
    return instance;
}

void MillenniumLogger::setLevel(Level level) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    current_level_ = level;
}

void MillenniumLogger::setLogFile(const std::string& filename) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    log_file_ = filename;
    if (log_to_file_) {
        file_stream_ = std::make_unique<std::ofstream>(filename, std::ios::app);
        if (!file_stream_->is_open()) {
            log_to_file_ = false;
        }
    }
}

void MillenniumLogger::setLogToConsole(bool enable) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    log_to_console_ = enable;
}

void MillenniumLogger::setLogToFile(bool enable) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    log_to_file_ = enable;
    if (enable && !log_file_.empty()) {
        file_stream_ = std::make_unique<std::ofstream>(log_file_, std::ios::app);
        if (!file_stream_->is_open()) {
            log_to_file_ = false;
        }
    } else {
        file_stream_.reset();
    }
}

void MillenniumLogger::log(Level level, const std::string& message) {
    log(level, "", message);
}

void MillenniumLogger::log(Level level, const std::string& category, const std::string& message) {
    if (level >= current_level_) {
        writeLog(level, category, message);
    }
}

MillenniumLogger::Level MillenniumLogger::parseLevel(const std::string& level_str) {
    std::string upper = level_str;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
    
    if (upper == "VERBOSE") return VERBOSE;
    if (upper == "DEBUG") return DEBUG;
    if (upper == "INFO") return INFO;
    if (upper == "WARN") return WARN;
    if (upper == "ERROR") return ERROR;
    
    return INFO; // Default level
}

std::string MillenniumLogger::levelToString(Level level) {
    switch (level) {
        case VERBOSE: return "VERBOSE";
        case DEBUG: return "DEBUG";
        case INFO: return "INFO";
        case WARN: return "WARN";
        case ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

void MillenniumLogger::writeLog(Level level, const std::string& category, const std::string& message) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    
    std::string timestamp = formatTimestamp();
    std::string levelStr = formatLevel(level);
    
    std::ostringstream logLine;
    logLine << "[" << timestamp << "] [" << levelStr << "]";
    
    if (!category.empty()) {
        logLine << " [" << category << "]";
    }
    
    logLine << " " << message;
    
    std::string formattedMessage = logLine.str();
    
    // Store in memory
    {
        std::lock_guard<std::mutex> memory_lock(memory_logs_mutex_);
        memory_logs_.push_back(formattedMessage);
        
        // Keep only the most recent logs
        if (memory_logs_.size() > MAX_MEMORY_LOGS) {
            memory_logs_.erase(memory_logs_.begin(), memory_logs_.begin() + (memory_logs_.size() - MAX_MEMORY_LOGS));
        }
    }
    
    if (log_to_console_) {
        if (level >= WARN) {
            std::cerr << formattedMessage << std::endl;
        } else {
            std::cout << formattedMessage << std::endl;
        }
    }
    
    if (log_to_file_ && file_stream_ && file_stream_->is_open()) {
        *file_stream_ << formattedMessage << std::endl;
        file_stream_->flush();
    }
}

std::string MillenniumLogger::formatTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    
    return oss.str();
}

std::string MillenniumLogger::formatLevel(Level level) const {
    return levelToString(level);
}

std::vector<std::string> MillenniumLogger::getRecentLogs(int max_entries) const {
    std::lock_guard<std::mutex> lock(memory_logs_mutex_);
    
    std::vector<std::string> result;
    int start_idx = std::max(0, static_cast<int>(memory_logs_.size()) - max_entries);
    
    for (int i = start_idx; i < static_cast<int>(memory_logs_.size()); ++i) {
        result.push_back(memory_logs_[i]);
    }
    
    return result;
}

// LogEntry implementation
MillenniumLogger::LogEntry::LogEntry(MillenniumLogger& logger, Level level, const std::string& category)
    : logger_(logger), level_(level), category_(category) {
}

MillenniumLogger::LogEntry::~LogEntry() {
    logger_.log(level_, category_, stream_.str());
}
