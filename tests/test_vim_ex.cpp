// Vim Command-line mode: ranges, :substitute, :global and the other Ex commands.

#include "vimtest.h"

// ---- ranges ----

TEST(vim_ex_ranges) {
    VT t("1\n2\n3\n4\n5\n6");
    t.k(":2,3d<CR>");
    CHECK_EQ(t.text(), "1\n4\n5\n6");
    t.k(":$d<CR>");
    CHECK_EQ(t.text(), "1\n4\n5");
    t.k("gg:.,+1d<CR>");
    CHECK_EQ(t.text(), "5");
    t.set("1\n2\n3\n4\n5\n6");
    t.k(":%d<CR>");
    CHECK_EQ(t.text(), "");
    t.set("a\nb\nc\nd");
    t.k(":/c/d<CR>");
    CHECK_EQ(t.text(), "a\nb\nd");
    t.k("G:?a?,.-1d<CR>");
    CHECK_EQ(t.text(), "d");
    t.set("1\n2\n3\n4\n5");
    t.k("3Gma:1,'ad<CR>");
    CHECK_EQ(t.text(), "4\n5");
    t.set("1\n2\n3\n4\n5");
    t.k(":2;+1d<CR>");  // ";" makes the first address the current line
    CHECK_EQ(t.text(), "1\n4\n5");
}

TEST(vim_ex_range_invalid) {
    VT t("1\n2");
    t.k(":5d<CR>");
    CHECK(t.msg().find("E16") != std::string::npos);
    CHECK_EQ(t.text(), "1\n2");
    t.k(":'zd<CR>");
    CHECK(t.msg().find("E20") != std::string::npos);
    t.k(":2,1d<CR>");  // a backwards range is swapped
    CHECK_EQ(t.text(), "");
}

TEST(vim_ex_goto_line) {
    VT t("a\nb\n  c\nd");
    t.k(":3<CR>");
    CHECK_EQ(t.cur(), Pos({2, 2}));
    t.k(":$<CR>");
    CHECK_EQ(t.cur().y, 3);
    t.k(":0<CR>");
    CHECK_EQ(t.cur().y, 0);
    t.k(":+2<CR>");
    CHECK_EQ(t.cur().y, 2);
    t.k("``");  // ":N" is a jump
    CHECK_EQ(t.cur().y, 0);
}

// ---- :substitute ----

TEST(vim_ex_substitute_basic) {
    VT t("aaa\naaa");
    t.k(":s/a/b/<CR>");
    CHECK_EQ(t.text(), "baa\naaa");
    t.k(":s/a/c/g<CR>");
    CHECK_EQ(t.text(), "bcc\naaa");
    t.k(":%s/a/d/g<CR>");
    CHECK_EQ(t.text(), "bcc\nddd");
    CHECK_EQ(t.cur(), Pos({1, 0}));  // on the last substituted line
}

TEST(vim_ex_substitute_patterns_and_groups) {
    VT t("John Smith\nJane Doe");
    t.k(":%s/\\(\\w\\+\\) \\(\\w\\+\\)/\\2, \\1/<CR>");
    CHECK_EQ(t.text(), "Smith, John\nDoe, Jane");
    t.k(":%s/\\v(\\w+), (\\w+)/\\2 \\1/<CR>");
    CHECK_EQ(t.text(), "John Smith\nJane Doe");
    t.k(":%s/o/[&]/g<CR>");
    CHECK_EQ(t.text(), "J[o]hn Smith\nJane D[o]e");
    t.k(":%s/\\[\\(.\\)\\]/\\U\\1/g<CR>");
    CHECK_EQ(t.text(), "JOhn Smith\nJane DOe");
    t.k(":%s/\\w\\+/\\u\\L&/g<CR>");
    CHECK_EQ(t.text(), "John Smith\nJane Doe");
    t.k(":1s/John/\\=x/<CR>");  // expressions are not evaluated: literal text
    CHECK_EQ(t.text(), "=x Smith\nJane Doe");
}

