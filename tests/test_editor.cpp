// The text buffer (src/editor.*): editing, selection, cursor movement, undo/redo,
// searching and file I/O.

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "terminal.h"
#include "test.h"

static Editor mk(const std::vector<const char32_t*>& ls, int y = 0, int x = 0) {
    Editor e;
    e.lines.clear();
    for (auto l : ls) e.lines.push_back(l);
    e.cy = y;
    e.cx = x;
    return e;
}

static std::string txt(const Editor& e) {
    std::string r;
    for (size_t i = 0; i < e.lines.size(); i++) r += (i ? "\n" : "") + toUtf8(e.lines[i]);
    return r;
}

TEST(editor_default_tab_width_is_4) {
    CHECK_EQ(Settings().tabw, 4);
    CHECK_EQ(S.tabw, 4);
    Editor e;
    CHECK_EQ(e.colOf(U"\tx", 1), 4);
}

TEST(editor_columns_with_tabs_and_wide_chars) {
    Editor e;
    S.tabw = 8;
    u32 l = U"\tab日c";
    CHECK_EQ(e.colOf(l, 0), 0);
    CHECK_EQ(e.colOf(l, 1), 8);
    CHECK_EQ(e.colOf(l, 3), 10);
    CHECK_EQ(e.colOf(l, 4), 12);
    CHECK_EQ(e.idxOf(l, 0), 0);
    CHECK_EQ(e.idxOf(l, 7), 0);
    CHECK_EQ(e.idxOf(l, 8), 1);
    CHECK_EQ(e.idxOf(l, 11), 3);
    CHECK_EQ(e.idxOf(l, 99), 5);
    S.tabw = 4;
    CHECK_EQ(e.colOf(l, 1), 4);
}

TEST(editor_type_and_overwrite) {
    Editor e = mk({U"ac"}, 0, 1);
    e.typeChar('b');
    CHECK_EQ(txt(e), "abc");
    CHECK(e.modified);
    S.overwrite = true;
    e.typeChar('X');
    CHECK_EQ(txt(e), "abX");
    e.typeChar('Y');  // at the end overwriting appends
    CHECK_EQ(txt(e), "abXY");
    S.overwrite = false;
}

TEST(editor_type_replaces_selection) {
    Editor e = mk({U"hello world"}, 0, 0);
    e.setCursor(0, 5, true);
    e.typeChar('J');
    CHECK_EQ(txt(e), "J world");
}

TEST(editor_backspace_delete) {
    Editor e = mk({U"ab", U"cd"}, 1, 0);
    e.backspace();
    CHECK_EQ(txt(e), "abcd");
    CHECK_EQ(e.cx, 2);
    e.backspace();
    CHECK_EQ(txt(e), "acd");
    e.delFwd();
    CHECK_EQ(txt(e), "ad");
    e.cx = 2;
    e.delFwd();  // at the end of the last line: nothing
    CHECK_EQ(txt(e), "ad");
    e.cx = 0;
    e.backspace();
    CHECK_EQ(txt(e), "ad");
    e.setCursor(0, 2, true);
    e.backspace();  // deletes the selection
    CHECK_EQ(txt(e), "");
}

TEST(editor_newline_autoindent) {
    S.autoindent = true;
    Editor e = mk({U"    foo"}, 0, 7);
    e.newline();
    CHECK_EQ(txt(e), "    foo\n    ");
    CHECK_EQ(e.cy, 1);
    CHECK_EQ(e.cx, 4);
    S.autoindent = false;
    e.newline();
    CHECK_EQ(e.cx, 0);
    S.autoindent = true;
}

TEST(editor_tab_and_indent) {
    S.expandtab = false;
    Editor e = mk({U"a", U"b", U"c"}, 0, 0);
    e.tab(false);
    CHECK_EQ(txt(e), "\ta\nb\nc");
    S.expandtab = true;
    S.tabw = 4;
    e.cx = 2;
    e.tab(false);
    CHECK_EQ(toUtf8(e.lines[0]), "\ta   ");  // column 5 to the next tab stop (8)
    e = mk({U"a", U"b", U"c"}, 0, 0);
    e.setCursor(2, 0, true);  // selection of two lines (the end in column 0 does not count)
    e.tab(false);
    CHECK_EQ(txt(e), "    a\n    b\nc");
    CHECK(e.hasSel());
    e.tab(true);
    CHECK_EQ(txt(e), "a\nb\nc");
    e = mk({U"\t\tx"}, 0, 3);
    e.indent(true);
    CHECK_EQ(txt(e), "\tx");
    S.expandtab = false;
    S.tabw = 8;
}

