// Vim operators (":help operator"), text objects, counts and the dot command.

#include "vimtest.h"

// ---- delete ----

TEST(vim_op_d_motions) {
    VT t("one ‸two three");
    t.k("de");
    CHECK_EQ(t.state(), "one ‸ three");
    t.set("one two ‸three");
    t.k("db");
    CHECK_EQ(t.state(), "one ‸three");
    t.set("a‸bcdef");
    t.k("d$");
    CHECK_EQ(t.state(), "‸a");
    t.set("abc‸def");
    t.k("d0");
    CHECK_EQ(t.state(), "‸def");
    t.set("  abc‸def");
    t.k("d^");
    CHECK_EQ(t.state(), "  ‸def");
    t.set("‸a,b,c");
    t.k("dt,");
    CHECK_EQ(t.state(), "‸,b,c");
    t.k("df,");  // "f," finds the next "," after the cursor
    CHECK_EQ(t.state(), "‸c");
    t.set("a,b,‸c");
    t.k("dF,");
    CHECK_EQ(t.state(), "a,b‸c");
    t.set("a,b,‸c");
    t.k("dT,");
    CHECK_EQ(t.state(), "a,b,‸c");
}

TEST(vim_op_dw_at_line_end_does_not_join) {
    VT t("foo b‸ar\nnext");
    t.k("dw");
    CHECK_EQ(t.state(), "foo ‸b\nnext");
    t.set("‸foo\n  bar");
    t.k("dw");
    CHECK_EQ(t.state(), "‸\n  bar");
    t.set("foo ‸bar");  // last word of the buffer
    t.k("dw");
    CHECK_EQ(t.state(), "foo‸ ");
}

TEST(vim_op_dd_counts_and_registers) {
    VT t("1\n2\n‸3\n4\n5");
    t.k("2dd");
    CHECK_EQ(t.state(), "1\n2\n‸5");
    CHECK_EQ(t.reg('"'), "3\n4\n");
    CHECK_EQ(t.regType('"'), RT_LINE);
    CHECK_EQ(t.reg('1'), "3\n4\n");
    t.k("dd");
    CHECK_EQ(t.state(), "1\n‸2");  // deleting the last line moves up
    int b = t.vim.beeps;
    t.k("10dd");  // a count beyond the last line fails on the last line (like Vim)
    CHECK_EQ(t.vim.beeps, b + 1);
    CHECK_EQ(t.text(), "1\n2");
    t.k("k10dd");  // otherwise it deletes up to the end
    CHECK_EQ(t.state(), "‸");
}

TEST(vim_op_dd_whole_buffer) {
    VT t("a\n‸b");
    t.k("ggdG");
    CHECK_EQ(t.state(), "‸");
    CHECK_EQ(t.reg('"'), "a\nb\n");
}

TEST(vim_op_dj_dk_linewise) {
    VT t("1\n‸2\n3\n4");
    t.k("dj");
    CHECK_EQ(t.state(), "1\n‸4");
    t.set("1\n2\n‸3\n4");
    t.k("dk");
    CHECK_EQ(t.state(), "1\n‸4");
    int b = t.vim.beeps;
    t.set("‸1");
    t.k("dj");  // fails on the last line
    CHECK_EQ(t.vim.beeps, b + 1);
    CHECK_EQ(t.text(), "1");
}

TEST(vim_op_d_paragraph_exclusive_rule) {
    // "d}" from the start of a line becomes linewise (exclusive motion ending in column 0)
    VT t("‸a\nb\n\nc");
    t.k("d}");
    CHECK_EQ(t.text(), "\nc");
    t.set("x ‸a\nb\n\nc");
    t.k("d}");  // otherwise it stops at the end of the previous line
    CHECK_EQ(t.text(), "x \n\nc");
}

TEST(vim_op_d_multiline_becomes_linewise) {
    VT t("  ‸foo\nbar\nbaz");
    t.k("d/baz<CR>");
    // only blanks before the start and the end is at column 0 of a later line: linewise
    CHECK_EQ(t.text(), "baz");
    t.set("x‸foo\nbar\nbaz");
    t.k("d/baz<CR>");  // the end moves to the end of the previous line
    CHECK_EQ(t.text(), "x\nbaz");
}

