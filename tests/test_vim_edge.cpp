// Vim emulation edge cases: Unicode, tabs, empty buffers and lines, invalid and
// cancelled input, key codes and key notation.

#include "vimtest.h"

// ---- Unicode ----

TEST(vim_unicode_motions) {
    VT t("‸äöü straße 日本語 x");
    t.k("w");
    CHECK_EQ(t.cur().x, 4);
    t.k("w");
    CHECK_EQ(t.cur().x, 11);  // CJK is a word of its own class
    t.k("e");
    CHECK_EQ(t.cur().x, 13);
    t.k("$");
    CHECK_EQ(t.cur().x, 15);
    t.k("0fß");
    CHECK_EQ(t.cur().x, 8);
    t.k("x");
    CHECK_EQ(t.text(), "äöü strae 日本語 x");
}

TEST(vim_unicode_wide_columns) {
    VT t("‸日本語ab\nabcdefgh");
    t.k("2lj");  // column 4 (two wide characters) in the next line
    CHECK_EQ(t.cur(), Pos({1, 4}));
    t.k("k");
    CHECK_EQ(t.cur(), Pos({0, 2}));
    t.k("5|");  // column 5 is the second half of 語
    CHECK_EQ(t.cur().x, 2);
}

TEST(vim_unicode_text_objects_and_case) {
    VT t("x «‸ünïcödé» y");
    t.k("ciwÄ<Esc>");
    CHECK_EQ(t.text(), "x «Ä» y");
    t.set("‸éa");
    t.k("~~");
    CHECK_EQ(t.text(), "ÉA");
    t.set("‸ΑΒΓ");
    t.k("guiw");
    CHECK_EQ(t.text(), "αβγ");
    t.set("‸привет");
    t.k("gUiw");
    CHECK_EQ(t.text(), "ПРИВЕТ");
}

TEST(vim_unicode_search_and_registers) {
    VT t("‸a€b€c");
    t.k("/€<CR>");
    CHECK_EQ(t.cur().x, 1);
    t.k("n");
    CHECK_EQ(t.cur().x, 3);
    t.k("yl$p");
    CHECK_EQ(t.text(), "a€b€c€");
    t.set("‸🙂 smile");
    t.k("dw");
    CHECK_EQ(t.text(), "smile");
    t.k("i🎉<Esc>");
    CHECK_EQ(t.text(), "🎉smile");
    CHECK_EQ(t.cur().x, 0);
}

TEST(vim_unicode_replace_and_r) {
    VT t("‸abc");
    t.k("rü");
    CHECK_EQ(t.text(), "übc");
    t.k("Rß日<Esc>");
    CHECK_EQ(t.text(), "ß日c");
    t.k("u");
    CHECK_EQ(t.text(), "übc");
}

TEST(vim_unicode_combining) {
    VT t("‸éx");  // e + combining acute accent
    t.k("x");
    CHECK_EQ(t.text(), "́x");
}

// ---- tabs ----

TEST(vim_tabs_cursor_columns) {
    S.tabw = 8;  // the expected columns are for tab stops every 8 columns
    VT t("‸\tab\n12345678ab");
    t.k("j$k");
    CHECK_EQ(t.cur(), Pos({0, 2}));
    t.k("0j");
    CHECK_EQ(t.cur(), Pos({1, 0}));
    t.k("8|k");  // column 8 is inside the tab
    CHECK_EQ(t.cur(), Pos({0, 0}));
    t.k(":set ts=4<CR>");
    t.k("j5|k");
    CHECK_EQ(t.cur(), Pos({0, 1}));
}

TEST(vim_tabs_x_and_insert) {
    VT t("‸\tx");
    t.k("x");
    CHECK_EQ(t.text(), "x");
    t.k("i<Tab><Esc>");
    CHECK_EQ(t.text(), "\tx");
    t.k(":set et ts=4<CR>A<Tab>y<Esc>");
    CHECK_EQ(t.text(), "\tx   y");  // "x" is in column 4, the next tab stop is 8
}

TEST(vim_tabs_shift_mixed) {
    VT t("‸a");
    t.k(":set ts=8 sw=4 noet<CR>>>");
    CHECK_EQ(t.text(), "    a");
    t.k(">>");
    CHECK_EQ(t.text(), "\ta");
    t.k(">>");
    CHECK_EQ(t.text(), "\t    a");
    t.k("<<");
    CHECK_EQ(t.text(), "\ta");
}

TEST(vim_tabs_word_motion) {
    VT t("‸a\t\tb");
    t.k("w");
    CHECK_EQ(t.cur().x, 3);
    t.k("diw");
    t.k("0dw");
    CHECK_EQ(t.text(), "");
}

TEST(vim_tabs_text_object_aw) {
    VT t("x\t‸y\tz");
    t.k("daw");
    CHECK_EQ(t.text(), "x\tz");
}

// ---- empty buffers and lines ----

