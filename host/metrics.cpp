#include "metrics.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>

Metrics& Metrics::getInstance() {
    static Metrics instance;
    return instance;
}

void Metrics::incrementCounter(const std::string& name, uint64_t amount) {
    std::lock_guard<std::mutex> lock(counters_mutex_);
    counters_[name].increment(amount);
}

void Metrics::resetCounter(const std::string& name) {
    std::lock_guard<std::mutex> lock(counters_mutex_);
    auto it = counters_.find(name);
    if (it != counters_.end()) {
        it->second.reset();
    }
}

uint64_t Metrics::getCounter(const std::string& name) const {
    std::lock_guard<std::mutex> lock(counters_mutex_);
    auto it = counters_.find(name);
    return (it != counters_.end()) ? it->second.get() : 0;
}

void Metrics::setGauge(const std::string& name, double value) {
    std::lock_guard<std::mutex> lock(gauges_mutex_);
    gauges_[name].set(value);
}

void Metrics::incrementGauge(const std::string& name, double amount) {
    std::lock_guard<std::mutex> lock(gauges_mutex_);
    gauges_[name].increment(amount);
}

void Metrics::decrementGauge(const std::string& name, double amount) {
    std::lock_guard<std::mutex> lock(gauges_mutex_);
    gauges_[name].decrement(amount);
}

double Metrics::getGauge(const std::string& name) const {
    std::lock_guard<std::mutex> lock(gauges_mutex_);
    auto it = gauges_.find(name);
    return (it != gauges_.end()) ? it->second.get() : 0.0;
}

void Metrics::observeHistogram(const std::string& name, double value) {
    std::lock_guard<std::mutex> lock(histograms_mutex_);
    histograms_[name].observe(value);
}

Metrics::Histogram::Statistics Metrics::getHistogramStatistics(const std::string& name) const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(histograms_mutex_));
    auto it = histograms_.find(name);
    if (it != histograms_.end()) {
        return it->second.getStatistics();
    }
    
    Histogram::Statistics empty_stats;
    empty_stats.count = 0;
    empty_stats.sum = empty_stats.min = empty_stats.max = 0.0;
    empty_stats.mean = empty_stats.median = empty_stats.p95 = empty_stats.p99 = 0.0;
    return empty_stats;
}

std::map<std::string, uint64_t> Metrics::getAllCounters() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(counters_mutex_));
    std::map<std::string, uint64_t> result;
    for (const auto& pair : counters_) {
        result[pair.first] = pair.second.get();
    }
    return result;
}

std::map<std::string, double> Metrics::getAllGauges() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(gauges_mutex_));
    std::map<std::string, double> result;
    for (const auto& pair : gauges_) {
        result[pair.first] = pair.second.get();
    }
    return result;
}

std::map<std::string, Metrics::Histogram::Statistics> Metrics::getAllHistograms() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(histograms_mutex_));
    std::map<std::string, Histogram::Statistics> result;
    for (const auto& pair : histograms_) {
        result[pair.first] = pair.second.getStatistics();
    }
    return result;
}

void Metrics::resetAll() {
    {
        std::lock_guard<std::mutex> lock(counters_mutex_);
        for (auto& pair : counters_) {
            pair.second.reset();
        }
    }
    
    {
        std::lock_guard<std::mutex> lock(gauges_mutex_);
        for (auto& pair : gauges_) {
            pair.second.set(0.0);
        }
    }
    
    {
        std::lock_guard<std::mutex> lock(histograms_mutex_);
        histograms_.clear();
    }
}

