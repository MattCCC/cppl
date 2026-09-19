#pragma once

#include <functional>
#include <string>
#include <vector>

namespace cppl::testing {

struct TestCase {
    std::string name;
    std::function<void()> body;
};

std::vector<TestCase>& registry();

struct Registrar {
    Registrar(std::string name, std::function<void()> body);
};

[[noreturn]] void fail(const char* file, int line, const std::string& message);

}  // namespace cppl::testing

#define CPPL_TEST(name)                                                        \
    static void name();                                                        \
    static const ::cppl::testing::Registrar cppl_registrar_##name(#name, name); \
    static void name()

#define CPPL_CHECK(condition)                                                  \
    do {                                                                       \
        if (!(condition)) {                                                    \
            ::cppl::testing::fail(__FILE__, __LINE__, "failed: " #condition);  \
        }                                                                      \
    } while (false)

#define CPPL_CHECK_EQ(actual, expected)                                        \
    do {                                                                       \
        if (!((actual) == (expected))) {                                       \
            ::cppl::testing::fail(__FILE__, __LINE__,                          \
                                  "failed: " #actual " == " #expected);        \
        }                                                                      \
    } while (false)
