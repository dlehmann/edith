// Terminal input decoding, the screen buffer with its diff output, and the dialog widgets.

#include <unistd.h>

#include "screen.h"
#include "terminal.h"
#include "test.h"
#include "widgets.h"

static Key rk(const std::string& bytes) {
    sendKeys(bytes);
    return readKeyRaw();
}

// ---- keyboard and mouse decoding ----

TEST(term_plain_keys) {
    Key k = rk("a");
    CHECK_EQ(k.code, int(K_CHAR));
    CHECK_EQ(k.ch, char32_t('a'));
    k = rk("\xC3\xA4");
    CHECK_EQ(k.ch, char32_t(U'ä'));
    k = rk("\xF0\x9F\x98\x80");
    CHECK_EQ(k.ch, char32_t(U'😀'));
    CHECK_EQ(rk("\r").code, int(K_ENTER));
    CHECK_EQ(rk("\n").code, int(K_ENTER));
    CHECK_EQ(rk("\t").code, int(K_TAB));
    CHECK_EQ(rk("\x7f").code, int(K_BKSP));
    CHECK_EQ(rk("\x08").code, int(K_BKSP));
    k = rk("\x01");
    CHECK_EQ(k.code, int(K_CHAR));
    CHECK_EQ(k.mods, int(M_CTRL));
    CHECK_EQ(k.ch, char32_t('a'));
    k = rk(std::string(1, '\0'));
    CHECK_EQ(k.ch, char32_t(' '));
    CHECK_EQ(k.mods, int(M_CTRL));
}

TEST(term_escape_and_alt) {
    CHECK_EQ(rk("\x1b").code, int(K_ESC));  // alone: Escape after a short wait
    Key k = rk("\x1bx");
    CHECK_EQ(k.code, int(K_CHAR));
    CHECK_EQ(k.mods, int(M_ALT));
    CHECK_EQ(k.ch, char32_t('x'));
    k = rk("\x1b" "F");  // Alt+Shift+F is folded
    CHECK_EQ(k.ch, char32_t('f'));
    k = rk("\x1b\x1b");
    CHECK_EQ(k.code, int(K_ESC));
    CHECK_EQ(readKeyRaw().code, int(K_ESC));
}

TEST(term_cursor_and_function_keys) {
    CHECK_EQ(rk("\x1b[A").code, int(K_UP));
    CHECK_EQ(rk("\x1b[B").code, int(K_DOWN));
    CHECK_EQ(rk("\x1bOC").code, int(K_RIGHT));
    CHECK_EQ(rk("\x1bOD").code, int(K_LEFT));
    Key k = rk("\x1b[1;5C");
    CHECK_EQ(k.code, int(K_RIGHT));
    CHECK_EQ(k.mods, int(M_CTRL));
    k = rk("\x1b[1;2H");
    CHECK_EQ(k.code, int(K_HOME));
    CHECK_EQ(k.mods, int(M_SHIFT));
    CHECK_EQ(rk("\x1b[F").code, int(K_END));
    CHECK_EQ(rk("\x1b[1~").code, int(K_HOME));
    CHECK_EQ(rk("\x1b[4~").code, int(K_END));
    CHECK_EQ(rk("\x1b[2~").code, int(K_INS));
    CHECK_EQ(rk("\x1b[3~").code, int(K_DEL));
    CHECK_EQ(rk("\x1b[5~").code, int(K_PGUP));
    CHECK_EQ(rk("\x1b[6~").code, int(K_PGDN));
    CHECK_EQ(rk("\x1bOP").code, int(K_F1));
    CHECK_EQ(rk("\x1b[13~").code, int(K_F1) + 2);
    CHECK_EQ(rk("\x1b[21~").code, int(K_F1) + 9);
    CHECK_EQ(rk("\x1b[24~").code, int(K_F1) + 11);
    CHECK_EQ(rk("\x1b[[A").code, int(K_F1));  // Linux console
    k = rk("\x1b[Z");
    CHECK_EQ(k.code, int(K_TAB));
    CHECK_EQ(k.mods, int(M_SHIFT));
    CHECK_EQ(rk("\x1b[99~").code, int(K_NONE));
}