TEST(editor_paste_and_get_text) {
    Editor e = mk({U"ad"}, 0, 1);
    e.paste(U"b\nc");
    CHECK_EQ(txt(e), "ab\ncd");
    CHECK_EQ(e.cy, 1);
    CHECK_EQ(e.cx, 1);
    CHECK_EQ(toUtf8(e.getText({0, 1}, {1, 1})), "b\nc");
    e.paste(U"");
    CHECK_EQ(txt(e), "ab\ncd");
    e.deleteRange({0, 1}, {1, 1});
    CHECK_EQ(txt(e), "ad");
}

TEST(editor_select_all_and_word) {
    Editor e = mk({U"foo bar", U"baz"});
    e.selectAll();
    Pos a, b;
    CHECK(e.sel(a, b));
    CHECK_EQ(a, Pos({0, 0}));
    CHECK_EQ(b, Pos({1, 3}));
    e.selectWord(0, 5);
    CHECK(e.sel(a, b));
    CHECK_EQ(a, Pos({0, 4}));
    CHECK_EQ(b, Pos({0, 7}));
    e.selectWord(0, 7);  // just after a word
    CHECK(e.sel(a, b));
    CHECK_EQ(a, Pos({0, 4}));
    e = mk({U"a  b"});
    e.selectWord(0, 2);  // between spaces: no selection
    CHECK(!e.hasSel());
}

TEST(editor_word_left_right) {
    Editor e = mk({U"foo, bar", U"x"});
    int y = 0, x = 0;
    e.wordRight(y, x);
    CHECK_EQ(x, 5);
    e.wordRight(y, x);
    CHECK_EQ(x, 8);
    e.wordRight(y, x);
    CHECK_EQ(Pos({y, x}), Pos({1, 0}));
    e.wordLeft(y, x);
    CHECK_EQ(Pos({y, x}), Pos({0, 8}));
    e.wordLeft(y, x);
    CHECK_EQ(x, 5);
    e.wordLeft(y, x);
    CHECK_EQ(x, 0);
}

TEST(editor_move_keys) {
    Editor e = mk({U"  abc", U"x", U"defgh"}, 0, 4);
    e.moveKey(K_DOWN, 0, 10);
    CHECK_EQ(Pos({e.cy, e.cx}), Pos({1, 1}));
    e.moveKey(K_DOWN, 0, 10);  // wanted column is kept
    CHECK_EQ(Pos({e.cy, e.cx}), Pos({2, 4}));
    e.moveKey(K_UP, 0, 10);
    e.moveKey(K_UP, 0, 10);
    CHECK_EQ(Pos({e.cy, e.cx}), Pos({0, 4}));
    e.moveKey(K_HOME, 0, 10);  // smart home: first the indent
    CHECK_EQ(e.cx, 2);
    e.moveKey(K_HOME, 0, 10);
    CHECK_EQ(e.cx, 0);
    e.moveKey(K_END, 0, 10);
    CHECK_EQ(e.cx, 5);
    e.moveKey(K_RIGHT, 0, 10);  // wraps to the next line
    CHECK_EQ(Pos({e.cy, e.cx}), Pos({1, 0}));
    e.moveKey(K_LEFT, 0, 10);
    CHECK_EQ(Pos({e.cy, e.cx}), Pos({0, 5}));
    e.moveKey(K_END, M_CTRL, 10);
    CHECK_EQ(Pos({e.cy, e.cx}), Pos({2, 5}));
    e.moveKey(K_HOME, M_CTRL, 10);
    CHECK_EQ(Pos({e.cy, e.cx}), Pos({0, 0}));
    e.moveKey(K_RIGHT, M_CTRL | M_SHIFT, 10);  // select by word
    CHECK(e.hasSel());
    e.moveKey(K_PGDN, 0, 2);
    CHECK_EQ(e.cy, 1);
    CHECK(!e.hasSel());
    e.moveKey(K_PGUP, 0, 2);
    CHECK_EQ(e.cy, 0);
}

TEST(editor_ctrl_up_down_scroll) {
    Editor e;
    e.lines.assign(50, U"x");
    e.cy = 0;
    e.moveKey(K_DOWN, M_CTRL, 10);
    CHECK_EQ(e.top, 1);
    CHECK_EQ(e.cy, 1);  // the cursor stays in the view
    e.moveKey(K_UP, M_CTRL, 10);
    CHECK_EQ(e.top, 0);
}