TEST(vim_ex_substitute_flags) {
    VT t("Aaa");
    t.k(":s/a/x/gi<CR>");
    CHECK_EQ(t.text(), "xxx");
    t.set("Aaa");
    t.k(":set ic<CR>:s/a/x/gI<CR>");
    CHECK_EQ(t.text(), "Axx");
    t.set("aaa\nbab");
    t.k(":%s/a/x/gn<CR>");  // count only
    CHECK_EQ(t.text(), "aaa\nbab");
    CHECK_EQ(t.msg(), "4 matches on 2 lines");
    t.k(":s/z/y/<CR>");
    CHECK(t.msg().find("E486") != std::string::npos);
    t.k(":s/z/y/e<CR>");  // no error
    CHECK(t.msg().find("E486") == std::string::npos);
    t.k(":s/a/b/q<CR>");
    CHECK(t.msg().find("E488") != std::string::npos);
}

TEST(vim_ex_substitute_count_argument) {
    VT t("a\na\na\na");
    t.k(":s/a/b/ 2<CR>");
    CHECK_EQ(t.text(), "b\nb\na\na");
    t.k(":3s/a/c/g 5<CR>");
    CHECK_EQ(t.text(), "b\nb\nc\nc");
}

TEST(vim_ex_substitute_repeat) {
    VT t("a a\na a\na a");
    t.k(":s/a/b/<CR>j:s<CR>");
    CHECK_EQ(t.text(), "b a\nb a\na a");
    t.k("j&");
    CHECK_EQ(t.text(), "b a\nb a\nb a");
    t.k("gg:s/a/c/g<CR>j:&&<CR>");
    CHECK_EQ(t.text(), "b c\nb c\nb a");
    t.k("g&");  // all lines with the same flags
    CHECK_EQ(t.text(), "b c\nb c\nb c");
    t.set("x y");
    t.k(":s/x/X/<CR>/y<CR>:~<CR>");  // last replacement with the last search pattern
    CHECK_EQ(t.text(), "X X");
}

TEST(vim_ex_substitute_tilde_and_ampersand) {
    VT t("a b");
    t.k(":s/a/x/<CR>:s/b/~y/<CR>");
    CHECK_EQ(t.text(), "x xy");
    t.k(":s/x/\\&/<CR>");
    CHECK_EQ(t.text(), "& xy");
}

TEST(vim_ex_substitute_line_breaks) {
    VT t("a,b,c");
    t.k(":s/,/\\r/g<CR>");
    CHECK_EQ(t.text(), "a\nb\nc");
    t.k(":%s/\\n//<CR>");  // joining lines
    CHECK_EQ(t.text(), "abc");
    t.set("x\ny\nz");
    t.k(":1,2s/$/;/<CR>");
    CHECK_EQ(t.text(), "x;\ny;\nz");
    t.set("one\ntwo");
    t.k(":%s/one\\ntwo/both/<CR>");
    CHECK_EQ(t.text(), "both");
}

TEST(vim_ex_substitute_empty_matches) {
    VT t("abc");
    t.k(":s/x*/-/g<CR>");
    CHECK_EQ(t.text(), "-a-b-c-");
    t.set("abc");
    t.k(":s/b*/-/g<CR>");
    CHECK_EQ(t.text(), "-a-c-");
    t.set("a b");
    t.k(":s/^/> /<CR>");
    CHECK_EQ(t.text(), "> a b");
}

TEST(vim_ex_substitute_other_delimiters_and_empty_pattern) {
    VT t("/usr/bin");
    t.k(":s#/usr#/opt#<CR>");
    CHECK_EQ(t.text(), "/opt/bin");
    t.k(":s+/bin+\\+x+<CR>");
    CHECK_EQ(t.text(), "/opt+x");
    t.k("/opt<CR>:s//O/<CR>");  // empty pattern: the last search pattern
    CHECK_EQ(t.text(), "/O+x");
    t.k(":s/\\//\\|/<CR>");  // an unescaped "|" would end the command (as in Vim)
    CHECK_EQ(t.text(), "|O+x");
}

TEST(vim_ex_substitute_confirm) {
    VT t("a a a a");
    struct H : VimHost {
        std::string answers = "ynaq";
        size_t i = 0;
        int confirm(const u32&) override { return i < answers.size() ? answers[i++] : 'q'; }
    } h;
    Vim v(t.ed, &h);
    v.feedNotation(":s/a/b/gc<CR>");
    CHECK_EQ(t.text(), "b a b b");
    h.answers = "nl";
    h.i = 0;
    t.set("a a a");
    v.feedNotation(":s/a/c/gc<CR>");
    CHECK_EQ(t.text(), "a c a");
    h.answers = "q";
    h.i = 0;
    v.feedNotation(":s/a/d/gc<CR>");
    CHECK_EQ(t.text(), "a c a");
}

