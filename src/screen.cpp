// The screen buffer: drawing primitives and the diff-based output to the terminal.

#include "screen.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include <sys/ioctl.h>
#include <unistd.h>

#include "utf8.h"

// Original CGA palette for true color terminals, followed by the extra colors (Col::DIMBLACK)
static const uint32_t CGA[17] = {
    0x000000, 0xAA0000, 0x00AA00, 0xAA5500, 0x0000AA, 0xAA00AA, 0x00AAAA, 0xAAAAAA,
    0x555555, 0xFF5555, 0x55FF55, 0xFFFF55, 0x5555FF, 0xFF55FF, 0x55FFFF, 0xFFFFFF,
    0x000077,  // DIMBLACK: black at 30 % over blue (0x0000AA)
};

// The ANSI color for terminals without true color: the extra colors become black.
static int ansi(int c) { return c < 16 ? c : 0; }

void Screen::resize() {
    winsize ws{};
    if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        w = ws.ws_col;
        h = ws.ws_row;
    } else {  // terminals (or serial lines) that do not report a size: $COLUMNS and $LINES
        const char* c = getenv("COLUMNS");
        const char* l = getenv("LINES");
        if (c && atoi(c) > 0) w = atoi(c);
        if (l && atoi(l) > 0) h = atoi(l);
    }
    buf.assign(size_t(w) * h, Cell{' ', 7, 0});
    old.assign(size_t(w) * h, Cell{0xFFFFFFFF, 0, 0});
    force = true;
}

void Screen::put(int x, int y, char32_t c, Attr a) {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    if (isControl(c)) c = '?';
    if (cw(c) == 2) {
        if (x + 1 >= w) {
            buf[y * w + x] = {' ', a.fg, a.bg};
            return;
        }
        buf[y * w + x] = {c, a.fg, a.bg};
        buf[y * w + x + 1] = {0, a.fg, a.bg};
        return;
    }
    buf[y * w + x] = {c, a.fg, a.bg};
}

int Screen::text(int x, int y, const u32& s, Attr a, int maxw) {
    int c = 0;
    for (char32_t ch : s) {
        int cwid = cw(ch);
        if (cwid == 0) continue;
        if (c + cwid > maxw) break;
        put(x + c, y, ch, a);
        c += cwid;
    }
    return c;
}

void Screen::fill(int x, int y, int n, Attr a, char32_t ch) {
    for (int i = 0; i < n; i++) put(x + i, y, ch, a);
}

void Screen::hot(int x, int y, const u32& s, Attr a, Attr ha) {
    int c = 0;
    bool nextHot = false;
    for (char32_t ch : s) {
        if (ch == '&' && !nextHot) { nextHot = true; continue; }
        put(x + c, y, ch, nextHot ? ha : a);
        nextHot = false;
        c += cw(ch);
    }
}

void Screen::recolor(int x, int y, Attr a) {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    buf[y * w + x].fg = a.fg;
    buf[y * w + x].bg = a.bg;
}

void Screen::box(int x, int y, int bw, int bh, Attr a, const u32& title, bool fillIn) {
    put(x, y, U'┌', a);
    fill(x + 1, y, bw - 2, a, U'─');
    put(x + bw - 1, y, U'┐', a);
    for (int r = 1; r < bh - 1; r++) {
        put(x, y + r, U'│', a);
        if (fillIn) fill(x + 1, y + r, bw - 2, a);
        put(x + bw - 1, y + r, U'│', a);
    }
    put(x, y + bh - 1, U'└', a);
    fill(x + 1, y + bh - 1, bw - 2, a, U'─');
    put(x + bw - 1, y + bh - 1, U'┘', a);
    if (!title.empty()) {
        u32 t = U" " + title + U" ";
        text(x + (bw - strWidth(t)) / 2, y, t, a, bw - 2);
    }
}

void Screen::shadow(int x, int y, int bw, int bh) {
    for (int r = y + 1; r <= y + bh; r++) {
        recolor(x + bw, r, Col::SHADOW);
        recolor(x + bw + 1, r, Col::SHADOW);
    }
    for (int c = x + 2; c < x + bw + 2; c++) recolor(c, y + bh, Col::SHADOW);
}

void Screen::sgr(std::string& o, int fg, int bg) {
    char t[64];
    if (truecolor) {
        uint32_t f = CGA[fg < 17 ? fg : 0], b = CGA[bg < 17 ? bg : 0];
        snprintf(t, sizeof t, "\x1b[0;38;2;%u;%u;%u;48;2;%u;%u;%um", f >> 16, (f >> 8) & 255, f & 255,
                 b >> 16, (b >> 8) & 255, b & 255);
        o += t;
        return;
    }
    fg = ansi(fg);
    bg = ansi(bg);
    if (boldBright)  // the bright colors as bold (Linux console, old terminals)
        snprintf(t, sizeof t, "\x1b[0;%s%d;%dm", fg >= 8 ? "1;" : "", 30 + (fg & 7), 40 + (bg & 7));
    else
        snprintf(t, sizeof t, "\x1b[0;%d;%dm", fg < 8 ? 30 + fg : 90 + fg - 8, bg < 8 ? 40 + bg : 100 + bg - 8);
    o += t;
}

void Screen::flush() {
    std::string o;
    o.reserve(size_t(w) * h * 2);
    o += "\x1b[?25l";
    int lfg = -1, lbg = -1;
    char t[64];
    for (int y = 0; y < h; y++) {
        int a = -1, b = -1;
        for (int x = 0; x < w; x++) {
            int i = y * w + x;
            if (force || !(buf[i] == old[i])) {
                if (a < 0) a = x;
                b = x;
            }
        }
        if (a < 0) continue;
        if (a > 0 && buf[y * w + a].ch == 0) a--;
        if (b + 1 < w) b++;
        snprintf(t, sizeof t, "\x1b[%d;%dH", y + 1, a + 1);
        o += t;
        for (int x = a; x <= b; x++) {
            Cell c = buf[y * w + x];
            if (c.fg != lfg || c.bg != lbg) {
                sgr(o, c.fg, c.bg);
                lfg = c.fg;
                lbg = c.bg;
            }
            char32_t ch = c.ch;
            int cwid = ch ? cw(ch) : 1;
            if (ch == 0 || cwid == 0) {
                ch = ' ';
            } else if (cwid == 2) {
                if (x + 1 < w && buf[y * w + x + 1].ch == 0) {
                    putUtf8(o, ch);
                    x++;
                    continue;
                }
                ch = ' ';
            }
            putUtf8(o, ch);
        }
    }
    old = buf;
    force = false;
    if (cvis) {
        snprintf(t, sizeof t, "\x1b[%d;%dH\x1b[%d q\x1b[?25h", cy + 1, cx + 1, cshape);
        o += t;
    }
    writeAll(o);
}

Screen* g_scr = nullptr;

Key getKey() {
    Key k = macKey(readKeyRaw());
    if (k.code == K_RESIZE && g_scr) g_scr->resize();
    if (k.code != K_NONE && k.code != K_ALTMENU && k.code != K_RESIZE && k.code != K_TIMEOUT) g_altPending = false;
    return k;
}
