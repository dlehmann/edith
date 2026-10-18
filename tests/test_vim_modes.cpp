// Vim modes: Insert, Replace, Visual (character, line, block), Command-line,
// Operator-pending and Insert Normal mode (Ctrl-O).

#include "vimtest.h"

// ---- Insert mode ----

TEST(vim_insert_commands) {
    VT t("  ab‸cd");
    t.k("iX<Esc>");
    CHECK_EQ(t.state(), "  ab‸Xcd");
    t.k("aY<Esc>");
    CHECK_EQ(t.state(), "  abX‸Ycd");
    t.k("IZ<Esc>");
    CHECK_EQ(t.state(), "  ‸ZabXYcd");
    t.k("AW<Esc>");
    CHECK_EQ(t.state(), "  ZabXYcd‸W");
    t.k("gIV<Esc>");
    CHECK_EQ(t.state(), "‸V  ZabXYcdW");
    CHECK_EQ(t.mode(), VM_NORMAL);
}

TEST(vim_insert_o_O) {
    VT t("‸a\nb");
    t.k("ox<Esc>");
    CHECK_EQ(t.state(), "a\n‸x\nb");
    t.k("Oy<Esc>");
    CHECK_EQ(t.state(), "a\n‸y\nx\nb");
    t.k("3oz<Esc>");
    CHECK_EQ(t.text(), "a\ny\nz\nz\nz\nx\nb");
}

TEST(vim_insert_autoindent) {
    VT t("    ‸foo");
    t.k(":set ai<CR>");
    t.k("A<CR>bar<Esc>");
    CHECK_EQ(t.text(), "    foo\n    bar");
    t.k("o<Esc>");  // an unused indent is removed
    CHECK_EQ(t.text(), "    foo\n    bar\n");
    t.k("ggA<CR><CR>x<Esc>");
    CHECK_EQ(t.text(), "    foo\n\n    x\n    bar\n");
    t.k(":set noai<CR>");
    t.set("    ‸foo");
    t.k("o<Esc>");  // without 'autoindent' the new line has no indent
    CHECK_EQ(t.text(), "    foo\n");
}

TEST(vim_insert_enter_splits_line) {
    VT t("ab‸cd");
    t.k("i<CR><Esc>");
    CHECK_EQ(t.state(), "ab\n‸cd");
    t.set("ab‸cd");
    t.k("i<C-j>x<Esc>");
    CHECK_EQ(t.text(), "ab\nxcd");
}

TEST(vim_insert_backspace_and_delete) {
    VT t("ab\n‸cd");
    t.k("i<BS><Esc>");  // joins with the previous line ('backspace' eol)
    CHECK_EQ(t.text(), "abcd");
    t.set("ab‸cd");
    t.k("i<BS><BS><BS>x<Esc>");
    CHECK_EQ(t.text(), "xcd");
    t.set("ab‸cd");
    t.k("i<Del><Esc>");
    CHECK_EQ(t.text(), "abd");
    t.set("ab‸\ncd");
    t.k("a<Del><Esc>");
    CHECK_EQ(t.text(), "abcd");
}

TEST(vim_insert_ctrl_w_ctrl_u) {
    VT t("foo bar‸");
    t.k("a<C-w><Esc>");
    CHECK_EQ(t.text(), "foo ");
    t.set("foo.bar‸");
    t.k("a<C-w><C-w><Esc>");
    CHECK_EQ(t.text(), "foo");
    t.set("    foo bar‸");
    t.k("a<C-u><Esc>");  // keeps the indent
    CHECK_EQ(t.text(), "    ");
    t.set("abc‸");
    t.k("a typed<C-u>X<Esc>");  // only the newly typed text
    CHECK_EQ(t.text(), "abcX");
}

