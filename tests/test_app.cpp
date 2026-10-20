// The application (src/app*.cpp): keyboard, menus, dialogs, mouse, files, drawing and
// the Vim mode integration. Key sequences go through the real input decoder.

#include <chrono>
#include <thread>
#include <unistd.h>

#include "apptest.h"
#include "app.h"
#include "i18n.h"
#include "languages.h"
#include "menus.h"
#include "test.h"


// ---- editing and shortcuts ----

TEST(app_typing_and_editing_keys) {
    AppT t;
    t.keys("hello\rworld");
    CHECK_EQ(t.text(), "hello\nworld");
    t.keys("\x7f\x7f");
    CHECK_EQ(t.text(), "hello\nwor");
    t.keys(std::string(UP) + "\x1b[H" + "\x1b[3~");
    CHECK_EQ(t.text(), "ello\nwor");
    t.keys("\t");
    CHECK_EQ(t.text(), "\tello\nwor");
    t.keys("\x1b[2~X");  // Ins: overwrite mode
    CHECK(S.overwrite);
    CHECK_EQ(t.text(), "\tXllo\nwor");
    t.keys("\x1b[2~");
    CHECK(!S.overwrite);
    CHECK(t.ed().modified);
}

TEST(app_clipboard_shortcuts) {
    AppT t("one two");
    t.keys(ctrl('a') + ctrl('c'));
    CHECK_EQ(toUtf8(t.a().clip), "one two");
    t.keys("\x1b[F" + ctrl('v'));  // End, paste
    CHECK_EQ(t.text(), "one twoone two");
    t.keys(ctrl('a') + ctrl('x'));
    CHECK_EQ(t.text(), "");
    t.keys(ctrl('z'));
    CHECK_EQ(t.text(), "one twoone two");
    t.keys(ctrl('y'));
    CHECK_EQ(t.text(), "");
    t.keys(ctrl('v'));
    CHECK_EQ(t.text(), "one twoone two");
    t.keys("\x1b[1;2D\x1b[1;2D\x1b[2;5~");  // Shift+Left twice, Ctrl+Ins copies
    CHECK_EQ(toUtf8(t.a().clip), "wo");
    t.keys("\x1b[1;5H\x1b[2;2~");  // Ctrl+Home, Shift+Ins pastes
    CHECK_EQ(t.text(), "woone twoone two");
}

TEST(app_shift_selection_and_delete) {
    AppT t("abcdef");
    t.keys("\x1b[1;2C\x1b[1;2C\x1b[3~");
    CHECK_EQ(t.text(), "cdef");
    t.keys("\x1b[1;2F\x1b[3;2~");  // Shift+End, Shift+Del cuts
    CHECK_EQ(t.text(), "");
    CHECK_EQ(toUtf8(t.a().clip), "cdef");
}

TEST(app_bracketed_paste) {
    AppT t("x");
    t.keys("\x1b[200~a\r\nb\x1b[201~");
    CHECK_EQ(t.text(), "a\nbx");
}

// ---- menus ----

TEST(app_menu_with_alt_letter) {
    AppT t("abc");
    t.keys(alt('e') + alt('a'));  // Edit > Select All
    CHECK(t.ed().hasSel());
    t.keys(alt('e') + alt('c'));  // Edit > Copy
    CHECK_EQ(toUtf8(t.a().clip), "abc");
    t.keys(alt('o') + alt('n'));  // Options > Line Numbers
    CHECK_EQ(S.lineNums, 1);
    CHECK(t.a().gutter() > 0);
    CHECK(t.row(2).find("1") != std::string::npos);
    t.keys(alt('o') + alt('r'));
    CHECK_EQ(S.lineNums, 2);
    t.keys(alt('o') + alt('r'));
    CHECK_EQ(S.lineNums, 0);
    t.keys(alt('o') + alt('h'));  // h moves in an open menu: Alt+H chooses "Syntax &Highlighting"
    CHECK(!S.syntax);
    t.keys(alt('o') + alt('a'));
    CHECK(!S.autoindent);
    t.keys(alt('o') + alt('s'));
    CHECK(S.expandtab);
}

TEST(app_menu_with_f10_and_arrows) {
    AppT t("abc");
    t.keys(std::string(F10) + RIGHT + DOWN + DOWN + "\x1b[F" + UP +
           "\r");  // Edit, End = Reformat Code, Up = Select All
    CHECK(t.ed().hasSel());
    t.keys(std::string(F10) + ESC);
    CHECK(!t.a().quit);
    t.keys(std::string(F10) + LEFT + "\r" + ESC);  // Help menu opens and closes
    CHECK(!t.a().quit);
}

TEST(app_menu_drawing_both_languages) {
    {
        AppT t;
        CHECK(t.row(0).find("File") != std::string::npos);
        CHECK(t.row(0).find("Options") != std::string::npos);
        CHECK(t.row(24).find("F1=Help") != std::string::npos);
        CHECK(t.row(1).find("Untitled") != std::string::npos);
    }
    g_uiLang = UiLang::German;
    buildMenus();
    {
        AppT t;
        CHECK(t.row(0).find("Datei") != std::string::npos);
        CHECK(t.row(0).find("Optionen") != std::string::npos);
        CHECK(t.row(24).find("F1=Hilfe") != std::string::npos);
        CHECK(t.row(1).find("Unbenannt") != std::string::npos);
        sendKeys(alt('o'));
        sendKeys(ESC);
        t.keys("");
        t.keys(alt('o') + alt('v'));  // German hotkey of "Vim-Modus"
        CHECK(S.vim);
    }
}

