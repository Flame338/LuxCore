#pragma once

#include <cmath>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace testing {

struct TestCase {
    std::string name;
    void (*run)();
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

inline int& failure_count() {
    static int failures = 0;
    return failures;
}

struct Registrar {
    Registrar(const char* name, void (*run)()) { registry().push_back({name, run}); }
};

template <typename T>
std::string show(const T& value) {
    if constexpr (requires(std::ostringstream& out) { out << value; }) {
        std::ostringstream out;
        out << value;
        return out.str();
    } else if constexpr (requires { value.begin(); value.end(); }) {
        std::string text = "[";
        for (const auto& element : value) {
            text += show(element) + ",";
        }
        return text + "]";
    } else {
        return "<unprintable>";
    }
}

template <typename A, typename B>
void check_equal(const A& actual, const B& expected, const char* actual_text,
                 const char* expected_text, const char* file, int line) {
    if (!(actual == expected)) {
        std::cout << file << ":" << line << ": CHECK_EQ(" << actual_text << ", " << expected_text
                  << ") failed\n    actual:   " << show(actual) << "\n    expected: "
                  << show(expected) << "\n";
        failure_count()++;
    }
}

inline void check_near(double actual, double expected, double tolerance, const char* actual_text,
                       const char* expected_text, const char* file, int line) {
    if (std::fabs(actual - expected) > tolerance) {
        std::cout << file << ":" << line << ": CHECK_NEAR(" << actual_text << ", " << expected_text
                  << ") failed\n    actual:   " << actual << "\n    expected: " << expected << "\n";
        failure_count()++;
    }
}

}

#define TEST(name)                                                  \
    static void name();                                             \
    static testing::Registrar registrar_##name(#name, name);        \
    static void name()

#define CHECK(condition)                                                                   \
    do {                                                                                   \
        if (!(condition)) {                                                                \
            std::cout << __FILE__ << ":" << __LINE__ << ": CHECK(" #condition ") failed\n"; \
            testing::failure_count()++;                                                    \
        }                                                                                  \
    } while (0)

#define CHECK_EQ(actual, expected) \
    testing::check_equal((actual), (expected), #actual, #expected, __FILE__, __LINE__)

#define CHECK_NEAR(actual, expected, tolerance) \
    testing::check_near((actual), (expected), (tolerance), #actual, #expected, __FILE__, __LINE__)
