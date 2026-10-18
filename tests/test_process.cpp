// Running external programs: runCommand() and runShell() (process.h).

#include <cstdlib>

#include "process.h"
#include "test.h"

TEST(process_run_command) {
    std::string out, err;
    int st;
    CHECK(runCommand("tr a-z A-Z", "", "abc\n", 5000, out, err, st));
    CHECK_EQ(out, "ABC\n");
    CHECK_EQ(st, 0);
    CHECK(runCommand("echo oops >&2; exit 3", "", "", 5000, out, err, st));
    CHECK_EQ(st, 3);
    CHECK_EQ(err, "oops\n");
    CHECK(runCommand("pwd -P", "/tmp", "", 5000, out, err, st));
    char* real = realpath("/tmp", nullptr);  // /private/tmp on macOS
    CHECK_EQ(out, std::string(real) + "\n");
    free(real);
    std::string big(2 << 20, 'x');  // more than a pipe buffer in both directions
    CHECK(runCommand("cat", "", big, 20000, out, err, st));
    CHECK_EQ(out.size(), big.size());
    CHECK(runCommand("head -c 3", "", big, 20000, out, err, st));  // stops reading early
    CHECK_EQ(out, "xxx");
    CHECK(runCommand("no-such-program-xyz", "", "", 5000, out, err, st));
    CHECK_EQ(st, 127);
    CHECK(runCommand("echo unlimited", "", "", -1, out, err, st));  // -1 = no time limit
    CHECK_EQ(out, "unlimited\n");
}

TEST(process_run_command_timeout) {
    std::string out, err;
    int st;
    auto t0 = Clock::now();
    CHECK(!runCommand("sleep 10", "", "", 300, out, err, st));  // stopped after the timeout
    CHECK(Clock::now() - t0 < std::chrono::seconds(5));
    // also when the program keeps writing output
    t0 = Clock::now();
    CHECK(!runCommand("while :; do echo x; sleep 0.01; done", "", "", 300, out, err, st));
    CHECK(Clock::now() - t0 < std::chrono::seconds(5));
}

TEST(process_run_shell) {
    std::string out;
    int st;
    CHECK(runShell("echo out; echo err >&2", "", out, st));  // both streams in out
    CHECK(out.find("out\n") != std::string::npos && out.find("err\n") != std::string::npos);
    CHECK_EQ(st, 0);
    CHECK(runShell("sort # a comment at the end", "b\na\n", out, st));
    CHECK_EQ(out, "a\nb\n");
    CHECK(runShell("exit 4", "", out, st));
    CHECK_EQ(st, 4);
    // no access to the terminal: reading stdin gets the input and then end of file
    CHECK(runShell("cat; echo done", "", out, st));
    CHECK_EQ(out, "done\n");
}
