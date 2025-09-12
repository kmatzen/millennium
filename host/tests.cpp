#include "test_framework.h"
#include "config.h"
#include "logger.h"
#include "metrics.h"
#include <sstream>
#include <fstream>

// Test functions
TEST_FUNCTION(test_config_defaults) {
    Config& config = Config::getInstance();
    
    ASSERT_EQUAL_INT(50, config.getCallCostCents(), "Default call cost should be 50 cents");
    ASSERT_EQUAL_INT(9600, config.getBaudRate(), "Default baud rate should be 9600");
    ASSERT_EQUAL("INFO", config.getLogLevel(), "Default log level should be INFO");
    ASSERT_EQUAL_INT(33, config.getUpdateIntervalMs(), "Default update interval should be 33ms");
    
    return true;
}

TEST_FUNCTION(test_config_environment) {
    // Set environment variables
    setenv("MILLENNIUM_SIP_USERNAME", "test_user", 1);
    setenv("MILLENNIUM_SIP_PASSWORD", "test_pass", 1);
    setenv("MILLENNIUM_LOG_LEVEL", "DEBUG", 1);
    
    Config& config = Config::getInstance();
    config.loadFromEnvironment();
    
    ASSERT_EQUAL("test_user", config.getSipUsername(), "SIP username should be loaded from environment");
    ASSERT_EQUAL("test_pass", config.getSipPassword(), "SIP password should be loaded from environment");
    ASSERT_EQUAL("DEBUG", config.getLogLevel(), "Log level should be loaded from environment");
    
    // Clean up
    unsetenv("MILLENNIUM_SIP_USERNAME");
    unsetenv("MILLENNIUM_SIP_PASSWORD");
    unsetenv("MILLENNIUM_LOG_LEVEL");
    
    return true;
}

TEST_FUNCTION(test_config_file) {
    // Create a temporary config file
    std::ofstream config_file("test_config.conf");
    config_file << "call.cost_cents=75\n";
    config_file << "logging.level=WARN\n";
    config_file << "sip.username=file_user\n";
    config_file.close();
    
    Config& config = Config::getInstance();
    bool loaded = config.loadFromFile("test_config.conf");
    
    ASSERT_TRUE(loaded, "Config file should load successfully");
    ASSERT_EQUAL_INT(75, config.getCallCostCents(), "Call cost should be loaded from file");
    ASSERT_EQUAL("WARN", config.getLogLevel(), "Log level should be loaded from file");
    ASSERT_EQUAL("file_user", config.getSipUsername(), "SIP username should be loaded from file");
    
    // Clean up
    std::remove("test_config.conf");
    
    return true;
}

TEST_FUNCTION(test_logger_levels) {
    MillenniumLogger& logger = MillenniumLogger::getInstance();
    
    // Test level parsing
    ASSERT_EQUAL_INT(MillenniumLogger::DEBUG, MillenniumLogger::parseLevel("DEBUG"), "DEBUG level should parse correctly");
    ASSERT_EQUAL_INT(MillenniumLogger::INFO, MillenniumLogger::parseLevel("INFO"), "INFO level should parse correctly");
    ASSERT_EQUAL_INT(MillenniumLogger::WARN, MillenniumLogger::parseLevel("WARN"), "WARN level should parse correctly");
    ASSERT_EQUAL_INT(MillenniumLogger::ERROR, MillenniumLogger::parseLevel("ERROR"), "ERROR level should parse correctly");
    ASSERT_EQUAL_INT(MillenniumLogger::INFO, MillenniumLogger::parseLevel("INVALID"), "Invalid level should default to INFO");
    
    // Test level to string conversion
    ASSERT_EQUAL("DEBUG", MillenniumLogger::levelToString(MillenniumLogger::DEBUG), "DEBUG should convert to string correctly");
    ASSERT_EQUAL("INFO", MillenniumLogger::levelToString(MillenniumLogger::INFO), "INFO should convert to string correctly");
    ASSERT_EQUAL("WARN", MillenniumLogger::levelToString(MillenniumLogger::WARN), "WARN should convert to string correctly");
    ASSERT_EQUAL("ERROR", MillenniumLogger::levelToString(MillenniumLogger::ERROR), "ERROR should convert to string correctly");
    
    return true;
}

