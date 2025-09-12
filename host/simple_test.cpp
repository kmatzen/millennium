#include "config.h"
#include "logger.h"
#include <iostream>

int main() {
    std::cout << "Testing basic components..." << std::endl;
    
    // Test config
    Config& config = Config::getInstance();
    std::cout << "Config loaded successfully" << std::endl;
    std::cout << "Default call cost: " << config.getCallCostCents() << " cents" << std::endl;
    
    // Test logger
    MillenniumLogger& logger = MillenniumLogger::getInstance();
    logger.setLevel(MillenniumLogger::INFO);
    logger.info("Test", "Logger is working!");
    
    std::cout << "All basic components working!" << std::endl;
    return 0;
}