TEST(term_other_terminals) {
    // rxvt / urxvt: Shift+arrows "ESC [ a-d", Ctrl+arrows "ESC O a-d", "$ ^ @" instead of "~"
    Key k = rk("\x1b[a");
    CHECK_EQ(k.code, int(K_UP));
    CHECK_EQ(k.mods, int(M_SHIFT));
    k = rk("\x1b[c");
    CHECK_EQ(k.code, int(K_RIGHT));
    CHECK_EQ(k.mods, int(M_SHIFT));
    k = rk("\x1bOd");
    CHECK_EQ(k.code, int(K_LEFT));
    CHECK_EQ(k.mods, int(M_CTRL));
    k = rk("\x1bOb");
    CHECK_EQ(k.code, int(K_DOWN));
    CHECK_EQ(k.mods, int(M_CTRL));
    k = rk("\x1b[3$");
    CHECK_EQ(k.code, int(K_DEL));
    CHECK_EQ(k.mods, int(M_SHIFT));
    k = rk("\x1b[6^");
    CHECK_EQ(k.code, int(K_PGDN));
    CHECK_EQ(k.mods, int(M_CTRL));
    k = rk("\x1b[5@");
    CHECK_EQ(k.code, int(K_PGUP));
    CHECK_EQ(k.mods, int(M_CTRL | M_SHIFT));
    k = rk("\x1b[7~");  // rxvt Home / End
    CHECK_EQ(k.code, int(K_HOME));
    CHECK_EQ(rk("\x1b[8~").code, int(K_END));
    // the Linux console: F1-F5
    CHECK_EQ(rk("\x1b[[A").code, int(K_F1));
    CHECK_EQ(rk("\x1b[[E").code, int(K_F1 + 4));
    // "ESC O" with modifiers
    k = rk("\x1bO2P");
    CHECK_EQ(k.code, int(K_F1));
    CHECK_EQ(k.mods, int(M_SHIFT));
    k = rk("\x1bO5C");
    CHECK_EQ(k.code, int(K_RIGHT));
    CHECK_EQ(k.mods, int(M_CTRL));
    // xterm's modifyOtherKeys: CSI 27 ; mods ; key ~
    k = rk("\x1b[27;5;113~");
    CHECK_EQ(k.code, int(K_CHAR));
    CHECK_EQ(k.mods, int(M_CTRL));
    CHECK_EQ(k.ch, char32_t('q'));
    k = rk("\x1b[27;2;65~");  // Shift+A: just the character
    CHECK_EQ(k.code, int(K_CHAR));
    CHECK_EQ(k.mods, 0);
    CHECK_EQ(k.ch, char32_t('A'));
    k = rk("\x1b[27;5;13~");
    CHECK_EQ(k.code, int(K_ENTER));
    CHECK_EQ(k.mods, int(M_CTRL));
}

TEST(term_kitty_protocol) {
    Key k = rk("\x1b[97;5u");
    CHECK_EQ(k.code, int(K_CHAR));
    CHECK_EQ(k.mods, int(M_CTRL));
    CHECK_EQ(k.ch, char32_t('a'));
    CHECK_EQ(rk("\x1b[27u").code, int(K_ESC));
    CHECK_EQ(rk("\x1b[13u").code, int(K_ENTER));
    CHECK_EQ(rk("\x1b[127u").code, int(K_BKSP));
    k = rk("\x1b[97;1;65u");  // key with text
    CHECK_EQ(k.ch, char32_t('A'));
    CHECK_EQ(rk("\x1b[57443u").code, int(K_NONE));  // Alt pressed
    CHECK(g_altPending);
    CHECK_EQ(rk("\x1b[57443;1:3u").code, int(K_ALTMENU));  // and released alone
    CHECK_EQ(rk("\x1b[97;1:3u").code, int(K_NONE));  // key release events are ignored
    k = rk("\x1b[100;3u");  // Alt+D
    CHECK_EQ(k.mods, int(M_ALT));
    CHECK_EQ(k.ch, char32_t('d'));
    CHECK_EQ(rk("\x1b[57417u").code, int(K_LEFT));  // keypad
    // invalid code points in the text field are dropped, long numbers do not overflow
    k = rk("\x1b[97;1;55296u");
    CHECK_EQ(k.code, int(K_CHAR));
    CHECK_EQ(k.ch, char32_t('a'));
    k = rk("\x1b[97;1;99999999999999999999u");
    CHECK_EQ(k.ch, char32_t('a'));
    CHECK_EQ(rk("\x1b[99999999999999999999;5u").code, int(K_NONE));
    CHECK_EQ(rk("\x1b[57000u").code, int(K_NONE));  // a surrogate as the key code
}

TEST(term_bracketed_paste) {
    Key k = rk("\x1b[200~line 1\r\nline 2\rline 3\x1b[201~");
    CHECK_EQ(k.code, int(K_PASTE));
    CHECK_EQ(toUtf8(g_paste), "line 1\nline 2\nline 3");
}