TEST(vim_op_force_motion_type) {
    VT t("ab‸cd\nefgh");
    t.k("dvj");  // linewise -> characterwise
    CHECK_EQ(t.text(), "abgh");
    t.set("ab‸cd\nefgh");
    t.k("dVl");  // characterwise -> linewise
    CHECK_EQ(t.text(), "efgh");
    t.set("‸abc");
    t.k("dve");  // inclusive -> exclusive
    CHECK_EQ(t.text(), "c");
}

TEST(vim_op_d_search_and_marks) {
    VT t("‸one two three");
    t.k("d/thr<CR>");
    CHECK_EQ(t.text(), "three");
    t.set("a\n‸b\nc\nd");
    t.k("mxjjd'x");
    CHECK_EQ(t.text(), "a");
    t.set("ab‸cdef");
    t.k("mx$d`x");
    CHECK_EQ(t.text(), "abf");
}

// ---- change ----

TEST(vim_op_cw_ce_cc_C) {
    VT t("‸foo bar");
    t.k("cwxy<Esc>");
    CHECK_EQ(t.state(), "x‸y bar");  // "cw" works like "ce"
    t.set("foo ‸   bar");
    t.k("cwz<Esc>");  // on white space "cw" changes just the white space
    CHECK_EQ(t.state(), "foo ‸zbar");
    t.set("a‸bc def");
    t.k("2cwX<Esc>");
    CHECK_EQ(t.text(), "aX");
    t.set("  a‸bc\nx");
    t.k("ccnew<Esc>");
    CHECK_EQ(t.text(), "new\nx");
    t.set("ab‸cd");
    t.k("CXY<Esc>");
    CHECK_EQ(t.state(), "abX‸Y");
}

TEST(vim_op_cc_keeps_indent_with_autoindent) {
    VT t("    fo‸o");
    t.k(":set ai<CR>ccbar<Esc>");
    CHECK_EQ(t.text(), "    bar");
    t.set("    fo‸o\nx");
    t.k("cc<Esc>");  // unused indent is removed again
    CHECK_EQ(t.text(), "\nx");
}

TEST(vim_op_s_S) {
    VT t("a‸bcd");
    t.k("2sXY<Esc>");
    CHECK_EQ(t.text(), "aXYd");
    t.k("SZ<Esc>");
    CHECK_EQ(t.text(), "Z");
    t.set("‸");
    t.k("sq<Esc>");
    CHECK_EQ(t.text(), "q");
}

// ---- yank ----

TEST(vim_op_yank) {
    VT t("one ‸two three");
    t.k("yw");
    CHECK_EQ(t.reg('"'), "two ");
    CHECK_EQ(t.reg('0'), "two ");
    CHECK_EQ(t.cur().x, 4);
    t.k("yb");  // moves to the start of the yanked text
    CHECK_EQ(t.reg('0'), "one ");
    CHECK_EQ(t.cur().x, 0);
    t.k("yy");
    CHECK_EQ(t.reg('"'), "one two three\n");
    t.k("Y");
    CHECK_EQ(t.regType('"'), RT_LINE);
    t.k("y$");
    CHECK_EQ(t.reg('"'), "one two three");
    CHECK(!t.ed.modified);
}

TEST(vim_op_yank_lines_message) {
    VT t("‸1\n2\n3\n4");
    t.k("3yy");
    CHECK_EQ(t.msg(), "3 lines yanked");
    t.k("yj");
    CHECK_EQ(t.reg('"'), "1\n2\n");
}

// ---- put ----

TEST(vim_op_put_char_and_line) {
    VT t("‸abc");
    t.k("ylp");
    CHECK_EQ(t.state(), "a‸abc");
    t.k("P");
    CHECK_EQ(t.state(), "a‸aabc");
    t.k("3p");
    CHECK_EQ(t.state(), "aaaa‸aabc");
    t.set("‸one\ntwo");
    t.k("yyjp");
    CHECK_EQ(t.state(), "one\ntwo\n‸one");
    t.k("ggP");
    CHECK_EQ(t.state(), "‸one\none\ntwo\none");
    t.k("2p");
    CHECK_EQ(t.text(), "one\none\none\none\ntwo\none");
}

