#include "config.h"
#include "logger.h"
#include "metrics.h"
#include <iostream>
#include <iomanip>

int main() {
    std::cout << "=== Millennium Daemon Metrics ===" << std::endl;
    std::cout << std::endl;
    
    Metrics& metrics = Metrics::getInstance();
    
    // Display counters
    std::cout << "COUNTERS:" << std::endl;
    std::cout << "---------" << std::endl;
    auto counters = metrics.getAllCounters();
    if (counters.empty()) {
        std::cout << "No counters recorded yet." << std::endl;
    } else {
        for (const auto& pair : counters) {
            std::cout << std::setw(20) << std::left << pair.first 
                      << ": " << pair.second << std::endl;
        }
    }
    std::cout << std::endl;
    
    // Display gauges
    std::cout << "GAUGES:" << std::endl;
    std::cout << "-------" << std::endl;
    auto gauges = metrics.getAllGauges();
    if (gauges.empty()) {
        std::cout << "No gauges recorded yet." << std::endl;
    } else {
        for (const auto& pair : gauges) {
            std::cout << std::setw(20) << std::left << pair.first 
                      << ": " << std::fixed << std::setprecision(2) << pair.second << std::endl;
        }
    }
    std::cout << std::endl;
    
    // Display histograms
    std::cout << "HISTOGRAMS:" << std::endl;
    std::cout << "-----------" << std::endl;
    auto histograms = metrics.getAllHistograms();
    if (histograms.empty()) {
        std::cout << "No histograms recorded yet." << std::endl;
    } else {
        for (const auto& pair : histograms) {
            std::cout << pair.first << ":" << std::endl;
            auto stats = pair.second;
            std::cout << "  Count: " << stats.count << std::endl;
            std::cout << "  Sum: " << std::fixed << std::setprecision(2) << stats.sum << std::endl;
            std::cout << "  Min: " << std::fixed << std::setprecision(2) << stats.min << std::endl;
            std::cout << "  Max: " << std::fixed << std::setprecision(2) << stats.max << std::endl;
            std::cout << "  Mean: " << std::fixed << std::setprecision(2) << stats.mean << std::endl;
            std::cout << "  Median: " << std::fixed << std::setprecision(2) << stats.median << std::endl;
            std::cout << "  95th percentile: " << std::fixed << std::setprecision(2) << stats.p95 << std::endl;
            std::cout << "  99th percentile: " << std::fixed << std::setprecision(2) << stats.p99 << std::endl;
            std::cout << std::endl;
        }
    }
    
    // Export JSON format
    std::cout << "JSON EXPORT:" << std::endl;
    std::cout << "------------" << std::endl;
    std::cout << metrics.exportJSON() << std::endl;
    
    return 0;
}