TEST(vim_ex_substitute_gdefault) {
    VT t("aa");
    t.k(":set gd<CR>:s/a/b/<CR>");
    CHECK_EQ(t.text(), "bb");
    t.set("aa");
    t.k(":s/a/c/g<CR>");  // "g" inverts 'gdefault'
    CHECK_EQ(t.text(), "ca");
}

TEST(vim_ex_substitute_unicode) {
    VT t("Grüße aus Köln");
    t.k(":s/ö/oe/<CR>:s/\\(ü\\)/[\\1]/<CR>");
    CHECK_EQ(t.text(), "Gr[ü]ße aus Koeln");
    t.k(":s/[äöüß]/_/g<CR>");
    CHECK_EQ(t.text(), "Gr[_]_e aus Koeln");
}

TEST(vim_ex_substitute_message) {
    VT t("a\na\na");
    t.k(":%s/a/b/<CR>");
    CHECK_EQ(t.msg(), "3 substitutions on 3 lines");
}

// ---- :global ----

TEST(vim_ex_global) {
    VT t("foo 1\nbar 2\nfoo 3\nbaz");
    t.k(":g/foo/d<CR>");
    CHECK_EQ(t.text(), "bar 2\nbaz");
    t.set("foo 1\nbar 2\nfoo 3\nbaz");
    t.k(":v/foo/d<CR>");
    CHECK_EQ(t.text(), "foo 1\nfoo 3");
    t.set("foo 1\nbar 2\nfoo 3\nbaz");
    t.k(":g!/foo/d<CR>");
    CHECK_EQ(t.text(), "foo 1\nfoo 3");
    t.set("a\nb\nc");
    t.k(":g/^/m0<CR>");  // reverse the lines
    CHECK_EQ(t.text(), "c\nb\na");
    t.set("x1\ny\nx2");
    t.k(":g/x/s/$/!/<CR>");
    CHECK_EQ(t.text(), "x1!\ny\nx2!");
    t.k(":g/x/normal Aok<CR>");
    CHECK_EQ(t.text(), "x1!ok\ny\nx2!ok");
    t.k(":g/!/t$<CR>");
    CHECK_EQ(t.text(), "x1!ok\ny\nx2!ok\nx1!ok\nx2!ok");
}

TEST(vim_ex_global_deleted_lines_are_skipped) {
    VT t("a\nx\na\ny\nb");
    t.k(":g/a/.,+1d<CR>");
    CHECK_EQ(t.text(), "b");
}

TEST(vim_ex_global_range_and_print) {
    VT t("a1\na2\nb\na3");
    t.k(":2,$g/a/p<CR>");
    CHECK_EQ(t.host.shown.size(), size_t(2));
    t.k(":g/zz/d<CR>");
    CHECK(t.msg().find("Pattern not found") != std::string::npos);
    t.k(":g/a/g/a/d<CR>");
    CHECK(t.msg().find("E147") != std::string::npos);
}

// ---- line commands ----

TEST(vim_ex_delete_yank_put) {
    VT t("1\n2\n3\n4");
    t.k(":2d a<CR>");
    CHECK_EQ(t.text(), "1\n3\n4");
    CHECK_EQ(t.reg('a'), "2\n");
    t.k(":$pu a<CR>");
    CHECK_EQ(t.text(), "1\n3\n4\n2");
    t.k(":1y<CR>:0put<CR>");
    CHECK_EQ(t.text(), "1\n1\n3\n4\n2");
    t.k(":1d 3<CR>");
    CHECK_EQ(t.text(), "4\n2");
    t.k(":2y b 1<CR>");
    CHECK_EQ(t.reg('b'), "2\n");
    t.k(":1pu! b<CR>");
    CHECK_EQ(t.text(), "2\n4\n2");
}

TEST(vim_ex_move_copy) {
    VT t("1\n2\n3\n4");
    t.k(":1m$<CR>");
    CHECK_EQ(t.text(), "2\n3\n4\n1");
    t.k(":3,4m0<CR>");
    CHECK_EQ(t.text(), "4\n1\n2\n3");
    t.k(":1t0<CR>");
    CHECK_EQ(t.text(), "4\n4\n1\n2\n3");
    t.k(":1,2co$<CR>");
    CHECK_EQ(t.text(), "4\n4\n1\n2\n3\n4\n4");
    t.k(":1,3m2<CR>");
    CHECK(t.msg().find("E134") != std::string::npos);
}

