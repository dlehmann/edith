// Drawing of the menu bar, the editor window and the status line.

#include "app.h"

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <cstring>

#include "i18n.h"
#include "languages.h"
#include "lexer.h"
#include "menus.h"
#include "paths.h"
#include "utf8.h"

int App::hlStateAt(Editor& e, Doc& d, int y) {
    consumeDirty(e, d);
    d.hlValid = std::min(d.hlValid, e.n() - 1);
    if (int(d.hlStates.size()) < e.n() + 1) d.hlStates.resize(size_t(e.n()) + 1);
    while (d.hlValid < y) {
        const u32& l = e.lines[size_t(d.hlValid)];
        if (hlBuf.size() < l.size()) hlBuf.resize(l.size());
        d.hlStates[size_t(d.hlValid) + 1] = highlightLine(d.lang, l, d.hlStates[size_t(d.hlValid)], hlBuf.data());
        d.hlValid++;
    }
    return d.hlStates[size_t(y)];
}

int App::gutterFor(const Editor& e) const {
    if (!S.lineNums) return 0;
    int dg = 1;
    for (int n = e.n(); n >= 10; n /= 10) dg++;
    return std::max(3, dg) + 2;  // a blank on both sides of the numbers, the second one in the text color
}

std::vector<std::pair<int, int>> App::menuLayout() const {
    std::vector<std::pair<int, int>> pos;
    int x = 2;
    for (size_t i = 0; i < MENUS.size(); i++) {
        int len = int(stripAmp(MENUS[i].title).size()) + 2;
        if (i + 1 == MENUS.size()) pos.push_back({std::max(x, scr.w - len - 1), len});
        else { pos.push_back({x, len}); x += len; }
    }
    return pos;
}

int App::menuAt(int x) const {
    auto pos = menuLayout();
    for (size_t i = 0; i < pos.size(); i++)
        if (x >= pos[i].first && x < pos[i].first + pos[i].second) return int(i);
    return -1;
}

void App::drawMenubar(int active) {
    scr.fill(0, 0, scr.w, Col::MENU);
    auto pos = menuLayout();
    for (size_t i = 0; i < MENUS.size(); i++) {
        bool a = int(i) == active;
        scr.hot(pos[i].first, 0, U" " + u32(MENUS[i].title) + U" ", a ? Col::MSEL : Col::MENU,
                a ? Col::MSELHOT : Col::MHOT);
    }
}

// The symbol shown for an invisible character with Options > Show Invisible Characters, 0 for a
// visible one. Zero-width characters return 1: they mark the cell of the following character.
static char32_t invisibleSymbol(char32_t ch) {
    if (ch == ' ') return U'·';
    if (ch == '\t') return U'→';
    if (ch < 32) return 0x2400 + ch;  // control pictures ␀ ... ␟
    if (ch == 127) return U'␡';
    if (ch >= 128 && ch < 160) return U'¿';
    if (ch == 0xA0 || ch == 0x202F || ch == 0x2007) return U'°';  // no-break spaces
    if ((ch >= 0x2000 && ch <= 0x200A) || ch == 0x205F || ch == 0x3000) return U'·';
    if ((ch >= 0x200B && ch <= 0x200F) || (ch >= 0x202A && ch <= 0x202E) || (ch >= 0x2060 && ch <= 0x2064) ||
        ch == 0xFEFF)
        return 1;
    return 0;
}

