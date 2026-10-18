#pragma once
// Helper for the application tests: the whole application with key sequences that go
// through the real input decoder, and access to the screen buffer.

#include <memory>
#include <string>

#include "app.h"
#include "test.h"

namespace apptest {

inline const char* ESC = "\x1b[27u";   // Escape in the kitty protocol (no ambiguity with Alt)
inline const char* F10 = "\x1b[21~";
inline const char* UP = "\x1b[A";
inline const char* DOWN = "\x1b[B";
inline const char* RIGHT = "\x1b[C";
inline const char* LEFT = "\x1b[D";

inline std::string ctrl(char c) { return std::string(1, char(c & 0x1f)); }
inline std::string alt(char c) { return std::string("\x1b") + c; }
inline std::string click(int x, int y, int btn = 0, bool release = false) {
    return "\x1b[<" + std::to_string(btn) + ";" + std::to_string(x + 1) + ";" + std::to_string(y + 1) +
           (release ? "m" : "M");
}

struct AppT {
    std::unique_ptr<App> app;

    explicit AppT(const std::string& text = "") {
        app = std::make_unique<App>();
        g_scr = &app->scr;
        app->scr.w = 80;
        app->scr.h = 25;
        app->scr.resize();
        u32 t = U(text);
        app->ed.lines.clear();
        size_t s = 0;
        for (;;) {
            size_t q = t.find('\n', s);
            app->ed.lines.push_back(t.substr(s, q == u32::npos ? u32::npos : q - s));
            if (q == u32::npos) break;
            s = q + 1;
        }
        app->vim.reset();
        draw();
    }
    ~AppT() { g_scr = nullptr; }

    App& a() { return *app; }
    Editor& ed() { return app->ed; }

    void draw() {
        app->drawAll();
        app->placeCursor();
    }
    void keys(const std::string& bytes) {
        sendKeys(bytes);
        while (!app->quit && inputPending()) app->handleKey(getKey());
        draw();
    }
    std::string text() const {
        std::string r;
        for (size_t i = 0; i < app->ed.lines.size(); i++) r += (i ? "\n" : "") + toUtf8(app->ed.lines[i]);
        return r;
    }
    std::string row(int y) const {
        u32 r;
        const Screen& s = app->scr;
        for (int x = 0; x < s.w; x++) {
            char32_t c = s.buf[size_t(y * s.w + x)].ch;
            if (c) r += c;
        }
        while (!r.empty() && r.back() == ' ') r.pop_back();
        return toUtf8(r);
    }
    Attr cell(int x, int y) const {
        const Cell& c = app->scr.buf[size_t(y * app->scr.w + x)];
        return Attr{c.fg, c.bg};
    }
    Pos cur() const { return {app->ed.cy, app->ed.cx}; }
};

}  // namespace apptest
using namespace apptest;