TEST(term_mouse) {
    Key k = rk("\x1b[<0;10;5M");
    CHECK_EQ(k.code, int(K_MOUSE));
    CHECK(g_mouse.press);
    CHECK_EQ(g_mouse.x, 9);
    CHECK_EQ(g_mouse.y, 4);
    k = rk("\x1b[<0;10;5m");
    CHECK(g_mouse.release);
    k = rk("\x1b[<32;11;5M");
    CHECK(g_mouse.motion);
    k = rk("\x1b[<64;1;1M");
    CHECK_EQ(g_mouse.wheel, -1);
    k = rk("\x1b[<65;1;1M");
    CHECK_EQ(g_mouse.wheel, 1);
    k = rk("\x1b[<4;3;3M");
    CHECK_EQ(g_mouse.mods, int(M_SHIFT));
    k = rk(std::string("\x1b[M") + char(32) + char(33 + 4) + char(33 + 2));  // X10
    CHECK(g_mouse.press);
    CHECK_EQ(g_mouse.x, 4);
    CHECK_EQ(g_mouse.y, 2);
    // coordinates outside the screen are not reported
    CHECK_EQ(rk("\x1b[<0;0;5M").code, int(K_NONE));
    CHECK_EQ(rk("\x1b[<0;5;0M").code, int(K_NONE));
    CHECK_EQ(rk(std::string("\x1b[M") + char(32) + char(20) + char(40)).code, int(K_NONE));
}

TEST(term_timeout_and_pending) {
    CHECK(!inputPending());
    g_keyTimeout = 20;
    CHECK_EQ(readKeyRaw().code, int(K_TIMEOUT));
    g_keyTimeout = -1;
    sendKeys("x");
    CHECK(inputPending());
    readKeyRaw();
    CHECK(!inputPending());
}

// ---- screen ----

// Captures what Screen::flush() writes to stdout
static std::string capture(Screen& s) {
    int p[2];
    if (pipe(p) != 0) return "";
    int save = dup(1);
    dup2(p[1], 1);
    s.flush();
    dup2(save, 1);
    close(save);
    close(p[1]);
    std::string out;
    char b[65536];
    ssize_t n;
    while ((n = read(p[0], b, sizeof b)) > 0) out.append(b, size_t(n));
    close(p[0]);
    return out;
}

static std::string row(Screen& s, int y) {
    u32 r;
    for (int x = 0; x < s.w; x++) {
        char32_t c = s.buf[size_t(y * s.w + x)].ch;
        if (c) r += c;
    }
    while (!r.empty() && r.back() == ' ') r.pop_back();
    return toUtf8(r);
}

TEST(screen_drawing) {
    Screen s;
    s.w = 20;
    s.h = 5;
    s.resize();  // stdout is not a terminal: the size stays
    CHECK_EQ(s.w, 20);
    s.clear();
    s.text(1, 0, U"hello", Attr{15, 1});
    CHECK_EQ(row(s, 0), " hello");
    CHECK_EQ(int(s.buf[1].fg), 15);
    s.put(19, 1, U'日', Col::TEXT);  // a wide character does not fit in the last column
    CHECK_EQ(s.buf[size_t(1 * 20 + 19)].ch, char32_t(' '));
    s.put(0, 1, U'日', Col::TEXT);
    CHECK_EQ(s.buf[size_t(20 + 1)].ch, char32_t(0));
    s.put(-1, 0, 'x', Col::TEXT);  // outside: ignored
    s.put(0, 99, 'x', Col::TEXT);
    int w = s.text(0, 2, U"abcdef", Col::TEXT, 3);
    CHECK_EQ(w, 3);
    CHECK_EQ(row(s, 2), "abc");
    s.fill(0, 3, 4, Col::TEXT, '*');
    CHECK_EQ(row(s, 3), "****");
    s.hot(0, 4, U"&Open", Col::MENU, Col::MHOT);
    CHECK_EQ(row(s, 4), "Open");
    CHECK_EQ(int(s.buf[size_t(4 * 20)].fg), int(Col::MHOT.fg));
    CHECK_EQ(int(s.buf[size_t(4 * 20 + 1)].fg), int(Col::MENU.fg));
    // control characters (C0, DEL, C1) are never written to the terminal
    s.text(0, 0, u32(U"a\x01\x7f\x9b\x9d\xa0z"), Col::TEXT);
    CHECK_EQ(row(s, 0), "a????\u00a0z");
}