void App::drawLine(int x0, int left, int width, int y, const u32& l, int s, int e, const uint8_t* cls,
                   const std::vector<std::pair<int, int>>* hl, int eol) {
    int right = left + width;
    int col = 0;
    bool inv = S.invisibles, zeroW = false;  // zeroW: a zero-width character precedes this cell
    size_t i = 0;
    for (; i < l.size(); i++) {
        char32_t ch = l[i];
        int w = ch == '\t' ? S.tabw - col % S.tabw : cw(ch);
        char32_t sym = inv ? invisibleSymbol(ch) : 0;
        if (w == 0) {
            if (sym == 1) zeroW = true;
            continue;
        }
        if (col + w <= left) { col += w; zeroW = false; continue; }
        if (col >= right) break;
        bool sel = int(i) >= s && int(i) < e;
        Attr a = sel ? Col::SEL : cls ? Attr{HLCOL[cls[i]], Col::TEXT.bg} : Col::TEXT;
        bool found = false;
        if (hl && !sel)
            for (auto& r : *hl)
                if (int(i) >= r.first && int(i) < r.second) found = true;
        if (found) a = Col::SEARCH;
        else if (sym && !sel) a = Col::INVIS;
        if (zeroW && !sel) a = Attr{a.fg, Col::ZEROW.bg};
        zeroW = false;
        if (sym) {
            for (int c = std::max(col, left); c < std::min(col + w, right); c++)
                scr.put(x0 + c - left, y, c == col ? sym : U' ', a);
        } else if (ch == '\t' || col < left || col + w > right) {
            for (int c = std::max(col, left); c < std::min(col + w, right); c++) scr.put(x0 + c - left, y, ' ', a);
        } else {
            scr.put(x0 + col - left, y, ch, a);
        }
        col += w;
    }
    if (i < l.size() || col < left || col >= right) return;
    // the line break: selected, marked with Show Invisible Characters, or after a zero-width character
    bool sel = int(l.size()) >= s && int(l.size()) < e;
    char32_t sym = inv && eol ? (eol == 2 ? U'↵' : U'¶') : U' ';
    if (sel) scr.put(x0 + col - left, y, sym, Col::SEL);
    else if (zeroW) scr.put(x0 + col - left, y, sym, Col::ZEROW);
    else if (sym != ' ') scr.put(x0 + col - left, y, sym, Col::INVIS);
}

u32 App::titleOf(const Editor& e) const {
    u32 name = e.filename.empty() ? u32(tr(U"Untitled")) : U(baseOf(e.filename));
    return name + (e.modified ? U" *" : U"");
}

void App::drawWindow(Win& w, bool active) {
    Doc& d = *w.doc;
    Editor& e = textOf(&d);
    View v;
    if (active) {
        v.cy = ed.cy;
        v.top = ed.top;
        v.left = ed.left;
    } else {
        v = w.v;
        v.cy = std::clamp(v.cy, 0, e.n() - 1);
        v.top = std::clamp(v.top, 0, std::max(0, e.n() - 1));
    }
    int nl = e.n();
    int g = gutterFor(e);
    int x0 = w.x + g, width = std::max(1, w.w - g);
    Pos sa, sb;
    bool hs = active && !S.vim && e.sel(sa, sb);
    std::vector<std::pair<int, int>> hlSpans;
    consumeDirty(e, d);
    bool useTs = hlOn(d) && d.tsd.L;
    uint32_t vs = 0;
    if (useTs) {
        d.tsd.sync(e.lines);
        int last = std::min(nl, v.top + w.h);
        vs = v.top < nl ? d.tsd.ls[size_t(v.top)] : uint32_t(d.tsd.text.size());
        uint32_t ve = last < nl ? d.tsd.ls[size_t(last)] : uint32_t(d.tsd.text.size());
        d.tsd.highlight(vs, ve, d.tsVis);
    }
    for (int r = 0; r < w.h; r++) {
        int y = w.y + r;
        scr.fill(w.x, y, w.w, Col::TEXT);
        int li = v.top + r;
        if (g) {
            scr.fill(w.x, y, g - 1, Col::GUTTER);
            if (li < nl) {
                int num = (S.lineNums == 2 && li != v.cy) ? std::abs(li - v.cy) : li + 1;
                scr.text(w.x, y, fmtU("%*d", g - 2, num), li == v.cy ? Col::GUTTERCUR : Col::GUTTER);
            }
        }
        if (li >= nl) continue;
        int s = -1, en = -1;
        if (S.vim) {
            if (!active || !vim.visualSpan(li, s, en)) s = en = -1;
            hlSpans.clear();
            vim.searchSpansIn(e.lines, li, hlSpans);
        } else if (hs && li >= sa.y && li <= sb.y) {
            s = li == sa.y ? sa.x : 0;
            en = li == sb.y ? sb.x : int(e.lines[size_t(li)].size()) + 1;
        }
        const uint8_t* cls = nullptr;
        const u32& l = e.lines[size_t(li)];
        if (useTs) {
            if (hlBuf.size() < l.size()) hlBuf.resize(l.size());
            size_t off = d.tsd.ls[size_t(li)] - vs;
            for (size_t k = 0; k < l.size(); k++) {
                hlBuf[k] = off < d.tsVis.size() ? d.tsVis[off] : uint8_t(H_TEXT);
                char32_t ch = l[k];
                off += ch < 0x80 ? 1 : ch < 0x800 ? 2 : ch < 0x10000 ? 3 : 4;
            }
            cls = hlBuf.data();
        } else if (hlOn(d)) {
            int st = hlStateAt(e, d, li);
            if (hlBuf.size() < l.size()) hlBuf.resize(l.size());
            int out = highlightLine(d.lang, l, st, hlBuf.data());
            if (li == d.hlValid && li + 1 < int(d.hlStates.size())) {  // remember the state right away
                d.hlStates[size_t(li) + 1] = out;
                d.hlValid = li + 1;
            }
            cls = hlBuf.data();
        }
        drawLine(x0, v.left, width, y, l, s, en, cls, S.vim && !hlSpans.empty() ? &hlSpans : nullptr,
                 li + 1 < nl ? (e.crlf ? 2 : 1) : 0);
    }
    // status line of the window (only with several windows)
    if (w.sy >= 0) {
        Attr a = active ? Col::TITLE : Col::FRAME;
        scr.fill(w.x, w.sy, w.w, a, active ? U' ' : U'─');
        scr.text(w.x + 1, w.sy, U" " + titleOf(e) + U" ", active ? Col::TITLE : Col::FRAME, w.w - 2);
    }
}