TEST(app_menu_dropdown_drawn) {
    AppT t;
    sendKeys(ESC);
    // menuLoop draws the dropdown before reading the Esc
    CHECK_EQ(t.a().menuLoop(3, true), int(A_NONE));
    bool found = false;
    for (int y = 1; y < 20; y++) found |= t.row(y).find("Vim Mode") != std::string::npos;
    CHECK(found);
}

TEST(app_menu_enabled_and_checked) {
    AppT t("x");
    CHECK(!t.a().enabled(A_UNDO));
    CHECK(!t.a().enabled(A_COPY));
    CHECK(!t.a().enabled(A_PASTE));
    t.keys("y");
    CHECK(t.a().enabled(A_UNDO));
    S.syntax = true;
    CHECK(t.a().checked(A_SYNTAX));
    CHECK(!t.a().checked(A_VIM));
    S.vim = true;
    CHECK(t.a().checked(A_VIM));
}

// ---- dialogs ----

TEST(app_find_and_repeat) {
    AppT t("foo bar\nbar foo\nfoo");
    t.keys(ctrl('f') + "bar\r");
    Pos a, b;
    CHECK(t.ed().sel(a, b));
    CHECK_EQ(a, Pos({0, 4}));
    t.keys("\x1bOR");  // F3
    CHECK(t.ed().sel(a, b));
    CHECK_EQ(a, Pos({1, 0}));
    t.keys(ctrl('f') + "zzz\r" + "\r");  // not found: a message box
    CHECK_EQ(a, Pos({1, 0}));
}

TEST(app_find_options) {
    AppT t("Foo foo foobar");
    t.keys(ctrl('f') + "foo\t \t \r");  // match case, whole word
    Pos a, b;
    CHECK(t.ed().sel(a, b));
    CHECK_EQ(a, Pos({0, 4}));
    CHECK(t.a().findCase);
    CHECK(t.a().findWhole);
}

TEST(app_find_regex) {
    AppT t("id 12 x\nab 345\nnone");
    t.keys(ctrl('f') + "\\d+\t\t\t \r");  // Find What, case, whole word, regular expression
    CHECK(t.a().findRegex);
    Pos a, b;
    CHECK(t.ed().sel(a, b));
    CHECK_EQ(a, Pos({0, 3}));
    CHECK_EQ(b, Pos({0, 5}));
    t.keys("\x1bOR");  // F3 finds the next number
    CHECK(t.ed().sel(a, b));
    CHECK_EQ(a, Pos({1, 3}));
    CHECK_EQ(b, Pos({1, 6}));
    t.keys(ctrl('f') + "(\r" + "\r");  // an invalid pattern: an error message (E54), nothing found
    CHECK(t.ed().sel(a, b));
    CHECK_EQ(a, Pos({1, 3}));
}

TEST(app_replace_regex) {
    AppT t("x=1, y=22\nz=333");
    // regular expression with groups, Change All
    t.keys(ctrl('r') + "(\\w)=(\\d+)\t\\2:\\1\t\t\t \t\t\r" + "\r");
    CHECK(t.a().findRegex);
    CHECK_EQ(t.text(), "1:x, 22:y\n333:z");
    t.keys(ctrl('z'));
    CHECK_EQ(t.text(), "x=1, y=22\nz=333");
    // with confirmation: change, skip, change
    t.keys(ctrl('r') + "\\d+\t<\\0>\r" + "\r" + RIGHT + "\r" + "\r" + "\r");
    CHECK_EQ(t.text(), "x=<1>, y=22\nz=<333>");
    // a line break in the replacement
    t.keys(ctrl('r') + ", \t\\n\t\t\t\t\t\r" + "\r");
    CHECK_EQ(t.text(), "x=<1>\ny=22\nz=<333>");
}

TEST(app_replace_all_and_confirm) {
    AppT t("a b a b a");
    // Find What, Change To, three check boxes, then the second button: Change All
    t.keys(ctrl('r') + "a\tX\t\t\t\t\t\r" + "\r");
    CHECK_EQ(t.text(), "X b X b X");
    t.keys(ctrl('z'));
    t.keys("\x1b[1;5H");  // Ctrl+Home
    // Find and Verify: change, skip, change
    t.keys(ctrl('r') + "a\tY\r" + "\r" + RIGHT + "\r" + "\r" + "\r");
    CHECK_EQ(t.text(), "Y b a b Y");
}

TEST(app_default_tab_width_dialog) {
    AppT t("\tx");
    CHECK_EQ(t.row(2).substr(0, std::string("│    x").size()), "│    x");  // the tab is four columns wide
    t.keys(alt('o') + alt('t') + "\r");  // the dialog starts with 4; Enter keeps it
    CHECK_EQ(S.tabw, 4);
}

TEST(app_goto_and_tab_width) {
    AppT t("1\n2\n3\n4");
    t.keys(ctrl('g') + "3\r");
    CHECK_EQ(t.cur().y, 2);
    t.keys(ctrl('g') + "99\r");
    CHECK_EQ(t.cur().y, 3);
    t.keys(alt('o') + alt('t') + "4\r");
    CHECK_EQ(S.tabw, 4);
    t.keys(alt('o') + alt('t') + "99\r" + "\r");  // invalid: error message
    CHECK_EQ(S.tabw, 4);
}