TEST(vim_insert_ctrl_t_ctrl_d) {
    VT t("‸foo");
    t.k(":set sw=4 et<CR>");
    t.k("i<C-t><Esc>");
    CHECK_EQ(t.text(), "    foo");
    t.k("a<C-t><C-t><C-d><Esc>");
    CHECK_EQ(t.text(), "        foo");
    t.k("A0<C-d><Esc>");
    CHECK_EQ(t.text(), "foo");
}

TEST(vim_insert_tab_expandtab) {
    VT t("‸");
    t.k("i<Tab>x<Esc>");
    CHECK_EQ(t.text(), "\tx");
    t.set("‸ab");
    t.k(":set et ts=4<CR>a<Tab>x<Esc>");
    CHECK_EQ(t.text(), "a   xb");
}

TEST(vim_insert_ctrl_r_register) {
    VT t("‸word rest");
    t.k("yiwA <C-r>\"<Esc>");
    CHECK_EQ(t.text(), "word rest word");
    t.k("\"ayy");
    t.k("o<C-r>a<Esc>");  // a linewise register inserts a line break
    CHECK_EQ(t.text(), "word rest word\nword rest word\n");
    t.set("‸x");
    t.k("i<C-r>%<Esc>");  // file name (empty)
    CHECK_EQ(t.text(), "x");
}

TEST(vim_insert_ctrl_a_ctrl_at) {
    VT t("‸");
    t.k("ifoo<Esc>");
    t.k("A <C-a><Esc>");
    CHECK_EQ(t.text(), "foo foo");
    t.k("o<C-@>");  // inserts the last text and stops Insert mode
    CHECK_EQ(t.text(), "foo foo\n foo");
    CHECK_EQ(t.mode(), VM_NORMAL);
    CHECK_EQ(t.reg('.'), " foo");
}

TEST(vim_insert_ctrl_v_literal) {
    VT t("‸");
    t.k("i<C-v><Tab><C-v>065<C-v>u00e4<C-v>x41<Esc>");
    CHECK_EQ(t.text(), "\tAäA");
    t.set("‸");
    t.k("i<C-v><Esc>x<Esc>");
    CHECK_EQ(t.text(), "\x1bx");
    t.set("‸");
    t.k("i<C-v>9z<Esc>");  // a short code ends at the first non-digit
    CHECK_EQ(t.text(), "\tz");
}

TEST(vim_insert_ctrl_k_digraphs) {
    VT t("‸");
    t.k("i<C-k>a:<C-k>ss<C-k>Eu<C-k>e'<C-k>:o<Esc>");
    CHECK_EQ(t.text(), "äß€éö");
}

TEST(vim_insert_ctrl_e_ctrl_y) {
    VT t("abc\n‸\nxyz");
    t.k("i<C-y><C-y><C-e><Esc>");
    CHECK_EQ(t.text(), "abc\nabz\nxyz");
}

TEST(vim_insert_ctrl_n_ctrl_p_completion) {
    VT t("foobar foobaz\n‸");
    t.k("ifoo<C-p><Esc>");
    CHECK_EQ(t.text(), "foobar foobaz\nfoobaz");
    t.set("foobar foobaz\n‸");
    t.k("ifoo<C-p><C-p><Esc>");
    CHECK_EQ(t.text(), "foobar foobaz\nfoobar");
    t.set("‸\nalpha alps");
    t.k("ial<C-n><Esc>");
    CHECK_EQ(t.text(), "alpha\nalpha alps");
    t.set("‸");
    t.k("izz<C-n><Esc>");  // nothing found
    CHECK_EQ(t.text(), "zz");
}

TEST(vim_insert_ctrl_o) {
    VT t("abc‸d");
    t.k("i<C-o>0X<Esc>");
    CHECK_EQ(t.text(), "Xabcd");
    t.set("ab‸c");
    t.k("A<C-o>~");  // at the end: the command runs on the last character, Insert continues at the end
    CHECK_EQ(t.mode(), VM_INSERT);
    t.k("!<Esc>");
    CHECK_EQ(t.text(), "abC!");
    t.set("‸one two");
    t.k("i<C-o>dwX<Esc>");
    CHECK_EQ(t.text(), "Xtwo");
    t.set("‸a");
    t.k("i<C-o>:s/a/b/<CR>c<Esc>");
    CHECK_EQ(t.text(), "cb");
}

