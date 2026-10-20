// Split windows and tabs, with and without Vim mode.

#include "apptest.h"
#include "i18n.h"
#include "languages.h"
#include "menus.h"

namespace {

int nwin(AppT& t) { return int(t.a().windows().size()); }
int ntab(AppT& t) { return int(t.a().tabs.size()); }
std::string textOf(AppT& t, Win* w) {
    Editor& e = t.a().textOf(w->doc);
    std::string r;
    for (size_t i = 0; i < e.lines.size(); i++) r += (i ? "\n" : "") + toUtf8(e.lines[i]);
    return r;
}
std::string screen(AppT& t) {
    std::string r;
    for (int y = 0; y < t.a().scr.h; y++) r += t.row(y) + "\n";
    return r;
}
const char* F6 = "\x1b[17~";
const char* SF6 = "\x1b[17;2~";
const char* CPGDN = "\x1b[6;5~";
const char* CPGUP = "\x1b[5;5~";
const char* F4 = "\x1bOS";

}  // namespace

// ---- windows without Vim mode ----

TEST(tab_close_f4) {
    AppT t("one");
    t.keys(ctrl('t'));
    t.keys(ctrl('t'));
    CHECK_EQ(ntab(t), 3);
    t.keys(F4);
    CHECK_EQ(ntab(t), 2);
    S.vim = true;  // also in Vim mode
    t.keys(F4);
    CHECK_EQ(ntab(t), 1);
    CHECK_EQ(t.text(), "one");
    S.vim = false;
    t.keys(F4);  // the last tab: exits (nothing is modified)
    CHECK(t.a().quit);
}

TEST(win_split_horizontally_shares_the_buffer) {
    AppT t("one\ntwo");
    t.keys(alt('w') + alt('h'));  // Window > Split Horizontally
    CHECK_EQ(nwin(t), 2);
    std::vector<Win*> ws = t.a().windows();
    CHECK(ws[0] == t.a().win);  // the new window is above and active
    CHECK_EQ(ws[0]->doc, ws[1]->doc);
    CHECK(ws[0]->y < ws[1]->y);
    CHECK(ws[0]->sy >= 0 && ws[1]->sy >= 0);  // every window has a status line
    CHECK(t.row(ws[0]->sy).find("Untitled") != std::string::npos);
    t.keys("X");  // both windows show the change
    CHECK_EQ(t.text(), "Xone\ntwo");
    CHECK(t.row(ws[1]->y).find("Xone") != std::string::npos);
    CHECK(t.row(ws[0]->sy).find("Untitled *") != std::string::npos);
}

TEST(win_separate_cursors_and_scrolling) {
    std::string s;
    for (int i = 0; i < 100; i++) s += (i ? "\n" : "") + std::to_string(i);
    AppT t(s);
    t.keys(alt('w') + alt('h'));
    t.keys("\x1b[1;5F");  // Ctrl+End in the top window
    CHECK_EQ(t.cur().y, 99);
    t.keys(F6);  // the other window still shows the start
    CHECK_EQ(t.cur().y, 0);
    CHECK_EQ(t.ed().top, 0);
    t.keys(F6);
    CHECK_EQ(t.cur().y, 99);
    CHECK(t.ed().top > 50);
    t.keys(SF6);
    CHECK_EQ(t.cur().y, 0);
}

TEST(win_split_vertically_and_separator) {
    AppT t("abc");
    t.keys(alt('w') + alt('v'));
    std::vector<Win*> ws = t.a().windows();
    CHECK_EQ(nwin(t), 2);
    CHECK(ws[0]->x < ws[1]->x);
    CHECK_EQ(ws[0]->y, ws[1]->y);
    int sep = ws[0]->x + ws[0]->w;
    CHECK_EQ(t.a().scr.buf[size_t(5 * t.a().scr.w + sep)].ch, char32_t(U'│'));
    CHECK(t.row(ws[1]->y).find("abc") != std::string::npos);
}

