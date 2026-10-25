#pragma once

// A minimal test harness: TEST_CASE registers a function, CHECK records
// failures without aborting, main() runs everything.

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace test
{
struct Case
{
    const char *name;
    std::function<void()> run;
};

inline std::vector<Case>& registry()
{
    static std::vector<Case> cases;
    return cases;
}

inline int failures = 0;

struct Register
{
    Register(const char *name, std::function<void()> fn)
    {
        registry().push_back({name, std::move(fn)});
    }
};

inline std::string data(const std::string& file)
{
    return std::string(TEST_DATA_DIR) + "/" + file;
}
} // namespace test

#define TEST_CASE(name) \
    static void name(); \
    static test::Register register_ ## name(#name, name); \
    static void name()

#define CHECK(cond) \
    do { \
        if (!(cond)) \
        { \
            std::printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++test::failures; \
        } \
    } while (0)

#define CHECK_NEAR(a, b) CHECK(std::abs(double(a) - double(b)) < 1e-6)