TEST(vim_empty_buffer_commands) {
    VT t("");
    int b = t.vim.beeps;
    t.k("x");
    t.k("dd");
    CHECK_EQ(t.text(), "");
    t.k("j");
    t.k("k");
    t.k("w");
    t.k("e");
    t.k("$");
    t.k("0");
    t.k("G");
    t.k("gg");
    CHECK_EQ(t.cur(), Pos({0, 0}));
    CHECK(t.vim.beeps > b);
    t.k("yyp");
    CHECK_EQ(t.text(), "\n");
    t.k("ggdG");
    CHECK_EQ(t.text(), "");
    t.k("p");  // the register holds both deleted (empty) lines
    CHECK_EQ(t.text(), "\n\n");
    t.k("u");
    CHECK_EQ(t.text(), "");
    t.k("J~rx>><<==guu");
    CHECK_EQ(t.text(), "");
    t.k("viwd");
    t.k("vd");
    CHECK_EQ(t.text(), "");
    t.k(":%s/x/y/<CR>");
    CHECK(t.msg().find("E486") != std::string::npos);
    t.k(":g/x/d<CR>:sort<CR>:%d<CR>");
    CHECK_EQ(t.text(), "");
    t.k("/x<CR>n*#%");
    CHECK_EQ(t.cur(), Pos({0, 0}));
    t.k("ciw<Esc>");
    CHECK_EQ(t.text(), "");
    t.k("o<Esc>");
    CHECK_EQ(t.text(), "\n");
}

TEST(vim_empty_lines) {
    VT t("a\n\n‸\nb");
    t.k("dd");
    CHECK_EQ(t.text(), "a\n\nb");
    t.k("k");
    CHECK_EQ(t.cur(), Pos({1, 0}));
    t.k("ix<Esc>");
    CHECK_EQ(t.text(), "a\nx\nb");
    t.set("a\n‸\nb");
    t.k("D");
    CHECK_EQ(t.text(), "a\n\nb");
    t.k("A!<Esc>");
    CHECK_EQ(t.text(), "a\n!\nb");
    t.set("a\n‸\nb");
    t.k("p");
    CHECK_EQ(t.text(), "a\n\nb");  // nothing yanked yet
    t.k("yl");
    t.k("vy");
    CHECK_EQ(t.reg('"'), "\n");  // a Visual selection on an empty line is its line break
    t.set("a\n‸\nb");
    t.k("vd");
    CHECK_EQ(t.text(), "a\nb");
}

TEST(vim_empty_line_insert_and_cursor) {
    VT t("‸");
    t.k("a");
    CHECK_EQ(t.cur().x, 0);
    t.k("<Esc>A<Esc>I<Esc>");
    CHECK_EQ(t.cur().x, 0);
    t.k("ra");
    CHECK_EQ(t.text(), "");
}

// ---- invalid and cancelled input ----

TEST(vim_invalid_commands_beep) {
    VT t("‸abc");
    int b = t.vim.beeps;
    t.k("gZ");
    CHECK_EQ(t.vim.beeps, b + 1);
    t.k("z!");
    CHECK_EQ(t.vim.beeps, b + 2);
    t.k("[x");
    CHECK_EQ(t.vim.beeps, b + 3);
    t.k("dix");  // invalid text object
    CHECK_EQ(t.vim.beeps, b + 4);
    t.k("m1");  // invalid mark name
    CHECK_EQ(t.vim.beeps, b + 5);
    t.k("K");
    CHECK_EQ(t.vim.beeps, b + 6);
    CHECK_EQ(t.text(), "abc");
    CHECK_EQ(t.mode(), VM_NORMAL);
    CHECK(t.vim.cmdbuf.empty());
}

TEST(vim_escape_in_normal_mode) {
    VT t("‸abc");
    int b = t.vim.beeps;
    t.k("<Esc>");
    CHECK_EQ(t.vim.beeps, b + 1);  // Esc in Normal mode beeps
    t.k("3<Esc>x");  // a count is cancelled
    CHECK_EQ(t.text(), "bc");
    t.k("\"a<Esc>p");
    CHECK_EQ(t.text(), "bac");  // the register is not used
    t.k("r<Esc>");
    CHECK_EQ(t.text(), "bac");
    t.k("g<Esc>z<Esc>m<Esc>q<Esc>@<Esc>'<Esc>`<Esc>");
    CHECK_EQ(t.text(), "bac");
    CHECK(t.vim.cmdbuf.empty());
    CHECK_EQ(t.vim.recReg, char32_t(0));
}

TEST(vim_ctrl_c_cancels) {
    VT t("‸abc");
    t.k("d<C-c>");
    t.k("i<C-c>");
    CHECK_EQ(t.mode(), VM_NORMAL);
    t.k("v<C-c>");
    CHECK_EQ(t.mode(), VM_NORMAL);
    t.k(":x<C-c>");
    CHECK_EQ(t.mode(), VM_NORMAL);
    CHECK_EQ(t.text(), "abc");
}

TEST(vim_failed_motion_flushes_typeahead) {
    VT t("‸a\nb");
    t.k("jjx");  // the second "j" fails, "x" is flushed like in Vim
    CHECK_EQ(t.text(), "a\nb");
    t.k("x");
    CHECK_EQ(t.text(), "a\n");
}