TEST(vim_op_put_gp_gP) {
    VT t("‸ab");
    t.k("ylgp");
    CHECK_EQ(t.text(), "aab");
    CHECK_EQ(t.cur().x, 2);
    t.set("‸1\n2");
    t.k("yygp");
    CHECK_EQ(t.text(), "1\n1\n2");
    CHECK_EQ(t.cur().y, 2);
    t.k("ggyygP");
    CHECK_EQ(t.cur().y, 1);
}

TEST(vim_op_put_multiline_charwise) {
    VT t("a‸b\ncd");
    t.k("vj");
    t.k("y");
    CHECK_EQ(t.reg('"'), "b\ncd");
    t.set("‸XY");
    t.vim.regs['"'] = t.vim.getReg('0');
    t.k("p");
    CHECK_EQ(t.text(), "Xb\ncdY");
    CHECK_EQ(t.cur(), Pos({0, 1}));  // on the first inserted character
}

TEST(vim_op_put_adjust_indent) {
    S.tabw = 8;  // the expected columns are for tab stops every 8 columns
    VT t("    ‸x\nfoo");
    t.k("jyyk]p");
    CHECK_EQ(t.text(), "    x\n    foo\nfoo");
    t.k("[p");
    CHECK_EQ(t.text(), "    x\n    foo\n    foo\nfoo");
}

TEST(vim_op_put_empty_register) {
    VT t("‸a");
    t.k("\"qp");
    CHECK(t.msg().find("E353") != std::string::npos);
    CHECK_EQ(t.text(), "a");
}

// ---- shift, case, join, format, filter ----

TEST(vim_op_shift) {
    VT t("‸a\nb\nc");
    t.k(":set sw=4 et<CR>");
    t.k(">>");
    CHECK_EQ(t.text(), "    a\nb\nc");
    t.k("2>>");
    CHECK_EQ(t.text(), "        a\n    b\nc");
    t.k("<j");
    CHECK_EQ(t.text(), "    a\nb\nc");
    t.k(">G");
    CHECK_EQ(t.text(), "        a\n    b\n    c");
    t.k("<<<<<<");
    CHECK_EQ(t.text(), "a\n    b\n    c");
    t.k(":set noet sw=8 ts=8<CR>G>>");
    CHECK_EQ(t.text(), "a\n    b\n\t    c");
}

TEST(vim_op_shift_skips_empty_lines) {
    VT t("‸a\n\nb");
    t.k(":set sw=2 et<CR>>2j");
    CHECK_EQ(t.text(), "  a\n\n  b");
}

TEST(vim_op_shiftround) {
    VT t("   ‸a");
    t.k(":set sw=4 et sr<CR>>>");
    CHECK_EQ(t.text(), "    a");
}

TEST(vim_op_case) {
    VT t("‸Hello World");
    t.k("g~w");
    CHECK_EQ(t.text(), "hELLO World");
    t.k("gUiw");
    CHECK_EQ(t.text(), "HELLO World");
    t.k("guu");
    CHECK_EQ(t.text(), "hello world");
    t.k("gUU");
    CHECK_EQ(t.text(), "HELLO WORLD");
    t.k("g~~");
    CHECK_EQ(t.text(), "hello world");
    t.k("gUgU");
    CHECK_EQ(t.text(), "HELLO WORLD");
    t.k("gugu");
    CHECK_EQ(t.text(), "hello world");
    t.k("g?g?");
    CHECK_EQ(t.text(), "uryyb jbeyq");
    t.k("g??");
    CHECK_EQ(t.text(), "hello world");
    t.k("0~~");
    CHECK_EQ(t.state(), "HE‸llo world");
    t.k("0");
    t.k("4~");
    CHECK_EQ(t.state(), "heLL‸o world");
}

TEST(vim_op_case_unicode) {
    VT t("‸äöü straße");
    t.k("gUU");
    CHECK_EQ(t.text(), "ÄÖÜ STRAßE");
    t.k("guu");
    CHECK_EQ(t.text(), "äöü straße");
    t.k("~");
    CHECK_EQ(t.text(), "Äöü straße");
}

