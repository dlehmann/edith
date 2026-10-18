// Vim motions (":help motion.txt"): left-right, up-down, word, text object motions,
// jumps, marks and searching.

#include "vimtest.h"

// ---- left-right motions ----

TEST(vim_motion_h_l) {
    VT t("ab‸cdef");
    t.k("h");
    CHECK_EQ(t.state(), "a‸bcdef");
    t.k("3l");
    CHECK_EQ(t.state(), "abcd‸ef");
    t.k("10l");  // stops at the last character
    CHECK_EQ(t.state(), "abcde‸f");
    int b = t.vim.beeps;
    t.k("l");    // cannot move: beep
    CHECK_EQ(t.vim.beeps, b + 1);
    t.k("10h");
    CHECK_EQ(t.state(), "‸abcdef");
    t.k("h");
    CHECK_EQ(t.vim.beeps, b + 2);
}

TEST(vim_motion_arrows_backspace_space) {
    VT t("ab\n‸cd");
    t.k("<Right>");
    CHECK_EQ(t.state(), "ab\nc‸d");
    t.k("<Left><Left>");
    CHECK_EQ(t.state(), "ab\n‸cd");
    t.k("<BS>");  // wraps to the previous line ('whichwrap' b)
    CHECK_EQ(t.state(), "a‸b\ncd");
    t.k(" ");     // and space wraps forward
    CHECK_EQ(t.state(), "ab\n‸cd");
    t.k("<C-h>");
    CHECK_EQ(t.state(), "a‸b\ncd");
}

TEST(vim_motion_0_caret_dollar) {
    VT t("   foo ba‸r  ");
    t.k("0");
    CHECK_EQ(t.cur().x, 0);
    t.k("^");
    CHECK_EQ(t.cur().x, 3);
    t.k("$");
    CHECK_EQ(t.cur().x, 11);
    t.k("g_");
    CHECK_EQ(t.cur().x, 9);
    t.k("<Home>");
    CHECK_EQ(t.cur().x, 0);
    t.k("<End>");
    CHECK_EQ(t.cur().x, 11);
}

TEST(vim_motion_dollar_count_and_curswant) {
    VT t("‸short\na much longer line\nmid line");
    t.k("$j");
    CHECK_EQ(t.cur(), Pos({1, 17}));  // '$' keeps the cursor at the end of each line
    t.k("j");
    CHECK_EQ(t.cur(), Pos({2, 7}));
    t.k("gg2$");
    CHECK_EQ(t.cur(), Pos({1, 17}));
}

TEST(vim_motion_bar_column) {
    VT t("‸abcdefgh");
    t.k("5|");
    CHECK_EQ(t.cur().x, 4);
    t.k("|");
    CHECK_EQ(t.cur().x, 0);
    t.k("99|");
    CHECK_EQ(t.cur().x, 7);
}

TEST(vim_motion_f_t_F_T_repeat) {
    VT t("‸a,b,c,d;e");
    t.k("f,");
    CHECK_EQ(t.cur().x, 1);
    t.k(";");
    CHECK_EQ(t.cur().x, 3);
    t.k(",");
    CHECK_EQ(t.cur().x, 1);
    t.k("2f,");
    CHECK_EQ(t.cur().x, 5);
    t.k("F,");
    CHECK_EQ(t.cur().x, 3);
    t.k("0t,");
    CHECK_EQ(t.cur().x, 0);  // "t," right before a "," does not move ... (a,): target is index 0
    t.k("l t,");
    CHECK_EQ(t.cur().x, 2);
    t.k(";");                // ";" after "t" jumps to before the next ","
    CHECK_EQ(t.cur().x, 4);
    t.k("$T,");
    CHECK_EQ(t.cur().x, 6);
    int b = t.vim.beeps;
    t.k("fz");               // not found: no movement, beep
    CHECK_EQ(t.cur().x, 6);
    CHECK_EQ(t.vim.beeps, b + 1);
}

