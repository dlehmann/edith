// Vim registers (":help registers"), the clipboard and undo/redo.

#include "vimtest.h"

// A host with a clipboard (like the application with OSC 52)
struct ClipHost : VimHost {
    int sets = 0;
    void clipboardSet(const u32& t) override {
        clip = t;
        sets++;
    }
};

TEST(vim_reg_named) {
    VT t("‸one two three");
    t.k("\"ayiw");
    CHECK_EQ(t.reg('a'), "one");
    CHECK_EQ(t.reg('"'), "one");  // the unnamed register points to the last used one
    t.k("w\"byiw");
    t.k("$\"ap");
    CHECK_EQ(t.text(), "one two threeone");
    t.k("\"bP");
    CHECK_EQ(t.text(), "one two threeontwoe");
}

TEST(vim_reg_append_uppercase) {
    VT t("‸a\nb");
    t.k("\"ayy");
    t.k("j\"Ayy");
    CHECK_EQ(t.reg('a'), "a\nb\n");
    t.set("‸ab");
    t.k("\"qyl");
    t.k("l\"Qyl");
    CHECK_EQ(t.reg('q'), "ab");
    CHECK_EQ(t.regType('q'), RT_CHAR);
    t.k("\"Qyy");  // appending a line makes it linewise
    CHECK_EQ(t.regType('q'), RT_LINE);
}

TEST(vim_reg_numbered_shift) {
    VT t("‸1\n2\n3\n4");
    t.k("dddddd");
    CHECK_EQ(t.reg('1'), "3\n");
    CHECK_EQ(t.reg('2'), "2\n");
    CHECK_EQ(t.reg('3'), "1\n");
    t.k("\"3p");
    CHECK_EQ(t.text(), "4\n1");
}

TEST(vim_reg_small_delete) {
    VT t("‸abc def\nx");
    t.k("dw");
    CHECK_EQ(t.reg('-'), "abc ");
    CHECK_EQ(t.reg('1'), "");  // small deletes do not go into "1
    t.k("dd");
    CHECK_EQ(t.reg('1'), "def\n");
    CHECK_EQ(t.reg('-'), "abc ");
    t.set("‸(a\nb)");
    t.k("d%");  // some motions always use "1
    CHECK_EQ(t.reg('1'), "(a\nb)\n");  // whole lines: the delete became linewise
}

TEST(vim_reg_yank_zero) {
    VT t("‸keep\ndel");
    t.k("yyjdd");
    CHECK_EQ(t.reg('0'), "keep\n");
    CHECK_EQ(t.reg('"'), "del\n");
    t.k("\"0p");
    CHECK_EQ(t.text(), "keep\nkeep");
}

TEST(vim_reg_black_hole) {
    VT t("‸a\nb");
    t.k("yy");
    t.k("j\"_dd");
    CHECK_EQ(t.reg('"'), "a\n");
    CHECK_EQ(t.text(), "a");
}

TEST(vim_reg_readonly) {
    VT t("‸x");
    t.k("ifoo<Esc>");
    CHECK_EQ(t.reg('.'), "foo");
    t.k(":s/x/y/<CR>");
    CHECK_EQ(t.reg(':'), "s/x/y/");
    t.k("/oo<CR>");
    CHECK_EQ(t.reg('/'), "oo");
    t.ed.filename = "/tmp/name.txt";
    CHECK_EQ(t.reg('%'), "/tmp/name.txt");
    t.k("\".p");
    CHECK_EQ(t.text(), "fofoooy");
    int b = t.vim.beeps;
    t.k("\".yy");  // read-only registers cannot be written
    CHECK_EQ(t.vim.beeps, b + 1);
    t.k("\":dd");
    CHECK_EQ(t.vim.beeps, b + 2);
    CHECK_EQ(t.text(), "fofoooy");
}

TEST(vim_reg_invalid_name) {
    VT t("‸abc");
    int b = t.vim.beeps;
    t.k("\"!yy");
    CHECK_EQ(t.vim.beeps, b + 1);
    t.k("\"#p");
    CHECK_EQ(t.vim.beeps, b + 2);
    CHECK_EQ(t.text(), "abc");
}

TEST(vim_reg_clipboard_plus_star) {
    Editor ed;
    ClipHost host;
    Vim vim(ed, &host);
    ed.lines = {U"hello world"};
    vim.reset();
    vim.feedNotation("\"+yiw");
    CHECK_EQ(toUtf8(host.clip), "hello");
    CHECK_EQ(host.sets, 1);
    host.clip = U"from outside";
    vim.feedNotation("$\"+p");
    CHECK_EQ(toUtf8(ed.lines[0]), "hello worldfrom outside");
    host.clip = U"line\n";
    vim.feedNotation("\"*P");
    CHECK_EQ(ed.n(), 2);  // text ending with a line break is linewise
    CHECK_EQ(toUtf8(ed.lines[0]), "line");
}

TEST(vim_reg_clipboard_unnamedplus) {
    Editor ed;
    ClipHost host;
    Vim vim(ed, &host);
    ed.lines = {U"abc"};
    vim.reset();
    vim.feedNotation(":set clipboard=unnamedplus<CR>yy");
    CHECK_EQ(toUtf8(host.clip), "abc\n");
    host.clip = U"Z";
    vim.feedNotation("p");
    CHECK_EQ(toUtf8(ed.lines[0]), "aZbc");
}

TEST(vim_reg_block_type_kept_in_clipboard) {
    Editor ed;
    ClipHost host;
    Vim vim(ed, &host);
    ed.lines = {U"ab", U"cd"};
    vim.reset();
    vim.feedNotation("<C-v>j\"+y");
    CHECK_EQ(vim.getReg('+').type, RT_BLOCK);
    vim.feedNotation("$\"+p");
    CHECK_EQ(toUtf8(ed.lines[0]), "aba");
    CHECK_EQ(toUtf8(ed.lines[1]), "cdc");
}