TEST(win_nested_splits_and_close) {
    AppT t("x");
    t.keys(alt('w') + alt('h') + alt('w') + alt('v') + alt('w') + alt('h'));
    CHECK_EQ(nwin(t), 4);
    t.keys(ctrl('w'));  // Ctrl+W closes the window
    CHECK_EQ(nwin(t), 3);
    t.keys(alt('w') + alt('o'));  // Close Other Windows
    CHECK_EQ(nwin(t), 1);
    CHECK_EQ(t.a().win->sy, -1);  // a single window has no status line
    CHECK_EQ(t.a().docs.size(), size_t(1));
}

TEST(win_close_asks_for_modified_buffer) {
    AppT t("a");
    t.keys(alt('w') + alt('h') + "X");
    t.keys(ctrl('w'));  // the other window still shows the buffer: no question
    CHECK_EQ(nwin(t), 1);
    CHECK_EQ(t.text(), "Xa");
    t.keys(ctrl('t'));  // a second tab, then back
    t.keys(CPGUP + ctrl('w') + RIGHT + RIGHT + "\r");  // the last window of the buffer: Cancel
    CHECK_EQ(ntab(t), 2);
    t.keys(ctrl('w') + RIGHT + "\r");  // No: the window and the changes go away
    CHECK_EQ(ntab(t), 1);
    CHECK(!t.a().quit);
}

TEST(win_open_in_split_keeps_the_other_window) {
    std::string f = tempDir() + "/win_open.txt";
    writeFile(f, "file text");
    AppT t("first");
    t.keys(alt('w') + alt('h'));
    t.keys(ctrl('o') + f + "\r");
    std::vector<Win*> ws = t.a().windows();
    CHECK_EQ(textOf(t, ws[0]), "file text");
    CHECK_EQ(textOf(t, ws[1]), "first");
    CHECK_EQ(t.a().docs.size(), size_t(2));
    CHECK(t.row(ws[1]->sy).find("Untitled") != std::string::npos);
    CHECK(t.row(ws[0]->sy).find("win_open.txt") != std::string::npos);
    t.keys(alt('f') + alt('n'));  // File > New also keeps the other window
    CHECK_EQ(textOf(t, t.a().windows()[1]), "first");
}

TEST(win_split_needs_room) {
    AppT t("x");
    for (int i = 0; i < 12; i++) t.keys(alt('w') + alt('h') + "\r");  // the error message is closed with Enter
    CHECK(nwin(t) < 12);
    CHECK(t.a().win->h >= 1);
}

TEST(win_mouse_activate_and_resize) {
    AppT t("top\nlines");
    t.keys(alt('w') + alt('h'));
    std::vector<Win*> ws = t.a().windows();
    t.keys(click(ws[1]->x + 2, ws[1]->y) + click(ws[1]->x + 2, ws[1]->y, 0, true));
    CHECK(t.a().win == ws[1]);
    CHECK_EQ(t.cur(), Pos({0, 2}));
    int h0 = ws[0]->h;
    int sy = ws[0]->sy;
    t.keys(click(5, sy) + click(5, sy - 4, 32) + click(5, sy - 4, 0, true));  // drag the status line up
    CHECK_EQ(ws[0]->h, h0 - 4);
    t.keys(alt('w') + alt('v'));
    ws = t.a().windows();
    int sep = ws[0]->x + ws[0]->w;
    int w0 = ws[0]->w;
    t.keys(click(sep, ws[0]->y) + click(sep + 6, ws[0]->y, 32) + click(sep + 6, ws[0]->y, 0, true));
    CHECK_EQ(t.a().windows()[0]->w, w0 + 6);
}

TEST(win_key_during_border_drag) {
    // closing windows with a key while a border is dragged must not leave a dangling drag node
    AppT t("text");
    t.keys(alt('w') + alt('h'));
    t.keys(alt('w') + alt('v'));
    std::vector<Win*> ws = t.a().windows();
    CHECK_EQ(ws.size(), size_t(3));
    int sep = ws[0]->x + ws[0]->w;
    t.keys(click(sep, ws[0]->y));  // press on the border, no release
    CHECK_EQ(t.a().dragging, 4);
    t.keys(ctrl('w') + ctrl('w'));
    CHECK_EQ(t.a().dragging, 0);
    CHECK(t.a().dragNode == nullptr);
    t.keys(click(sep + 4, 2, 32) + click(sep + 4, 2, 0, true));  // the drag goes on with the mouse
    CHECK_EQ(t.a().windows().size(), size_t(1));
}