TEST(vim_motion_f_escape_cancels) {
    VT t("‸abc");
    int b = t.vim.beeps;
    t.k("f<Esc>");
    CHECK_EQ(t.cur().x, 0);
    CHECK_EQ(t.vim.beeps, b);
    t.k("fc");
    CHECK_EQ(t.cur().x, 2);
}

// ---- up-down motions ----

TEST(vim_motion_j_k_counts) {
    VT t("‸l1\nl2\nl3\nl4\nl5");
    t.k("j");
    CHECK_EQ(t.cur().y, 1);
    t.k("3j");
    CHECK_EQ(t.cur().y, 4);
    int b = t.vim.beeps;
    t.k("j");  // at the last line: fails
    CHECK_EQ(t.vim.beeps, b + 1);
    t.k("10k");  // moves as far as possible
    CHECK_EQ(t.cur().y, 0);
    t.k("<Down><C-n><C-j>");
    CHECK_EQ(t.cur().y, 3);
    t.k("<Up><C-p>");
    CHECK_EQ(t.cur().y, 1);
}

TEST(vim_motion_j_keeps_column) {
    VT t("abcde‸f\nab\nabcdefgh");
    t.k("j");
    CHECK_EQ(t.cur(), Pos({1, 1}));
    t.k("j");
    CHECK_EQ(t.cur(), Pos({2, 5}));  // back to the wanted column
}

TEST(vim_motion_plus_minus_underscore_enter) {
    VT t("‸a\n  b\n    c");
    t.k("+");
    CHECK_EQ(t.cur(), Pos({1, 2}));
    t.k("<CR>");
    CHECK_EQ(t.cur(), Pos({2, 4}));
    t.k("-");
    CHECK_EQ(t.cur(), Pos({1, 2}));
    t.k("2_");
    CHECK_EQ(t.cur(), Pos({2, 4}));
    t.k("gg_");
    CHECK_EQ(t.cur(), Pos({0, 0}));
}

TEST(vim_motion_G_gg_percent) {
    VT t("a\nb\n  c\nd\ne\nf\ng\nh\ni\nj");
    t.k("G");
    CHECK_EQ(t.cur().y, 9);
    t.k("gg");
    CHECK_EQ(t.cur().y, 0);
    t.k("3G");
    CHECK_EQ(t.cur(), Pos({2, 2}));
    t.k("5gg");
    CHECK_EQ(t.cur().y, 4);
    t.k("50%");
    CHECK_EQ(t.cur().y, 4);
    t.k("100%");
    CHECK_EQ(t.cur().y, 9);
    t.k("99G");  // past the end: last line
    CHECK_EQ(t.cur().y, 9);
}

TEST(vim_motion_H_after_deleting_the_view) {
    std::string s;
    for (int i = 0; i < 50; i++) s += (i ? "\n" : "") + std::to_string(i);
    VT t(s);
    t.ed.top = 40;
    t.ed.cy = 45;
    t.k(":10,$d<CR>HLM");  // the view starts behind the end of the buffer now
    CHECK(t.cur().y < t.ed.n());
    t.k("<C-e><C-y><C-d>");
    CHECK(t.cur().y < t.ed.n());
}

TEST(vim_motion_H_M_L) {
    std::string s;
    for (int i = 0; i < 50; i++) s += (i ? "\n" : "") + std::to_string(i);
    VT t(s);
    t.ed.top = 10;
    t.ed.cy = 15;
    t.k("H");
    CHECK_EQ(t.cur().y, 10);
    t.k("L");
    CHECK_EQ(t.cur().y, 29);  // the default host shows 20 lines
    t.k("M");
    CHECK_EQ(t.cur().y, 19);
    t.k("3H");
    CHECK_EQ(t.cur().y, 12);
    t.k("3L");
    CHECK_EQ(t.cur().y, 27);
}

// ---- word motions ----