TEST(vim_invalid_regex_errors) {
    VT t("‸a(b");
    t.k("/\\(<CR>");
    CHECK(t.msg().find("E54") != std::string::npos);
    t.k(":s/a\\{2/x/<CR>");
    CHECK(t.msg().find("E554") != std::string::npos);
    t.k("/[z-a]<CR>");
    CHECK(t.msg().find("E944") != std::string::npos);
    t.k("/a\\)<CR>");
    CHECK(t.msg().find("E55") != std::string::npos);
    CHECK_EQ(t.text(), "a(b");
    t.k("/(<CR>");  // "(" alone is a literal character in a magic pattern
    CHECK_EQ(t.cur().x, 1);
}

TEST(vim_n_without_pattern) {
    VT t("‸a");
    t.k("n");
    CHECK(t.msg().find("E35") != std::string::npos);
    t.k(":s<CR>");
    CHECK(t.msg().find("E35") != std::string::npos);
    t.k(":&&<CR>");
    CHECK(t.msg().find("E35") != std::string::npos);
}

TEST(vim_unknown_keys_in_insert) {
    VT t("‸");
    t.k("ia<C-x>b<C-z>c<Esc>");
    CHECK_EQ(t.text(), "ab\x1a" "c");
}

TEST(vim_external_cursor_changes) {
    VT t("abc\ndef");
    // the application moved the cursor past the end (mouse click)
    t.ed.cy = 0;
    t.ed.cx = 3;
    t.vim.afterExternal();
    CHECK_EQ(t.cur(), Pos({0, 2}));
    // a mouse drag selection becomes Visual mode
    t.ed.anch = true;
    t.ed.anchor = {0, 0};
    t.ed.cy = 1;
    t.ed.cx = 2;
    t.ed.cx = 1;
    t.vim.afterExternal();
    CHECK_EQ(t.mode(), VM_VISUAL);
    CHECK_EQ(t.cur(), Pos({1, 1}));  // the character under the mouse pointer is included
    t.k("d");
    CHECK_EQ(t.text(), "f");
    // a click without dragging ends Visual mode
    t.k("v");
    t.ed.anch = false;
    t.vim.afterExternal();
    CHECK_EQ(t.mode(), VM_NORMAL);
}

TEST(vim_handle_key_passes_app_keys) {
    VT t("x");
    Key k;
    k.code = K_CHAR;
    k.ch = 's';
    k.mods = M_CTRL;
    CHECK(!t.vim.handleKey(k));  // Ctrl+S saves (application)
    k.ch = 'q';
    CHECK(!t.vim.handleKey(k));
    k.code = K_F1;
    k.mods = 0;
    CHECK(!t.vim.handleKey(k));  // function keys
    k.code = K_CHAR;
    k.ch = 'f';
    k.mods = M_ALT;
    CHECK(!t.vim.handleKey(k));  // Alt+letter opens the menus
    k.mods = 0;
    k.ch = 'x';
    CHECK(t.vim.handleKey(k));
    CHECK_EQ(t.text(), "");
}

// ---- key codes ----

TEST(vim_key_codes) {
    Key k;
    k.code = K_CHAR;
    k.ch = 'a';
    CHECK_EQ(vimKeyCode(k), char32_t('a'));
    k.mods = M_CTRL;
    CHECK_EQ(vimKeyCode(k), char32_t(1));
    k.ch = '[';
    CHECK_EQ(vimKeyCode(k), char32_t(27));
    k.ch = ' ';
    CHECK_EQ(vimKeyCode(k), VK::NUL);
    k = Key();
    k.code = K_ESC;
    CHECK_EQ(vimKeyCode(k), char32_t(27));
    k.code = K_ENTER;
    CHECK_EQ(vimKeyCode(k), char32_t(13));
    k.code = K_BKSP;
    CHECK_EQ(vimKeyCode(k), char32_t(8));
    k.code = K_TAB;
    k.mods = M_SHIFT;
    CHECK_EQ(vimKeyCode(k), VK::STAB);
    k.code = K_LEFT;
    k.mods = M_CTRL;
    CHECK_EQ(vimKeyCode(k), VK::key(K_LEFT, M_CTRL));
}

TEST(vim_key_notation_roundtrip) {
    u32 k = vimParseKeys(U"a<Esc><CR><C-v><lt><Up><S-Left><Space><BS><Tab><Bar><Bslash><Nop>x<unknown>");
    CHECK_EQ(k.size(), size_t(13 + 9));
    CHECK_EQ(k[1], char32_t(27));
    CHECK_EQ(k[3], char32_t(22));
    CHECK_EQ(k[4], char32_t('<'));
    CHECK_EQ(k[5], VK::UP);
    CHECK_EQ(k[6], VK::key(K_LEFT, M_SHIFT));
    CHECK_EQ(toUtf8(vimKeyNames(vimParseKeys(U"a<Esc><C-v><Up>"))), "a<Esc><C-v><Up>");
    CHECK_EQ(toUtf8(vimKeyNames(U"\x1c")), "<C-\\>");
}
