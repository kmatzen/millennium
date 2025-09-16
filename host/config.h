#pragma once

#include <string>
#include <map>
#include <memory>

class Config {
public:
    static Config& getInstance();
    
    // Configuration loading
    bool loadFromFile(const std::string& configPath);
    bool loadFromEnvironment();
    
    // Getters with defaults
    std::string getString(const std::string& key, const std::string& defaultValue = "") const;
    int getInt(const std::string& key, int defaultValue = 0) const;
    bool getBool(const std::string& key, bool defaultValue = false) const;
    
    // SIP Configuration
    std::string getSipUsername() const { return getString("sip.username"); }
    std::string getSipPassword() const { return getString("sip.password"); }
    std::string getSipDomain() const { return getString("sip.domain"); }
    std::string getSipUri() const { return getString("sip.uri"); }
    
    // Hardware Configuration
    std::string getDisplayDevice() const { 
        return getString("hardware.display_device", "/dev/serial/by-id/usb-Arduino_LLC_Millennium_Beta-if00"); 
    }
    int getBaudRate() const { return getInt("hardware.baud_rate", 9600); }
    
    // Call Configuration
    int getCallCostCents() const { return getInt("call.cost_cents", 50); }
    int getCallTimeoutSeconds() const { return getInt("call.timeout_seconds", 300); }
    
    // Logging Configuration
    std::string getLogLevel() const { return getString("logging.level", "INFO"); }
    std::string getLogFile() const { return getString("logging.file", ""); }
    bool getLogToFile() const { return getBool("logging.to_file", false); }
    
    // System Configuration
    int getUpdateIntervalMs() const { return getInt("system.update_interval_ms", 33); }
    int getMaxRetries() const { return getInt("system.max_retries", 3); }
    
    // Metrics Server Configuration
    bool getMetricsServerEnabled() const { return getBool("metrics_server.enabled", false); }
    int getMetricsServerPort() const { return getInt("metrics_server.port", 8080); }
    bool getMetricsServerDisableDuringAudio() const { return getBool("metrics_server.disable_during_audio", true); }
    
    // Web Server Configuration
    bool getWebServerEnabled() const { return getBool("web_server.enabled", true); }
    int getWebServerPort() const { return getInt("web_server.port", 8081); }
    bool getWebServerDisableDuringCalls() const { return getBool("web_server.disable_during_calls", true); }
    int getWebServerPollDelayMs() const { return getInt("web_server.poll_delay_ms", 10); }
    int getWebServerCallPollDelayMs() const { return getInt("web_server.call_poll_delay_ms", 100); }
    int getWebServerRateLimitNormal() const { return getInt("web_server.rate_limit_normal", 10); }
    int getWebServerRateLimitHighPriority() const { return getInt("web_server.rate_limit_high_priority", 2); }
    bool getWebServerEmergencyAudioMode() const { return getBool("web_server.emergency_audio_mode", true); }
    bool getWebServerDisableDuringHandsetUp() const { return getBool("web_server.disable_during_handset_up", false); }
    bool getWebServerDisableCompletelyDuringAudio() const { return getBool("web_server.disable_completely_during_audio", false); }
    
    // Main Loop Configuration
    int getMainLoopDelayMs() const { return getInt("main_loop.delay_ms", 1); }
    int getMainLoopAudioDelayMs() const { return getInt("main_loop.audio_delay_ms", 5); }
    int getMainLoopCallDelayMs() const { return getInt("main_loop.call_delay_ms", 10); }
    bool getMainLoopPanicMode() const { return getBool("main_loop.panic_mode", false); }
    bool getLoggingDisableDuringAudio() const { return getBool("logging.disable_during_audio", false); }
    int getMainLoopAudioPollingDivisor() const { return getInt("main_loop.audio_polling_divisor", 10); }
    
    // Validation
    bool validate() const;
    
private:
    Config() = default;
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;
    
    std::map<std::string, std::string> config_;
    
    void setDefaultValues();
    std::string trim(const std::string& str) const;
};