TEST(vim_ex_join) {
    VT t("a\nb\nc\nd");
    t.k(":j<CR>");
    CHECK_EQ(t.text(), "a b\nc\nd");
    t.k(":1,3j<CR>");
    CHECK_EQ(t.text(), "a b c d");
    t.set("a\n  b");
    t.k(":j!<CR>");
    CHECK_EQ(t.text(), "a  b");
}

TEST(vim_ex_shift) {
    VT t("a\nb");
    t.k(":set sw=2 et<CR>:%><CR>");
    CHECK_EQ(t.text(), "  a\n  b");
    t.k(":1>><CR>");
    CHECK_EQ(t.text(), "      a\n  b");
    t.k(":%<<CR>");
    CHECK_EQ(t.text(), "    a\nb");
}

TEST(vim_ex_normal) {
    VT t("a\nb\nc");
    t.k(":%norm Ax<CR>");
    CHECK_EQ(t.text(), "ax\nbx\ncx");
    t.k(":norm! 0d$<CR>");
    CHECK_EQ(t.text(), "ax\nbx\n");
    t.k(":1,2normal ihello<CR>");  // an unfinished Insert mode is ended
    CHECK_EQ(t.text(), "helloax\nhellobx\n");
    CHECK_EQ(t.mode(), VM_NORMAL);
    t.k("u");  // one undo step
    CHECK_EQ(t.text(), "ax\nbx\n");
}

TEST(vim_ex_normal_with_mappings) {
    VT t("abc");
    t.k(":nnoremap x dd<CR>");
    t.k(":norm! x<CR>");  // "!" ignores mappings
    CHECK_EQ(t.text(), "bc");
    t.k(":norm x<CR>");
    CHECK_EQ(t.text(), "");
}

TEST(vim_ex_marks) {
    VT t("a\nb\nc");
    t.k(":2ka<CR>:3mark b<CR>");
    t.k("gg'a");
    CHECK_EQ(t.cur().y, 1);
    t.k("'b");
    CHECK_EQ(t.cur().y, 2);
    t.k(":marks<CR>");
    CHECK(t.host.shown.size() >= 3);
    t.k(":delm a<CR>'a");
    CHECK(t.msg().find("E20") != std::string::npos);
    t.k(":delm!<CR>'b");
    CHECK(t.msg().find("E20") != std::string::npos);
    t.k(":k1<CR>");
    CHECK(t.msg().find("E191") != std::string::npos);
}

TEST(vim_ex_sort) {
    VT t("c\na\nb\na");
    t.k(":sort<CR>");
    CHECK_EQ(t.text(), "a\na\nb\nc");
    t.k(":sort!<CR>");
    CHECK_EQ(t.text(), "c\nb\na\na");
    t.k(":sort u<CR>");
    CHECK_EQ(t.text(), "a\nb\nc");
    t.set("x10\nx9\nfoo\nx-1");
    t.k(":sort n<CR>");
    CHECK_EQ(t.text(), "foo\nx-1\nx9\nx10");
    t.set("B\na\nC");
    t.k(":sort i<CR>");
    CHECK_EQ(t.text(), "a\nB\nC");
    t.set("a3 z\nb1 y\nc2 x");
    t.k(":sort /\\a\\d /<CR>");  // sort by what follows the match
    CHECK_EQ(t.text(), "c2 x\nb1 y\na3 z");
    t.k(":sort /\\d/ r<CR>");  // sort by the match
    CHECK_EQ(t.text(), "b1 y\nc2 x\na3 z");
    t.set("0x1F\n0xA\n0x2");
    t.k(":sort x<CR>");
    CHECK_EQ(t.text(), "0x2\n0xA\n0x1F");
    t.set("z\ny\nb\na");
    t.k(":2,3sort<CR>");
    CHECK_EQ(t.text(), "z\nb\ny\na");
}

TEST(vim_ex_retab) {
    S.tabw = 8;  // the expected columns are for tab stops every 8 columns
    VT t("\tx\n    y");
    t.k(":set et<CR>:retab<CR>");
    CHECK_EQ(t.text(), "        x\n    y");
    t.set("        x\n  \ty");
    t.k(":set noet<CR>:retab!<CR>");
    CHECK_EQ(t.text(), "\tx\n\ty");
    t.k(":retab 4<CR>");
    CHECK_EQ(S.tabw, 4);
}