TEST_FUNCTION(test_metrics_counters) {
    Metrics& metrics = Metrics::getInstance();
    
    // Test counter operations
    ASSERT_EQUAL_INT(0, metrics.getCounter("test_counter"), "New counter should start at 0");
    
    metrics.incrementCounter("test_counter", 5);
    ASSERT_EQUAL_INT(5, metrics.getCounter("test_counter"), "Counter should increment correctly");
    
    metrics.incrementCounter("test_counter", 3);
    ASSERT_EQUAL_INT(8, metrics.getCounter("test_counter"), "Counter should increment again");
    
    metrics.resetCounter("test_counter");
    ASSERT_EQUAL_INT(0, metrics.getCounter("test_counter"), "Counter should reset to 0");
    
    return true;
}

TEST_FUNCTION(test_metrics_gauges) {
    Metrics& metrics = Metrics::getInstance();
    
    // Test gauge operations
    ASSERT_EQUAL_DOUBLE(0.0, metrics.getGauge("test_gauge"), 0.001, "New gauge should start at 0");
    
    metrics.setGauge("test_gauge", 42.5);
    ASSERT_EQUAL_DOUBLE(42.5, metrics.getGauge("test_gauge"), 0.001, "Gauge should be set correctly");
    
    metrics.incrementGauge("test_gauge", 7.5);
    ASSERT_EQUAL_DOUBLE(50.0, metrics.getGauge("test_gauge"), 0.001, "Gauge should increment correctly");
    
    metrics.decrementGauge("test_gauge", 10.0);
    ASSERT_EQUAL_DOUBLE(40.0, metrics.getGauge("test_gauge"), 0.001, "Gauge should decrement correctly");
    
    return true;
}

TEST_FUNCTION(test_metrics_histograms) {
    Metrics& metrics = Metrics::getInstance();
    
    // Test histogram operations
    metrics.observeHistogram("test_histogram", 10.0);
    metrics.observeHistogram("test_histogram", 20.0);
    metrics.observeHistogram("test_histogram", 30.0);
    
    auto stats = metrics.getHistogramStatistics("test_histogram");
    ASSERT_EQUAL_INT(3, stats.count, "Histogram should have 3 observations");
    ASSERT_EQUAL_DOUBLE(60.0, stats.sum, 0.001, "Histogram sum should be 60");
    ASSERT_EQUAL_DOUBLE(10.0, stats.min, 0.001, "Histogram min should be 10");
    ASSERT_EQUAL_DOUBLE(30.0, stats.max, 0.001, "Histogram max should be 30");
    ASSERT_EQUAL_DOUBLE(20.0, stats.mean, 0.001, "Histogram mean should be 20");
    
    return true;
}

TEST_FUNCTION(test_metrics_export) {
    Metrics& metrics = Metrics::getInstance();
    
    // Add some test data
    metrics.incrementCounter("test_counter", 5);
    metrics.setGauge("test_gauge", 42.5);
    metrics.observeHistogram("test_histogram", 10.0);
    
    // Test JSON export
    std::string json = metrics.exportJSON();
    ASSERT_TRUE(json.find("test_counter") != std::string::npos, "JSON should contain counter");
    ASSERT_TRUE(json.find("test_gauge") != std::string::npos, "JSON should contain gauge");
    ASSERT_TRUE(json.find("test_histogram") != std::string::npos, "JSON should contain histogram");
    
    // Test Prometheus export
    std::string prometheus = metrics.exportPrometheus();
    ASSERT_TRUE(prometheus.find("test_counter") != std::string::npos, "Prometheus should contain counter");
    ASSERT_TRUE(prometheus.find("test_gauge") != std::string::npos, "Prometheus should contain gauge");
    ASSERT_TRUE(prometheus.find("test_histogram") != std::string::npos, "Prometheus should contain histogram");
    
    return true;
}

// Register all tests
void registerAllTests() {
    REGISTER_TEST("Config Defaults", test_config_defaults);
    REGISTER_TEST("Config Environment", test_config_environment);
    REGISTER_TEST("Config File", test_config_file);
    REGISTER_TEST("Logger Levels", test_logger_levels);
    REGISTER_TEST("Metrics Counters", test_metrics_counters);
    REGISTER_TEST("Metrics Gauges", test_metrics_gauges);
    REGISTER_TEST("Metrics Histograms", test_metrics_histograms);
    REGISTER_TEST("Metrics Export", test_metrics_export);
}

int main() {
    registerAllTests();
    return TestFramework::getInstance().runAllTests() ? 0 : 1;
}
