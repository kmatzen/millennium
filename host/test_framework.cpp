#include "test_framework.h"
#include <stdexcept>
#include <iomanip>

TestFramework& TestFramework::getInstance() {
    static TestFramework instance;
    return instance;
}

void TestFramework::addTest(const std::string& name, std::function<bool()> test_function) {
    TestCase test_case;
    test_case.name = name;
    test_case.test_function = test_function;
    tests_.push_back(test_case);
}

bool TestFramework::runAllTests() {
    std::cout << "Running " << tests_.size() << " tests...\n\n";
    
    for (auto& test_case : tests_) {
        runTestCase(test_case);
    }
    
    printResults();
    return getFailedTests() == 0;
}

bool TestFramework::runTest(const std::string& name) {
    for (auto& test_case : tests_) {
        if (test_case.name == name) {
            runTestCase(test_case);
            std::cout << formatTestResult(test_case) << "\n";
            return test_case.passed;
        }
    }
    
    std::cout << "Test not found: " << name << "\n";
    return false;
}

void TestFramework::printResults() const {
    std::cout << "\n" << std::string(50, '=') << "\n";
    std::cout << "TEST RESULTS\n";
    std::cout << std::string(50, '=') << "\n";
    
    for (const auto& test_case : tests_) {
        std::cout << formatTestResult(test_case) << "\n";
    }
    
    std::cout << std::string(50, '=') << "\n";
    std::cout << "Total: " << getTotalTests() 
              << " | Passed: " << getPassedTests() 
              << " | Failed: " << getFailedTests() << "\n";
    std::cout << std::string(50, '=') << "\n";
}

void TestFramework::runTestCase(TestCase& test_case) {
    try {
        test_case.passed = test_case.test_function();
        if (!test_case.passed && test_case.error_message.empty()) {
            test_case.error_message = "Test returned false";
        }
    } catch (const std::exception& e) {
        test_case.passed = false;
        test_case.error_message = "Exception: " + std::string(e.what());
    } catch (...) {
        test_case.passed = false;
        test_case.error_message = "Unknown exception";
    }
}

std::string TestFramework::formatTestResult(const TestCase& test_case) const {
    std::ostringstream oss;
    
    if (test_case.passed) {
        oss << "[PASS] " << test_case.name;
    } else {
        oss << "[FAIL] " << test_case.name;
        if (!test_case.error_message.empty()) {
            oss << " - " << test_case.error_message;
        }
    }
    
    return oss.str();
}

int TestFramework::getPassedTests() const {
    int passed = 0;
    for (const auto& test_case : tests_) {
        if (test_case.passed) {
            passed++;
        }
    }
    return passed;
}

// Assertion methods
void TestFramework::assertTrue(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error("Assertion failed: expected true" + 
                                (message.empty() ? "" : " - " + message));
    }
}

void TestFramework::assertFalse(bool condition, const std::string& message) {
    if (condition) {
        throw std::runtime_error("Assertion failed: expected false" + 
                                (message.empty() ? "" : " - " + message));
    }
}

void TestFramework::assertEqual(const std::string& expected, const std::string& actual, const std::string& message) {
    if (expected != actual) {
        throw std::runtime_error("Assertion failed: expected '" + expected + 
                                "', got '" + actual + "'" + 
                                (message.empty() ? "" : " - " + message));
    }
}

void TestFramework::assertEqual(int expected, int actual, const std::string& message) {
    if (expected != actual) {
        throw std::runtime_error("Assertion failed: expected " + std::to_string(expected) + 
                                ", got " + std::to_string(actual) + 
                                (message.empty() ? "" : " - " + message));
    }
}

void TestFramework::assertEqual(double expected, double actual, double tolerance, const std::string& message) {
    if (std::abs(expected - actual) > tolerance) {
        std::ostringstream oss;
        oss << "Assertion failed: expected " << std::fixed << std::setprecision(6) << expected 
            << ", got " << actual << " (tolerance: " << tolerance << ")" 
            << (message.empty() ? "" : " - " + message);
        throw std::runtime_error(oss.str());
    }
}

void TestFramework::assertNotNull(const void* ptr, const std::string& message) {
    if (ptr == nullptr) {
        throw std::runtime_error("Assertion failed: expected non-null pointer" + 
                                (message.empty() ? "" : " - " + message));
    }
}

void TestFramework::assertNull(const void* ptr, const std::string& message) {
    if (ptr != nullptr) {
        throw std::runtime_error("Assertion failed: expected null pointer" + 
                                (message.empty() ? "" : " - " + message));
    }
}
