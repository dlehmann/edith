#pragma once
// Helper for the Vim tests: a buffer with the Vim engine.
//
//   VT t("one tw‸o\nthree");   // "‸" marks the cursor (removed from the text)
//   t.k("dw");                 // keys in Vim notation: <Esc>, <CR>, <C-v>, <BS>, <Up> ...
//   CHECK_EQ(t.state(), "one ‸three");

#include "test.h"
#include "vim.h"

struct VT {
    Editor ed;
    VimHost host;
    Vim vim{ed, &host};

    explicit VT(const std::string& text = "") {
        S.autoindent = false;  // Vim's default
        S.vim = true;
        set(text);
    }

    void set(const std::string& text) {
        u32 t = U(text);
        int cy = 0, cx = 0;
        ed.lines.clear();
        u32 cur;
        for (char32_t c : t) {
            if (c == U'‸') {
                cy = int(ed.lines.size());
                cx = int(cur.size());
                continue;
            }
            if (c == '\n') {
                ed.lines.push_back(cur);
                cur.clear();
                continue;
            }
            cur += c;
        }
        ed.lines.push_back(cur);
        ed.cy = cy;
        ed.cx = cx;
        ed.undo.clear();
        ed.redo.clear();
        ed.modified = false;
        vim.reset();
        ed.cy = cy;
        ed.cx = cx;
        vim.curswant = vim.vcol(cy, cx);
    }

    VT& k(const char* keys) {
        vim.feedNotation(keys);
        return *this;
    }

    std::string text() const {
        std::string r;
        for (size_t i = 0; i < ed.lines.size(); i++) {
            if (i) r += "\n";
            r += toUtf8(ed.lines[i]);
        }
        return r;
    }

    // the text with "‸" at the cursor
    std::string state() const {
        std::string r;
        for (size_t i = 0; i < ed.lines.size(); i++) {
            if (i) r += "\n";
            u32 l = ed.lines[i];
            if (int(i) == ed.cy) l.insert(size_t(std::min(ed.cx, int(l.size()))), 1, U'‸');
            r += toUtf8(l);
        }
        return r;
    }

    Pos cur() const { return {ed.cy, ed.cx}; }
    std::string reg(char32_t r) { return toUtf8(vim.getReg(r).text()); }
    int regType(char32_t r) { return vim.getReg(r).type; }
    std::string msg() const { return toUtf8(vim.msg); }
    VimMode mode() const { return vim.mode; }
};