void App::drawSeparators(Node* n) {
    if (n->kind == Node::LEAF) return;
    for (size_t i = 0; i < n->kids.size(); i++) {
        Node* c = n->kids[i].get();
        if (n->kind == Node::COLS && i + 1 < n->kids.size())
            for (int y = n->y; y < n->y + n->h; y++) scr.put(c->x + c->w, y, U'│', Col::FRAME);
        drawSeparators(c);
    }
}

std::vector<std::pair<int, int>> App::tabLayout() const {
    std::vector<std::pair<int, int>> r;
    int n = int(tabs.size());
    if (n == 1) {  // one tab: its file name in the middle of the line
        int len = std::min(strWidth(titleOf(ed)) + 4, scr.w - 4);
        r.push_back({(scr.w - len) / 2, len});
        return r;
    }
    int avail = scr.w - 4;
    int each = std::max(5, n ? avail / n : avail);
    int x = 2;
    for (int t = 0; t < n; t++) {
        Win* w = tabs[size_t(t)]->cur;
        const Editor& e = w->doc == win->doc ? ed : w->doc->store;
        int len = strWidth(fmtU("%d ", t + 1) + titleOf(e)) + 4;  // " 1 name × "
        len = std::min(len, each);
        r.push_back({x, len});
        x += len + 1;
    }
    return r;
}

void App::drawTabLine() {
    int W = scr.w;
    scr.put(0, 1, U'┌', Col::FRAME);
    scr.fill(1, 1, W - 2, Col::FRAME, U'─');
    scr.put(W - 1, 1, U'┐', Col::FRAME);
    auto lay = tabLayout();
    for (size_t t = 0; t < tabs.size(); t++) {
        Win* w = tabs[t]->cur;
        const Editor& e = w->doc == win->doc ? ed : w->doc->store;
        u32 label = U" " + fmtU("%d ", int(t) + 1) + titleOf(e) + U" ";
        int x = lay[t].first, len = lay[t].second;
        if (x + len > W - 2) break;
        Attr a = int(t) == curTab ? Col::TITLE : Col::FRAME;
        scr.fill(x, 1, len, a);
        scr.text(x, 1, label, a, len - 2);
        scr.put(x + len - 2, 1, U'×', a);  // a click closes the tab
    }
}

