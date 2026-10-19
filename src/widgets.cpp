// Dialog widgets and the modal Dialog loop (see widgets.h).

#include "widgets.h"

#include <algorithm>

#include "utf8.h"

std::function<u32()> g_clipboardPaste;

Input::Input(int x_, int y_, int w_, u32 t) : w(w_), text(std::move(t)) {
    x = x_; y = y_; focusable = true;
    pos = int(text.size());
}

void Input::fix() {
    if (pos < scroll) scroll = pos;
    if (pos >= scroll + w) scroll = pos - w + 1;
}

void Input::draw(Screen& s, int ox, int oy, bool f) {
    fix();
    s.fill(ox + x, oy + y, w, Col::INPUT);
    s.text(ox + x, oy + y, text.substr(scroll), (f && fresh && !text.empty()) ? Col::INPUTSEL : Col::INPUT, w);
}

bool Input::cursor(int& cx, int& cy) {
    fix();
    cx = x + pos - scroll;
    cy = y;
    return true;
}

void Input::ins(const u32& t) {
    if (fresh) { text.clear(); pos = 0; }
    text.insert(size_t(pos), t);
    pos += int(t.size());
}

int Input::key(const Key& k) {
    if (k.code == K_CHAR && !(k.mods & (M_CTRL | M_ALT | M_CMD)) && k.ch >= 32) ins(u32(1, k.ch));
    else if (k.code == K_CHAR && (k.mods == M_CTRL || k.mods == M_CMD) && k.ch == 'v' && g_clipboardPaste) {
        u32 t = g_clipboardPaste();
        ins(t.substr(0, t.find('\n')));
    }
    else if (k.code == K_PASTE) ins(g_paste.substr(0, g_paste.find('\n')));
    else if (k.code == K_BKSP) {
        if (fresh) { text.clear(); pos = 0; }
        else if (pos > 0) { text.erase(pos - 1, 1); pos--; }
    } else if (k.code == K_DEL) {
        if (fresh) { text.clear(); pos = 0; }
        else if (pos < int(text.size())) text.erase(pos, 1);
    } else if (k.code == K_LEFT) pos = std::max(0, pos - 1);
    else if (k.code == K_RIGHT) pos = std::min(int(text.size()), pos + 1);
    else if (k.code == K_HOME) pos = 0;
    else if (k.code == K_END) pos = int(text.size());
    else return 0;
    fresh = false;
    return 0;
}

int Input::click(int mx, int) {
    fresh = false;
    pos = std::min(int(text.size()), scroll + mx - x);
    return 0;
}

void Check::draw(Screen& s, int ox, int oy, bool) {
    s.text(ox + x, oy + y, (val ? U"[X] " : U"[ ] ") + label, Col::DIALOG);
}

int Check::key(const Key& k) {
    if (k.code == K_CHAR && k.ch == ' ' && !k.mods) val = !val;
    return 0;
}

ListBox::ListBox(int x_, int y_, int w_, int h_, bool s) : w(w_), h(std::max(3, h_)), selectable(s) {
    x = x_; y = y_; focusable = true;
}

void ListBox::setItems(std::vector<u32> it) {
    items = std::move(it);
    sel = 0; top = 0;
}

void ListBox::fix() {
    int cnt = int(items.size());
    if (selectable) {
        sel = std::clamp(sel, 0, std::max(0, cnt - 1));
        if (sel < top) top = sel;
        if (sel >= top + ih()) top = sel - ih() + 1;
    }
    top = std::clamp(top, 0, std::max(0, cnt - ih()));
}

void ListBox::setSel(int i) {
    int old = sel;
    sel = std::clamp(i, 0, std::max(0, int(items.size()) - 1));
    fix();
    if (sel != old && onChange && !items.empty()) onChange(sel);
}

void ListBox::draw(Screen& s, int ox, int oy, bool f) {
    fix();
    s.box(ox + x, oy + y, w, h, Col::DIALOG);
    int iw = w - 2;
    for (int r = 0; r < ih(); r++) {
        int i = top + r;
        Attr a = Col::DIALOG;
        if (selectable && i == sel && i < int(items.size())) a = f ? Col::LISTSEL : Col::LISTUNF;
        s.fill(ox + x + 1, oy + y + 1 + r, iw, a);
        if (i < int(items.size())) s.text(ox + x + 2, oy + y + 1 + r, items[i], a, iw - 2);
    }
    int cnt = int(items.size());
    if (cnt > ih() && ih() >= 2) {
        int X = ox + x + w - 1, Y = oy + y + 1;
        s.put(X, Y, U'↑', Col::SCROLL);
        s.put(X, Y + ih() - 1, U'↓', Col::SCROLL);
        for (int r = 1; r < ih() - 1; r++) s.put(X, Y + r, U'░', Col::SCROLL);
        if (ih() > 2) {
            int pos = top * (ih() - 3) / std::max(1, cnt - ih());
            s.put(X, Y + 1 + pos, U'■', Col::SCROLL);
        }
    }
}