TEST(vim_motion_w_b_e) {
    VT t("‸foo.bar baz  qux");
    t.k("w");
    CHECK_EQ(t.cur().x, 3);   // punctuation is its own word
    t.k("w");
    CHECK_EQ(t.cur().x, 4);
    t.k("w");
    CHECK_EQ(t.cur().x, 8);
    t.k("w");
    CHECK_EQ(t.cur().x, 13);
    t.k("b");
    CHECK_EQ(t.cur().x, 8);
    t.k("2b");
    CHECK_EQ(t.cur().x, 3);
    t.k("e");
    CHECK_EQ(t.cur().x, 6);
    t.k("e");
    CHECK_EQ(t.cur().x, 10);
    t.k("ge");
    CHECK_EQ(t.cur().x, 6);
}

TEST(vim_motion_W_B_E_gE) {
    VT t("‸foo.bar baz-1 qux");
    t.k("W");
    CHECK_EQ(t.cur().x, 8);
    t.k("E");
    CHECK_EQ(t.cur().x, 12);
    t.k("W");
    CHECK_EQ(t.cur().x, 14);
    t.k("B");
    CHECK_EQ(t.cur().x, 8);
    t.k("gE");
    CHECK_EQ(t.cur().x, 6);
}

TEST(vim_motion_w_across_lines_and_empty_lines) {
    VT t("‸one\n\n  two");
    t.k("w");
    CHECK_EQ(t.cur(), Pos({1, 0}));  // an empty line is a word
    t.k("w");
    CHECK_EQ(t.cur(), Pos({2, 2}));
    t.k("b");
    CHECK_EQ(t.cur(), Pos({1, 0}));
    t.k("b");
    CHECK_EQ(t.cur(), Pos({0, 0}));
    t.k("e");
    CHECK_EQ(t.cur(), Pos({0, 2}));
    t.k("e");  // "e" skips empty lines
    CHECK_EQ(t.cur(), Pos({2, 4}));
}

TEST(vim_motion_w_at_end_of_buffer) {
    VT t("foo b‸ar");
    t.k("w");  // moves to the last character
    CHECK_EQ(t.cur().x, 6);
    int b = t.vim.beeps;
    t.k("w");  // now it fails
    CHECK_EQ(t.vim.beeps, b + 1);
    t.k("e");
    CHECK_EQ(t.vim.beeps, b + 2);
}

TEST(vim_motion_b_at_start_fails) {
    VT t("‸foo");
    int b = t.vim.beeps;
    t.k("b");
    CHECK_EQ(t.vim.beeps, b + 1);
    t.k("ge");
    CHECK_EQ(t.vim.beeps, b + 2);
}

TEST(vim_motion_word_counts) {
    VT t("‸a b c d e f");
    t.k("3w");
    CHECK_EQ(t.cur().x, 6);
    t.k("2e");
    CHECK_EQ(t.cur().x, 10);
    t.k("4b");
    CHECK_EQ(t.cur().x, 2);
}

// ---- text object motions ----

TEST(vim_motion_paragraphs) {
    VT t("‸a\nb\n\nc\nd\n\n\ne");
    t.k("}");
    CHECK_EQ(t.cur(), Pos({2, 0}));
    t.k("}");
    CHECK_EQ(t.cur(), Pos({5, 0}));
    t.k("}");
    CHECK_EQ(t.cur(), Pos({7, 0}));  // end of the buffer
    t.k("{");
    CHECK_EQ(t.cur(), Pos({6, 0}));
    t.k("2{");
    CHECK_EQ(t.cur(), Pos({0, 0}));
    t.k("2}");
    CHECK_EQ(t.cur(), Pos({5, 0}));
}

TEST(vim_motion_whitespace_line_is_not_paragraph_boundary) {
    VT t("‸a\n  \nb\n\nc");
    t.k("}");
    CHECK_EQ(t.cur(), Pos({3, 0}));
}

