#pragma once

#include <string>
#include <map>
#include <atomic>
#include <chrono>
#include <mutex>
#include <vector>
#include <algorithm>

class Metrics {
public:
    struct Counter {
        std::atomic<uint64_t> value{0};
        std::chrono::steady_clock::time_point last_reset;
        
        Counter() : last_reset(std::chrono::steady_clock::now()) {}
        
        void increment(uint64_t amount = 1) { value.fetch_add(amount); }
        void reset() { 
            value.store(0); 
            last_reset = std::chrono::steady_clock::now();
        }
        uint64_t get() const { return value.load(); }
    };
    
    struct Gauge {
        std::atomic<double> value{0.0};
        std::chrono::steady_clock::time_point last_update;
        
        Gauge() : last_update(std::chrono::steady_clock::now()) {}
        
        void set(double val) { 
            value.store(val); 
            last_update = std::chrono::steady_clock::now();
        }
        void increment(double amount = 1.0) { 
            double current = value.load();
            while (!value.compare_exchange_weak(current, current + amount)) {
                // Retry if compare_exchange_weak failed
            }
            last_update = std::chrono::steady_clock::now();
        }
        void decrement(double amount = 1.0) { 
            double current = value.load();
            while (!value.compare_exchange_weak(current, current - amount)) {
                // Retry if compare_exchange_weak failed
            }
            last_update = std::chrono::steady_clock::now();
        }
        double get() const { return value.load(); }
    };
    
    struct Histogram {
        std::vector<double> values;
        std::mutex values_mutex;
        std::atomic<uint64_t> count{0};
        std::atomic<double> sum{0.0};
        std::atomic<double> min_value{std::numeric_limits<double>::max()};
        std::atomic<double> max_value{std::numeric_limits<double>::lowest()};
        
        void observe(double value) {
            {
                std::lock_guard<std::mutex> lock(values_mutex);
                values.push_back(value);
                // Keep only last 1000 values to prevent memory growth
                if (values.size() > 1000) {
                    values.erase(values.begin());
                }
            }
            
            count.fetch_add(1);
            double current_sum = sum.load();
            while (!sum.compare_exchange_weak(current_sum, current_sum + value)) {
                // Retry if compare_exchange_weak failed
            }
            
            // Update min/max atomically
            double current_min = min_value.load();
            while (value < current_min && !min_value.compare_exchange_weak(current_min, value)) {
                // Retry if compare_exchange_weak failed
            }
            
            double current_max = max_value.load();
            while (value > current_max && !max_value.compare_exchange_weak(current_max, value)) {
                // Retry if compare_exchange_weak failed
            }
        }
        
        struct Statistics {
            uint64_t count;
            double sum;
            double min;
            double max;
            double mean;
            double median;
            double p95;
            double p99;
        };
        
        Statistics getStatistics() const {
            Statistics stats;
            stats.count = count.load();
            stats.sum = sum.load();
            stats.min = min_value.load();
            stats.max = max_value.load();
            
            if (stats.count > 0) {
                stats.mean = stats.sum / stats.count;
            } else {
                stats.mean = 0.0;
            }
            
            // Create a copy of values to avoid const issues
            std::vector<double> values_copy;
            {
                std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(values_mutex));
                values_copy = values;
            }
            
            if (!values_copy.empty()) {
                std::sort(values_copy.begin(), values_copy.end());
                
                size_t size = values_copy.size();
                stats.median = (size % 2 == 0) 
                    ? (values_copy[size/2 - 1] + values_copy[size/2]) / 2.0
                    : values_copy[size/2];
                
                stats.p95 = values_copy[static_cast<size_t>(size * 0.95)];
                stats.p99 = values_copy[static_cast<size_t>(size * 0.99)];
            } else {
                stats.median = stats.p95 = stats.p99 = 0.0;
            }
            
            return stats;
        }
    };
    
    static Metrics& getInstance();
    
    // Counter operations
    void incrementCounter(const std::string& name, uint64_t amount = 1);
    void resetCounter(const std::string& name);
    uint64_t getCounter(const std::string& name) const;
    
    // Gauge operations
    void setGauge(const std::string& name, double value);
    void incrementGauge(const std::string& name, double amount = 1.0);
    void decrementGauge(const std::string& name, double amount = 1.0);
    double getGauge(const std::string& name) const;
    
    // Histogram operations
    void observeHistogram(const std::string& name, double value);
    Histogram::Statistics getHistogramStatistics(const std::string& name) const;
    
    // Utility methods
    std::map<std::string, uint64_t> getAllCounters() const;
    std::map<std::string, double> getAllGauges() const;
    std::map<std::string, Histogram::Statistics> getAllHistograms() const;
    
    void resetAll();
    
    // Export methods
    std::string exportPrometheus() const;
    std::string exportJSON() const;

private:
    Metrics() = default;
    Metrics(const Metrics&) = delete;
    Metrics& operator=(const Metrics&) = delete;
    
    mutable std::mutex counters_mutex_;
    mutable std::mutex gauges_mutex_;
    mutable std::mutex histograms_mutex_;
    
    std::map<std::string, Counter> counters_;
    std::map<std::string, Gauge> gauges_;
    std::map<std::string, Histogram> histograms_;
    
    std::chrono::steady_clock::time_point start_time_;
    
    // Helper methods
    std::string sanitizeMetricName(const std::string& name) const;
    std::string formatTimestamp() const;
};
