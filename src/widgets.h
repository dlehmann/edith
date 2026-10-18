#pragma once
// Dialog building blocks: labels, input fields, check boxes, buttons, list boxes
// and the modal Dialog that hosts them.

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "common.h"
#include "screen.h"
#include "terminal.h"

// The base of all dialog elements. Coordinates are relative to the dialog. The int results
// of key(), click() and activate() end the dialog with that value when they are not 0.
struct Widget {
    int x = 0, y = 0;
    bool focusable = false;
    virtual ~Widget() = default;
    virtual void draw(Screen&, int ox, int oy, bool focused) = 0;
    virtual bool hit(int, int) { return false; }
    virtual int key(const Key&) { return 0; }
    virtual int click(int, int) { return 0; }
    virtual bool cursor(int&, int&) { return false; }
    virtual bool canActivate() { return false; }
    virtual int activate() { return 0; }
    virtual void wheel(int) {}
    virtual bool arrowsMoveFocus() { return false; }
};

struct Label : Widget {
    u32 text;
    Label(int x_, int y_, u32 t) : text(std::move(t)) { x = x_; y = y_; }
    void draw(Screen& s, int ox, int oy, bool) override { s.text(ox + x, oy + y, text, Col::DIALOG); }
};

struct Input : Widget {
    int w;
    u32 text;
    int pos = 0, scroll = 0;
    bool fresh = true;  // the first key press replaces the content
    Input(int x_, int y_, int w_, u32 t);
    void set(const u32& t) { text = t; pos = int(t.size()); scroll = 0; fresh = true; }
    void fix();
    void draw(Screen& s, int ox, int oy, bool f) override;
    bool hit(int mx, int my) override { return my == y && mx >= x && mx < x + w; }
    bool cursor(int& cx, int& cy) override;
    void ins(const u32& t);
    int key(const Key& k) override;
    int click(int mx, int) override;
};

struct Check : Widget {
    u32 label;
    bool val;
    Check(int x_, int y_, u32 l, bool v) : label(std::move(l)), val(v) { x = x_; y = y_; focusable = true; }
    void draw(Screen& s, int ox, int oy, bool) override;
    bool hit(int mx, int my) override { return my == y && mx >= x && mx < x + 4 + int(label.size()); }
    bool cursor(int& cx, int& cy) override { cx = x + 1; cy = y; return true; }
    int key(const Key& k) override;
    int click(int, int) override { val = !val; return 0; }
    bool arrowsMoveFocus() override { return true; }
};

struct Button : Widget {
    u32 label;
    int result;
    Button(int x_, int y_, const u32& l, int r) : label(U"< " + l + U" >"), result(r) {
        x = x_;
        y = y_;
        focusable = true;
    }
    void draw(Screen& s, int ox, int oy, bool f) override {
        s.text(ox + x, oy + y, label, f ? Col::BUTTONF : Col::DIALOG);
    }
    bool hit(int mx, int my) override { return my == y && mx >= x && mx < x + int(label.size()); }
    int key(const Key& k) override { return (k.code == K_CHAR && k.ch == ' ' && !k.mods) ? result : 0; }
    int click(int, int) override { return result; }
    bool arrowsMoveFocus() override { return true; }
};

struct ListBox : Widget {
    int w, h;
    std::vector<u32> items;
    int sel = 0, top = 0;
    bool selectable;
    std::function<void(int)> onChange;
    std::function<int(int)> onActivate;
    Clock::time_point lastT;
    int lastI = -1;
    ListBox(int x_, int y_, int w_, int h_, bool s);
    int ih() const { return h - 2; }
    void setItems(std::vector<u32> it);
    void fix();
    void setSel(int i);
    void draw(Screen& s, int ox, int oy, bool f) override;
    bool hit(int mx, int my) override { return mx >= x && mx < x + w && my >= y && my < y + h; }
    int key(const Key& k) override;
    bool canActivate() override { return selectable && bool(onActivate); }
    int activate() override { return (onActivate && !items.empty()) ? onActivate(sel) : 0; }
    void wheel(int d) override;
    int click(int mx, int my) override;
};

// A modal dialog: a framed box with widgets, Tab/arrow focus handling and mouse support.
//
//     Dialog d(scr, tr(U"Title"), 40, 8);
//     Input* in = d.add<Input>(2, 2, 30, U"");
//     d.addButtons({{tr(U"OK"), 1}, {tr(U"Cancel"), -1}});
//     if (d.run() == 1) use(in->text);
struct Dialog {
    Screen& scr;
    u32 title;
    int w, h, y = -1;
    std::vector<std::unique_ptr<Widget>> ws;
    int focus = -1, def = 0;          // focused widget, result of Enter (0 = none)
    std::function<void()> background;  // draws what is behind the dialog

    Dialog(Screen& s, u32 t, int w_, int h_) : scr(s), title(std::move(t)), w(w_), h(h_) {}

    template <class T, class... A>
    T* add(A&&... a) {
        auto p = std::make_unique<T>(std::forward<A>(a)...);
        T* r = p.get();
        ws.push_back(std::move(p));
        if (focus < 0 && r->focusable) focus = int(ws.size()) - 1;
        return r;
    }

    void addButtons(const std::vector<std::pair<u32, int>>& bs);
    void focusOn(Widget* wd);
    void origin(int& ox, int& oy) const;
    void draw();
    void moveFocus(int d);
    int mouse();

    // Result: the value of the chosen button, -1 for Esc
    int run();
};