TEST(app_language_dialog) {
    AppT t("x");
    t.keys(alt('o') + alt('l') + DOWN + "\r");  // Alt+L in the open menu: "None (plain text)"
    CHECK(t.a().doc().langManual);
    CHECK_EQ(t.a().doc().lang, -1);
    CHECK(t.row(24).find("Text") != std::string::npos);
    t.keys(alt('o') + alt('l') + "\x1b[H" + "\r");  // detect automatically
    CHECK(!t.a().doc().langManual);
}

static std::string screenText(AppT& t) {
    std::string r;
    for (int y = 0; y < t.a().scr.h; y++) r += t.row(y) + "\n";
    return r;
}

TEST(app_about_dialog_both_languages) {
    {
        AppT t;
        sendKeys("\r");
        t.a().doAct(A_ABOUT);  // the dialog stays in the screen buffer after Enter
        std::string s = screenText(t);
        CHECK(s.find("About Edith") != std::string::npos);
        CHECK(s.find("A user-friendly console editor") != std::string::npos);
        CHECK(s.find("Written in 2026.") != std::string::npos);
        CHECK(s.find("Released under the MIT License.") != std::string::npos);
        CHECK(s.find("tree-sitter") == std::string::npos);  // no technical details
        CHECK(s.find("C++") == std::string::npos);
    }
    g_uiLang = UiLang::German;
    buildMenus();
    {
        AppT t;
        sendKeys("\r");
        t.a().doAct(A_ABOUT);
        std::string s = screenText(t);
        CHECK(s.find("Über Edith") != std::string::npos);
        CHECK(s.find("Ein benutzerfreundlicher Konsolen-Editor") != std::string::npos);
        CHECK(s.find("Geschrieben 2026.") != std::string::npos);
        CHECK(s.find("Veröffentlicht unter der MIT-Lizenz.") != std::string::npos);
    }
}

TEST(app_help_and_about) {
    AppT t;
    t.keys("\x1bOP" + std::string(ESC));  // F1 opens the help, Esc closes it
    t.keys(alt('h') + alt('a') + "\r");
    CHECK(!t.a().quit);
}

// ---- files ----

TEST(app_save_open_new_exit) {
    std::string f = tempDir() + "/app_file.txt";
    AppT t("content");
    t.ed().modified = true;
    t.keys(ctrl('s') + f + "\r");  // no name yet: Save As dialog
    CHECK_EQ(readFile(f), "content");
    CHECK_EQ(t.ed().filename, f);
    CHECK(!t.ed().modified);
    CHECK(t.row(1).find("app_file.txt") != std::string::npos);
    t.keys("X");
    CHECK(t.row(1).find("app_file.txt *") != std::string::npos);
    t.keys(ctrl('s'));
    CHECK_EQ(readFile(f), "Xcontent");
    t.keys(ctrl('n'));
    CHECK_EQ(t.text(), "");
    t.keys(ctrl('o') + f + "\r");
    CHECK_EQ(t.text(), "Xcontent");
    t.keys("Y" + ctrl('q') + RIGHT + "\r");  // modified: "No" discards and exits
    CHECK(t.a().quit);
}

TEST(app_save_as_overwrite_and_new_file) {
    std::string f = tempDir() + "/app_exists.txt";
    writeFile(f, "old");
    AppT t("new");
    t.keys(alt('f') + alt('a') + f + "\r" + RIGHT + "\r");  // "No" keeps the old file
    CHECK_EQ(readFile(f), "old");
    t.keys(alt('f') + alt('a') + f + "\r" + "\r");  // "Yes" overwrites
    CHECK_EQ(readFile(f), "new");
    std::string g = tempDir() + "/app_created.txt";
    t.keys(ctrl('o') + g + "\r" + "\r");  // does not exist: create it
    CHECK_EQ(t.ed().filename, g);
    CHECK_EQ(t.text(), "");
}

TEST(app_open_path_detects_language) {
    std::string f = tempDir() + "/script.py";
    writeFile(f, "def f():\n    return 1\n");
    AppT t;
    t.a().openPath(f);
    CHECK(t.a().doc().lang >= 0);
    CHECK_EQ(std::string(LANGS[t.a().doc().lang].name), "Python");
    t.draw();
    CHECK(t.row(24).find("Python") != std::string::npos);
    // syntax colors: "def" is a keyword
    int x = t.a().tx();
    CHECK_EQ(int(t.cell(x, 2).fg), int(HLCOL[H_KEYWORD]));
    t.a().openPath(tempDir() + "/missing.c");  // a new file
    CHECK_EQ(std::string(LANGS[t.a().doc().lang].name), "C");
    CHECK_EQ(t.text(), "");
}

TEST(app_open_error) {
    AppT t;
    sendKeys("\r");
    t.a().openPath(tempDir());  // a directory: error message
    CHECK(t.ed().filename.empty());
}

TEST(app_exit_confirm_cancel) {
    AppT t("x");
    t.keys("y" + ctrl('q') + ESC);
    CHECK(!t.a().quit);
    t.keys(ctrl('q') + RIGHT + RIGHT + "\r");  // Cancel
    CHECK(!t.a().quit);
}

// ---- mouse ----

TEST(app_mouse_click_drag_double_click) {
    AppT t("hello world\nsecond");
    int x0 = t.a().tx(), y0 = t.a().ty();
    t.keys(click(x0 + 6, y0) + click(x0 + 6, y0, 0, true));
    CHECK_EQ(t.cur(), Pos({0, 6}));
    t.keys(click(x0 + 0, y0 + 1) + click(x0 + 3, y0 + 1, 32) + click(x0 + 3, y0 + 1, 0, true));
    Pos a, b;
    CHECK(t.ed().sel(a, b));
    CHECK_EQ(a, Pos({1, 0}));
    CHECK_EQ(b, Pos({1, 3}));
    t.keys(click(x0 + 1, y0) + click(x0 + 1, y0, 0, true) + click(x0 + 1, y0) + click(x0 + 1, y0, 0, true));
    CHECK(t.ed().sel(a, b));
    CHECK_EQ(b, Pos({0, 5}));  // double click selects the word
    t.keys(click(x0 + 2, y0 + 1, 4));  // Shift+click extends
    CHECK(t.ed().sel(a, b));
}