TEST(vim_insert_arrows_break_undo) {
    VT t("‸");
    t.k("iab<Left>X<Esc>");
    CHECK_EQ(t.text(), "aXb");
    t.k("u");
    CHECK_EQ(t.text(), "ab");
    t.k("u");
    CHECK_EQ(t.text(), "");
}

TEST(vim_insert_ctrl_g_u_breaks_undo) {
    VT t("‸");
    t.k("ione<C-g>u two<Esc>u");
    CHECK_EQ(t.text(), "one");
}

TEST(vim_insert_count) {
    VT t("‸x");
    t.k("3ia<Esc>");
    CHECK_EQ(t.state(), "aa‸ax");
    t.k("2Ab<CR><Esc>");
    CHECK_EQ(t.text(), "aaaxb\nb\n");
}

TEST(vim_insert_ctrl_c_skips_count) {
    VT t("‸x");
    t.k("3ia<C-c>");
    CHECK_EQ(t.text(), "ax");
    CHECK_EQ(t.mode(), VM_NORMAL);
}

TEST(vim_insert_textwidth_wrap) {
    VT t("‸");
    t.k(":set tw=10<CR>iaaa bbb ccc ddd<Esc>");
    CHECK_EQ(t.text(), "aaa bbb\nccc ddd");
}

TEST(vim_insert_key_toggles_replace) {
    VT t("‸abc");
    t.k("iX<Insert>YZ<Esc>");
    CHECK_EQ(t.text(), "XYZc");
    t.k("<Insert>Q<Esc>");  // Insert key in Normal mode = "i"
    CHECK_EQ(t.text(), "XYQZc");
}

// ---- Replace mode ----

TEST(vim_replace_mode) {
    VT t("‸abcd");
    t.k("Rxy<Esc>");
    CHECK_EQ(t.state(), "x‸ycd");
    t.k("$Rzzz<Esc>");  // appends at the end of the line
    CHECK_EQ(t.text(), "xyczzz");
    t.set("‸abcd");
    t.k("Rxyz<BS><BS><Esc>");  // backspace restores the original text
    CHECK_EQ(t.text(), "xbcd");
    t.set("‸ab");
    t.k("Rwxyz<BS><BS><Esc>");
    CHECK_EQ(t.text(), "wx");
    t.set("‸abcd");
    t.k("2Rxy<Esc>");  // the replacement is repeated, still replacing
    CHECK_EQ(t.text(), "xyxy");
    CHECK_EQ(t.mode(), VM_NORMAL);
}

TEST(vim_replace_mode_enter) {
    VT t("‸abcd");
    t.k("Rx<CR>y<Esc>");
    CHECK_EQ(t.text(), "x\nycd");
}

// ---- Visual mode ----

TEST(vim_visual_char) {
    VT t("‸abcdef");
    t.k("vlld");
    CHECK_EQ(t.state(), "‸def");
    CHECK_EQ(t.mode(), VM_NORMAL);
    t.k("vly");
    CHECK_EQ(t.reg('"'), "de");
    t.set("ab‸cdef");
    t.k("vhhx");  // backwards
    CHECK_EQ(t.text(), "def");
    t.set("‸ab\ncd");
    t.k("v$d");  // "$" includes the line break
    CHECK_EQ(t.text(), "cd");
}

TEST(vim_visual_line) {
    VT t("1\n‸2\n3\n4");
    t.k("Vjd");
    CHECK_EQ(t.state(), "1\n‸4");
    t.k("Vky");
    CHECK_EQ(t.reg('"'), "1\n4\n");
    CHECK_EQ(t.regType('"'), RT_LINE);
    t.set("‸a\nb");
    t.k("VjJ");
    CHECK_EQ(t.text(), "a b");
}