TEST(vim_ex_left_right_center) {
    S.tabw = 8;  // the expected columns are for tab stops every 8 columns
    VT t("  abc\nxy");
    t.k(":%le<CR>");
    CHECK_EQ(t.text(), "abc\nxy");
    t.k(":%ri 6<CR>");
    CHECK_EQ(t.text(), "   abc\n    xy");
    t.k(":set et<CR>:%ce 7<CR>");
    CHECK_EQ(t.text(), "  abc\n  xy");
    t.k(":1le 2<CR>");
    CHECK_EQ(t.text(), "  abc\n  xy");
}

TEST(vim_ex_bar_separates_commands) {
    VT t("a\nb");
    t.k(":1d | s/b/c/<CR>");
    CHECK_EQ(t.text(), "c");
    t.k(":s/c/x\\|y/<CR>");  // an escaped bar is part of the argument
    CHECK_EQ(t.text(), "x|y");
}

TEST(vim_ex_set_options) {
    VT t("x");
    t.k(":set ts=4 sw=2 et<CR>");
    CHECK_EQ(S.tabw, 4);
    CHECK_EQ(t.vim.shiftWidth(), 2);
    CHECK(S.expandtab);
    t.k(":set noet<CR>");
    CHECK(!S.expandtab);
    t.k(":set et!<CR>");
    CHECK(S.expandtab);
    t.k(":set invet<CR>");
    CHECK(!S.expandtab);
    t.k(":set ts+=2<CR>");
    CHECK_EQ(S.tabw, 6);
    t.k(":set ts-=1<CR>");
    CHECK_EQ(S.tabw, 5);
    t.k(":set ts&<CR>");  // Edith's default tab width
    CHECK_EQ(S.tabw, 4);
    t.k(":set ts?<CR>");
    CHECK_EQ(t.msg(), "  tabstop=4");
    t.k(":set et?<CR>");
    CHECK_EQ(t.msg(), "noexpandtab");
    t.k(":set nu<CR>");
    CHECK_EQ(S.lineNums, 1);
    t.k(":set rnu<CR>");
    CHECK_EQ(S.lineNums, 2);
    t.k(":set nornu<CR>");
    CHECK_EQ(S.lineNums, 1);
    t.k(":set nonu<CR>");
    CHECK_EQ(S.lineNums, 0);
    t.k(":set list<CR>");
    CHECK(S.invisibles);
    t.k(":set list?<CR>");
    CHECK_EQ(t.msg(), "  list");
    t.k(":set listchars=tab:>-<CR>");  // accepted, the symbols are fixed
    CHECK_EQ(t.msg(), "");
    t.k(":set nolist<CR>");
    CHECK(!S.invisibles);
    t.k(":set ai<CR>");
    CHECK(S.autoindent);
    t.k(":set nf=hex,alpha<CR>");
    CHECK_EQ(t.vim.opt.nrformats, "hex,alpha");
    t.k(":set nf-=alpha<CR>");
    CHECK_EQ(t.vim.opt.nrformats, "hex");
    t.k(":set nowrap list<CR>");  // accepted, no effect
    CHECK(!t.vim.statusIsError());
    t.k(":set foo<CR>");
    CHECK(t.msg().find("E518") != std::string::npos);
    t.k(":set ts=abc<CR>");
    CHECK(t.msg().find("E521") != std::string::npos);
    t.k(":set ts=99<CR>");
    CHECK(t.msg().find("E487") != std::string::npos);
    CHECK_EQ(S.tabw, 4);
    t.k(":set<CR>");
    CHECK(!t.host.shown.empty());
}

TEST(vim_ex_alpha_nrformats) {
    VT t("‸a");
    t.k(":set nf=alpha<CR><C-a>");
    CHECK_EQ(t.text(), "b");
}

TEST(vim_ex_unknown_command) {
    VT t("x");
    t.k(":frobnicate<CR>");
    CHECK(t.msg().find("E492") != std::string::npos);
    CHECK(t.vim.statusIsError());
    t.k(":\"a comment<CR>");
    CHECK(!t.vim.statusIsError());
}