TEST(app_mouse_wheel_scrollbars_and_menu) {
    std::string s;
    for (int i = 0; i < 100; i++) s += (i ? "\n" : "") + std::to_string(i);
    AppT t(s);
    t.keys("\x1b[<65;10;10M");
    CHECK_EQ(t.ed().top, 3);
    t.keys("\x1b[<64;10;10M");
    CHECK_EQ(t.ed().top, 0);
    int sx = t.a().scr.w - 1;
    t.keys(click(sx, t.a().ty() + t.a().th() - 1));  // down arrow of the scroll bar
    CHECK_EQ(t.ed().top, 1);
    t.keys(click(sx, t.a().ty() + 10) + click(sx, t.a().ty() + 10, 0, true));
    CHECK(t.ed().top > 1);
    t.keys(click(3, 0) + ESC);  // click on "File" opens the menu
    CHECK(!t.a().quit);
    S.lineNums = 1;
    t.draw();
    t.keys(click(1, t.a().ty()) + click(1, t.a().ty(), 0, true));  // the number column selects the line
    CHECK(t.ed().hasSel());
}

// ---- drawing ----

TEST(app_draw_status_and_window) {
    AppT t("abc\ndef");
    t.keys(DOWN);
    CHECK(t.row(24).find("00002:001") != std::string::npos);
    CHECK(t.row(24).find("│ Text │ UTF-8 │ ") != std::string::npos);  // a new buffer is UTF-8
    CHECK(t.row(1).find("┌") == 0);
    CHECK(t.row(23).find("└") == 0);
    t.a().scr.w = 30;
    t.a().scr.h = 8;
    t.a().scr.resize();
    t.draw();
    CHECK(t.row(0).find("Window too small") != std::string::npos);
}

TEST(app_draw_selection_colors) {
    AppT t("abcdef");
    t.keys("\x1b[1;2C\x1b[1;2C");
    int x = t.a().tx();
    CHECK_EQ(int(t.cell(x, 2).bg), int(Col::SEL.bg));
    CHECK_EQ(int(t.cell(x + 2, 2).bg), int(Col::TEXT.bg));
}

TEST(app_redraw_and_resize) {
    AppT t("x");
    t.keys(ctrl('l'));
    CHECK(t.a().scr.force);
}

// ---- Vim mode ----

TEST(app_vim_toggle_and_insert) {
    AppT t("abc");
    t.keys(alt('o') + alt('v'));
    CHECK(S.vim);
    t.keys("x");
    CHECK_EQ(t.text(), "bc");
    t.keys("i");
    CHECK(t.row(24).find("-- INSERT --") != std::string::npos);
    CHECK_EQ(t.a().scr.cshape, 5);
    t.keys(std::string("Z") + ESC);
    CHECK_EQ(t.text(), "Zbc");
    CHECK_EQ(t.a().scr.cshape, 1);
    t.keys(alt('o') + alt('v'));  // off again: normal Edith keys
    CHECK(!S.vim);
    t.keys("x");
    CHECK_EQ(t.text(), "xZbc");
}

TEST(app_vim_toggle_f12) {
    const char* F12 = "\x1b[24~";
    AppT t("abc");
    t.keys(F12);
    CHECK(S.vim);
    t.keys("iZ");
    t.keys(F12);  // off in Insert mode as well: the function keys stay with Edith
    CHECK(!S.vim);
    t.keys("x");
    CHECK_EQ(t.text(), "Zxabc");
    t.keys(F12);
    CHECK(S.vim);
    S.vim = false;
    // the menus show the keys
    auto menuShows = [&](int menu, const char* item, const char* key) {
        sendKeys(ESC);
        CHECK_EQ(t.a().menuLoop(menu, true), int(A_NONE));
        bool found = false;
        for (int y = 1; y < 20; y++)
            found |= t.row(y).find(item) != std::string::npos && t.row(y).find(key) != std::string::npos;
        return found;
    };
    CHECK(menuShows(3, "Vim Mode", "F12"));
    CHECK(menuShows(0, "Close Tab", "F4"));
}

TEST(app_vim_status_line_german) {
    g_uiLang = UiLang::German;
    buildMenus();
    AppT t("abc");
    S.vim = true;
    t.keys("i");
    CHECK(t.row(24).find("-- EINFÜGEN --") != std::string::npos);
    t.keys(std::string(ESC) + "v");
    CHECK(t.row(24).find("-- VISUELL --") != std::string::npos);
    t.keys(std::string(ESC) + ":foo\r");
    CHECK(t.row(24).find("E492: Kein Editor-Befehl: foo") != std::string::npos);
    CHECK_EQ(int(t.cell(1, 24).bg), int(Col::ERRMSG.bg));
}

TEST(app_vim_cmdline_display_and_cursor) {
    AppT t("abc");
    S.vim = true;
    t.keys(":s/b/X");
    CHECK(t.row(24).find(":s/b/X") == 1);
    CHECK_EQ(t.a().scr.cy, 24);
    CHECK_EQ(t.a().scr.cx, 7);
    t.keys("/\r");
    CHECK_EQ(t.text(), "aXc");
    t.keys("2d");
    CHECK(t.row(24).find("2d") != std::string::npos);  // the pending command
    t.keys(ESC);
}