TEST(vim_op_tildeop) {
    VT t("‸abc def");
    t.k(":set top<CR>~w");
    CHECK_EQ(t.text(), "ABC def");
}

TEST(vim_op_join) {
    VT t("‸a\n   b\nc");
    t.k("J");
    CHECK_EQ(t.state(), "a‸ b\nc");
    t.set("‸a\nb\nc\nd");
    t.k("3J");
    CHECK_EQ(t.text(), "a b c\nd");
    t.set("‸a.\nb");
    t.k("J");  // 'joinspaces': two spaces after a period
    CHECK_EQ(t.text(), "a.  b");
    t.set("‸a.\nb");
    t.k(":set nojs<CR>J");
    CHECK_EQ(t.text(), "a. b");
    t.set("‸f(\n)");
    t.k("J");  // no space before ')'
    CHECK_EQ(t.text(), "f()");
    t.set("‸a \nb");
    t.k("J");  // trailing white space: no extra space
    CHECK_EQ(t.text(), "a b");
    t.set("‸a\n\nb");
    t.k("J");
    CHECK_EQ(t.text(), "a\nb");
    t.set("‸a\n  b");
    t.k("gJ");
    CHECK_EQ(t.text(), "a  b");
    int b = t.vim.beeps;
    t.k("J");  // last line
    CHECK_EQ(t.vim.beeps, b + 1);
}

TEST(vim_op_format_gq) {
    VT t("‸one two three four five six seven\neight nine\n\nnext para");
    t.k(":set tw=20<CR>gqip");
    CHECK_EQ(t.text(), "one two three four\nfive six seven eight\nnine\n\nnext para");
    CHECK_EQ(t.cur().y, 2);
    t.set("‸aaa bbb ccc ddd");
    t.k(":set tw=8<CR>gww");
    CHECK_EQ(t.text(), "aaa bbb\nccc ddd");
    CHECK_EQ(t.cur(), Pos({0, 0}));
}

TEST(vim_op_reindent) {
    VT t("‸int f() {\nx;\nif (a) {\ny;\n}\n}");
    t.k(":set sw=4 et<CR>=G");
    CHECK_EQ(t.text(), "int f() {\n    x;\n    if (a) {\n        y;\n    }\n}");
    t.k("2G==");
    CHECK_EQ(t.text(), "int f() {\n    x;\n    if (a) {\n        y;\n    }\n}");
}

TEST(vim_op_filter) {
    VT t("‸c\nb\na\nz");
    t.k("!2jsort<CR>");
    CHECK_EQ(t.text(), "a\nb\nc\nz");
    t.k("gg!!tr a-z A-Z<CR>");
    CHECK_EQ(t.text(), "A\nb\nc\nz");
}

// ---- text objects ----

TEST(vim_obj_word) {
    VT t("foo b‸ar baz");
    t.k("diw");
    CHECK_EQ(t.state(), "foo ‸ baz");
    t.set("foo b‸ar baz");
    t.k("daw");
    CHECK_EQ(t.state(), "foo ‸baz");
    t.set("foo bar b‸az");
    t.k("daw");  // no trailing white space: the leading one is deleted
    CHECK_EQ(t.text(), "foo bar");
    t.set("a.b‸.c d");
    t.k("ciWx<Esc>");
    CHECK_EQ(t.text(), "x d");
    t.set("a.b‸.c d");
    t.k("daW");
    CHECK_EQ(t.text(), "d");
    t.set("‸a b c d");
    t.k("d3iw");  // word, space, word
    CHECK_EQ(t.text(), " c d");
    t.set("‸a b c d");
    t.k("d2aw");
    CHECK_EQ(t.text(), "c d");
    t.set("foo  ‸  bar");
    t.k("diw");  // on white space: the white space
    CHECK_EQ(t.text(), "foobar");
    t.set("foo  ‸  bar baz");
    t.k("daw");  // white space and the following word
    CHECK_EQ(t.text(), "foo baz");
}