TEST(editor_scroll_to) {
    Editor e;
    e.lines.assign(100, U"0123456789012345678901234567890123456789");
    e.cy = 50;
    e.scrollTo(10, 20);
    CHECK_EQ(e.top, 41);
    e.cy = 10;
    e.scrollTo(10, 20, true);
    CHECK_EQ(e.top, 5);
    e.cx = 35;
    e.scrollTo(10, 20);
    CHECK_EQ(e.left, 16);
    e.cx = 0;
    e.scrollTo(10, 20);
    CHECK_EQ(e.left, 0);
}

TEST(editor_undo_merging) {
    Editor e = mk({U""});
    e.typeChar('a');
    e.typeChar('b');
    e.typeChar('c');
    e.backspace();
    e.backspace();
    CHECK_EQ(txt(e), "a");
    CHECK(e.doUndo());  // the backspaces
    CHECK_EQ(txt(e), "abc");
    CHECK(e.doUndo());  // the typing
    CHECK_EQ(txt(e), "");
    CHECK(!e.doUndo());
    CHECK(e.doRedo());
    CHECK_EQ(txt(e), "abc");
    e.newline();  // a new change clears redo
    CHECK(!e.doRedo());
}

TEST(editor_undo_limit) {
    Editor e = mk({U""});
    for (int i = 0; i < 400; i++) e.checkpoint(0);
    CHECK_EQ(e.undo.size(), size_t(300));
}

TEST(editor_find) {
    Editor e = mk({U"Foo bar", U"foobar foo"});
    Pos s, en;
    CHECK(e.find(U"foo", false, false, {0, 0}, s, en));
    CHECK_EQ(s, Pos({0, 0}));
    CHECK(e.find(U"foo", true, false, {0, 0}, s, en));
    CHECK_EQ(s, Pos({1, 0}));
    CHECK(e.find(U"foo", true, true, {0, 0}, s, en));  // whole words only
    CHECK_EQ(s, Pos({1, 7}));
    CHECK(e.find(U"foo", false, false, {1, 8}, s, en));  // wraps around
    CHECK_EQ(s, Pos({0, 0}));
    CHECK(!e.find(U"xyz", false, false, {0, 0}, s, en));
    CHECK(!e.find(U"", false, false, {0, 0}, s, en));
    CHECK(e.find(U"BAR", false, false, {0, 1}, s, en));
    CHECK_EQ(en, Pos({0, 7}));
}

TEST(editor_replace_all) {
    Editor e = mk({U"a A a", U"aa"});
    CHECK_EQ(e.replaceAll(U"a", U"xy", true, false), 4);
    CHECK_EQ(txt(e), "xy A xy\nxyxy");
    CHECK(e.doUndo());
    CHECK_EQ(txt(e), "a A a\naa");
    CHECK_EQ(e.replaceAll(U"a", U"b", false, true), 3);
    CHECK_EQ(txt(e), "b b b\naa");
    size_t u = e.undo.size();
    CHECK_EQ(e.replaceAll(U"zz", U"y", false, false), 0);
    CHECK_EQ(e.undo.size(), u);  // nothing changed: no undo step
    CHECK(e.doUndo());
    CHECK_EQ(e.replaceAll(U"zz", U"y", false, false), 0);
    CHECK_EQ(e.redo.size(), size_t(1));  // and the redo list stays
}

TEST(editor_load_save_formats) {
    std::string d = tempDir();
    std::string f = d + "/editor_formats.txt";
    writeFile(f, "a\r\nb\r\n");
    Editor e;
    std::string err;
    CHECK(e.load(f, err));
    CHECK(e.crlf);
    CHECK_EQ(txt(e), "a\nb\n");
    bool conv;
    CHECK(e.save(f, err, conv));
    CHECK_EQ(readFile(f), "a\r\nb\r\n");
    CHECK(!e.modified);
    writeFile(f, "\xEF\xBB\xBFx");
    CHECK(e.load(f, err));
    CHECK(e.bom);
    CHECK_EQ(txt(e), "x");
    CHECK(e.save(f, err, conv));
    CHECK_EQ(readFile(f), "\xEF\xBB\xBFx");
    writeFile(f, "gr\xFC\xDF");  // Latin-1
    CHECK(e.load(f, err));
    CHECK(e.latin1);
    CHECK_EQ(txt(e), "grüß");
    CHECK(e.save(f, err, conv));
    CHECK(!conv);
    CHECK_EQ(readFile(f), "gr\xFC\xDF");
    e.lines[0] += U"€";  // not representable in Latin-1
    CHECK(e.save(f, err, conv));
    CHECK(conv);
    CHECK(!e.latin1);
    CHECK_EQ(readFile(f), "grüß€");
    // a surrogate (Vim's Ctrl-V u d800) is saved as U+FFFD: the file stays UTF-8
    e.lines[0] = U"a";
    e.lines[0] += char32_t(0xD800);
    CHECK(e.save(f, err, conv));
    CHECK_EQ(readFile(f), "a\xEF\xBF\xBD");
    CHECK(e.load(f, err));
    CHECK(!e.latin1);
}