TEST(app_vim_visual_drawing) {
    AppT t("abcdef\nghij");
    S.vim = true;
    t.keys("lvl");
    int x = t.a().tx();
    CHECK_EQ(int(t.cell(x, 2).bg), int(Col::TEXT.bg));
    CHECK_EQ(int(t.cell(x + 1, 2).bg), int(Col::SEL.bg));
    CHECK_EQ(int(t.cell(x + 2, 2).bg), int(Col::SEL.bg));  // inclusive
    CHECK_EQ(int(t.cell(x + 3, 2).bg), int(Col::TEXT.bg));
    t.keys(std::string(ESC) + "V");
    CHECK_EQ(int(t.cell(x + 5, 2).bg), int(Col::SEL.bg));
    t.keys(std::string(ESC) + "0j" + ctrl('v') + "lk");  // block
    CHECK_EQ(int(t.cell(x + 1, 2).bg), int(Col::SEL.bg));
    CHECK_EQ(int(t.cell(x + 2, 2).bg), int(Col::TEXT.bg));
    CHECK_EQ(int(t.cell(x + 1, 3).bg), int(Col::SEL.bg));
}

TEST(app_vim_hlsearch_drawing) {
    AppT t("foo bar foo");
    S.vim = true;
    t.keys(":set hls\r/foo\r");
    int x = t.a().tx();
    CHECK_EQ(int(t.cell(x + 8, 2).bg), int(Col::SEARCH.bg));
    CHECK_EQ(int(t.cell(x + 4, 2).bg), int(Col::TEXT.bg));
    t.keys(":noh\r");
    CHECK_EQ(int(t.cell(x + 8, 2).bg), int(Col::TEXT.bg));
}

TEST(app_vim_menu_and_shortcuts) {
    std::string f = tempDir() + "/app_vim.txt";
    AppT t("one two");
    S.vim = true;
    t.keys("viw" + alt('e') + alt('c'));  // Edit > Copy in Visual mode
    CHECK_EQ(toUtf8(t.a().clip), "one");
    CHECK_EQ(t.a().vim.mode, VM_NORMAL);
    t.keys("$" + alt('e') + alt('p'));  // Edit > Paste inserts before the cursor (like a bracketed paste)
    CHECK_EQ(t.text(), "one twoneo");
    t.keys(ctrl('a'));  // Ctrl+A is Vim's increment, not Select All
    CHECK(!t.ed().hasSel());
    t.keys(alt('e') + alt('a'));  // Select All: Visual mode
    CHECK(t.a().vim.visualActive());
    t.keys("d");
    CHECK_EQ(t.text(), "");
    t.ed().filename = f;
    t.keys("ihi" + std::string(ESC) + ctrl('s'));  // Ctrl+S saves in Vim mode
    CHECK_EQ(readFile(f), "hi");
    t.keys(":q\r");
    CHECK(t.a().quit);
}

TEST(app_vim_find_dialog_moves_cursor) {
    AppT t("abc foo");
    S.vim = true;
    t.keys(ctrl('f'));  // Ctrl+F is a Vim scroll command: the dialog is in the menu
    t.keys(alt('s') + alt('f') + "foo\r");
    CHECK_EQ(t.cur(), Pos({0, 4}));
    CHECK(!t.a().vim.visualActive());
}

TEST(app_vim_mouse_selection_becomes_visual) {
    AppT t("hello world");
    S.vim = true;
    int x0 = t.a().tx(), y0 = t.a().ty();
    t.keys(click(x0, y0) + click(x0 + 4, y0, 32));
    CHECK(t.a().vim.visualActive());
    t.keys(click(x0 + 4, y0, 0, true) + "d");  // the character under the pointer is included
    CHECK_EQ(t.text(), " world");
    t.keys(click(x0 + 2, y0) + click(x0 + 2, y0, 0, true) + click(x0 + 2, y0) + click(x0 + 2, y0, 0, true));
    CHECK(t.a().vim.visualActive());  // a double click selects the word
    t.keys("y");
    CHECK_EQ(toUtf8(t.a().vim.getReg('"').text()), "world");
}

TEST(app_vim_paste_and_write_quit) {
    std::string f = tempDir() + "/app_vim_wq.txt";
    AppT t("x");
    S.vim = true;
    t.keys("\x1b[200~ab\x1b[201~");  // pasted before the cursor in Normal mode
    CHECK_EQ(t.text(), "abx");
    t.keys(":w " + f + "\r");
    CHECK_EQ(readFile(f), "abx");
    CHECK(t.row(24).find("written") != std::string::npos);
    t.keys("dd:q\r");
    CHECK(!t.a().quit);  // E37
    CHECK(t.row(24).find("E37") != std::string::npos);
    t.keys(":q!\r");
    CHECK(t.a().quit);
}

TEST(app_vim_lines_dialog_and_confirm) {
    AppT t("a a");
    S.vim = true;
    t.keys(":reg\r" + std::string(ESC));  // shown in a dialog, closed with Esc
    t.keys(":s/a/b/gc\r" + std::string("\r") + RIGHT + "\r");  // Yes, then No
    CHECK_EQ(t.text(), "b a");
}