TEST(win_highlighting_per_buffer) {
    std::string py = tempDir() + "/w.py", c = tempDir() + "/w.c";
    writeFile(py, "def f(): pass\n");
    writeFile(c, "int x;\n");
    AppT t;
    t.a().openFiles({py, c}, 'O');
    t.draw();
    CHECK_EQ(nwin(t), 2);
    std::vector<Win*> ws = t.a().windows();
    CHECK_EQ(std::string(LANGS[ws[0]->doc->lang].name), "Python");
    CHECK_EQ(std::string(LANGS[ws[1]->doc->lang].name), "C");
    CHECK(t.row(24).find("Python") != std::string::npos);
    t.keys(F6);
    CHECK(t.row(24).find("│ C │") != std::string::npos);
    // both windows are colored: "def" and "int" are keywords / types
    CHECK_EQ(int(t.cell(ws[0]->x, ws[0]->y).fg), int(HLCOL[H_KEYWORD]));
    CHECK(int(t.cell(ws[1]->x, ws[1]->y).fg) != int(Col::TEXT.fg));
}

TEST(win_open_files_from_command_line) {
    std::string a = tempDir() + "/wa.txt", b = tempDir() + "/wb.txt", c = tempDir() + "/wc.txt";
    writeFile(a, "A");
    writeFile(b, "B");
    writeFile(c, "C");
    {
        AppT t;
        t.a().openFiles({a, b, c}, 't');
        t.draw();
        CHECK_EQ(ntab(t), 3);
        CHECK_EQ(t.a().curTab, 0);
        CHECK_EQ(t.text(), "A");
        CHECK(t.row(1).find("1 wa.txt") != std::string::npos);
        CHECK(t.row(1).find("3 wc.txt") != std::string::npos);
    }
    {
        AppT t;
        t.a().openFiles({a, b, c}, 'o');
        std::vector<Win*> ws = t.a().windows();
        CHECK_EQ(nwin(t), 3);
        CHECK_EQ(textOf(t, ws[0]), "A");
        CHECK_EQ(textOf(t, ws[1]), "B");
        CHECK_EQ(textOf(t, ws[2]), "C");
        CHECK(ws[0]->y < ws[1]->y && ws[1]->y < ws[2]->y);
    }
}

// ---- tabs without Vim mode ----

TEST(tab_new_switch_close) {
    AppT t("first");
    t.keys(ctrl('t'));
    CHECK_EQ(ntab(t), 2);
    CHECK_EQ(t.a().curTab, 1);
    CHECK_EQ(t.text(), "");
    CHECK(t.row(1).find("1 Untitled") != std::string::npos);
    CHECK(t.row(1).find("2 Untitled") != std::string::npos);
    t.keys("second");
    t.keys(CPGUP);
    CHECK_EQ(t.text(), "first");
    t.keys(CPGDN);
    CHECK_EQ(t.text(), "second");
    t.keys(CPGDN);  // wraps around
    CHECK_EQ(t.text(), "first");
    auto lay = t.a().tabLayout();
    t.keys(click(lay[1].first + 1, 1));
    CHECK_EQ(t.a().curTab, 1);
    t.keys(alt('f') + alt('l') + RIGHT + "\r");  // Close Tab: modified, "No"
    CHECK_EQ(ntab(t), 1);
    CHECK_EQ(t.text(), "first");
    CHECK(t.row(1).find("1 Untitled") == std::string::npos);  // one tab: the file name only
}