TEST(vim_visual_o_swaps_ends) {
    VT t("ab‸cdef");
    t.k("vlo");
    CHECK_EQ(t.cur().x, 2);
    t.k("hd");
    CHECK_EQ(t.text(), "aef");
}

TEST(vim_visual_switch_modes) {
    VT t("‸ab\ncd");
    t.k("vj");
    CHECK_EQ(t.mode(), VM_VISUAL);
    t.k("V");
    CHECK_EQ(t.mode(), VM_VLINE);
    t.k("<C-v>");
    CHECK_EQ(t.mode(), VM_VBLOCK);
    t.k("<C-v>");
    CHECK_EQ(t.mode(), VM_NORMAL);
    t.k("v<Esc>");
    CHECK_EQ(t.mode(), VM_NORMAL);
    t.k("v<C-c>");
    CHECK_EQ(t.mode(), VM_NORMAL);
    t.k("vv");
    CHECK_EQ(t.mode(), VM_NORMAL);
}

TEST(vim_visual_gv) {
    VT t("‸abc def");
    t.k("viw<Esc>$gvd");
    CHECK_EQ(t.text(), " def");
    t.k("wvl<Esc>gvy");
    CHECK_EQ(t.reg('"'), "de");
}

TEST(vim_visual_text_objects) {
    VT t("foo b‸ar baz");
    t.k("viwd");
    CHECK_EQ(t.text(), "foo  baz");
    t.set("x(a (b‸) c)");
    t.k("vi(d");
    CHECK_EQ(t.text(), "x(a () c)");
    t.set("x(a (b‸b) c)");
    t.k("vi(i(d");  // repeated: the next outer block
    CHECK_EQ(t.text(), "x()");
    t.set("a\n‸b\n\nc");
    t.k("vipd");
    CHECK_EQ(t.text(), "\nc");
    t.set("foo b‸ar baz qux");
    t.k("vawawd");
    CHECK_EQ(t.text(), "foo qux");
}

TEST(vim_visual_operators) {
    VT t("‸Hello World");
    t.k("veU");
    CHECK_EQ(t.text(), "HELLO World");
    t.k("wveu");
    CHECK_EQ(t.text(), "HELLO world");
    t.k("0vll~");
    CHECK_EQ(t.text(), "helLO world");
    t.k("0vlrx");
    CHECK_EQ(t.text(), "xxlLO world");
    t.k("0veg?");
    CHECK_EQ(t.text(), "kkyYB world");
    t.set("‸a\nb\nc");
    t.k(":set sw=2 et<CR>Vj>");
    CHECK_EQ(t.text(), "  a\n  b\nc");
    t.k("Vj2<");
    CHECK_EQ(t.text(), "a\nb\nc");
    t.k("Vj3>");
    CHECK_EQ(t.text(), "      a\n      b\nc");
}

TEST(vim_visual_change) {
    VT t("ab‸cdef");
    t.k("vlcXY<Esc>");
    CHECK_EQ(t.text(), "abXYef");
    t.set("1\n‸2\n3");
    t.k("VcX<Esc>");
    CHECK_EQ(t.text(), "1\nX\n3");
    t.set("1\n‸2\n3");
    t.k("vSY<Esc>");  // "S" in Visual mode is linewise
    CHECK_EQ(t.text(), "1\nY\n3");
    t.set("ab‸cd\nxy");
    t.k("vX");
    CHECK_EQ(t.text(), "xy");
}

TEST(vim_visual_I_A) {
    VT t("  a‸b\ncd");
    t.k("vjI#<Esc>");  // characterwise: insert at the start of the first line
    CHECK_EQ(t.text(), "#  ab\ncd");
    t.set("‸ab\ncd");
    t.k("vjA!<Esc>");
    CHECK_EQ(t.text(), "ab\ncd!");
}