TEST(app_vim_set_filetype_and_syntax) {
    AppT t("x");
    S.vim = true;
    t.keys(":set ft=python\r");
    CHECK(t.a().doc().lang >= 0);
    CHECK_EQ(std::string(LANGS[t.a().doc().lang].name), "Python");
    t.keys(":syntax off\r");
    CHECK(!S.syntax);
    t.keys(":syntax on\r");
    CHECK(S.syntax);
    t.keys(":set ft=nonsense\r");
    CHECK(t.row(24).find("E474") != std::string::npos);
}

TEST(app_vim_run_loop_mapping_timeout) {
    // the main loop waits 'timeoutlen' for the rest of an ambiguous mapping
    AppT t("");
    S.vim = true;
    sendKeys(":set tm=50\r:inoremap jk <Esc>\rij");
    std::thread later([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        sendKeys(std::string(ESC) + ":q!\r");
    });
    t.a().run();
    later.join();
    CHECK(t.a().quit);
    CHECK_EQ(t.text(), "j");  // the "j" was inserted after the timeout
}

TEST(app_vim_mapping_completes_without_timeout) {
    AppT t("");
    S.vim = true;
    t.keys(":inoremap jk <Esc>\rijk");
    CHECK_EQ(t.text(), "");
    CHECK_EQ(t.a().vim.mode, VM_NORMAL);
}

TEST(app_vim_new_file_resets_state) {
    AppT t("abc");
    S.vim = true;
    t.keys("ma");
    t.keys(alt('f') + alt('n'));
    CHECK(t.a().vim.marks.empty());
    CHECK_EQ(t.a().vim.mode, VM_NORMAL);
}

TEST(app_vim_command_line_option) {
    // "edith --vim" sets S.vim before the application starts
    S.vim = true;
    AppT t("x");
    t.keys("dd");
    CHECK_EQ(t.text(), "");
}

TEST(app_vim_fast_escape_is_not_lost) {
    AppT t("abc");
    S.vim = true;
    t.keys("i");
    t.keys("X\x1b:");  // Esc and ":" within a few milliseconds look like Alt+":"
    CHECK_EQ(t.a().vim.mode, VM_CMDLINE);
    t.keys("s/X/Y/\r");
    CHECK_EQ(t.text(), "Yabc");
    t.keys(alt('o') + std::string(ESC));  // a menu hotkey still opens the menu
    CHECK_EQ(t.a().vim.mode, VM_NORMAL);
}

TEST(app_vim_wheel_keeps_cursor_visible) {
    std::string s;
    for (int i = 0; i < 100; i++) s += (i ? "\n" : "") + std::to_string(i);
    AppT t(s);
    S.vim = true;
    t.keys("\x1b[<65;10;10M\x1b[<65;10;10M");
    CHECK_EQ(t.ed().top, 6);
    CHECK_EQ(t.cur().y, 6);
    t.keys("dd");
    CHECK_EQ(toUtf8(t.ed().lines[6]), "7");
}

// ---- every interface language: menus and dialogs show their texts completely ----

TEST(app_every_language_menus_and_dialogs) {
    for (auto& li : uiLanguages()) {
        g_uiLang = li.id;
        buildMenus();
        std::string code = li.code;
        AppT t("text");
        // the menu bar and the status line
        for (auto& m : MENUS)
            if (t.row(0).find(toUtf8(stripAmp(m.title))) == std::string::npos)
                testFail(__FILE__, __LINE__, code + ": menu bar misses " + toUtf8(stripAmp(m.title)));
        u32 hint = statusHint();
        CHECK(t.row(24).find(toUtf8(hint)) != std::string::npos);
        // every dropdown with all its items
        for (size_t mi = 0; mi < MENUS.size(); mi++) {
            sendKeys(ESC);
            t.a().menuLoop(int(mi), true);
            std::string s = screenText(t);
            for (auto& it : MENUS[mi].items) {
                if (!it.label) continue;
                if (s.find(toUtf8(stripAmp(it.label))) == std::string::npos)
                    testFail(__FILE__, __LINE__, code + ": menu item cut off: " + toUtf8(stripAmp(it.label)));
                if (*it.sc && s.find(toUtf8(it.sc)) == std::string::npos)
                    testFail(__FILE__, __LINE__, code + ": shortcut cut off: " + toUtf8(it.sc));
            }
        }
        // dialogs: the labels are complete and the input fields come after them
        auto dialogShows = [&](const char32_t* label, const char* what) {
            std::string s = screenText(t);
            std::string l = toUtf8(tr(label));
            if (s.find(l) == std::string::npos) testFail(__FILE__, __LINE__, code + ": " + what + " misses " + l);
        };
        sendKeys(ESC);
        t.a().findDialog();
        dialogShows(U"Find What:", "find dialog");
        dialogShows(U"Match Upper/Lowercase", "find dialog");
        sendKeys(ESC);
        t.a().replaceDialog();
        dialogShows(U"Change To:", "change dialog");
        dialogShows(U"Find and Verify", "change dialog");
        dialogShows(U"Change All", "change dialog");
        sendKeys(ESC);
        t.a().gotoDialog();
        dialogShows(U"Line number:", "go to dialog");
        sendKeys(ESC);
        t.a().doAct(A_TABWIDTH);
        dialogShows(U"Tab width:", "tab width dialog");
        sendKeys(ESC);
        t.a().fileDialog(tr(U"Save As"), "");
        dialogShows(U"File Name:", "file dialog");
        sendKeys("\r");
        t.a().doAct(A_ABOUT);
        dialogShows(U"About Edith", "about dialog");
        sendKeys(ESC);
        t.a().helpDialog();
        dialogShows(U"Help: Keyboard and Mouse", "help dialog");
        std::string first = toUtf8(helpText()[1]);
        while (!first.empty() && first.back() == ' ') first.pop_back();
        if (screenText(t).find(first.substr(2)) == std::string::npos)
            testFail(__FILE__, __LINE__, code + ": help line cut off: " + first);
        // a message box with the question when exiting
        t.keys("x");
        sendKeys(ESC);
        t.a().confirmDiscard();
        dialogShows(U"Yes", "question");
        dialogShows(U"Cancel", "question");
        g_checks++;
    }
    g_uiLang = UiLang::English;
    buildMenus();
}

