#pragma once

#include <string>
#include <vector>
#include <functional>
#include <iostream>
#include <sstream>

class TestFramework {
public:
    struct TestCase {
        std::string name;
        std::function<bool()> test_function;
        bool passed = false;
        std::string error_message;
    };
    
    static TestFramework& getInstance();
    
    void addTest(const std::string& name, std::function<bool()> test_function);
    bool runAllTests();
    bool runTest(const std::string& name);
    
    void printResults() const;
    
    // Utility methods for tests
    static void assertTrue(bool condition, const std::string& message = "");
    static void assertFalse(bool condition, const std::string& message = "");
    static void assertEqual(const std::string& expected, const std::string& actual, const std::string& message = "");
    static void assertEqual(int expected, int actual, const std::string& message = "");
    static void assertEqual(double expected, double actual, double tolerance = 0.001, const std::string& message = "");
    static void assertNotNull(const void* ptr, const std::string& message = "");
    static void assertNull(const void* ptr, const std::string& message = "");
    
    // Test statistics
    int getTotalTests() const { return tests_.size(); }
    int getPassedTests() const;
    int getFailedTests() const { return getTotalTests() - getPassedTests(); }

private:
    TestFramework() = default;
    TestFramework(const TestFramework&) = delete;
    TestFramework& operator=(const TestFramework&) = delete;
    
    std::vector<TestCase> tests_;
    
    void runTestCase(TestCase& test_case);
    std::string formatTestResult(const TestCase& test_case) const;
};

// Macro for easy test registration
#define REGISTER_TEST(name, test_func) \
    TestFramework::getInstance().addTest(name, test_func)

// Macro for test functions
#define TEST_FUNCTION(name) \
    bool name()

// Macro for assertions that throw exceptions
#define ASSERT_TRUE(condition, message) \
    TestFramework::assertTrue(condition, message)

#define ASSERT_FALSE(condition, message) \
    TestFramework::assertFalse(condition, message)

#define ASSERT_EQUAL(expected, actual, message) \
    TestFramework::assertEqual(expected, actual, message)

#define ASSERT_EQUAL_INT(expected, actual, message) \
    TestFramework::assertEqual(expected, actual, message)

#define ASSERT_EQUAL_DOUBLE(expected, actual, tolerance, message) \
    TestFramework::assertEqual(expected, actual, tolerance, message)

#define ASSERT_NOT_NULL(ptr, message) \
    TestFramework::assertNotNull(ptr, message)

#define ASSERT_NULL(ptr, message) \
    TestFramework::assertNull(ptr, message)