TEST(vim_visual_put_replaces_selection) {
    VT t("‸one two");
    t.k("yiwwviwp");
    CHECK_EQ(t.text(), "one one");
    CHECK_EQ(t.reg('"'), "two");  // "p" puts the replaced text into the unnamed register
    t.set("‸one two");
    t.k("yiwwviwP");
    CHECK_EQ(t.text(), "one one");
    CHECK_EQ(t.reg('"'), "one");  // "P" keeps it
    t.set("‸L1\nx y");
    t.k("yyjwvp");  // a linewise register replaces a character selection with lines
    CHECK_EQ(t.text(), "L1\nx \nL1\n");
    t.set("‸word\nL2\nL3");
    t.k("yiwjVjp");  // a character register replaces lines
    CHECK_EQ(t.text(), "word\nword");
}

TEST(vim_visual_colon_range) {
    VT t("a\n‸b\nc\nd");
    t.k("Vj:s/$/!/<CR>");
    CHECK_EQ(t.text(), "a\nb!\nc!\nd");
    t.k("gg:'<,'>d<CR>");
    CHECK_EQ(t.text(), "a\nd");
}

TEST(vim_visual_search_extends) {
    VT t("‸abc def ghi");
    t.k("v/gh<CR>d");
    CHECK_EQ(t.text(), "hi");
}

TEST(vim_visual_invalid_commands) {
    VT t("‸abc\ndef");
    t.k("xu");
    t.k("Vj");
    int b = t.vim.beeps;
    t.k("<C-r>");  // not a Visual mode command
    CHECK_EQ(t.vim.beeps, b + 1);
    CHECK_EQ(t.mode(), VM_VLINE);
    t.k(".");
    CHECK_EQ(t.vim.beeps, b + 2);
    t.k("ma<Esc>'a");  // marks work
    CHECK_EQ(t.cur().y, 1);
    CHECK_EQ(t.text(), "abc\ndef");
}

TEST(vim_visual_dollar_and_motions) {
    VT t("‸abc\nde");
    t.k("vj$y");
    CHECK_EQ(t.reg('"'), "abc\nde");
    t.k("vGd");
    CHECK_EQ(t.text(), "e");
}

// ---- Visual block mode ----

TEST(vim_vblock_delete_yank) {
    VT t("‸abcd\nefgh\nijkl");
    t.k("<C-v>jld");
    CHECK_EQ(t.text(), "cd\ngh\nijkl");
    CHECK_EQ(t.regType('"'), RT_BLOCK);
    CHECK_EQ(t.reg('"'), "ab\nef");
    t.k("G$<C-v>kky");  // after "$" the block reaches to the end of every line
    CHECK_EQ(t.reg('"'), "d\nh\njkl");
}

TEST(vim_vblock_put) {
    VT t("‸ab\ncd\nef");
    t.k("<C-v>jy$p");
    CHECK_EQ(t.text(), "aba\ncdc\nef");
    t.set("‸xy\nz");
    t.vim.regs['a'] = VimReg{{U"1", U"2"}, RT_BLOCK};
    t.k("l\"aP");
    CHECK_EQ(t.text(), "x1y\nz2");  // short lines are padded
    t.set("‸ab\ncd");
    t.vim.regs['a'] = VimReg{{U"1", U"2", U"3"}, RT_BLOCK};
    t.k("\"ap");
    CHECK_EQ(t.text(), "a1b\nc2d\n 3");
}

