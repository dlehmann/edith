// Running external programs (see process.h).

#include "process.h"

#include <cerrno>
#include <chrono>
#include <csignal>

#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

bool runCommand(const std::string& cmd, const std::string& cwd, const std::string& input, int timeoutMs,
                std::string& out, std::string& err, int& status) {
    out.clear();
    err.clear();
    status = -1;
    int pin[2], pout[2], perr[2];
    if (pipe(pin) != 0) return false;
    if (pipe(pout) != 0) { close(pin[0]); close(pin[1]); return false; }
    if (pipe(perr) != 0) { close(pin[0]); close(pin[1]); close(pout[0]); close(pout[1]); return false; }
    pid_t pid = fork();
    if (pid < 0) {
        for (int fd : {pin[0], pin[1], pout[0], pout[1], perr[0], perr[1]}) close(fd);
        return false;
    }
    if (pid == 0) {
        dup2(pin[0], 0);
        dup2(pout[1], 1);
        dup2(perr[1], 2);
        for (int fd : {pin[0], pin[1], pout[0], pout[1], perr[0], perr[1]}) close(fd);
        setsid();  // no access to the editor's terminal
        if (!cwd.empty() && chdir(cwd.c_str()) != 0) {}
        signal(SIGPIPE, SIG_DFL);
        execl("/bin/sh", "sh", "-c", cmd.c_str(), (char*)nullptr);
        _exit(127);
    }
    close(pin[0]);
    close(pout[1]);
    close(perr[1]);
    void (*oldPipe)(int) = signal(SIGPIPE, SIG_IGN);
    fcntl(pin[1], F_SETFL, O_NONBLOCK);
    size_t written = 0;
    int win = pin[1];
    if (input.empty()) { close(win); win = -1; }
    bool outOpen = true, errOpen = true, timedOut = false;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (outOpen || errOpen) {
        pollfd fds[3];
        int nf = 0, iOut = -1, iErr = -1, iIn = -1;
        if (outOpen) { iOut = nf; fds[nf++] = {pout[0], POLLIN, 0}; }
        if (errOpen) { iErr = nf; fds[nf++] = {perr[0], POLLIN, 0}; }
        if (win >= 0) { iIn = nf; fds[nf++] = {win, POLLOUT, 0}; }
        int r = poll(fds, nfds_t(nf), 100);
        if (r < 0 && errno != EINTR) break;
        if (timeoutMs >= 0 && std::chrono::steady_clock::now() >= deadline) { timedOut = true; break; }
        if (r <= 0) continue;
        char buf[65536];
        if (iOut >= 0 && (fds[iOut].revents & (POLLIN | POLLHUP | POLLERR))) {
            ssize_t n = read(pout[0], buf, sizeof buf);
            if (n > 0) out.append(buf, size_t(n));
            else outOpen = false;
        }
        if (iErr >= 0 && (fds[iErr].revents & (POLLIN | POLLHUP | POLLERR))) {
            ssize_t n = read(perr[0], buf, sizeof buf);
            if (n > 0) err.append(buf, size_t(n));
            else errOpen = false;
        }
        if (iIn >= 0 && (fds[iIn].revents & (POLLOUT | POLLERR | POLLHUP))) {
            ssize_t n = (fds[iIn].revents & POLLOUT) ? write(win, input.data() + written, input.size() - written) : -1;
            if (n > 0) written += size_t(n);
            if (n < 0 || written >= input.size()) { close(win); win = -1; }
        }
    }
    if (win >= 0) close(win);
    close(pout[0]);
    close(perr[0]);
    if (timedOut) {
        kill(pid, SIGKILL);
        kill(-pid, SIGKILL);
    }
    int st = 0;
    while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
    signal(SIGPIPE, oldPipe);
    if (timedOut) return false;
    status = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
    return true;
}

bool runShell(const std::string& cmd, const std::string& input, std::string& out, int& status) {
    std::string err;
    // the newline ends a comment at the end of the command
    return runCommand("(" + cmd + "\n) 2>&1", "", input, -1, out, err, status);
}