TEST(vim_motion_sentences) {
    VT t("‸One two.  Three four!  Five\nsix.\n\nSeven.");
    t.k(")");
    CHECK_EQ(t.cur(), Pos({0, 10}));
    t.k(")");
    CHECK_EQ(t.cur(), Pos({0, 23}));
    t.k(")");
    CHECK_EQ(t.cur(), Pos({2, 0}));   // the empty line
    t.k(")");
    CHECK_EQ(t.cur(), Pos({3, 0}));
    t.k("(");
    CHECK_EQ(t.cur(), Pos({2, 0}));
    t.k("(");
    CHECK_EQ(t.cur(), Pos({0, 23}));
    t.k("2(");
    CHECK_EQ(t.cur(), Pos({0, 0}));
}

TEST(vim_motion_percent_brackets) {
    VT t("‸if (a[1] == {2}) x");
    t.k("%");
    CHECK_EQ(t.cur().x, 15);
    t.k("%");
    CHECK_EQ(t.cur().x, 3);
    t.k("f[%");
    CHECK_EQ(t.cur().x, 7);
    VT m("{\n  (a)\n‸}");
    m.k("%");
    CHECK_EQ(m.cur(), Pos({0, 0}));
    VT none("‸no brackets");
    int b = none.vim.beeps;
    none.k("%");
    CHECK_EQ(none.vim.beeps, b + 1);
    CHECK_EQ(none.cur().x, 0);
}

TEST(vim_motion_unmatched_brackets) {
    VT t("f(a, (b), ‸c) { x { y } z }");
    t.k("[(");
    CHECK_EQ(t.cur().x, 1);
    t.k("])");
    CHECK_EQ(t.cur().x, 11);
    VT u("{ x { y } ‸z }");
    u.k("[{");
    CHECK_EQ(u.cur().x, 0);
    u.k("]}");
    CHECK_EQ(u.cur().x, 12);
}

TEST(vim_motion_sections) {
    VT t("‸int a;\n{\n  x;\n}\nint b;\n{\n}");
    t.k("]]");
    CHECK_EQ(t.cur(), Pos({1, 0}));
    t.k("]]");
    CHECK_EQ(t.cur(), Pos({5, 0}));
    t.k("[[");
    CHECK_EQ(t.cur(), Pos({1, 0}));
    t.k("][");
    CHECK_EQ(t.cur(), Pos({3, 0}));
    t.k("G[]");
    CHECK_EQ(t.cur(), Pos({3, 0}));
}

TEST(vim_motion_go_byte_offset) {
    VT t("ab\ncd");
    t.k("4go");
    CHECK_EQ(t.cur(), Pos({1, 0}));
    t.k("go");
    CHECK_EQ(t.cur(), Pos({0, 0}));
}

// ---- marks and jumps ----

TEST(vim_motion_marks) {
    VT t("aaa\nb‸bb\nccc");
    t.k("ma");
    t.k("G$");
    t.k("`a");
    CHECK_EQ(t.cur(), Pos({1, 1}));
    t.k("G'a");
    CHECK_EQ(t.cur(), Pos({1, 0}));
    t.k("``");  // back to the position before the jump
    CHECK_EQ(t.cur().y, 2);
    t.k("''");
    CHECK_EQ(t.cur().y, 1);
    int b = t.vim.beeps;
    t.k("`z");  // mark not set
    CHECK_EQ(t.vim.beeps, b + 1);
    CHECK(t.msg().find("E20") != std::string::npos);
}

TEST(vim_motion_marks_follow_line_changes) {
    VT t("a\nb\n‸c");
    t.k("mx");
    t.k("ggOnew<Esc>");
    t.k("gg'x");
    CHECK_EQ(t.cur().y, 3);
    t.k("ggdd");
    t.k("'x");
    CHECK_EQ(t.cur().y, 2);
}