TEST(vim_reg_menu_copy_cut_paste) {
    Editor ed;
    ClipHost host;
    Vim vim(ed, &host);
    ed.lines = {U"one two"};
    vim.reset();
    vim.feedNotation("viw");
    vim.menuCopy(false);
    CHECK_EQ(toUtf8(host.clip), "one");
    CHECK_EQ(vim.mode, VM_NORMAL);
    vim.feedNotation("wviw");
    vim.menuCopy(true);
    CHECK_EQ(toUtf8(ed.lines[0]), "one ");
    CHECK_EQ(toUtf8(host.clip), "two");
    vim.feedNotation("0");
    vim.pasteText(U"XY");
    CHECK_EQ(toUtf8(ed.lines[0]), "XYone ");
    vim.feedNotation("u");
    CHECK_EQ(toUtf8(ed.lines[0]), "one ");  // a paste is one undo step
}

TEST(vim_reg_paste_in_modes) {
    VT t("‸ab");
    t.k("i");
    t.vim.pasteText(U"1\n2");
    t.k("<Esc>");
    CHECK_EQ(t.text(), "1\n2ab");
    t.k(":");
    t.vim.pasteText(U"s/a/b/\nignored");
    CHECK_EQ(toUtf8(t.vim.cmdText), "s/a/b/");
    t.k("<Esc>");
    t.set("‸abc");
    t.k("vl");
    t.vim.pasteText(U"Z");  // replaces the selection
    CHECK_EQ(t.text(), "Zc");
}

TEST(vim_reg_list) {
    VT t("‸abc");
    t.k("\"ayy:reg a<CR>");
    CHECK_EQ(t.host.shown.size(), size_t(2));
    CHECK(toUtf8(t.host.shown[1]).find("\"a   abc^J") != std::string::npos);
    t.k(":di<CR>");
    CHECK(t.host.shown.size() >= 3);
}

// ---- undo / redo ----

TEST(vim_undo_groups_commands) {
    VT t("‸a b c");
    t.k("dwdw");
    CHECK_EQ(t.text(), "c");
    t.k("u");
    CHECK_EQ(t.text(), "b c");
    t.k("u");
    CHECK_EQ(t.text(), "a b c");
    t.k("u");  // nothing left
    CHECK_EQ(t.msg(), "Already at oldest change");
    t.k("<C-r><C-r>");
    CHECK_EQ(t.text(), "c");
    t.k("<C-r>");
    CHECK_EQ(t.msg(), "Already at newest change");
}

TEST(vim_undo_insert_is_one_step) {
    VT t("‸");
    t.k("ihello<CR>world<BS><BS>ld<Esc>");
    CHECK_EQ(t.text(), "hello\nworld");
    t.k("u");
    CHECK_EQ(t.text(), "");
}

TEST(vim_undo_change_is_one_step) {
    VT t("‸foo bar");
    t.k("cwxyz<Esc>");
    t.k("u");
    CHECK_EQ(t.text(), "foo bar");
    t.k("<C-r>");
    CHECK_EQ(t.text(), "xyz bar");
}

TEST(vim_undo_count) {
    VT t("‸abcd");
    t.k("xxx");
    t.k("2u");
    CHECK_EQ(t.text(), "bcd");
    t.k("2<C-r>");
    CHECK_EQ(t.text(), "d");
}

TEST(vim_undo_no_change_no_step) {
    VT t("‸a");
    t.k("x");
    t.k("x");   // nothing to delete: no undo step
    t.k("rz");  // ... and this one fails on the empty line
    t.k("u");
    CHECK_EQ(t.text(), "a");
    CHECK(t.ed.undo.empty());
}

TEST(vim_undo_new_change_clears_redo) {
    VT t("‸abc");
    t.k("xu");
    t.k("rZ");
    t.k("<C-r>");
    CHECK_EQ(t.text(), "Zbc");
    CHECK_EQ(t.msg(), "Already at newest change");
}

TEST(vim_undo_dot_and_macro) {
    VT t("‸a b c d");
    t.k("dw..");
    t.k("u");
    CHECK_EQ(t.text(), "c d");
    t.set("‸1 2 3");
    t.k("qqxxq");
    t.k("@q");
    t.k("u");  // every change in a macro is undone separately
    CHECK_EQ(t.text(), " 3");
}

TEST(vim_undo_ex_commands) {
    VT t("a\na\na");
    t.k(":%s/a/b/<CR>");
    CHECK_EQ(t.text(), "b\nb\nb");
    t.k("u");
    CHECK_EQ(t.text(), "a\na\na");
    t.k(":g/a/d<CR>");
    CHECK_EQ(t.text(), "");
    t.k("u");  // :g is one undo step
    CHECK_EQ(t.text(), "a\na\na");
    t.k(":redo<CR>");
    CHECK_EQ(t.text(), "");
    t.k(":undo<CR>");
    CHECK_EQ(t.text(), "a\na\na");
}

TEST(vim_undo_restores_modified_flag) {
    VT t("‸a");
    CHECK(!t.ed.modified);
    t.k("x");
    CHECK(t.ed.modified);
    t.set("‸");
    t.k("x");
    CHECK(!t.ed.modified);
}

TEST(vim_undo_cursor_position) {
    VT t("1\n2\n3\n‸4\n5");
    t.k("ddgg");
    t.k("u");
    CHECK_EQ(t.cur().y, 3);
}