TEST(tab_close_by_click_on_x) {
    AppT t("first");
    t.keys(ctrl('t') + "second" + ctrl('t'));
    CHECK_EQ(ntab(t), 3);
    auto lay = t.a().tabLayout();
    const Screen& s = t.a().scr;
    for (auto& [x, len] : lay) CHECK(s.buf[size_t(s.w + x + len - 2)].ch == U'×');
    t.keys(click(lay[2].first + lay[2].second - 2, 1));  // unmodified: closes at once
    CHECK_EQ(ntab(t), 2);
    lay = t.a().tabLayout();
    t.keys(click(lay[1].first + lay[1].second - 2, 1) + RIGHT + RIGHT + "\r");  // modified: Cancel
    CHECK_EQ(ntab(t), 2);
    CHECK_EQ(t.a().curTab, 1);
    t.keys(CPGUP);
    t.keys(click(lay[1].first + lay[1].second - 2, 1) + RIGHT + "\r");  // an inactive tab, "No"
    CHECK_EQ(ntab(t), 1);
    CHECK_EQ(t.text(), "first");
}

TEST(tab_close_by_click_on_x_last_tab) {
    AppT t("");
    t.keys("text");
    auto lay = t.a().tabLayout();
    CHECK_EQ(lay.size(), size_t(1));
    const Screen& s = t.a().scr;
    int x = lay[0].first + lay[0].second - 2;
    CHECK(s.buf[size_t(s.w + x)].ch == U'×');
    CHECK(t.row(1).find("Untitled") != std::string::npos);
    t.keys(click(x, 1) + RIGHT + RIGHT + "\r");  // modified: Cancel
    CHECK(!t.a().quit);
    t.keys(click(x, 1) + RIGHT + "\r");  // "No": the last tab exits like F4
    CHECK(t.a().quit);
}

TEST(tab_close_in_file_menu) {
    bool inFile = false, inWindow = false;
    for (auto& it : MENUS[0].items) inFile |= it.act == A_CLOSETAB;
    for (auto& it : MENUS[4].items) inWindow |= it.act == A_CLOSETAB;
    CHECK(inFile);
    CHECK(!inWindow);
}

TEST(tab_active_window_per_tab) {
    AppT t("a");
    t.keys(alt('w') + alt('h') + F6);  // tab 1: two windows, the second is active
    Win* w1 = t.a().win;
    t.keys(ctrl('t') + "b");
    t.keys(CPGUP);
    CHECK(t.a().win == w1);
    CHECK_EQ(nwin(t), 2);
}

TEST(tab_open_in_new_tab) {
    std::string f = tempDir() + "/tab_open.txt";
    writeFile(f, "tab file");
    AppT t("orig");
    t.keys(alt('f') + alt('b') + f + "\r");  // File > Open in New Tab
    CHECK_EQ(ntab(t), 2);
    CHECK_EQ(t.text(), "tab file");
    CHECK(t.row(1).find("tab_open.txt") != std::string::npos);
}

TEST(tab_open_in_new_tab_german) {
    std::string f = tempDir() + "/tab_open_de.txt";
    writeFile(f, "neu");
    g_uiLang = UiLang::German;
    buildMenus();
    AppT t("alt");
    t.keys(alt('d') + alt('t') + f + "\r");  // Datei > In neuem Tab öffnen
    CHECK_EQ(ntab(t), 2);
    CHECK_EQ(t.text(), "neu");
    bool inFile = false, inWindow = false;
    for (auto& it : MENUS[0].items) inFile |= it.act == A_OPENTAB;
    for (auto& it : MENUS[4].items) inWindow |= it.act == A_OPENTAB;
    CHECK(inFile);
    CHECK(!inWindow);
}

TEST(tab_exit_asks_for_every_modified_buffer) {
    AppT t("a");
    t.keys("1" + ctrl('t') + "2");
    t.keys(ctrl('q') + RIGHT + "\r" + RIGHT + RIGHT + "\r");  // first: No, second: Cancel
    CHECK(!t.a().quit);
    t.keys(ctrl('q') + RIGHT + "\r" + RIGHT + "\r");  // No, No
    CHECK(t.a().quit);
}