TEST(vim_vblock_insert_append) {
    VT t("‸abc\ndef\nghi");
    t.k("<C-v>jjI# <Esc>");
    CHECK_EQ(t.text(), "# abc\n# def\n# ghi");
    t.set("‸abc\nd\nghi");
    t.k("l<C-v>jjA|<Esc>");
    CHECK_EQ(t.text(), "ab|c\nd |\ngh|i");
    t.set("‸ab\nlong\nx");
    t.k("<C-v>jj$A;<Esc>");  // with "$": append at every line end
    CHECK_EQ(t.text(), "ab;\nlong;\nx;");
    t.set("‸abc\nd\nghi");
    t.k("ll<C-v>jjIX<Esc>");  // "I" skips short lines
    CHECK_EQ(t.text(), "abXc\nd\nghXi");
}

TEST(vim_vblock_change) {
    VT t("‸abcd\nefgh");
    t.k("l<C-v>jlcXY<Esc>");
    CHECK_EQ(t.text(), "aXYd\neXYh");
    t.set("‸abcd\nefgh");
    t.k("l<C-v>jC!<Esc>");
    CHECK_EQ(t.text(), "a!\ne!");
}

TEST(vim_vblock_replace_case_shift) {
    VT t("‸abcd\nefgh");
    t.k("l<C-v>jlrx");
    CHECK_EQ(t.text(), "axxd\nexxh");
    t.k("0<C-v>jU");
    CHECK_EQ(t.text(), "Axxd\nExxh");
    t.k(":set sw=2<CR>l<C-v>j>");
    CHECK_EQ(t.text(), "A  xxd\nE  xxh");
    t.k("<C-v>j<");
    CHECK_EQ(t.text(), "Axxd\nExxh");
}

TEST(vim_vblock_O_other_corner) {
    VT t("‸abcd\nefgh");
    t.k("<C-v>jllO");
    CHECK_EQ(t.cur(), Pos({1, 0}));
    t.k("d");
    CHECK_EQ(t.text(), "d\nh");
}

TEST(vim_vblock_with_tabs) {
    S.tabw = 8;  // the expected columns are for tab stops every 8 columns
    VT t("‸a\tb\nabcdefghij");
    // on the tab the block reaches to the end of the tab (columns 1 to 7)
    t.k("j4l<C-v>kd");
    CHECK_EQ(t.text(), "ab\naij");
    t.set("ab‸cdef\na\tb\nabcdef");
    t.k("<C-v>jjld");  // the tab in the middle line is split into spaces where the block cuts it
    CHECK_EQ(t.text(), "abef\na     b\nabef");
}

// ---- Command-line mode ----

TEST(vim_cmdline_editing) {
    VT t("‸abc");
    t.k(":s/b/X");
    CHECK_EQ(t.mode(), VM_CMDLINE);
    CHECK_EQ(toUtf8(t.vim.cmdlineText()), ":s/b/X");
    t.k("<BS>Q<Left><Del>Y<Home>%<End>/<CR>");
    CHECK_EQ(t.text(), "aYc");
    t.k(":s/c/Z<Left><Right>/<CR>");
    CHECK_EQ(t.text(), "aYZ");
    CHECK_EQ(t.mode(), VM_NORMAL);
}

TEST(vim_cmdline_cancel) {
    VT t("‸abc");
    t.k(":d<Esc>");
    CHECK_EQ(t.text(), "abc");
    CHECK_EQ(t.mode(), VM_NORMAL);
    t.k(":<BS>");  // backspace on an empty line leaves Command-line mode
    CHECK_EQ(t.mode(), VM_NORMAL);
    t.k(":d<C-c>");
    CHECK_EQ(t.text(), "abc");
    t.k("/b<Esc>");
    CHECK_EQ(t.cur().x, 0);
}

TEST(vim_cmdline_ctrl_u_ctrl_w) {
    VT t("‸abc");
    t.k(":foo bar<C-w><C-w>s/a/q/<CR>");
    CHECK_EQ(t.text(), "qbc");
    t.k(":junk<C-u>s/q/r/<CR>");
    CHECK_EQ(t.text(), "rbc");
}