TEST(vim_obj_quotes) {
    VT t("say \"he‸llo\" now");
    t.k("di\"");
    CHECK_EQ(t.state(), "say \"‸\" now");
    t.set("say \"he‸llo\" now");
    t.k("da\"");
    CHECK_EQ(t.text(), "say now");
    t.set("say \"he‸llo\" now");
    t.k("d2i\"");  // with a count the quotes are included, but no white space
    CHECK_EQ(t.text(), "say  now");
    t.set("‸x = 'a\\'b' + 'c'");
    t.k("ci'Q<Esc>");  // first string after the cursor, escaped quote skipped
    CHECK_EQ(t.text(), "x = 'Q' + 'c'");
    t.set("a `cmd‸` b");
    t.k("yi`");
    CHECK_EQ(t.reg('"'), "cmd");
    t.set("x \"a\" y \"b‸\"");  // cursor on the closing quote
    t.k("di\"");
    CHECK_EQ(t.text(), "x \"a\" y \"\"");
    int b = t.vim.beeps;
    t.set("‸no quotes");
    t.k("di\"");
    CHECK_EQ(t.vim.beeps, b + 1);
}

TEST(vim_obj_brackets) {
    VT t("f(a, (b‸), c)");
    t.k("di(");
    CHECK_EQ(t.text(), "f(a, (), c)");
    t.set("f(a, (b‸), c)");
    t.k("d2i(");
    CHECK_EQ(t.text(), "f()");
    t.set("f(a, (b‸), c)");
    t.k("da)");
    CHECK_EQ(t.text(), "f(a, , c)");
    t.set("x[1, ‸2]");
    t.k("ci[9<Esc>");
    CHECK_EQ(t.text(), "x[9]");
    t.set("<a ‸href>");
    t.k("di<");
    CHECK_EQ(t.text(), "<>");
    t.set("‸(abc)");  // on the opening bracket
    t.k("dib");
    CHECK_EQ(t.text(), "()");
    t.set("(ab‸c)");
    t.k("yab");
    CHECK_EQ(t.reg('"'), "(abc)");
    t.set("{a ‸b}");
    t.k("diB");
    CHECK_EQ(t.text(), "{}");
}

TEST(vim_obj_braces_multiline) {
    VT t("if (x) {\n    fo‸o;\n    bar;\n}");
    t.k("di{");
    CHECK_EQ(t.text(), "if (x) {\n}");
    t.set("if (x) {\n    fo‸o;\n}");
    t.k(":set ai<CR>ci{baz<Esc>");
    CHECK_EQ(t.text(), "if (x) {\n    baz\n}");
    t.set("if (x) {\n    fo‸o;\n}");
    t.k("da{");
    CHECK_EQ(t.text(), "if (x) ");
    t.set("{\n  ‸a\n}");
    t.k("yi{");
    CHECK_EQ(t.reg('"'), "  a\n");
    CHECK_EQ(t.regType('"'), RT_LINE);
}

TEST(vim_obj_tags) {
    VT t("<div><b>bo‸ld</b> text</div>");
    t.k("dit");
    CHECK_EQ(t.text(), "<div><b></b> text</div>");
    t.set("<div><b>bo‸ld</b> text</div>");
    t.k("dat");
    CHECK_EQ(t.text(), "<div> text</div>");
    t.set("<div><b>bo‸ld</b> text</div>");
    t.k("d2it");
    CHECK_EQ(t.text(), "<div></div>");
    t.set("<p class=\"x\">\n  a‸b\n</p>");
    t.k("yit");
    CHECK_EQ(t.reg('"'), "\n  ab\n");
}

TEST(vim_obj_paragraph) {
    VT t("a\nb‸\n\n\nc\nd");
    t.k("dip");
    CHECK_EQ(t.text(), "\n\nc\nd");
    t.set("a\nb‸\n\n\nc\nd");
    t.k("dap");
    CHECK_EQ(t.text(), "c\nd");
    t.set("a\nb\n\nc\n‸d");
    t.k("dap");  // the last paragraph: the blank lines before it
    CHECK_EQ(t.text(), "a\nb");
    t.set("a\n‸\n\nb");
    t.k("dip");  // on blank lines: the blank lines
    CHECK_EQ(t.text(), "a\nb");
    t.set("‸a\n\nb\n\nc");
    t.k("d3ip");
    CHECK_EQ(t.text(), "\nc");
}