TEST(tab_menu_enabled_states) {
    AppT t("x");
    CHECK(!t.a().enabled(A_NEXTTAB));
    CHECK(!t.a().enabled(A_NEXTWIN));
    t.keys(ctrl('t'));
    CHECK(t.a().enabled(A_NEXTTAB));
    t.keys(alt('w') + alt('v'));
    CHECK(t.a().enabled(A_NEXTWIN));
}

TEST(win_menu_texts_german) {
    g_uiLang = UiLang::German;
    buildMenus();
    AppT t("x");
    t.keys(alt('f') + alt('h'));  // Fenster > Horizontal teilen
    CHECK_EQ(nwin(t), 2);
    t.keys(alt('f') + alt('t'));  // Neuer Tab
    CHECK_EQ(ntab(t), 2);
    CHECK(t.row(1).find("Unbenannt") != std::string::npos);
    sendKeys(ESC);
    t.a().menuLoop(4, true);  // the dropdown is drawn before the Esc is read
    CHECK(screen(t).find("Nächstes Fenster") != std::string::npos);
    CHECK(screen(t).find("Strg+Bild↓") != std::string::npos);
}

// ---- Vim mode ----

TEST(vimwin_ctrl_w_split_move_close) {
    AppT t("abc\ndef");
    S.vim = true;
    t.keys(ctrl('w') + "s");
    CHECK_EQ(nwin(t), 2);
    t.keys(ctrl('w') + "v");
    CHECK_EQ(nwin(t), 3);
    std::vector<Win*> ws = t.a().windows();
    CHECK(t.a().win == ws[0]);
    t.keys(ctrl('w') + "l");
    CHECK(t.a().win == ws[1]);
    t.keys(ctrl('w') + "j");
    CHECK(t.a().win == ws[2]);
    t.keys(ctrl('w') + "k");
    CHECK(t.a().win == ws[0] || t.a().win == ws[1]);
    t.keys(ctrl('w') + "b");
    CHECK(t.a().win == ws[2]);
    t.keys(ctrl('w') + "t");
    CHECK(t.a().win == ws[0]);
    t.keys(ctrl('w') + ctrl('w'));
    CHECK(t.a().win == ws[1]);
    t.keys(ctrl('w') + "W");
    CHECK(t.a().win == ws[0]);
    t.keys("3" + ctrl('w') + "w");  // window number 3
    CHECK(t.a().win == ws[2]);
    t.keys(ctrl('w') + "p");  // the previous window
    CHECK(t.a().win == ws[0]);
    t.keys(ctrl('w') + "c");
    CHECK_EQ(nwin(t), 2);
    t.keys(ctrl('w') + "o");
    CHECK_EQ(nwin(t), 1);
    t.keys(ctrl('w') + "c");
    CHECK(t.row(24).find("E444") != std::string::npos);
}

TEST(vimwin_ex_split_commands) {
    std::string f = tempDir() + "/vw.txt";
    writeFile(f, "other file");
    AppT t("main");
    S.vim = true;
    t.keys(":sp " + f + "\r");
    std::vector<Win*> ws = t.a().windows();
    CHECK_EQ(nwin(t), 2);
    CHECK_EQ(textOf(t, ws[0]), "other file");
    CHECK_EQ(textOf(t, ws[1]), "main");
    t.keys(":vsplit\r");
    CHECK_EQ(nwin(t), 3);
    t.keys(":new\r");
    CHECK_EQ(t.text(), "");
    t.keys(":vnew\r");
    CHECK_EQ(nwin(t), 5);
    t.keys(":only\r");
    CHECK_EQ(nwin(t), 1);
    t.keys(":5split\r");  // with a height
    CHECK_EQ(t.a().win->h, 5);
    t.keys(":resize 3\r");
    CHECK_EQ(t.a().win->h, 3);
    t.keys(":res +2\r");
    CHECK_EQ(t.a().win->h, 5);
    t.keys(ctrl('w') + "-");
    CHECK_EQ(t.a().win->h, 4);
    t.keys("2" + ctrl('w') + "+");
    CHECK_EQ(t.a().win->h, 6);
    t.keys(ctrl('w') + "_");  // as high as possible
    CHECK(t.a().win->h > 15);
    t.keys(ctrl('w') + "=");
    std::vector<Win*> w2 = t.a().windows();
    CHECK(std::abs(w2[0]->h - w2[1]->h) <= 1);
    t.keys(":close\r:vs\r:vertical resize 20\r");
    CHECK_EQ(t.a().win->w, 20);
    t.keys(ctrl('w') + ">");
    CHECK_EQ(t.a().win->w, 21);
    t.keys(":wincmd |\r");
    CHECK(t.a().win->w > 60);
}