void App::drawEditor() {
    int W = scr.w, H = scr.h;
    layoutAll();
    if (tabs.size() > 1) {
        drawTabLine();
    } else {
        scr.put(0, 1, U'┌', Col::FRAME);
        scr.fill(1, 1, W - 2, Col::FRAME, U'─');
        scr.put(W - 1, 1, U'┐', Col::FRAME);
        auto [x, len] = tabLayout()[0];
        scr.text(x, 1, U" " + titleOf(ed) + U" ", Col::TITLE, len - 2);
        scr.put(x + len - 2, 1, U'×', Col::TITLE);
        scr.put(x + len - 1, 1, U' ', Col::TITLE);
    }
    int ay = 2, ah = std::max(1, H - 4);
    for (int r = 0; r < ah; r++) scr.put(0, ay + r, U'│', Col::FRAME);
    for (Win* w : windows()) drawWindow(*w, w == win);
    drawSeparators(tab().root.get());

    // vertical scroll bar (active window)
    int nl = ed.n();
    int X = W - 1;
    if (ah >= 2) {
        scr.put(X, ay, U'↑', Col::SCROLL);
        scr.put(X, ay + ah - 1, U'↓', Col::SCROLL);
        int tl2 = ah - 2;
        for (int r = 0; r < tl2; r++) scr.put(X, ay + 1 + r, U'░', Col::SCROLL);
        if (tl2 > 0) {
            int pos = nl > th() ? std::min(tl2 - 1, ed.top * (tl2 - 1) / std::max(1, nl - th())) : 0;
            scr.put(X, ay + 1 + std::max(0, pos), U'■', Col::SCROLL);
        }
    }
    // bottom frame line with the horizontal scroll bar
    int by = H - 2;
    scr.put(0, by, U'└', Col::FRAME);
    scr.put(W - 1, by, U'┘', Col::FRAME);
    scr.put(1, by, U'←', Col::SCROLL);
    scr.put(W - 2, by, U'→', Col::SCROLL);
    int htl = W - 4;
    scr.fill(2, by, htl, Col::SCROLL, U'░');
    if (htl > 0) {
        int rng = std::max(1, maxLineWidth() - tw());
        int pos = std::clamp(ed.left * (htl - 1) / rng, 0, htl - 1);
        scr.put(2 + pos, by, U'■', Col::SCROLL);
    }
}

int App::maxLineWidth() const {
    size_t m = 0;
    for (auto& l : ed.lines) m = std::max(m, l.size());
    return int(m);
}

void App::drawStatus(const u32* status) {
    int y = scr.h - 1;
    scr.fill(0, y, scr.w, Col::STATUS);
    if (S.vim && !status && vim.cmdlineActive()) {
        // Command-line mode uses the whole status line
        u32 t = vim.cmdlineText();
        int w = scr.w - 2;
        int cur = vim.cmdlineCursor();
        int off = cur >= w ? cur - w + 1 : 0;
        u32 vis;
        int c = 0;
        for (char32_t ch : t) {
            int cwid = ch < 32 ? 2 : cw(ch);
            if (c >= off) {
                if (ch < 32) { vis += '^'; vis += char32_t(ch + 64); }
                else vis += ch;
            }
            c += cwid;
        }
        scr.text(1, y, vis, Col::STATUS, w);
        return;
    }
    u32 left = status ? *status : statusMsg;
    Attr la = !status && !statusMsg.empty() && statusErr ? Col::ERRMSG : Col::STATUS;
    u32 right;
    if (S.vim) {
        // Vim mode: only Vim's messages, the partly typed command and "vim"
        if (!status) {
            u32 vs = vim.statusText();
            if (!vs.empty()) left = vs;
            if (vim.statusIsError()) la = Col::ERRMSG;
        }
        u32 sc = vim.showCmd();
        right = (sc.empty() ? u32() : sc.substr(0, 10) + U" ") + U"│ vim ";
    } else {
        if (left.empty()) left = statusHint();
        int col = ed.colOf(ed.lines[ed.cy], ed.cx);
        u32 ln = doc().lang >= 0 ? U(LANGS[doc().lang].name) : u32(U"Text");
        u32 ovr = S.overwrite ? tr(U"OVR ") : U"";
        u32 enc = ed.latin1 ? U"Latin-1" : ed.bom ? U"UTF-8 BOM" : U"UTF-8";
        u32 pos = fmtU("%05d:%03d", ed.cy + 1, col + 1);
        Pos a, b;
        if (ed.sel(a, b)) {
            // the selection: start - end (exclusive) and its length, a line break counts as one character
            long n = b.y - a.y;
            if (a.y == b.y) n = b.x - a.x;
            else n += long(ed.lines[size_t(a.y)].size()) - a.x + b.x;
            for (int y = a.y + 1; y < b.y; y++) n += long(ed.lines[size_t(y)].size());
            pos = fmtU("%05d:%03d - ", a.y + 1, ed.colOf(ed.lines[size_t(a.y)], a.x) + 1) +
                  fmtU("%05d:%03d", b.y + 1, ed.colOf(ed.lines[size_t(b.y)], b.x) + 1) + fmtU(" (%d)", int(n));
        }
        right = U"│ " + ln + U" │ " + enc + U" │ " + ovr + pos + U" ";
    }
    int rw = strWidth(right);
    scr.text(1, y, left, la, scr.w - rw - 2);
    scr.text(scr.w - rw, y, right, Col::STATUS);
}

