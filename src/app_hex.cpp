// Hex mode in the application: switching, drawing, keys, mouse, clipboard, Find and Go to.
// The bytes, the cursor and undo are in HexBuf (hex.cpp).
//
// A buffer in hex mode keeps its bytes in Doc::hex; App::ed keeps the file name and the
// modified flag (and the text it had before, which HexBuf::stale marks as outdated).

#include "app.h"

#include <algorithm>
#include <climits>
#include <cstdio>

#include "i18n.h"
#include "menus.h"
#include "utf8.h"
#include "widgets.h"

void App::hexFromFile(std::string data) {
    Editor e;
    e.filename = ed.filename;
    ed = std::move(e);
    auto h = std::make_unique<HexBuf>();
    h->data = std::move(data);
    h->stale = true;
    doc().hex = std::move(h);
    vim.reset();
}

void App::hexIfBinary() {
    if (hexMode() || ed.filename.empty() || ed.modified || !ed.undo.empty()) return;
    std::string data, err;
    if (readFile(ed.filename, data, err) && looksBinary(data)) hexFromFile(std::move(data));
}

void App::setHex(bool on) {
    Doc& d = doc();
    if (on == bool(d.hex)) return;
    vim.reset();
    if (on) {
        auto h = std::make_unique<HexBuf>();
        bool conv;
        h->data = ed.encode(conv);
        h->pos = std::min(ed.byteOffset({ed.cy, ed.cx}), h->size());
        h->fromText = true;
        ed.anch = false;
        d.hex = std::move(h);
        hexScroll(true);
        return;
    }
    HexBuf& h = *d.hex;
    if (h.stale) {
        // the text of the bytes; switching back from a text is one undo step
        Editor t;
        t.setData(h.data);
        if (h.fromText) ed.checkpoint(0);
        else { ed.undo.clear(); ed.redo.clear(); }
        ed.lines = std::move(t.lines);
        ed.crlf = t.crlf;
        ed.latin1 = t.latin1;
        ed.bom = t.bom;
        ed.touch(0);
    }
    Pos p = ed.posOfByte(h.pos);
    d.hex.reset();
    ed.anch = false;
    ed.cy = p.y;
    ed.cx = p.x;
    ed.wantCol = -1;
    ed.lastEdit = 0;
    autoLang();
    ed.scrollTo(th(), tw(), true);
}

HexLayout App::hexLayout() const { return HexLayout(tw(), doc().hex->size()); }

void App::hexSync() {
    if (!hexMode()) return;
    HexBuf& h = *doc().hex;
    if (h.modified) ed.modified = true;
    h.modified = false;
}

void App::hexScroll(bool center) {
    if (hexMode()) doc().hex->scrollTo(hexLayout().per, th(), center);
}

// ---- Drawing ----

static void putHex(Screen& scr, int x, int y, unsigned char b, Attr a) {
    static const char* const H = "0123456789ABCDEF";
    scr.put(x, y, char32_t(H[b >> 4]), a);
    scr.put(x + 1, y, char32_t(H[b & 15]), a);
}

void App::drawHexWindow(Win& w, bool active) {
    const HexBuf& h = *w.doc->hex;
    HexLayout L(w.w, h.size());
    size_t per = size_t(L.per);
    size_t pos = active ? h.pos : std::min(w.v.hexPos, h.size());
    size_t top = active ? h.top : std::min(w.v.hexTop, h.size() / per);
    size_t sa = 0, sb = 0;
    bool hs = active && h.sel(sa, sb);
    bool cursorInChars = active ? h.chars : w.v.hexChars;
    // clipped to the window
    auto put = [&](int col, int y, char32_t ch, Attr a) {
        if (col >= 0 && col < w.w) scr.put(w.x + col, y, ch, a);
    };
    for (int r = 0; r < w.h; r++) {
        int y = w.y + r;
        scr.fill(w.x, y, w.w, Col::TEXT);
        size_t row = top + size_t(r);
        if (row > h.size() / per) continue;
        size_t off = row * per;
        char buf[32];
        snprintf(buf, sizeof buf, "%0*zX", L.digits, off);
        for (int i = 0; buf[i]; i++) put(i, y, char32_t(buf[i]), row == pos / per ? Col::GUTTERCUR : Col::GUTTER);
        for (size_t i = 0; i < per && off + i < h.size(); i++) {
            size_t o = off + i;
            unsigned char b = (unsigned char)h.data[o];
            bool sel = hs && o >= sa && o < sb;
            bool printable = b >= 32 && b < 127;
            Attr ha = sel ? Col::SEL : b == 0 ? Col::INVIS : Col::TEXT;
            Attr ca = sel ? Col::SEL : printable ? Col::TEXT : Col::INVIS;
            // the byte under the cursor is marked in the other area
            if (active && o == pos && !sel) (cursorInChars ? ha : ca) = Col::SEARCH;
            int hc = L.hexCol(int(i));
            if (hc + 1 < w.w) putHex(scr, w.x + hc, y, b, ha);
            if (sel && o + 1 < sb && i + 1 < per)  // the space up to the next selected byte
                for (int c = hc + 2; c < L.hexCol(int(i) + 1); c++) put(c, y, U' ', Col::SEL);
            put(L.charCol(int(i)), y, printable ? char32_t(b) : U'.', ca);
        }
    }
}

