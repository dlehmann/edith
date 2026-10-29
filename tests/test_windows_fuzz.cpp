// Random window, tab and editing commands in Vim mode: the layout and the buffers stay
// consistent. (No commands that open dialogs, write files or run shell commands.)

#include <cstdlib>
#include <random>
#include <unistd.h>

#include "apptest.h"

static const char* const OPS[] = {
    "\x17s", "\x17v", "\x17n", "\x17" "c", "\x17o", "\x17w", "\x17W", "\x17h", "\x17j", "\x17k", "\x17l",
    "\x17+", "\x17-", "\x17<", "\x17>", "\x17=", "\x17_", "\x17|", "\x17T", "\x17p", "\x17t", "\x17" "b",
    "gt", "gT", "2gt", ":tabnew\r", ":tabclose!\r", ":tabonly!\r", ":new\r", ":vnew\r", ":q!\r", ":close!\r",
    ":only!\r", ":sp\r", ":vs\r", ":resize 3\r", ":vertical resize 10\r", ":tabmove 0\r", ":tabmove\r",
    "ihello\x1b[27u", "dd", "u", "\x12", "yyp", "G", "gg", "x", "ma", "'a", "Vjd", ":%s/l/L/g\r",
    "\x1b[6;5~", "\x1b[5;5~", "\x1b[17~", "\x1b[17;2~", "RESIZE", "RESIZE",
};

static bool consistent(AppT& t, std::string& why) {
    App& a = t.a();
    std::vector<Win*> ws = a.windows();
    if (ws.empty()) { why = "no windows"; return false; }
    if (std::find(ws.begin(), ws.end(), a.win) == ws.end()) { why = "active window not in the layout"; return false; }
    if (a.prev && std::find(ws.begin(), ws.end(), a.prev) == ws.end()) { why = "previous window"; return false; }
    std::vector<Doc*> used;
    for (Win* w : ws) {
        if (w->tabs.empty()) { why = "window without tabs"; return false; }
        if (w->cur < 0 || w->cur >= int(w->tabs.size())) { why = "shown tab"; return false; }
        if (w->tabs[size_t(w->cur)].doc != w->doc) { why = "shown tab and buffer differ"; return false; }
        for (size_t i = 0; i < w->tabs.size(); i++) {
            Doc* d = w->docOf(int(i));
            bool docOk = false;
            for (auto& o : a.docs) docOk |= o.get() == d;
            if (!docOk) { why = "tab without buffer"; return false; }
            used.push_back(d);
        }
        bool small = a.scr.w < 60 || a.scr.h < 20;  // tiny terminals may cut windows off
        if (!small && (w->x < 1 || w->y < 2 || w->x + w->w > a.scr.w - 1 || w->h < 1)) {
            why = "window rectangle";
            return false;
        }
        if (w->h < 1 || w->w < 1) { why = "empty window"; return false; }
    }
    for (auto& d : a.docs)
        if (std::find(used.begin(), used.end(), d.get()) == used.end()) { why = "buffer without tab"; return false; }
    Editor& e = a.ed;
    if (e.lines.empty() || e.cy < 0 || e.cy >= e.n() || e.cx < 0 || e.cx > int(e.lines[size_t(e.cy)].size())) {
        why = "cursor";
        return false;
    }
    for (auto& d : a.docs)
        if (d.get() != a.win->doc && d->store.lines.empty()) { why = "stored buffer without lines"; return false; }
    return true;
}

const char* const* fuzzOps(size_t& n) {
    n = sizeof OPS / sizeof *OPS;
    return OPS;
}

TEST(win_fuzz_random_window_commands) {
    const char* env = getenv("EDITH_FUZZ_RUNS");
    int runs = env ? atoi(env) / 10 : 40;
    if (env) alarm(0);
    for (int seed = 0; seed < runs; seed++) {
        std::mt19937 rng{unsigned(seed)};
        AppT t("line one\nline two\nline three");
        S.vim = true;
        t.a().vim.opt.write = false;
        t.a().vim.restricted = true;
        std::string done;
        for (int i = 0; i < 40 && !t.a().quit; i++) {
            const char* op = OPS[rng() % (sizeof OPS / sizeof *OPS)];
            done += op;
            done += " | ";
            if (std::string(op) == "RESIZE") {
                // the terminal changes its size
                static const int sizes[][2] = {{80, 25}, {40, 10}, {120, 40}, {45, 12}, {200, 60}};
                auto& sz = sizes[rng() % 5];
                t.a().scr.w = sz[0];
                t.a().scr.h = sz[1];
                t.a().scr.resize();
                t.a().layoutAll();
                t.draw();
            } else {
                t.keys(op);
            }
            std::string why;
            if (!consistent(t, why)) {
                testFail(__FILE__, __LINE__, "seed " + std::to_string(seed) + ": " + why + " after " + done);
                return;
            }
        }
        g_checks++;
    }
}