void App::drawAll(const u32* status, int activeMenu) {
    scr.clear();
    scr.cvis = false;
    if (tooSmall()) {
        scr.text(0, 0, tr(U"Window too small!"), Attr{15, 4});
        return;
    }
    drawMenubar(activeMenu);
    drawEditor();
    drawStatus(status);
}

void App::placeCursor() {
    if (S.vim && vim.cmdlineActive() && !tooSmall()) {
        int w = scr.w - 2, cur = vim.cmdlineCursor();
        scr.cvis = true;
        scr.cx = 1 + (cur >= w ? w - 1 : cur);
        scr.cy = scr.h - 1;
        scr.cshape = vim.cursorShape();
        return;
    }
    int r = ed.cy - ed.top;
    int c = ed.colOf(ed.lines[ed.cy], ed.cx) - ed.left;
    // in Normal mode the cursor is shown on the last column of a tab
    if (S.vim && vim.mode != VM_INSERT && vim.mode != VM_REPLACE && ed.cx < int(ed.lines[ed.cy].size()) &&
        ed.lines[ed.cy][size_t(ed.cx)] == '\t')
        c = ed.colOf(ed.lines[ed.cy], ed.cx + 1) - 1 - ed.left;
    if (!tooSmall() && r >= 0 && r < th() && c >= 0 && c < tw()) {
        scr.cvis = true;
        scr.cx = tx() + c;
        scr.cy = ty() + r;
        scr.cshape = S.overwrite ? 1 : 3;  // block cursor when overwriting, underline when inserting
        if (S.vim) scr.cshape = vim.cursorShape();
    }
}

void App::dropRect(int mi, int& x, int& y, int& w, int& h) const {
    int lw = 0, sw = 0;
    for (auto& it : MENUS[mi].items) {
        if (!it.label) continue;
        lw = std::max(lw, int(stripAmp(it.label).size()));
        sw = std::max(sw, int(u32(it.sc).size()));
    }
    w = lw + (sw ? sw + 3 : 0) + 5;
    h = int(MENUS[mi].items.size()) + 2;
    x = menuLayout()[mi].first;
    if (x + w + 2 > scr.w) x = std::max(0, scr.w - w - 2);
    y = 1;
}

void App::drawDropdown(int mi, int cur) {
    int x, y, w, h;
    dropRect(mi, x, y, w, h);
    scr.box(x, y, w, h, Col::MENU);
    scr.shadow(x, y, w, h);
    const auto& items = MENUS[mi].items;
    for (size_t i = 0; i < items.size(); i++) {
        int ry = y + 1 + int(i);
        const MItem& it = items[i];
        if (!it.label) {
            scr.put(x, ry, U'├', Col::MENU);
            scr.fill(x + 1, ry, w - 2, Col::MENU, U'─');
            scr.put(x + w - 1, ry, U'┤', Col::MENU);
            continue;
        }
        bool en = enabled(it.act), s = int(i) == cur;
        Attr base = s ? Col::MSEL : (en ? Col::MENU : Col::MDIS);
        Attr hot = en ? (s ? Col::MSELHOT : Col::MHOT) : base;
        scr.fill(x + 1, ry, w - 2, base);
        if (checked(it.act)) scr.put(x + 1, ry, U'√', base);
        scr.hot(x + 2, ry, it.label, base, hot);
        u32 sc = it.sc;
        if (!sc.empty()) scr.text(x + w - 2 - int(sc.size()), ry, sc, base);
    }
}