TEST(app_draw_status_encoding) {
    std::string bom = tempDir() + "/enc_bom.txt", lat = tempDir() + "/enc_latin1.txt";
    writeFile(bom, "\xEF\xBB\xBF" "abc\n");
    writeFile(lat, "gr\xFC\xDF\n");
    AppT t;
    t.a().openFiles({bom, lat}, 'O');
    t.draw();
    CHECK(t.row(24).find("│ UTF-8 BOM │") != std::string::npos);
    t.keys("\x1b[17~");  // F6: next window
    CHECK(t.row(24).find("│ Latin-1 │") != std::string::npos);
}

TEST(app_show_invisible_characters) {
    AppT t("a\tb  c\n\x01\xC2\xA0x\xE2\x80\x8By\n");
    auto row = [&](int y) {  // the window up to column 40 (without the scroll bar)
        u32 r;
        for (int x = 0; x < 40; x++) r += t.a().scr.buf[size_t(y * t.a().scr.w + x)].ch;
        return toUtf8(r.substr(0, r.find_last_not_of(' ') + 1));
    };
    CHECK_EQ(row(2), "│a   b  c");
    t.keys(alt('o') + alt('i'));  // Options > Show Invisible Characters
    CHECK(S.invisibles);
    CHECK_EQ(row(2), "│a→  b··c¶");
    CHECK_EQ(row(3), "│␁°xy¶");
    CHECK_EQ(row(4), "│");  // the last line has no line break
    CHECK_EQ(t.cell(1, 2).fg, HLCOL[H_TEXT]);
    CHECK_EQ(t.cell(2, 2).fg, Col::INVIS.fg);
    CHECK_EQ(t.cell(6, 2).fg, Col::INVIS.fg);
    CHECK_EQ(t.cell(9, 2).fg, Col::INVIS.fg);
    CHECK_EQ(t.cell(4, 3).bg, Col::ZEROW.bg);  // 'y' follows a zero-width space
    CHECK_EQ(t.cell(3, 3).bg, Col::TEXT.bg);
    // the cursor columns stay the same
    t.keys("\x1b[F");  // End
    CHECK_EQ(t.a().scr.cx, 9);
    t.keys(DOWN);
    CHECK_EQ(t.a().scr.cx, 5);
    // a selection keeps the symbols, CRLF files end their lines with ↵
    t.ed().crlf = true;
    t.keys(ctrl('a'));
    CHECK_EQ(row(2), "│a→  b··c↵");
    CHECK_EQ(t.cell(2, 2).bg, Col::SEL.bg);
    CHECK_EQ(t.cell(9, 2).bg, Col::SEL.bg);
    t.keys(alt('o') + alt('i'));
    CHECK(!S.invisibles);
    CHECK_EQ(row(2), "│a   b  c");
}

TEST(app_draw_status_selection) {
    AppT t("abc\ndef\tgh");
    t.keys("\x1b[C\x1b[1;2C\x1b[1;2C");  // Shift+Right twice from column 2
    CHECK(t.row(24).find("00001:002 - 00001:004 (2)") != std::string::npos);
    t.keys("\x1b[1;2B");  // Shift+Down: "bc", the line break and "def"
    CHECK(t.row(24).find("00001:002 - 00002:004 (6)") != std::string::npos);
    t.keys("\x1b[1;2C\x1b[1;2C");  // over the tab and "g": the end is a screen column
    CHECK(t.row(24).find("00001:002 - 00002:006 (8)") != std::string::npos);
    t.keys("\x1b[C");  // no selection: only the cursor position
    CHECK(t.row(24).find(" - ") == std::string::npos);
    CHECK(t.row(24).find("00002:007") != std::string::npos);
}

TEST(app_draw_status_vim) {
    AppT t("abc\ndefg");
    S.vim = true;
    t.draw();
    // only Vim's messages and, at the right end, "vim": no hint, language, encoding or position
    std::string r = t.row(24);
    std::string end = "│ vim";  // row() trims spaces
    CHECK(r.size() > end.size() && r.substr(r.size() - end.size()) == end);
    CHECK(r.find("F1") == std::string::npos);
    CHECK(r.find("Text") == std::string::npos);
    CHECK(r.find("UTF-8") == std::string::npos);
    CHECK(r.find("00001") == std::string::npos);
    t.keys("lvj");
    r = t.row(24);
    CHECK(r.find("-- VISUAL --") == 1);
    CHECK(r.substr(r.size() - end.size()) == end);
    CHECK(r.find("00001") == std::string::npos);  // no selection range in Vim mode
    t.keys("\x1b[27u");
    S.vim = false;
    t.draw();
    CHECK(t.row(24).find("vim") == std::string::npos);  // only in Vim mode
    CHECK(t.row(24).find("│ Text │ UTF-8 │ ") != std::string::npos);
}