TEST(vimwin_quit_semantics) {
    AppT t("text");
    S.vim = true;
    t.keys(":sp\rx");
    t.keys(":q\r");  // the buffer is still shown in the other window
    CHECK_EQ(nwin(t), 1);
    CHECK(!t.a().quit);
    t.keys(":q\r");  // the last window of a modified buffer
    CHECK(t.row(24).find("E37") != std::string::npos);
    CHECK(!t.a().quit);
    t.keys(":new\r:q\r");  // an empty window goes away
    CHECK_EQ(nwin(t), 1);
    t.keys(":tabnew\r:q\r");  // the last window of a tab closes the tab
    CHECK_EQ(ntab(t), 1);
    t.keys(":qa\r");
    CHECK(t.row(24).find("E162") != std::string::npos);
    t.keys(":qa!\r");
    CHECK(t.a().quit);
}

TEST(vimwin_ctrl_w_q_and_close_modified) {
    AppT t("t");
    S.vim = true;
    t.keys(":new\rix" + std::string(ESC));
    t.keys(ctrl('w') + "q");
    CHECK(t.row(24).find("E37") != std::string::npos);
    CHECK_EQ(nwin(t), 2);
    t.keys(":close\r");
    CHECK(t.row(24).find("E37") != std::string::npos);
    t.keys(":close!\r");
    CHECK_EQ(nwin(t), 1);
    CHECK_EQ(t.text(), "t");
    t.keys(":new\rix" + std::string(ESC) + ctrl('w') + "j:only\r");
    CHECK(t.row(24).find("E445") != std::string::npos);
    t.keys(":only!\r");
    CHECK_EQ(nwin(t), 1);
}

TEST(vimwin_write_all) {
    std::string a = tempDir() + "/vwa.txt", b = tempDir() + "/vwb.txt";
    writeFile(a, "a");
    writeFile(b, "b");
    AppT t;
    S.vim = true;
    t.a().openFiles({a, b}, 'o');
    t.keys("x" + ctrl('w') + "jx");
    t.keys(":wa\r");
    CHECK_EQ(readFile(a), "");
    CHECK_EQ(readFile(b), "");
    t.keys("ia" + std::string(ESC) + ctrl('w') + "kib" + std::string(ESC) + ":wqa\r");
    CHECK_EQ(readFile(a), "b");
    CHECK_EQ(readFile(b), "a");
    CHECK(t.a().quit);
}

TEST(vimwin_tabs) {
    AppT t("one");
    S.vim = true;
    t.keys(":tabnew\r");
    CHECK_EQ(ntab(t), 2);
    t.keys("itwo" + std::string(ESC) + ":tabe\r");
    CHECK_EQ(ntab(t), 3);
    CHECK_EQ(t.a().curTab, 2);
    t.keys("gt");
    CHECK_EQ(t.a().curTab, 0);
    t.keys("gT");
    CHECK_EQ(t.a().curTab, 2);
    t.keys("2gt");
    CHECK_EQ(t.a().curTab, 1);
    CHECK_EQ(t.text(), "two");
    t.keys(":tabn 1\r");
    CHECK_EQ(t.a().curTab, 0);
    t.keys(":tabp\r");
    CHECK_EQ(t.a().curTab, 2);
    t.keys(":tabfirst\r");
    CHECK_EQ(t.a().curTab, 0);
    t.keys(":tablast\r");
    CHECK_EQ(t.a().curTab, 2);
    t.keys(":tabmove 0\r");
    CHECK_EQ(t.a().curTab, 0);
    CHECK_EQ(t.text(), "");
    t.keys(":tabmove\r");
    CHECK_EQ(t.a().curTab, 2);
    t.keys("\x1b[6;5~");  // Ctrl+PageDown
    CHECK_EQ(t.a().curTab, 0);
    t.keys(":tabs\r" + std::string(ESC));
    t.keys(":tabclose\r");  // like Vim: the tab page on the right becomes the current one
    CHECK_EQ(ntab(t), 2);
    CHECK_EQ(t.text(), "two");
    t.keys("gt");
    CHECK_EQ(t.text(), "");
    t.keys(":tabonly\r");
    CHECK(t.row(24).find("E445") != std::string::npos);  // "two" is modified in the other tab
    t.keys(":tabonly!\r");
    CHECK_EQ(ntab(t), 1);
    t.keys(":tabclose\r");
    CHECK(t.row(24).find("E784") != std::string::npos);
}