TEST(editor_load_errors) {
    Editor e;
    std::string err;
    CHECK(!e.load(tempDir(), err));
    CHECK_EQ(err, "That is a directory.");
    CHECK(!e.load(tempDir() + "/does/not/exist", err));
    CHECK(!err.empty());
    bool conv;
    CHECK(!e.save("/nonexistent-dir/x.txt", err, conv));
}

TEST(editor_save_keeps_the_file) {
    std::string d = tempDir();
    std::string f = d + "/editor_mode.sh";
    writeFile(f, "old");
    chmod(f.c_str(), 0751);
    Editor e;
    std::string err;
    bool conv;
    CHECK(e.load(f, err));
    e.lines[0] = U"new";
    CHECK(e.save(f, err, conv));
    struct stat st;
    CHECK(stat(f.c_str(), &st) == 0 && (st.st_mode & 07777) == 0751);  // permissions kept
    CHECK_EQ(readFile(f), "new");
    // a symbolic link stays a link, the file it points to is written
    std::string sym = d + "/editor_link.sh";
    unlink(sym.c_str());
    CHECK(symlink(f.c_str(), sym.c_str()) == 0);
    e.lines[0] = U"via link";
    CHECK(e.save(sym, err, conv));
    CHECK(lstat(sym.c_str(), &st) == 0 && S_ISLNK(st.st_mode));
    CHECK_EQ(readFile(f), "via link");
    CHECK_EQ(e.filename, sym);
    // a hard link keeps sharing the text
    std::string hard = d + "/editor_hard.sh";
    unlink(hard.c_str());
    CHECK(link(f.c_str(), hard.c_str()) == 0);
    e.lines[0] = U"shared";
    CHECK(e.save(f, err, conv));
    CHECK_EQ(readFile(hard), "shared");
    // no temporary files are left behind
    int others = 0;
    if (DIR* dp = opendir(d.c_str())) {
        while (dirent* de = readdir(dp))
            if (std::string(de->d_name).find(".edith-") != std::string::npos) others++;
        closedir(dp);
    }
    CHECK_EQ(others, 0);
    // a new file
    std::string nf = d + "/editor_new.txt";
    unlink(nf.c_str());
    CHECK(e.save(nf, err, conv));
    CHECK_EQ(readFile(nf), "shared");
}

TEST(editor_load_empty_and_trailing_newline) {
    std::string f = tempDir() + "/editor_empty.txt";
    writeFile(f, "");
    Editor e;
    std::string err;
    CHECK(e.load(f, err));
    CHECK_EQ(e.n(), 1);
    writeFile(f, "x\n");
    CHECK(e.load(f, err));
    CHECK_EQ(e.n(), 2);
    bool conv;
    CHECK(e.save(f, err, conv));
    CHECK_EQ(readFile(f), "x\n");
}

TEST(editor_match_at) {
    CHECK(Editor::matchAt(U"abc", 1, U"BC", false, false));
    CHECK(!Editor::matchAt(U"abc", 1, U"BC", true, false));
    CHECK(!Editor::matchAt(U"abc", 1, U"b", false, true));
    CHECK(Editor::matchAt(U"a b", 2, U"b", false, true));
    CHECK(!Editor::matchAt(U"ab", 1, U"bc", false, false));
}

TEST(editor_dirty_tracking) {
    Editor e = mk({U"a", U"b", U"c"}, 2, 0);
    e.dirty = 100;
    e.typeChar('x');
    CHECK_EQ(e.dirty, 2);
    e.cy = 0;
    e.cx = 0;
    e.typeChar('y');
    CHECK_EQ(e.dirty, 0);
}