TEST(app_draw_line_number_colors) {
    AppT t("abc\ndef");
    S.lineNums = 1;
    t.draw();
    int g = t.a().gutter(), x = t.a().tx() - g, y = t.a().ty();
    CHECK_EQ(g, 5);  // a blank, three digits, a blank, then a blank in the text color
    for (int i = 0; i < g - 1; i++) {
        CHECK_EQ(int(t.cell(x + i, y + 1).bg), int(Col::DIMBLACK));
        CHECK_EQ(int(t.cell(x + i, y + 5).bg), int(Col::DIMBLACK));  // below the last line
    }
    CHECK_EQ(int(t.cell(x + g - 1, y).bg), int(Col::TEXT.bg));  // the space between numbers and text
    CHECK_EQ(int(t.cell(x + g - 1, y + 5).bg), int(Col::TEXT.bg));
    CHECK_EQ(t.a().scr.buf[size_t(y * t.a().scr.w + x + g - 3)].ch, U'1');
    CHECK_EQ(t.a().scr.buf[size_t(y * t.a().scr.w + x + g)].ch, U'a');
    CHECK_EQ(int(t.cell(x + g - 3, y + 1).fg), 7);  // white numbers
    CHECK_EQ(int(t.cell(x + g - 3, y).fg), 15);     // the cursor line brighter
    CHECK_EQ(int(t.cell(t.a().tx(), y).bg), int(Col::TEXT.bg));  // the text keeps its color
    S.lineNums = 0;
}

TEST(app_vim_toggle_keeps_selection) {
    const char* F12 = "\x1b[24~";
    const char* SRIGHT = "\x1b[1;2C";
    const char* SLEFT = "\x1b[1;2D";
    const char* SDOWN = "\x1b[1;2B";
    auto sel = [](AppT& t, int ay, int ax, int cy, int cx) {
        const Editor& e = t.ed();
        return e.anch && e.anchor.y == ay && e.anchor.x == ax && e.cy == cy && e.cx == cx;
    };
    // a selection becomes Visual mode, the end inclusive, and comes back unchanged
    AppT t("abc\ndef\nghi");
    S.vim = false;
    t.keys(std::string(SRIGHT) + SRIGHT);  // "ab"
    t.keys(F12);
    CHECK(S.vim);
    CHECK(t.a().vim.visualActive());
    CHECK(sel(t, 0, 0, 0, 1));
    CHECK(t.row(24).find("-- VISUAL --") != std::string::npos);
    t.keys(F12);
    CHECK(!S.vim);
    CHECK(sel(t, 0, 0, 0, 2));
    // over a line break and backwards
    t.keys(SDOWN);  // (0,0) to (1,2)
    t.keys(F12);
    CHECK(sel(t, 0, 0, 1, 1));
    t.keys(F12);
    CHECK(sel(t, 0, 0, 1, 2));
    t.keys("\x1b[C");  // no selection
    t.keys(std::string(SLEFT) + SLEFT);  // (1,3) back to (1,1): "ef"
    t.keys(F12);
    CHECK(sel(t, 1, 2, 1, 1));
    t.keys(F12);
    CHECK(sel(t, 1, 3, 1, 1));
    // a selection that ends at the start of a line includes the line break before it
    t.keys("\x1b[H\x1b[A" + std::string(SDOWN));  // (0,0) to (1,0)
    CHECK(sel(t, 0, 0, 1, 0));
    t.keys(F12);
    CHECK(t.a().vim.visualActive());
    t.keys(F12);
    CHECK(sel(t, 0, 0, 1, 0));
    t.keys(F12);
    t.keys("d");  // Vim works on the selection: "abc" and the line break
    CHECK_EQ(t.text(), "def\nghi");
    // Visual line mode becomes whole lines; without a selection nothing is selected
    t.keys("Vj");
    t.keys(F12);
    CHECK(sel(t, 0, 0, 1, 3));  // the last line has no line break
    t.keys(F12);
    t.keys("\x1b[27u");
    CHECK(!t.a().vim.visualActive());
    t.keys(F12);
    CHECK(!S.vim);
    CHECK(!t.ed().hasSel());
    t.keys(F12);
    CHECK(!t.a().vim.visualActive());
    S.vim = false;
}

TEST(app_menu_hjkl_and_alt_letters) {
    AppT t("abc");
    t.keys("x");
    CHECK_EQ(t.text(), "xabc");
    t.keys("\x1b[21~lj\r");  // F10, l to "Edit", j opens it, Enter: Undo
    CHECK_EQ(t.text(), "abc");
    t.keys(alt('o') + "hl\x1b[27u");  // h and l switch menus, the hotkey h is not chosen
    CHECK(S.syntax);
    // entries are chosen with Alt+letter only: plain and Shift+letters do nothing
    t.keys(alt('o') + "a" + "H" + "\x1b[27u");
    CHECK(S.syntax);
    CHECK(S.autoindent);
    t.keys(alt('o') + alt('h'));
    CHECK(!S.syntax);
    S.syntax = true;
    t.keys("\x1b[21~" + std::string("o") + "\x1b[27u");  // in the menu bar as well
    CHECK_EQ(t.text(), "abc");
    t.keys("\x1b[21~" + alt('o') + alt('a'));  // F10, Alt+O, Alt+A: Options > Auto Indent
    CHECK(!S.autoindent);
    S.autoindent = true;
    t.keys(alt('o') + alt('e') + "\x1b[27u");  // no entry "e" in Options: Alt+E opens the Edit menu
    S.vim = true;  // the same in Vim mode
    t.keys(alt('o') + alt('h'));
    CHECK(!S.syntax);
    S.syntax = true;
    S.vim = false;
}