TEST(vim_ex_misc) {
    VT t("a\nb\nc");
    t.k(":=<CR>");
    CHECK_EQ(t.msg(), "3");
    t.k(":2=<CR>");
    CHECK_EQ(t.msg(), "2");
    t.k(":echo \"hi there\"<CR>");
    CHECK_EQ(t.msg(), "hi there");
    t.k(":2p<CR>");
    CHECK_EQ(t.msg(), "b");
    t.k(":2#<CR>");
    CHECK_EQ(t.msg(), "  2 b");
    t.k(":f<CR>");
    CHECK(t.msg().find("[No Name]") != std::string::npos);
    t.k(":let mapleader = \",\"<CR>");
    CHECK_EQ(toUtf8(t.vim.leader), ",");
    t.k(":exe \"normal Ax\\<lt>Esc>\"<CR>");
    CHECK_EQ(t.text(), "a\nbx\nc");
    t.k(":startinsert<CR>");
    CHECK_EQ(t.mode(), VM_INSERT);
    t.k("<Esc>:noh<CR>");
    CHECK(!t.vim.hlOn);
    t.k(":!echo hi<CR>");
    CHECK(!t.host.shown.empty());
    CHECK_EQ(toUtf8(t.host.shown[0]), "hi");
    t.k(":r !echo new<CR>");
    CHECK_EQ(t.text(), "a\nbx\nnew\nc");
}

TEST(vim_ex_shell_output) {
    VT t("b\na\nc");
    // ":w !cmd" sends the lines to the command and shows its output; the text stays
    t.k(":w !sort<CR>");
    CHECK_EQ(t.host.shown.size(), size_t(3));
    CHECK_EQ(toUtf8(t.host.shown[0]), "a");
    CHECK_EQ(t.text(), "b\na\nc");
    t.k(":2,3w !tr a-z A-Z<CR>");
    CHECK_EQ(t.host.shown.size(), size_t(2));
    CHECK_EQ(toUtf8(t.host.shown[0]), "A");
    // stderr is shown as well as a failing exit status
    t.k(":!echo oops >&2; exit 3<CR>");
    CHECK_EQ(t.host.shown.size(), size_t(2));
    CHECK_EQ(toUtf8(t.host.shown[0]), "oops");
    CHECK_EQ(toUtf8(t.host.shown[1]), "shell returned 3");
    // filters get the lines on stdin
    t.k(":%!sort<CR>");
    CHECK_EQ(t.text(), "a\nb\nc");
}

// ---- files ----

TEST(vim_ex_write_quit_edit) {
    std::string dir = tempDir();
    VT t("hello\nworld");
    t.ed.modified = true;
    t.k(":q<CR>");
    CHECK(t.msg().find("E37") != std::string::npos);
    CHECK(!t.host.quitRequested);
    t.k(":w<CR>");
    CHECK(t.msg().find("E32") != std::string::npos);
    std::string f = dir + "/vim_write.txt";
    t.vim.feed(U(":w " + f) + U"\r");
    CHECK_EQ(readFile(f), "hello\nworld");
    CHECK(!t.ed.modified);
    CHECK_EQ(t.ed.filename, f);
    CHECK(t.msg().find("2L, 12B") != std::string::npos);
    t.k("ddx:w<CR>");
    CHECK_EQ(readFile(f), "orld");
    t.k("x:wq<CR>");
    CHECK_EQ(readFile(f), "rld");
    CHECK(t.host.quitRequested);
}

TEST(vim_ex_write_range_append_and_exists) {
    std::string dir = tempDir();
    std::string a = dir + "/vim_a.txt", b = dir + "/vim_b.txt";
    writeFile(b, "old\n");
    VT t("1\n2\n3");
    t.vim.feed(U(":2,3w " + a) + U"\r");
    CHECK_EQ(readFile(a), "2\n3\n");
    t.vim.feed(U(":w " + b) + U"\r");
    CHECK(t.msg().find("E13") != std::string::npos);
    t.vim.feed(U(":1w >> " + b) + U"\r");
    CHECK_EQ(readFile(b), "old\n1\n");
    t.vim.feed(U(":w! " + b) + U"\r");  // the whole buffer is written like Edith does
    CHECK_EQ(readFile(b), "1\n2\n3");
    CHECK_EQ(t.ed.filename, b);
}

TEST(vim_ex_x_and_update_write_only_when_modified) {
    std::string f = tempDir() + "/vim_x.txt";
    writeFile(f, "keep");
    VT t("other");
    t.ed.filename = f;
    t.k(":up<CR>");
    CHECK_EQ(readFile(f), "keep");
    t.k(":x<CR>");
    CHECK_EQ(readFile(f), "keep");
    CHECK(t.host.quitRequested);
}

