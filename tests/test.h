#pragma once
// A minimal test framework: TEST(name) { CHECK(...); CHECK_EQ(a, b); }
//
// The test binary (make test) links every object of src/ except main.o. stdout is
// redirected to /dev/null while the tests run (the Screen writes there), so results go
// to the saved original stdout. stdin is a pipe that the application tests write key
// sequences into (see sendKeys()).

#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

#include "common.h"
#include "editor.h"
#include "utf8.h"

struct TestCase {
    const char* name;
    const char* file;
    void (*fn)();
};

std::vector<TestCase>& testRegistry();

struct TestReg {
    TestReg(const char* n, const char* f, void (*fn)()) { testRegistry().push_back({n, f, fn}); }
};

#define TEST(name)                                                  \
    static void test_##name();                                      \
    static TestReg reg_##name(#name, __FILE__, test_##name);        \
    static void test_##name()

extern int g_checks, g_failures;
extern FILE* g_out;  // the original stdout
void testFail(const char* file, int line, const std::string& msg);

inline std::string show(const u32& s) { return "\"" + toUtf8(s) + "\""; }
inline std::string show(const char32_t* s) { return show(u32(s)); }
inline std::string show(const std::string& s) { return "\"" + s + "\""; }
inline std::string show(const char* s) { return show(std::string(s)); }
inline std::string show(bool b) { return b ? "true" : "false"; }
inline std::string show(char32_t c) { return "'" + toUtf8(u32(1, c)) + "' (" + std::to_string(unsigned(c)) + ")"; }
inline std::string show(const Pos& p) { return "(" + std::to_string(p.y) + "," + std::to_string(p.x) + ")"; }
template <class T>
std::string show(const T& v) {
    std::ostringstream o;
    o << v;
    return o.str();
}
template <class T>
std::string show(const std::vector<T>& v) {
    std::string r = "[";
    for (size_t i = 0; i < v.size(); i++) r += (i ? ", " : "") + show(v[i]);
    return r + "]";
}

#define CHECK(c)                                                                  \
    do {                                                                          \
        g_checks++;                                                               \
        if (!(c)) testFail(__FILE__, __LINE__, "CHECK(" #c ")");                  \
    } while (0)

#define CHECK_EQ(a, b)                                                                                   \
    do {                                                                                                 \
        g_checks++;                                                                                      \
        const auto& _a = (a);                                                                            \
        const auto& _b = (b);                                                                            \
        if (!(_a == _b))                                                                                 \
            testFail(__FILE__, __LINE__,                                                                 \
                     std::string(#a " == " #b "\n      got:      ") + show(_a) + "\n      expected: " + show(_b)); \
    } while (0)

// Writes raw bytes to the test stdin (read by readKeyRaw()).
void sendKeys(const std::string& bytes);
// Discards all unread test input.
void drainKeys();
// Resets all global settings to their defaults.
void resetGlobals();
// A temporary directory for file tests (removed at exit).
std::string tempDir();
std::string readFile(const std::string& path);
void writeFile(const std::string& path, const std::string& data);