// The right part of the status line in hex mode: "Hex", OVR and the offset of the cursor or
// the selection
u32 App::hexStatus() const {
    const HexBuf& h = *doc().hex;
    int dg = hexLayout().digits;
    char buf[96];
    size_t a, b;
    if (h.sel(a, b)) snprintf(buf, sizeof buf, "%0*zX - %0*zX (%zu)", dg, a, dg, b, b - a);
    else snprintf(buf, sizeof buf, "%0*zX", dg, h.pos);
    u32 ovr = h.insert ? U"" : tr(U"OVR ");
    return U"│ Hex │ " + ovr + U(buf) + U" ";
}

bool App::hexCursor() {
    const HexBuf& h = *doc().hex;
    HexLayout L = hexLayout();
    size_t row = h.pos / size_t(L.per);
    int i = int(h.pos % size_t(L.per));
    int c = h.chars ? L.charCol(i) : L.hexCol(i) + h.nib;
    if (row < h.top || row >= h.top + size_t(th()) || c >= tw()) return false;
    scr.cx = tx() + c;
    scr.cy = ty() + int(row - h.top);
    scr.cshape = h.insert ? 3 : 1;
    return true;
}

// ---- Keys and actions ----

void App::hexKey(const Key& k) {
    if (int a = shortcut(k)) {
        doAct(a);
        return;
    }
    HexBuf& h = *doc().hex;
    switch (k.code) {
        case K_UP: case K_DOWN: case K_LEFT: case K_RIGHT:
        case K_HOME: case K_END: case K_PGUP: case K_PGDN:
            h.moveKey(k.code, k.mods, hexLayout().per, th());
            break;
        case K_TAB:
            h.chars = !h.chars;
            h.nib = 0;
            h.lastKind = 0;
            break;
        case K_ESC: h.anch = false; break;
        case K_BKSP: h.backspace(); break;
        case K_DEL: h.delFwd(); break;
        case K_CHAR: {
            if (k.mods & (M_CTRL | M_ALT)) return;
            if (h.chars) {
                if (k.ch >= 32) h.typeChar(k.ch);
                break;
            }
            char32_t c = fold(k.ch);
            if (c >= '0' && c <= '9') h.typeHex(int(c - '0'));
            else if (c >= 'a' && c <= 'f') h.typeHex(int(c - 'a' + 10));
            break;
        }
        default: return;
    }
    hexSync();
    hexScroll();
}

// Bytes as text for the clipboard: UTF-8 when they are valid UTF-8, otherwise Latin-1
static u32 bytesAsText(const std::string& b) {
    u32 t;
    if (fromUtf8(b, t)) return t;
    t.clear();
    for (unsigned char c : b) t += c;
    return t;
}

void App::hexPaste(const u32& t) {
    HexBuf& h = *doc().hex;
    std::string b;
    // the hex area takes hex digits ("4F 0A") as bytes, everything else is inserted as text
    if (h.chars || !parseHex(t, b)) b = toUtf8(t);
    h.paste(b);
    hexSync();
    hexScroll();
}

bool App::hexAct(int a) {
    HexBuf& h = *doc().hex;
    switch (a) {
        case A_UNDO: h.doUndo(); break;
        case A_REDO: h.doRedo(); break;
        case A_CUT: case A_COPY: {
            std::string b = h.selected();
            if (b.empty()) break;
            clip = h.chars ? bytesAsText(b) : hexText(b);
            copyToSystem(clip);
            if (a == A_CUT) h.deleteSel();
            break;
        }
        case A_PASTE: hexPaste(pasteText()); break;
        case A_CLEAR: h.deleteSel(); break;
        case A_SELALL: h.selectAll(); break;
        case A_FIND: hexFindDialog(); break;
        case A_FINDNEXT: hexFindNext(); break;
        case A_GOTO: hexGotoDialog(); break;
        case A_OVERWRITE: h.insert = !h.insert; break;
        default: return false;
    }
    hexSync();
    return true;
}

// ---- Mouse ----