TEST(vim_ex_edit_and_read) {
    std::string f = tempDir() + "/vim_edit.txt";
    writeFile(f, "file line 1\nfile line 2\n");
    VT t("buffer");
    t.vim.feed(U(":r " + f) + U"\r");
    CHECK_EQ(t.text(), "buffer\nfile line 1\nfile line 2");
    t.vim.feed(U(":e " + f) + U"\r");
    CHECK(t.msg().find("E37") != std::string::npos);
    t.vim.feed(U(":e! " + f) + U"\r");
    CHECK_EQ(t.text(), "file line 1\nfile line 2\n");
    CHECK_EQ(t.ed.filename, f);
    t.k("dd:e<CR>");
    CHECK(t.msg().find("E37") != std::string::npos);
    t.k(":e!<CR>");  // revert
    CHECK_EQ(t.text(), "file line 1\nfile line 2\n");
    t.vim.feed(U(":e " + tempDir() + "/new_file.txt") + U"\r");
    CHECK_EQ(t.text(), "");
    CHECK(t.msg().find("[New]") != std::string::npos);
    t.k(":r /nonexistent/x<CR>");
    CHECK(t.msg().find("E484") != std::string::npos);
}

TEST(vim_ex_saveas_enew) {
    std::string f = tempDir() + "/vim_sav.txt";
    VT t("data");
    t.vim.feed(U(":sav " + f) + U"\r");
    CHECK_EQ(readFile(f), "data");
    CHECK_EQ(t.ed.filename, f);
    t.k("x:enew<CR>");
    CHECK(t.msg().find("E37") != std::string::npos);
    t.k(":enew!<CR>");
    CHECK_EQ(t.text(), "");
    CHECK(t.ed.filename.empty());
}

TEST(vim_ex_zz_zq) {
    VT t("a");
    t.ed.modified = true;
    t.k("ZQ");
    CHECK(t.host.quitRequested);
    VT u("b");
    u.k("ZZ");  // not modified: just quits
    CHECK(u.host.quitRequested);
}

TEST(vim_ex_nowrite_and_restricted) {
    std::string f = tempDir() + "/vim_nowrite.txt";
    VT t("text");
    t.k(":set nowrite<CR>");
    t.vim.feed(U(":w " + f) + U"\r");
    CHECK(t.msg().find("E142") != std::string::npos);
    CHECK_EQ(readFile(f), "<missing>");
    t.k(":set write<CR>");
    t.vim.restricted = true;
    t.k(":!echo hi<CR>");
    CHECK(t.msg().find("E145") != std::string::npos);
    t.k(":%!sort<CR>");
    CHECK(t.msg().find("E145") != std::string::npos);
    t.k("!!sort<CR>");
    CHECK(t.msg().find("E145") != std::string::npos);
    t.k(":r !echo x<CR>");
    CHECK(t.msg().find("E145") != std::string::npos);
    t.k(":w !cat<CR>");
    CHECK(t.msg().find("E145") != std::string::npos);
    CHECK_EQ(t.text(), "text");
}

TEST(vim_ex_easter_eggs) {
    VT t("‸abc");
    t.k(":help!<CR>");
    CHECK(t.vim.statusText() == U"E478: Don't panic!");
    CHECK(t.vim.statusIsError());
    t.k(":Ni!<CR>");
    CHECK(t.vim.statusText() == U"Do you demand a shrubbery?");
    t.k(":Ni<CR>");  // only with "!"
    CHECK(t.vim.statusText().find(U"E492") == 0);
    t.k(":h holy-grail<CR>");
    CHECK(t.vim.statusText() == U"You found it, Arthur!");
    t.k(":help spoon<CR>");
    CHECK(t.vim.statusText() == U"There is no spoon.");
    t.k(":help UserGettingBored<CR>");
    CHECK(t.vim.statusText().find(U"42 times") != u32::npos);
    t.k(":help 42<CR>");
    CHECK(!t.host.shown.empty());
    CHECK(t.host.shown[0].find(U"life, the universe") != u32::npos);
    t.host.shown.clear();
    t.k(":smi<CR>");
    CHECK(t.host.shown.size() > 5);
    CHECK_EQ(t.state(), "‸abc");  // the text stays as it is
}
