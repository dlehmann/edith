// Random key sequences for the Vim engine: no crash, and the cursor and buffer stay valid.
// File writes and shell commands are disabled ('nowrite', restricted mode).

#include <cstdlib>
#include <random>
#include <unistd.h>

#include "vimtest.h"

static const char* const VOCAB[] = {
    "h", "j", "k", "l", "w", "b", "e", "W", "B", "E", "ge", "0", "^", "$", "g_", "gg", "G", "f", "F", "t", "T",
    ";", ",", "%", "{", "}", "(", ")", "n", "N", "*", "#", "x", "X", "d", "c", "y", "p", "P", "J", "gJ", "~",
    "r", "u", "<C-r>", "o", "O", "i", "a", "I", "A", "R", "v", "V", "<C-v>", "<Esc>", "<CR>", "<BS>", ".",
    "q", "@", "\"", "a", "b", "1", "2", "3", "0", " ", "x", "é", "日", "\t", "(", ")", "{", "}", "\"", "'",
    "iw", "aw", "ip", "i(", "a\"", "it", "<C-a>", "<C-x>", "<C-o>", "<C-w>", "<C-u>", "<C-t>", "<C-d>",
    "<Del>", "<Left>", "<Right>", "<Up>", "<Down>", "<Home>", "<End>", "gv", "o", "gu", "gU", "g~", ">", "<",
    "=", "gq", "s", "S", "C", "D", "Y", "m", "'", "`", "<C-e>", "<C-y>", "<C-f>", "<C-b>", "zz", "H", "L", "M",
    "/a<CR>", "?b<CR>", ":s/a/b/g<CR>", ":%s/\\w\\+/X/<CR>", ":g/a/d<CR>", ":v/b/s/$/!/<CR>", ":%norm Ax<CR>",
    ":2,3m0<CR>", ":t.<CR>", ":%j<CR>", ":sort<CR>", ":%>|%<<CR>", ":nnoremap Q dd<CR>", "Q", ":undo<CR>",
    ":set et ts=4<CR>", ":set noet<CR>", ":set tw=10<CR>", ":set tw=0<CR>", "<C-k>", "<C-n>", "<C-p>",
    "<Insert>", "<C-g>", ":", "/", "<C-c>",
};

static void checkInvariants(VT& t, int seed, const std::string& keys) {
    bool ok = !t.ed.lines.empty() && t.ed.cy >= 0 && t.ed.cy < t.ed.n() && t.ed.cx >= 0 &&
              t.ed.cx <= int(t.ed.lines[size_t(t.ed.cy)].size());
    if (ok && (t.mode() == VM_NORMAL) && !t.vim.insOneCmd)
        ok = t.ed.cx <= std::max(0, int(t.ed.lines[size_t(t.ed.cy)].size()) - 1);
    if (!ok)
        testFail(__FILE__, __LINE__, "invalid state after seed " + std::to_string(seed) + " keys: " + keys +
                                         "\n      state: " + t.state());
}

TEST(vim_fuzz_random_keys) {
    const char* env = getenv("EDITH_FUZZ_RUNS");
    const int RUNS = env ? atoi(env) : 400;
    const char* first = getenv("EDITH_FUZZ_SEED");  // EDITH_FUZZ_RUNS / EDITH_FUZZ_SEED for longer runs
    int seed0 = first ? atoi(first) : 0;
    if (env) alarm(0);  // a long run: no watchdog
    for (int seed = seed0; seed < seed0 + RUNS; seed++) {
        std::mt19937 rng{unsigned(seed)};
        VT t("‸abc def\n  (x) \"q\" <b>t</b>\n\nfoo 12 bar\n\tend");
        // random keys can form ":w name" or ":!cmd": no files and no shell commands
        t.vim.opt.write = false;
        t.vim.restricted = true;
        std::string all;
        int n = 60;
        for (int i = 0; i < n; i++) {
            const char* k = VOCAB[rng() % (sizeof VOCAB / sizeof *VOCAB)];
            all += k;
            t.k(k);
            checkInvariants(t, seed, all);
            if (g_failures) return;
        }
        t.k("<Esc><Esc>");
        checkInvariants(t, seed, all);
        g_checks++;
    }
}