TEST(vim_obj_sentence) {
    VT t("One two. Thr‸ee four. Five.");
    t.k("das");
    CHECK_EQ(t.text(), "One two. Five.");
    t.set("One two. Thr‸ee four. Five.");
    t.k("dis");
    CHECK_EQ(t.text(), "One two.  Five.");
}

// ---- counts ----

TEST(vim_count_multiplies) {
    VT t("‸a b c d e f g h");
    t.k("2d3w");
    CHECK_EQ(t.text(), "g h");
    t.set("‸a b c d e f g h");
    t.k("3dw");
    CHECK_EQ(t.text(), "d e f g h");
    t.set("‸abcdefgh");
    t.k("3x");
    CHECK_EQ(t.text(), "defgh");
    t.k("10x");  // as many as there are
    CHECK_EQ(t.text(), "");
}

TEST(vim_count_zero_is_motion) {
    VT t("ab‸cdefghijklmnop");
    t.k("10l");
    CHECK_EQ(t.cur().x, 12);
    t.k("0");
    CHECK_EQ(t.cur().x, 0);
    t.k("20l");
    CHECK_EQ(t.cur().x, 15);
}

TEST(vim_count_large) {
    VT t("‸a\nb");
    t.k("99999999j");
    CHECK_EQ(t.cur().y, 1);
    t.k("123456789012k");  // overflow is clamped
    CHECK_EQ(t.cur().y, 0);
}

TEST(vim_count_x_X_r) {
    VT t("abc‸def");
    t.k("2X");
    CHECK_EQ(t.state(), "a‸def");
    t.k("3rx");
    CHECK_EQ(t.state(), "axx‸x");
    int b = t.vim.beeps;
    t.k("5rz");  // not enough characters: nothing happens
    CHECK_EQ(t.vim.beeps, b + 1);
    CHECK_EQ(t.text(), "axxx");
}

TEST(vim_r_enter) {
    VT t("ab‸ cd");
    t.k("r<CR>");
    CHECK_EQ(t.state(), "ab\n‸cd");
    t.set("a‸bcd");
    t.k("2r<CR>");  // a count replaces several characters by one line break
    CHECK_EQ(t.text(), "a\nd");
}

TEST(vim_increment_decrement) {
    VT t("x ‸9 y");
    t.k("<C-a>");
    CHECK_EQ(t.state(), "x 1‸0 y");
    t.k("5<C-x>");
    CHECK_EQ(t.text(), "x 5 y");
    t.k("10<C-x>");
    CHECK_EQ(t.text(), "x -5 y");
    t.set("‸abc 0x0f z");
    t.k("<C-a>");  // the number after the cursor, hexadecimal
    CHECK_EQ(t.text(), "abc 0x10 z");
    t.set("‸0b101");
    t.k("<C-a>");
    CHECK_EQ(t.text(), "0b110");
    t.set("‸007");
    t.k("<C-a>");  // no octal by default, leading zeros are kept
    CHECK_EQ(t.text(), "008");
    t.set("‸-3");
    t.k("4<C-a>");
    CHECK_EQ(t.text(), "1");
    int b = t.vim.beeps;
    t.set("‸none");
    t.k("<C-a>");
    CHECK_EQ(t.vim.beeps, b + 1);
}

TEST(vim_increment_visual_progressive) {
    VT t("‸0\n0\n0");
    t.k("VGg<C-a>");
    CHECK_EQ(t.text(), "1\n2\n3");
    t.k("ggVG<C-a>");
    CHECK_EQ(t.text(), "2\n3\n4");
}

// ---- dot repeat ----

TEST(vim_dot_simple) {
    VT t("‸a b c d");
    t.k("dw.");
    CHECK_EQ(t.text(), "c d");
    t.set("‸abcdef");
    t.k("x..");
    CHECK_EQ(t.text(), "def");
    t.set("‸1\n2\n3\n4\n5");
    t.k("dd2.");
    CHECK_EQ(t.text(), "4\n5");
    t.k(".");  // the new count is remembered
    CHECK_EQ(t.text(), "");
}