TEST(vim_motion_special_marks) {
    VT t("one\n‸two\nthree");
    t.k("Atail<Esc>");
    t.k("gg`.");
    CHECK_EQ(t.cur(), Pos({1, 6}));
    t.k("gg`^");  // where Insert mode stopped, limited to the last character
    CHECK_EQ(t.cur(), Pos({1, 6}));
    t.k("ggi<Esc>gi!<Esc>");  // "gi" inserts there again
    CHECK_EQ(t.text(), "!one\ntwotail\nthree");
    t.k("ggyj`]");
    CHECK_EQ(t.cur().y, 1);
    t.k("Gvk<Esc>gg`<");
    CHECK_EQ(t.cur().y, 1);
    t.k("`>");
    CHECK_EQ(t.cur().y, 2);
}

TEST(vim_motion_jumplist) {
    VT t("‸1\n2\n3\n4\n5\n6");
    t.k("3G");
    t.k("5G");
    t.k("<C-o>");
    CHECK_EQ(t.cur().y, 2);
    t.k("<C-o>");
    CHECK_EQ(t.cur().y, 0);
    t.k("<Tab>");  // Ctrl-I
    CHECK_EQ(t.cur().y, 2);
    t.k("<Tab>");
    CHECK_EQ(t.cur().y, 4);
}

TEST(vim_motion_changelist) {
    VT t("‸a\nb\nc\nd");
    t.k("x3Gx");
    t.k("G");
    t.k("g;");
    CHECK_EQ(t.cur().y, 2);
    t.k("g;");
    CHECK_EQ(t.cur().y, 0);
    t.k("g,");
    CHECK_EQ(t.cur().y, 2);
}

// ---- searching ----

TEST(vim_motion_search_forward_backward_wrap) {
    VT t("x foo\nfoo y\n‸z foo");
    t.k("/foo<CR>");
    CHECK_EQ(t.cur(), Pos({2, 2}));
    t.k("n");  // wraps around
    CHECK_EQ(t.cur(), Pos({0, 2}));
    CHECK(t.msg().find("BOTTOM") != std::string::npos);
    t.k("N");
    CHECK_EQ(t.cur(), Pos({2, 2}));
    t.k("?foo<CR>");
    CHECK_EQ(t.cur(), Pos({1, 0}));
    t.k("n");  // "n" keeps the direction of "?"
    CHECK_EQ(t.cur(), Pos({0, 2}));
    t.k("N");
    CHECK_EQ(t.cur(), Pos({1, 0}));
}

TEST(vim_motion_search_count_and_offsets) {
    VT t("‸a1 a2 a3 a4");
    t.k("3/a<CR>");
    CHECK_EQ(t.cur().x, 9);
    t.k("0/a2/e<CR>");
    CHECK_EQ(t.cur().x, 4);
    t.k("0/a3/s+1<CR>");
    CHECK_EQ(t.cur().x, 7);
    t.k("0/a4/b-1<CR>");
    CHECK_EQ(t.cur().x, 8);
    VT l("‸x\nfoo\n  bar");
    l.k("/foo/+1<CR>");
    CHECK_EQ(l.cur(), Pos({2, 2}));
}

TEST(vim_motion_search_nowrapscan) {
    VT t("foo\n‸bar");
    t.k(":set nows<CR>");
    t.k("/foo<CR>");
    CHECK_EQ(t.cur(), Pos({1, 0}));
    CHECK(t.msg().find("E385") != std::string::npos);
}

TEST(vim_motion_search_not_found) {
    VT t("‸abc");
    int b = t.vim.beeps;
    t.k("/xyz<CR>");
    CHECK(t.msg().find("E486") != std::string::npos);
    CHECK_EQ(t.vim.beeps, b + 1);
    CHECK_EQ(t.cur().x, 0);
}

TEST(vim_motion_search_regex) {
    VT t("‸foo1 bar22 baz333");
    t.k("/\\d\\{3}<CR>");
    CHECK_EQ(t.cur().x, 14);
    t.k("0/\\v(bar|baz)\\d+<CR>");
    CHECK_EQ(t.cur().x, 5);
    t.k("0/\\<baz<CR>");
    CHECK_EQ(t.cur().x, 11);
    t.k("0/r\\zs2<CR>");
    CHECK_EQ(t.cur().x, 8);
}