int ListBox::key(const Key& k) {
    int d = 0;
    switch (k.code) {
        case K_UP: d = -1; break;
        case K_DOWN: d = 1; break;
        case K_PGUP: d = -ih(); break;
        case K_PGDN: d = ih(); break;
        case K_HOME: d = -1000000; break;
        case K_END: d = 1000000; break;
    }
    if (!selectable) {
        if (d) { top += d; fix(); }
        return 0;
    }
    if (d) {
        setSel(int(std::clamp<long>(long(sel) + d, 0, std::max<long>(0, long(items.size()) - 1))));
    } else if (k.code == K_CHAR && !k.mods && k.ch > 32) {  // jump to the next entry starting with this letter
        int cnt = int(items.size());
        for (int j = 1; j <= cnt; j++) {
            int i = (sel + j) % cnt;
            if (!items[i].empty() && fold(items[i][0]) == fold(k.ch)) { setSel(i); break; }
        }
    }
    return 0;
}

void ListBox::wheel(int d) {
    if (selectable) setSel(sel + d);
    else { top += d; fix(); }
}

int ListBox::click(int mx, int my) {
    int row = my - y - 1;
    if (mx == x + w - 1 && int(items.size()) > ih()) {  // scroll bar
        if (row <= 0) wheel(-1);
        else if (row >= ih() - 1) wheel(1);
        else wheel(row < ih() / 2 ? -ih() : ih());
        return 0;
    }
    if (row >= 0 && row < ih() && top + row < int(items.size()) && selectable) {
        int i = top + row;
        auto now = Clock::now();
        bool dbl = lastI == i && now - lastT < std::chrono::milliseconds(400);
        lastT = now;
        lastI = i;
        setSel(i);
        if (dbl) { lastI = -1; return activate(); }
    }
    return 0;
}

void Dialog::addButtons(const std::vector<std::pair<u32, int>>& bs) {
    int total = 0;
    for (auto& b : bs) total += int(b.first.size()) + 4;
    total += 2 * (int(bs.size()) - 1);
    int bx = std::max(1, (w - total) / 2);
    for (auto& b : bs) {
        add<Button>(bx, h - 2, b.first, b.second);
        bx += int(b.first.size()) + 6;
    }
}

void Dialog::focusOn(Widget* wd) {
    for (size_t i = 0; i < ws.size(); i++)
        if (ws[i].get() == wd) focus = int(i);
}

void Dialog::origin(int& ox, int& oy) const {
    ox = std::max(0, (scr.w - w) / 2);
    oy = y >= 0 ? y : std::max(1, (scr.h - h) / 2);
}

void Dialog::draw() {
    int ox, oy;
    origin(ox, oy);
    scr.box(ox, oy, w, h, Col::DIALOG, title, true);
    scr.shadow(ox, oy, w, h);
    scr.put(ox, oy + h - 3, U'├', Col::DIALOG);
    scr.fill(ox + 1, oy + h - 3, w - 2, Col::DIALOG, U'─');
    scr.put(ox + w - 1, oy + h - 3, U'┤', Col::DIALOG);
    for (size_t i = 0; i < ws.size(); i++) ws[i]->draw(scr, ox, oy, int(i) == focus);
    scr.cvis = false;
    int cx, cy;
    if (focus >= 0 && ws[focus]->cursor(cx, cy)) {
        scr.cvis = true;
        scr.cx = ox + cx;
        scr.cy = oy + cy;
        scr.cshape = 3;
    }
}

void Dialog::moveFocus(int d) {
    int cnt = int(ws.size());
    int i = focus;
    for (int k = 0; k < cnt; k++) {
        i = (i + d + cnt) % cnt;
        if (ws[i]->focusable) { focus = i; return; }
    }
}

int Dialog::mouse() {
    const MouseEv& m = g_mouse;
    int ox, oy;
    origin(ox, oy);
    int mx = m.x - ox, my = m.y - oy;
    for (size_t i = 0; i < ws.size(); i++) {
        Widget* wd = ws[i].get();
        if (!wd->hit(mx, my)) continue;
        if (m.wheel) { wd->wheel(m.wheel * 3); return 0; }
        if (m.press && m.btn == 0) {
            if (wd->focusable) focus = int(i);
            return wd->click(mx, my);
        }
        return 0;
    }
    return 0;
}

int Dialog::run() {
    for (;;) {
        scr.clear();
        if (background) background();
        draw();
        scr.flush();
        Key k = getKey();
        if (k.code == K_RESIZE) continue;
        if (k.code == K_ESC) return -1;
        Widget* f = focus >= 0 ? ws[focus].get() : nullptr;
        if (k.code == K_TAB) { moveFocus((k.mods & M_SHIFT) ? -1 : 1); continue; }
        if (k.code == K_MOUSE) {
            int r = mouse();
            if (r) return r;
            continue;
        }
        if (k.code == K_ENTER) {
            if (auto b = dynamic_cast<Button*>(f)) return b->result;
            if (f && f->canActivate()) {
                int r = f->activate();
                if (r) return r;
                continue;
            }
            if (def) return def;
            continue;
        }
        if (f && k.code == K_CHAR && !k.mods && !dynamic_cast<Input*>(f)) {
            // h j k l move like the arrow keys, except in input fields; lists keep h and l for
            // jumping to an entry with that letter
            bool list = dynamic_cast<ListBox*>(f);
            switch (k.ch) {
                case 'j': k.code = K_DOWN; break;
                case 'k': k.code = K_UP; break;
                case 'h': if (!list) k.code = K_LEFT; break;
                case 'l': if (!list) k.code = K_RIGHT; break;
            }
        }
        if (f) {
            int r = f->key(k);
            if (r) return r;
            if (f->arrowsMoveFocus()) {
                if (k.code == K_LEFT || k.code == K_UP) moveFocus(-1);
                else if (k.code == K_RIGHT || k.code == K_DOWN) moveFocus(1);
            }
        }
    }
}