TEST(vim_dot_count_replaces_original) {
    VT t("‸a b c d e f g");
    t.k("2dw");
    CHECK_EQ(t.text(), "c d e f g");
    t.k("3.");
    CHECK_EQ(t.text(), "f g");
}

TEST(vim_dot_insert) {
    VT t("‸x");
    t.k("ihi <Esc>");
    t.k(".");
    CHECK_EQ(t.text(), "hihi  x");
    t.set("‸a\nb");
    t.k("Aend<Esc>j.");
    CHECK_EQ(t.text(), "aend\nbend");
    t.set("‸a\nb");
    t.k("onew<Esc>.");
    CHECK_EQ(t.text(), "a\nnew\nnew\nb");
    t.set("‸a");
    t.k("3ix<Esc>");
    CHECK_EQ(t.text(), "xxxa");
    t.k(".");
    CHECK_EQ(t.text(), "xxxxxxa");
    t.k("2.");
    CHECK_EQ(t.text(), "xxxxxxxxa");
}

TEST(vim_dot_change) {
    VT t("‸foo foo foo");
    t.k("cwbar<Esc>w.w.");
    CHECK_EQ(t.text(), "bar bar bar");
    t.set("‸a1 a2 a3");
    t.k("ciwX<Esc>ww.");
    CHECK_EQ(t.text(), "X a2 X");
    t.set("one\n‸two\nthree");
    t.k("ccX<Esc>j.");
    CHECK_EQ(t.text(), "one\nX\nX");
}

TEST(vim_dot_with_backspace_in_insert) {
    VT t("‸a\nb");
    t.k("Axy<BS>z<Esc>j.");
    CHECK_EQ(t.text(), "axz\nbxz");
}

TEST(vim_dot_visual) {
    VT t("‸abcdef\nabcdef");
    t.k("vlld");
    CHECK_EQ(t.text(), "def\nabcdef");
    t.k("j0.");  // the same amount of text
    CHECK_EQ(t.text(), "def\ndef");
    t.set("‸a\nb\nc\nd");
    t.k("Vj>.");
    CHECK_EQ(t.text(), "\t\ta\n\t\tb\nc\nd");
}

TEST(vim_dot_put_numbered_registers) {
    VT t("‸1\n2\n3");
    t.k("dddddd");
    t.k("\"1p");
    CHECK_EQ(t.text(), "\n3");
    t.k("..");
    CHECK_EQ(t.text(), "\n3\n2\n1");
}

TEST(vim_dot_replace_and_tilde) {
    VT t("‸abcd");
    t.k("rx l.");
    CHECK_EQ(t.text(), "xbxd");
    t.set("‸abcd");
    t.k("~.");
    CHECK_EQ(t.text(), "ABcd");
    t.set("‸ab ab");
    t.k("Rxy<Esc>w.");
    CHECK_EQ(t.text(), "xy xy");
}

TEST(vim_dot_join_and_indent) {
    VT t("‸a\nb\nc\nd");
    t.k("J.");
    CHECK_EQ(t.text(), "a b c\nd");
    t.set("‸a\nb");
    t.k(">>j.");
    CHECK_EQ(t.text(), "\ta\n\tb");
}

TEST(vim_dot_nothing_to_repeat) {
    VT t("‸a");
    int b = t.vim.beeps;
    t.k(".");
    CHECK_EQ(t.vim.beeps, b + 1);
    t.k("yy.");  // yanking is not repeatable
    CHECK_EQ(t.text(), "a");
}

TEST(vim_nested_commands) {
    VT t("‸f(a, g(b, c), d)");
    t.k("fbci(x, y<Esc>");
    CHECK_EQ(t.text(), "f(a, g(x, y), d)");
    t.k("F(F(ci(q<Esc>");
    CHECK_EQ(t.text(), "f(q)");
    t.set("‸word1 word2 word3");
    t.k("\"ayiww\"byiww\"cyiw");
    t.k("0ciw<C-r>c<Esc>");
    CHECK_EQ(t.text(), "word3 word2 word3");
    CHECK_EQ(t.reg('a'), "word1");
    CHECK_EQ(t.reg('b'), "word2");
}