TEST(vim_cmdline_history) {
    VT t("‸a\na\na");
    t.k(":s/a/b/<CR>j:<Up><CR>");
    CHECK_EQ(t.text(), "b\nb\na");
    t.k(":s/x/y/e<CR>");
    t.k("j:s<Up><Up><CR>");  // history filtered by the typed prefix
    CHECK_EQ(t.text(), "b\nb\nb");
    t.k("gg/b<CR>gg/<Up><CR>");
    CHECK_EQ(t.cur().y, 1);
}

TEST(vim_cmdline_ctrl_r) {
    VT t("‸hello world");
    t.k("yiw:s/<C-r>\"/bye/<CR>");
    CHECK_EQ(t.text(), "bye world");
    t.k("w:s/<C-r><C-w>/earth/<CR>");
    CHECK_EQ(t.text(), "bye earth");
}

TEST(vim_cmdline_count_prefix) {
    VT t("‸1\n2\n3\n4");
    t.k("3:d<CR>");  // ":.,.+2d"
    CHECK_EQ(t.text(), "4");
}

// ---- Operator-pending mode ----

TEST(vim_operator_pending_cancel) {
    VT t("‸abc");
    t.k("d<Esc>");
    CHECK_EQ(t.text(), "abc");
    CHECK_EQ(t.vim.cmdbuf.size(), size_t(0));
    t.k("dx");  // not a motion: the operator is cancelled
    CHECK_EQ(t.text(), "abc");
    t.k("x");
    CHECK_EQ(t.text(), "bc");
    t.k("2d3<Esc>");
    CHECK_EQ(t.text(), "bc");
    t.k("\"a<Esc>");
    CHECK_EQ(t.text(), "bc");
}

TEST(vim_status_text) {
    VT t("‸a");
    CHECK_EQ(toUtf8(t.vim.statusText()), "");
    t.k("i");
    CHECK_EQ(toUtf8(t.vim.statusText()), "-- INSERT --");
    t.k("<Esc>R");
    CHECK_EQ(toUtf8(t.vim.statusText()), "-- REPLACE --");
    t.k("<Esc>v");
    CHECK_EQ(toUtf8(t.vim.statusText()), "-- VISUAL --");
    t.k("V");
    CHECK_EQ(toUtf8(t.vim.statusText()), "-- VISUAL LINE --");
    t.k("<C-v>");
    CHECK_EQ(toUtf8(t.vim.statusText()), "-- VISUAL BLOCK --");
    t.k("<Esc>qa");
    CHECK_EQ(toUtf8(t.vim.statusText()), "recording @a");
    t.k("q2d");
    CHECK_EQ(toUtf8(t.vim.showCmd()), "2d");
    t.k("<Esc>i<C-o>");
    CHECK_EQ(toUtf8(t.vim.statusText()), "-- (insert) --");
}

TEST(vim_cursor_shapes) {
    VT t("‸a");
    CHECK_EQ(t.vim.cursorShape(), 1);
    t.k("i");
    CHECK_EQ(t.vim.cursorShape(), 5);
    t.k("<Esc>R");
    CHECK_EQ(t.vim.cursorShape(), 3);
    t.k("<Esc>:");
    CHECK_EQ(t.vim.cursorShape(), 5);
}

TEST(vim_visual_span_display) {
    VT t("‸abc\ndefg\nhi");
    t.k("vjl");
    int s, e;
    CHECK(t.vim.visualSpan(0, s, e));
    CHECK_EQ(s, 0);
    CHECK_EQ(e, 4);  // to the line break
    CHECK(t.vim.visualSpan(1, s, e));
    CHECK_EQ(s, 0);
    CHECK_EQ(e, 2);
    CHECK(!t.vim.visualSpan(2, s, e));
    t.k("<Esc>gg<C-v>jl");
    CHECK(t.vim.visualSpan(1, s, e));
    CHECK_EQ(s, 0);
    CHECK_EQ(e, 2);
}
