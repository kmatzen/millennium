#include "config.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <stdexcept>

Config& Config::getInstance() {
    static Config instance;
    return instance;
}

bool Config::loadFromFile(const std::string& configPath) {
    std::ifstream file(configPath);
    if (!file.is_open()) {
        return false;
    }
    
    setDefaultValues();
    
    std::string line;
    while (std::getline(file, line)) {
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') {
            continue;
        }
        
        // Find the equals sign
        size_t pos = line.find('=');
        if (pos == std::string::npos) {
            continue;
        }
        
        std::string key = trim(line.substr(0, pos));
        std::string value = trim(line.substr(pos + 1));
        
        if (!key.empty() && !value.empty()) {
            config_[key] = value;
        }
    }
    
    return true;
}

bool Config::loadFromEnvironment() {
    setDefaultValues();
    
    // Load SIP configuration from environment variables
    if (const char* username = std::getenv("MILLENNIUM_SIP_USERNAME")) {
        config_["sip.username"] = username;
    }
    if (const char* password = std::getenv("MILLENNIUM_SIP_PASSWORD")) {
        config_["sip.password"] = password;
    }
    if (const char* domain = std::getenv("MILLENNIUM_SIP_DOMAIN")) {
        config_["sip.domain"] = domain;
    }
    if (const char* uri = std::getenv("MILLENNIUM_SIP_URI")) {
        config_["sip.uri"] = uri;
    }
    
    // Load logging configuration
    if (const char* logLevel = std::getenv("MILLENNIUM_LOG_LEVEL")) {
        config_["logging.level"] = logLevel;
    }
    if (const char* logFile = std::getenv("MILLENNIUM_LOG_FILE")) {
        config_["logging.file"] = logFile;
    }
    
    return true;
}

std::string Config::getString(const std::string& key, const std::string& defaultValue) const {
    auto it = config_.find(key);
    return (it != config_.end()) ? it->second : defaultValue;
}

int Config::getInt(const std::string& key, int defaultValue) const {
    auto it = config_.find(key);
    if (it != config_.end()) {
        try {
            return std::stoi(it->second);
        } catch (const std::exception&) {
            return defaultValue;
        }
    }
    return defaultValue;
}

bool Config::getBool(const std::string& key, bool defaultValue) const {
    auto it = config_.find(key);
    if (it != config_.end()) {
        std::string value = it->second;
        std::transform(value.begin(), value.end(), value.begin(), ::tolower);
        return (value == "true" || value == "1" || value == "yes");
    }
    return defaultValue;
}

bool Config::validate() const {
    // Check required SIP configuration
    if (getSipUsername().empty() || getSipPassword().empty() || getSipDomain().empty()) {
        return false;
    }
    
    // Validate numeric values
    if (getCallCostCents() <= 0) {
        return false;
    }
    
    if (getUpdateIntervalMs() <= 0) {
        return false;
    }
    
    return true;
}

void Config::setDefaultValues() {
    config_["sip.username"] = "";
    config_["sip.password"] = "";
    config_["sip.domain"] = "";
    config_["sip.uri"] = "";
    
    config_["hardware.display_device"] = "/dev/serial/by-id/usb-Arduino_LLC_Millennium_Beta-if00";
    config_["hardware.baud_rate"] = "9600";
    
    config_["call.cost_cents"] = "50";
    config_["call.timeout_seconds"] = "300";
    
    config_["logging.level"] = "INFO";
    config_["logging.file"] = "";
    config_["logging.to_file"] = "false";
    
    config_["system.update_interval_ms"] = "33";
    config_["system.max_retries"] = "3";
}

std::string Config::trim(const std::string& str) const {
    size_t first = str.find_first_not_of(' ');
    if (first == std::string::npos) {
        return "";
    }
    size_t last = str.find_last_not_of(' ');
    return str.substr(first, (last - first + 1));
}