TEST(screen_box_and_shadow) {
    Screen s;
    s.w = 12;
    s.h = 6;
    s.resize();
    s.clear();
    s.box(1, 1, 6, 3, Col::DIALOG, U"T");
    CHECK_EQ(s.buf[size_t(1 * 12 + 1)].ch, char32_t(U'┌'));
    CHECK_EQ(s.buf[size_t(3 * 12 + 6)].ch, char32_t(U'┘'));
    CHECK(row(s, 1).find("T") != std::string::npos);
    s.shadow(1, 1, 6, 3);
    CHECK_EQ(int(s.buf[size_t(4 * 12 + 3)].fg), int(Col::SHADOW.fg));
}

TEST(screen_flush_diff_and_colors) {
    Screen s;
    s.w = 10;
    s.h = 2;
    s.resize();
    s.clear();
    s.text(0, 0, U"abc", Attr{15, 4});
    std::string first = capture(s);
    CHECK(first.find("abc") != std::string::npos);
    CHECK(first.find("\x1b[") != std::string::npos);
    std::string second = capture(s);
    CHECK(second.find("abc") == std::string::npos);  // nothing changed: nothing redrawn
    s.text(1, 0, U"X", Attr{15, 4});
    std::string third = capture(s);
    CHECK(third.find('X') != std::string::npos);
    CHECK(third.find("abc") == std::string::npos);
    s.truecolor = true;
    s.force = true;
    std::string tc = capture(s);
    CHECK(tc.find("38;2;") != std::string::npos);  // CGA palette as true color
    s.truecolor = false;
    s.force = true;
    CHECK(capture(s).find("\x1b[0;97;44m") != std::string::npos);  // bright white on blue
    s.boldBright = true;  // Linux console: bright colors as bold
    s.force = true;
    std::string bb = capture(s);
    CHECK(bb.find("\x1b[0;1;37;44m") != std::string::npos);
    CHECK(bb.find("97") == std::string::npos);
    s.boldBright = false;
    s.text(0, 1, U"7", Attr{7, Col::DIMBLACK});  // an extra color: true color only, otherwise black
    s.force = true;
    CHECK(capture(s).find("\x1b[0;37;40m7") != std::string::npos);
    s.truecolor = true;
    s.force = true;
    CHECK(capture(s).find("48;2;0;0;119m7") != std::string::npos);
    s.truecolor = false;
    s.cvis = true;
    s.cx = 3;
    s.cy = 1;
    s.cshape = 5;
    std::string cur = capture(s);
    CHECK(cur.find("\x1b[5 q") != std::string::npos);
    CHECK(cur.find("\x1b[2;4H") != std::string::npos);
}

// ---- widgets ----

TEST(widget_input) {
    Input in(0, 0, 10, U"old");
    Key k;
    k.code = K_CHAR;
    k.ch = 'n';
    in.key(k);  // the first key replaces the text
    CHECK_EQ(toUtf8(in.text), "n");
    k.ch = 'e';
    in.key(k);
    CHECK_EQ(toUtf8(in.text), "ne");
    Key l;
    l.code = K_LEFT;
    in.key(l);
    k.ch = 'X';
    in.key(k);
    CHECK_EQ(toUtf8(in.text), "nXe");
    Key b;
    b.code = K_BKSP;
    in.key(b);
    CHECK_EQ(toUtf8(in.text), "ne");
    Key d;
    d.code = K_DEL;
    in.key(d);
    CHECK_EQ(toUtf8(in.text), "n");
    Key h;
    h.code = K_HOME;
    in.key(h);
    CHECK_EQ(in.pos, 0);
    Key e;
    e.code = K_END;
    in.key(e);
    CHECK_EQ(in.pos, 1);
    g_paste = U"pasted\nsecond";
    Key p;
    p.code = K_PASTE;
    in.key(p);
    CHECK_EQ(toUtf8(in.text), "npasted");
    in.set(U"abc");
    CHECK(in.fresh);
    in.key(b);  // backspace on fresh text clears it
    CHECK_EQ(toUtf8(in.text), "");
}

TEST(widget_check_and_button) {
    Check c(0, 0, U"opt", false);
    Key sp;
    sp.code = K_CHAR;
    sp.ch = ' ';
    c.key(sp);
    CHECK(c.val);
    c.click(0, 0);
    CHECK(!c.val);
    Button b(0, 0, U"OK", 7);
    CHECK_EQ(b.key(sp), 7);
    CHECK_EQ(b.click(0, 0), 7);
    CHECK_EQ(toUtf8(b.label), "< OK >");
}