TEST(vimwin_ctrl_w_T_moves_to_new_tab) {
    AppT t("x");
    S.vim = true;
    t.keys(ctrl('w') + "v" + ctrl('w') + "T");
    CHECK_EQ(ntab(t), 2);
    CHECK_EQ(nwin(t), 1);
    t.keys("gt");
    CHECK_EQ(nwin(t), 1);
    CHECK_EQ(t.a().docs.size(), size_t(1));  // both show the same buffer
}

TEST(vimwin_marks_per_buffer_registers_shared) {
    std::string f = tempDir() + "/vwm.txt";
    writeFile(f, "second\nfile");
    AppT t("first\nbuffer");
    S.vim = true;
    t.keys("jma");
    t.keys(":sp " + f + "\r");
    t.keys("'a");
    CHECK(t.row(24).find("E20") != std::string::npos);  // mark a belongs to the other buffer
    t.keys("yy" + ctrl('w') + "j");
    t.keys("gg'a");
    CHECK_EQ(t.cur().y, 1);
    t.keys("p");  // registers are shared
    CHECK_EQ(t.text(), "first\nbuffer\nsecond");
    t.keys("u" + ctrl('w') + "k");
    t.keys("u");  // undo is per buffer: nothing to undo here
    CHECK_EQ(t.text(), "second\nfile");
}

TEST(vimwin_edit_in_split_keeps_other_buffer) {
    std::string f = tempDir() + "/vwe.txt";
    writeFile(f, "edited");
    AppT t("orig");
    S.vim = true;
    t.keys(":sp\rx");
    t.a().vim.feed(U(":e " + f) + U"\r");
    std::vector<Win*> ws = t.a().windows();
    CHECK_EQ(textOf(t, ws[0]), "edited");
    CHECK_EQ(textOf(t, ws[1]), "rig");  // unchanged and still modified
    CHECK(t.a().textOf(ws[1]->doc).modified);
    t.keys(":enew\r");
    CHECK_EQ(t.text(), "");
}

TEST(vimwin_status_lines_show_modes_only_in_active) {
    AppT t("abc");
    S.vim = true;
    t.keys(":vs\rv");
    std::vector<Win*> ws = t.a().windows();
    int x0 = ws[0]->x + t.a().gutter(), x1 = ws[1]->x + t.a().gutter();
    CHECK_EQ(int(t.cell(x0, ws[0]->y).bg), int(Col::SEL.bg));   // Visual selection in the active window
    CHECK_EQ(int(t.cell(x1, ws[1]->y).bg), int(Col::TEXT.bg));  // not in the other one
    t.keys(ESC);
}

TEST(vimwin_jumplist_per_window) {
    std::string s;
    for (int i = 0; i < 50; i++) s += (i ? "\n" : "") + std::to_string(i);
    AppT t(s);
    S.vim = true;
    t.keys("20G:sp\r40G");
    t.keys(ctrl('o'));
    CHECK_EQ(t.cur().y, 19);
    t.keys(ctrl('w') + "j" + ctrl('o'));
    CHECK_EQ(t.cur().y, 0);
}
