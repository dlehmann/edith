// Test runner: edith-tests [name-filter ...]

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

#include "i18n.h"
#include "menus.h"
#include "terminal.h"
#include "test.h"

int g_checks = 0, g_failures = 0;
FILE* g_out = stdout;
static const char* g_curTest = "";
static int g_pipeW = -1;
static std::string g_tmp;

std::vector<TestCase>& testRegistry() {
    static std::vector<TestCase> r;
    return r;
}

void testFail(const char* file, int line, const std::string& msg) {
    g_failures++;
    fprintf(g_out, "  FAIL %s (%s:%d)\n      %s\n", g_curTest, file, line, msg.c_str());
}

void sendKeys(const std::string& bytes) {
    size_t off = 0;
    while (off < bytes.size()) {
        ssize_t n = write(g_pipeW, bytes.data() + off, bytes.size() - off);
        if (n <= 0) break;
        off += size_t(n);
    }
}

void drainKeys() {
    while (inputPending()) readKeyRaw();
}

void resetGlobals() {
    S = Settings();
    g_uiLang = UiLang::English;
    g_macKeys = false;  // the same keys on every platform; test_macos.cpp turns the Mac keys on
    buildMenus();
}

std::string tempDir() { return g_tmp; }

std::string readFile(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return "<missing>";
    std::string s;
    char b[4096];
    size_t n;
    while ((n = fread(b, 1, sizeof b, f)) > 0) s.append(b, n);
    fclose(f);
    return s;
}

void writeFile(const std::string& path, const std::string& data) {
    FILE* f = fopen(path.c_str(), "wb");
    fwrite(data.data(), 1, data.size(), f);
    fclose(f);
}

static void onAlarm(int) {
    const char m1[] = "\n  TIMEOUT in test ";
    if (write(2, m1, sizeof m1 - 1) < 0) {}
    if (write(2, g_curTest, strlen(g_curTest)) < 0) {}
    if (write(2, "\n", 1) < 0) {}
    _exit(2);
}

int main(int argc, char** argv) {
    // results go to the original stdout, the editor's screen output to /dev/null
    g_out = fdopen(dup(1), "w");
    setvbuf(g_out, nullptr, _IOLBF, 0);
    int devnull = open("/dev/null", O_WRONLY);
    dup2(devnull, 1);
    // stdin is a pipe that the tests write key sequences into
    int p[2];
    if (pipe(p) != 0) return 2;
    dup2(p[0], 0);
    g_pipeW = p[1];
    fcntl(g_pipeW, F_SETFL, O_NONBLOCK);
    char tmpl[] = "/tmp/edith-tests-XXXXXX";
    if (!mkdtemp(tmpl)) return 2;
    g_tmp = tmpl;
    signal(SIGALRM, onAlarm);
    signal(SIGPIPE, SIG_IGN);

    int run = 0, failedTests = 0;
    for (auto& t : testRegistry()) {
        bool want = argc < 2;
        for (int i = 1; i < argc; i++)
            if (strstr(t.name, argv[i]) || strstr(t.file, argv[i])) want = true;
        if (!want) continue;
        g_curTest = t.name;
        resetGlobals();
        drainKeys();
        int before = g_failures;
        alarm(20);
        t.fn();
        alarm(0);
        run++;
        if (g_failures != before) failedTests++;
    }
    std::string rm = "rm -rf '" + g_tmp + "'";
    if (system(rm.c_str()) != 0) {}
    fprintf(g_out, "%d tests, %d checks, %d failed checks in %d tests\n", run, g_checks, g_failures, failedTests);
    return g_failures ? 1 : 0;
}