void App::hexMouseAt(int x, int y, bool select) {
    HexBuf& h = *doc().hex;
    HexLayout L = hexLayout();
    size_t per = size_t(L.per), lastRow = h.size() / per;
    int r = y - ty();
    if (r < 0) {  // dragging above or below the window scrolls
        if (h.top) h.top--;
        r = 0;
    } else if (r >= th()) {
        if (h.top + size_t(th()) <= lastRow) h.top++;
        r = th() - 1;
    }
    bool chars;
    int i = std::max(0, L.at(x - tx(), chars));
    size_t row = std::min(h.top + size_t(r), lastRow);
    h.chars = chars;
    h.setCursor(row * per + size_t(i), select);
}

void App::hexWheel(int d) {
    HexBuf& h = *doc().hex;
    long rows = long(h.size() / size_t(hexLayout().per)) + 1;
    h.top = size_t(std::clamp(long(h.top) + d, 0L, std::max(0L, rows - th())));
}

void App::hexVscroll(int pos, int len) {
    HexBuf& h = *doc().hex;
    size_t per = size_t(hexLayout().per);
    size_t rows = h.size() / per + 1, page = size_t(th());
    h.top = (rows > page ? rows - page : 0) * size_t(pos) / size_t(std::max(1, len - 1));
    // the cursor stays in the view
    size_t row = h.pos / per, col = h.pos % per;
    if (row < h.top) row = h.top;
    else if (row >= h.top + page) row = h.top + page - 1;
    h.anch = false;
    h.pos = std::min(row * per + col, h.size());
    h.nib = 0;
}

// ---- Find and Go to ----

void App::hexFindDialog() {
    HexBuf& h = *doc().hex;
    u32 init = hexFindText;
    std::string s = h.selected();
    if (!s.empty() && s.size() <= 64) {
        init = h.chars ? bytesAsText(s) : hexText(s);
        hexFindHex = !h.chars;
    }
    int w = std::min(64, scr.w - 4);
    Dialog d(scr, tr(U"Find"), w, 9);
    d.background = [this] { drawAll(); };
    int ix = std::max(16, 2 + strWidth(tr(U"Find What:")) + 2);
    d.add<Label>(2, 1, tr(U"Find What:"));
    Input* inp = d.add<Input>(ix, 1, w - ix - 3, init);
    Check* c1 = d.add<Check>(2, 3, tr(U"Hex Bytes (e.g. 4F 0A)"), hexFindHex);
    Check* c2 = d.add<Check>(2, 4, tr(U"Match Upper/Lowercase"), findCase);
    d.addButtons({{tr(U"OK"), 1}, {tr(U"Cancel"), -1}});
    d.def = 1;
    if (d.run() != 1 || inp->text.empty()) return;
    hexFindText = inp->text;
    hexFindHex = c1->val;
    findCase = c2->val;
    hexFindNext();
}

bool App::hexFindNext() {
    if (hexFindText.empty()) {
        hexFindDialog();
        return false;
    }
    HexBuf& h = *doc().hex;
    std::string pat;
    if (!hexFindHex) pat = toUtf8(hexFindText);
    else if (!parseHex(hexFindText, pat)) {
        msgbox(tr(U"Find"), tr(U"Please enter bytes as hex digits, e.g. 4F 0A."));
        return false;
    }
    size_t at;
    if (!h.find(pat, findCase || hexFindHex, h.pos, at)) {
        msgbox(tr(U"Find"), tr(U"Match not found."));
        return false;
    }
    h.anch = true;
    h.anchor = at;
    h.pos = at + pat.size();
    h.nib = 0;
    h.lastKind = 0;
    hexScroll(true);
    return true;
}

void App::hexGotoDialog() {
    HexBuf& h = *doc().hex;
    char cur[32];
    snprintf(cur, sizeof cur, "%zX", h.pos);
    u32 label = tr(U"Offset (hex):");
    Dialog d(scr, tr(U"Go to Offset"), std::max(40, strWidth(label) + 24), 7);
    d.background = [this] { drawAll(); };
    d.add<Label>(2, 2, label);
    Input* inp = d.add<Input>(strWidth(label) + 3, 2, 18, U(cur));
    d.addButtons({{tr(U"OK"), 1}, {tr(U"Cancel"), -1}});
    d.def = 1;
    if (d.run() != 1) return;
    u32 t = inp->text;
    while (!t.empty() && t.back() == ' ') t.pop_back();
    while (!t.empty() && t[0] == ' ') t.erase(0, 1);
    if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) t.erase(0, 2);
    size_t off = 0;
    bool ok = !t.empty() && t.size() <= 16;
    for (char32_t c : t) {
        int v = c >= '0' && c <= '9' ? int(c - '0') : fold(c) >= 'a' && fold(c) <= 'f' ? int(fold(c) - 'a' + 10) : -1;
        if (v < 0) ok = false;
        else off = off * 16 + size_t(v);
    }
    if (!ok) {
        error(tr(U"Please enter a hexadecimal number."));
        return;
    }
    h.setCursor(off, false);
    hexScroll(true);
}