TEST(vim_motion_search_ignorecase_smartcase) {
    VT t("‸x Foo foo");
    t.k("/foo<CR>");
    CHECK_EQ(t.cur().x, 6);
    t.k("0:set ic<CR>/foo<CR>");
    CHECK_EQ(t.cur().x, 2);
    t.k("0:set scs<CR>/Foo<CR>n");
    CHECK_EQ(t.cur().x, 2);
    t.k("0/foo\\C<CR>");
    CHECK_EQ(t.cur().x, 6);
    t.k(":set noic<CR>0/FOO\\c<CR>");
    CHECK_EQ(t.cur().x, 2);
}

TEST(vim_motion_search_empty_repeats_last) {
    VT t("‸a b a b");
    t.k("/b<CR>");
    CHECK_EQ(t.cur().x, 2);
    t.k("/<CR>");
    CHECK_EQ(t.cur().x, 6);
    t.k("0//e<CR>");
    CHECK_EQ(t.cur().x, 2);
}

TEST(vim_motion_star_hash) {
    VT t("‸foo bar foo foobar foo");
    t.k("*");
    CHECK_EQ(t.cur().x, 8);
    t.k("*");
    CHECK_EQ(t.cur().x, 19);  // whole words only
    t.k("#");
    CHECK_EQ(t.cur().x, 8);
    t.k("0g*");
    CHECK_EQ(t.cur().x, 8);
    t.k("g*");
    CHECK_EQ(t.cur().x, 12);  // also inside "foobar"
    t.k("n");
    CHECK_EQ(t.cur().x, 19);
}

TEST(vim_motion_star_uses_next_keyword) {
    VT t("‸  (bar) x bar");
    t.k("*");
    CHECK_EQ(t.cur().x, 10);
    CHECK_EQ(toUtf8(t.vim.lastPat), "\\<bar\\>");
}

TEST(vim_motion_gd) {
    VT t("int val;\nfoo(val);\nreturn ‸val;");
    t.k("gd");
    CHECK_EQ(t.cur(), Pos({0, 4}));
}

// ---- scrolling ----

TEST(vim_scroll_ctrl_e_y_d_u_f_b) {
    std::string s;
    for (int i = 0; i < 100; i++) s += (i ? "\n" : "") + std::to_string(i);
    VT t(s);
    t.k("<C-e>");
    CHECK_EQ(t.ed.top, 1);
    CHECK_EQ(t.cur().y, 1);  // the cursor stays in the view
    t.k("<C-y>");
    CHECK_EQ(t.ed.top, 0);
    t.k("<C-d>");
    CHECK_EQ(t.cur().y, 11);
    t.k("<C-u>");
    CHECK_EQ(t.cur().y, 1);
    t.k("<C-f>");
    CHECK_EQ(t.ed.top, 18);
    t.k("<C-b>");
    CHECK_EQ(t.ed.top, 0);
    t.k("gg5<C-d>");  // a count sets the amount for the following Ctrl-D / Ctrl-U
    CHECK_EQ(t.cur().y, 5);
    t.k("<C-d>");
    CHECK_EQ(t.cur().y, 10);
    VT other(s);  // ... per editor, not shared
    other.k("<C-d>");
    CHECK_EQ(other.cur().y, 10);
    t.k("50G$5zz");  // with a count: that line, in the wanted column
    CHECK_EQ(t.cur(), Pos({4, 0}));
    t.k("50Gzt");
    CHECK_EQ(t.ed.top, 49);
    t.k("zz");
    CHECK_EQ(t.ed.top, 39);
    t.k("zb");
    CHECK_EQ(t.ed.top, 30);
    t.k("<PageDown>");
    CHECK(t.ed.top > 30);
}