TEST(widget_listbox) {
    ListBox lb(0, 0, 20, 5, true);
    lb.setItems({U"apple", U"banana", U"cherry", U"avocado", U"date"});
    int changed = -1;
    lb.onChange = [&](int i) { changed = i; };
    Key k;
    k.code = K_DOWN;
    lb.key(k);
    CHECK_EQ(lb.sel, 1);
    CHECK_EQ(changed, 1);
    k.code = K_END;
    lb.key(k);
    CHECK_EQ(lb.sel, 4);
    CHECK(lb.top > 0);  // scrolled
    k.code = K_HOME;
    lb.key(k);
    CHECK_EQ(lb.sel, 0);
    k.code = K_CHAR;
    k.ch = 'a';  // jump to the next entry starting with "a"
    lb.key(k);
    CHECK_EQ(lb.sel, 3);
    lb.onActivate = [](int i) { return i + 100; };
    CHECK(lb.canActivate());
    CHECK_EQ(lb.activate(), 103);
    lb.wheel(1);
    CHECK(lb.top >= 0);
}

TEST(dialog_run_with_keys) {
    Screen s;
    s.w = 80;
    s.h = 25;
    s.resize();
    {
        Dialog d(s, U"Test", 40, 9);
        Input* in = d.add<Input>(2, 2, 20, U"");
        Check* c = d.add<Check>(2, 4, U"opt", false);
        d.addButtons({{U"OK", 1}, {U"Cancel", -1}});
        d.def = 1;
        sendKeys("hello\t \r");  // type, Tab to the check box, toggle, Enter
        CHECK_EQ(d.run(), 1);
        CHECK_EQ(toUtf8(in->text), "hello");
        CHECK(c->val);
    }
    {
        Dialog d(s, U"Test", 40, 9);
        d.add<Input>(2, 2, 20, U"");
        d.addButtons({{U"OK", 1}, {U"Cancel", -1}});
        d.def = 1;
        sendKeys("\x1b[27u");
        CHECK_EQ(d.run(), -1);  // Esc
        sendKeys("\t\t\r");      // Tab to "Cancel"
        CHECK_EQ(d.run(), -1);
        sendKeys("\t\t\x1b[C\r");  // from "Cancel" Tab twice to "OK", Right moves to "Cancel"
        CHECK_EQ(d.run(), -1);
    }
    {
        Dialog d(s, U"Click", 40, 9);
        d.addButtons({{U"OK", 1}, {U"Cancel", 2}});
        int ox, oy;
        d.origin(ox, oy);
        Button* b = nullptr;
        for (auto& w : d.ws)
            if (auto bb = dynamic_cast<Button*>(w.get())) if (bb->result == 2) b = bb;
        CHECK(b != nullptr);
        if (b) {
            std::string click = "\x1b[<0;" + std::to_string(ox + b->x + 2) + ";" + std::to_string(oy + b->y + 1) + "M";
            sendKeys(click);
            CHECK_EQ(d.run(), 2);
        }
    }
}

TEST(dialog_hjkl_moves_like_arrows) {
    Screen s;
    s.w = 80;
    s.h = 25;
    s.resize();
    {
        Dialog d(s, U"Test", 40, 9);
        Input* in = d.add<Input>(2, 2, 20, U"");
        d.addButtons({{U"OK", 1}, {U"Cancel", 2}});
        sendKeys("hjkl\tl\r");  // typed in the input field; Tab to "OK", l moves to "Cancel"
        CHECK_EQ(d.run(), 2);
        CHECK_EQ(toUtf8(in->text), "hjkl");
        sendKeys("h\r");  // from "Cancel" back to "OK"
        CHECK_EQ(d.run(), 1);
        d.def = 1;
        sendKeys("kx\r");  // up from "OK" to the input field, where x is typed
        CHECK_EQ(d.run(), 1);
        CHECK_EQ(toUtf8(in->text), "hjklx");
    }
    {
        Dialog d(s, U"List", 40, 14);
        ListBox* lb = d.add<ListBox>(2, 2, 20, 6, true);
        lb->setItems({U"alpha", U"beta", U"lima", U"hotel"});
        d.addButtons({{U"OK", 1}});
        d.def = 1;
        sendKeys("jj\r");  // j moves down in a list
        CHECK_EQ(d.run(), 1);
        CHECK_EQ(lb->sel, 2);
        sendKeys("k\r");
        CHECK_EQ(d.run(), 1);
        CHECK_EQ(lb->sel, 1);
        sendKeys("h\r");  // h and l jump to an entry with that letter
        CHECK_EQ(d.run(), 1);
        CHECK_EQ(lb->sel, 3);
        sendKeys("l\r");
        CHECK_EQ(d.run(), 1);
        CHECK_EQ(lb->sel, 2);
    }
}
