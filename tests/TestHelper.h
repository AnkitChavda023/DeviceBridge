#pragma once
#include <exception>
#include <functional>
#include <iostream>
#include <string>
#include <vector>
#include "util/Logger.h"

struct TestCase {
    std::string name;
    std::function<void()> body;
};

inline std::vector<TestCase>& allTests() {
    static std::vector<TestCase> tests;
    return tests;
}

inline int& failureCount() {
    static int count = 0;
    return count;
}

struct TestRegistrar {
    TestRegistrar(const char* name, std::function<void()> body) {
        TestCase t;
        t.name = name;
        t.body = body;
        allTests().push_back(t);
    }
};

#define TEST(name)                                  \
    static void name();                             \
    static TestRegistrar registrar_##name(#name, name); \
    static void name()

#define CHECK(condition)                                                              \
    do {                                                                              \
        if (!(condition)) {                                                           \
            std::cout << "    FAIL line " << __LINE__ << ": " #condition << std::endl; \
            failureCount()++;                                                         \
        }                                                                             \
    } while (0)

#define CHECK_EQ(actual, expected)                                                                   \
    do {                                                                                             \
        if (!((actual) == (expected))) {                                                             \
            std::cout << "    FAIL line " << __LINE__ << ": " #actual " == " #expected << std::endl; \
            failureCount()++;                                                                        \
        }                                                                                            \
    } while (0)

#define CHECK_NEAR(actual, expected, tolerance)                                                      \
    do {                                                                                             \
        double a_ = (actual);                                                                        \
        double e_ = (expected);                                                                      \
        if (a_ < e_ - (tolerance) || a_ > e_ + (tolerance)) {                                        \
            std::cout << "    FAIL line " << __LINE__ << ": " #actual " is " << a_ << ", expected " << e_ \
                      << std::endl;                                                                  \
            failureCount()++;                                                                        \
        }                                                                                            \
    } while (0)

#define CHECK_OK(status)                                                                             \
    do {                                                                                             \
        Status s_ = (status);                                                                        \
        if (!s_.ok()) {                                                                              \
            std::cout << "    FAIL line " << __LINE__ << ": " #status " -> " << s_.toString() << std::endl; \
            failureCount()++;                                                                        \
        }                                                                                            \
    } while (0)

#define CHECK_CODE(status, expectedCode)                                                             \
    do {                                                                                             \
        Status s_ = (status);                                                                        \
        if (s_.code != (expectedCode)) {                                                             \
            std::cout << "    FAIL line " << __LINE__ << ": " #status " -> " << s_.toString()        \
                      << ", expected " << errorName(expectedCode) << std::endl;                      \
            failureCount()++;                                                                        \
        }                                                                                            \
    } while (0)

inline int runAllTests(const char* suite) {
    Logger::setLevel(LogLevel::Off);
    int failedTests = 0;
    std::cout << "== " << suite << " ==" << std::endl;
    for (size_t i = 0; i < allTests().size(); i++) {
        int before = failureCount();
        try {
            allTests()[i].body();
        } catch (const std::exception& e) {
            std::cout << "    FAIL exception: " << e.what() << std::endl;
            failureCount()++;
        }
        bool failed = failureCount() != before;
        if (failed) {
            failedTests++;
        }
        std::cout << (failed ? "[FAIL] " : "[PASS] ") << allTests()[i].name << std::endl;
    }
    std::cout << suite << ": " << (allTests().size() - failedTests) << " passed, " << failedTests << " failed"
              << std::endl;
    return failedTests == 0 ? 0 : 1;
}

#define TEST_MAIN(suite) \
    int main() { return runAllTests(suite); }