std::string Metrics::exportPrometheus() const {
    std::ostringstream oss;
    
    // Add timestamp
    oss << "# HELP millennium_metrics_start_time Start time of the metrics collection\n";
    oss << "# TYPE millennium_metrics_start_time counter\n";
    oss << "millennium_metrics_start_time " << std::chrono::duration_cast<std::chrono::seconds>(
        start_time_.time_since_epoch()).count() << "\n\n";
    
    // Export counters
    auto counters = getAllCounters();
    for (const auto& pair : counters) {
        std::string name = sanitizeMetricName(pair.first);
        oss << "# HELP " << name << " Counter metric\n";
        oss << "# TYPE " << name << " counter\n";
        oss << name << " " << pair.second << "\n";
    }
    
    if (!counters.empty()) {
        oss << "\n";
    }
    
    // Export gauges
    auto gauges = getAllGauges();
    for (const auto& pair : gauges) {
        std::string name = sanitizeMetricName(pair.first);
        oss << "# HELP " << name << " Gauge metric\n";
        oss << "# TYPE " << name << " gauge\n";
        oss << name << " " << std::fixed << std::setprecision(2) << pair.second << "\n";
    }
    
    if (!gauges.empty()) {
        oss << "\n";
    }
    
    // Export histograms
    auto histograms = getAllHistograms();
    for (const auto& pair : histograms) {
        std::string name = sanitizeMetricName(pair.first);
        Histogram::Statistics stats = pair.second;
            
            oss << "# HELP " << name << "_count Histogram count\n";
            oss << "# TYPE " << name << "_count counter\n";
            oss << name << "_count " << stats.count << "\n";
            
            oss << "# HELP " << name << "_sum Histogram sum\n";
            oss << "# TYPE " << name << "_sum counter\n";
            oss << name << "_sum " << std::fixed << std::setprecision(2) << stats.sum << "\n";
            
            oss << "# HELP " << name << "_min Histogram minimum\n";
            oss << "# TYPE " << name << "_min gauge\n";
            oss << name << "_min " << std::fixed << std::setprecision(2) << stats.min << "\n";
            
            oss << "# HELP " << name << "_max Histogram maximum\n";
            oss << "# TYPE " << name << "_max gauge\n";
            oss << name << "_max " << std::fixed << std::setprecision(2) << stats.max << "\n";
            
            oss << "# HELP " << name << "_mean Histogram mean\n";
            oss << "# TYPE " << name << "_mean gauge\n";
            oss << name << "_mean " << std::fixed << std::setprecision(2) << stats.mean << "\n";
            
            oss << "# HELP " << name << "_median Histogram median\n";
            oss << "# TYPE " << name << "_median gauge\n";
            oss << name << "_median " << std::fixed << std::setprecision(2) << stats.median << "\n";
            
            oss << "# HELP " << name << "_p95 Histogram 95th percentile\n";
            oss << "# TYPE " << name << "_p95 gauge\n";
            oss << name << "_p95 " << std::fixed << std::setprecision(2) << stats.p95 << "\n";
            
            oss << "# HELP " << name << "_p99 Histogram 99th percentile\n";
            oss << "# TYPE " << name << "_p99 gauge\n";
            oss << name << "_p99 " << std::fixed << std::setprecision(2) << stats.p99 << "\n\n";
    }
    
    return oss.str();
}

std::string Metrics::exportJSON() const {
    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"timestamp\": \"" << formatTimestamp() << "\",\n";
    oss << "  \"counters\": {\n";
    
    // Export counters
    auto counters = getAllCounters();
    bool first = true;
    for (const auto& pair : counters) {
        if (!first) oss << ",\n";
        oss << "    \"" << pair.first << "\": " << pair.second;
        first = false;
    }
    
    oss << "\n  },\n";
    oss << "  \"gauges\": {\n";
    
    // Export gauges
    auto gauges = getAllGauges();
    first = true;
    for (const auto& pair : gauges) {
        if (!first) oss << ",\n";
        oss << "    \"" << pair.first << "\": " << std::fixed << std::setprecision(2) << pair.second;
        first = false;
    }
    
    oss << "\n  },\n";
    oss << "  \"histograms\": {\n";
    
    // Export histograms
    auto histograms = getAllHistograms();
    first = true;
    for (const auto& pair : histograms) {
        if (!first) oss << ",\n";
        Histogram::Statistics stats = pair.second;
        oss << "    \"" << pair.first << "\": {\n";
        oss << "      \"count\": " << stats.count << ",\n";
        oss << "      \"sum\": " << std::fixed << std::setprecision(2) << stats.sum << ",\n";
        oss << "      \"min\": " << std::fixed << std::setprecision(2) << stats.min << ",\n";
        oss << "      \"max\": " << std::fixed << std::setprecision(2) << stats.max << ",\n";
        oss << "      \"mean\": " << std::fixed << std::setprecision(2) << stats.mean << ",\n";
        oss << "      \"median\": " << std::fixed << std::setprecision(2) << stats.median << ",\n";
        oss << "      \"p95\": " << std::fixed << std::setprecision(2) << stats.p95 << ",\n";
        oss << "      \"p99\": " << std::fixed << std::setprecision(2) << stats.p99 << "\n";
        oss << "    }";
        first = false;
    }
    
    oss << "\n  }\n";
    oss << "}\n";
    
    return oss.str();
}

std::string Metrics::sanitizeMetricName(const std::string& name) const {
    std::string sanitized = name;
    
    // Replace invalid characters with underscores
    std::replace_if(sanitized.begin(), sanitized.end(),
        [](char c) { return !std::isalnum(c) && c != '_'; }, '_');
    
    // Ensure it starts with a letter
    if (!sanitized.empty() && !std::isalpha(sanitized[0])) {
        sanitized = "metric_" + sanitized;
    }
    
    return sanitized;
}

std::string Metrics::formatTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time_t), "%Y-%m-%dT%H:%M:%S");
    
    return oss.str();
}
