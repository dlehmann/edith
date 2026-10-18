// Vim macros (q, @) and mappings (:map and friends).

#include "vimtest.h"

// ---- macros ----

TEST(vim_macro_record_and_play) {
    VT t("‸1\n2\n3\n4");
    t.k("qaA!<Esc>jq");
    CHECK_EQ(t.reg('a'), "A!\x1bj");
    CHECK_EQ(t.vim.recReg, char32_t(0));
    t.k("@a");
    CHECK_EQ(t.text(), "1!\n2!\n3\n4");
    t.k("@@");
    CHECK_EQ(t.text(), "1!\n2!\n3!\n4");
    t.k("@a");
    CHECK_EQ(t.text(), "1!\n2!\n3!\n4!");
}

TEST(vim_macro_count_and_abort_on_error) {
    VT t("‸a\nb\nc\nd\ne");
    t.k("qqI-<Esc>jq");
    t.k("10@q");  // stops at the error in the last line ("j" fails)
    CHECK_EQ(t.text(), "-a\n-b\n-c\n-d\n-e");
    CHECK_EQ(t.cur().y, 4);
}

TEST(vim_macro_recursive) {
    VT t("‸x1\nx2\nx3");
    t.k("qwq");  // clear register w
    t.k("qwrYj@wq");
    t.k("@w");
    CHECK_EQ(t.text(), "Y1\nY2\nY3");
}

TEST(vim_macro_append_uppercase) {
    VT t("‸abc");
    t.k("qax");
    t.k("q");
    t.k("qAx");
    t.k("q");
    CHECK_EQ(t.reg('a'), "xx");
    t.set("‸abcdef");
    t.k("@a");
    CHECK_EQ(t.text(), "cdef");
}

TEST(vim_macro_from_yanked_text) {
    VT t("‸dd\n1\n2");
    t.k("\"ay$jj@a");  // a register filled by a yank can be executed
    CHECK_EQ(t.text(), "dd\n1");
}

TEST(vim_macro_with_ex_and_search) {
    VT t("‸foo bar\nfoo baz\nfoo qux");
    t.k("qq:s/foo/X/<CR>/foo<CR>q");
    t.k("@q");
    CHECK_EQ(t.text(), "X bar\nX baz\nfoo qux");
    t.k("@:");  // repeat the last command line
    CHECK_EQ(t.text(), "X bar\nX baz\nX qux");
}

TEST(vim_macro_invalid_registers) {
    VT t("‸a");
    int b = t.vim.beeps;
    t.k("q!");
    CHECK_EQ(t.vim.beeps, b + 1);
    CHECK_EQ(t.vim.recReg, char32_t(0));
    t.k("@@");
    CHECK(t.msg().find("E748") != std::string::npos);
    t.k("@:");
    CHECK(t.msg().find("E30") != std::string::npos);
    t.k("@z");  // empty register: nothing happens
    CHECK_EQ(t.text(), "a");
}

TEST(vim_macro_recording_status_and_mappings) {
    VT t("‸abc");
    t.k(":nnoremap X x<CR>");
    t.k("qmXq");
    CHECK_EQ(t.reg('m'), "X");  // typed keys are recorded, not the mapped ones
    t.k("@m");
    CHECK_EQ(t.text(), "c");
}

TEST(vim_macro_records_insert_special_keys) {
    VT t("‸");
    t.k("qaiab<BS>c<CR>d<Esc>q");
    t.set("‸");
    t.k("@a");
    CHECK_EQ(t.text(), "ac\nd");
}

// ---- mappings ----

TEST(vim_map_normal) {
    VT t("‸abc");
    t.k(":nnoremap Q x<CR>");
    t.k("Q");
    CHECK_EQ(t.text(), "bc");
    t.k(":nmap W Q<CR>");  // recursive
    t.k("W");
    CHECK_EQ(t.text(), "c");
    t.k(":nnoremap E W<CR>");  // not recursive: W is the motion
    t.k("E");
    CHECK_EQ(t.text(), "c");
}

TEST(vim_map_insert_jk_escape) {
    VT t("‸");
    t.k(":inoremap jk <lt>Esc><CR>");
    t.k("ihello jk");
    CHECK_EQ(t.text(), "hello ");
    CHECK_EQ(t.mode(), VM_NORMAL);
    t.k("Aj");  // waits for a possible "k"
    CHECK(t.vim.waiting());
    CHECK_EQ(t.text(), "hello ");
    t.k("x");   // no mapping: both keys are inserted
    CHECK_EQ(t.text(), "hello jx");
    t.k("j");
    t.vim.timeout();  // 'timeoutlen' expired: "j" is inserted
    CHECK_EQ(t.text(), "hello jxj");
    CHECK_EQ(t.mode(), VM_INSERT);
}

TEST(vim_map_visual_operator_pending_cmdline) {
    VT t("‸abc def");
    t.k(":xnoremap D d<CR>");
    t.k("vlD");
    CHECK_EQ(t.text(), "c def");
    t.k(":onoremap w e<CR>");  // only when an operator is pending
    t.set("‸abc def");
    t.k("dw");
    CHECK_EQ(t.text(), " def");
    t.k(":cnoremap QQ s/def/X/<CR>");
    t.k(":QQ<CR>");
    CHECK_EQ(t.text(), " X");
}

TEST(vim_map_leader_and_special_keys) {
    VT t("‸a\nb");
    t.k(":let mapleader=\",\"<CR>");
    t.k(":nnoremap <lt>leader>d dd<CR>");
    t.k(",d");
    CHECK_EQ(t.text(), "b");
    t.k(":nnoremap <lt>C-k> ifoo<lt>Esc><CR>");  // typed as "<C-k>" and "<Esc>"
    t.k("<C-k>");
    CHECK_EQ(t.text(), "foob");
    CHECK_EQ(t.mode(), VM_NORMAL);
    t.k(":nnoremap <lt>Space> x<CR>");
    t.k(" ");
    CHECK_EQ(t.text(), "fob");
    t.k(":nnoremap <lt>silent> <lt>F5> x<CR>");  // keys that do not exist are ignored
    CHECK(!t.vim.statusIsError());
}

TEST(vim_map_unmap_and_clear) {
    VT t("‸abcdef");
    t.k(":nmap Q x<CR>:imap Q y<CR>");
    t.k(":nunmap Q<CR>");
    t.k("Q");  // "Q" is not a Normal mode command here
    CHECK_EQ(t.text(), "abcdef");
    t.k("iQ<Esc>");
    CHECK_EQ(t.text(), "yabcdef");
    t.k(":iunmap Q<CR>:unmap Q<CR>");
    CHECK(t.msg().find("E31") != std::string::npos);
    t.k(":map Z x<CR>:mapclear<CR>Z");
    CHECK_EQ(t.text(), "yabcdef");
}

TEST(vim_map_list) {
    VT t("x");
    t.k(":nnoremap ab cd<CR>:imap <lt>C-x> foo<CR>");
    t.k(":map<CR>");
    CHECK_EQ(t.host.shown.size(), size_t(1));
    CHECK(toUtf8(t.host.shown[0]).find("ab") != std::string::npos);
    t.k(":imap<CR>");
    CHECK(toUtf8(t.host.shown[0]).find("<C-x>") != std::string::npos);
    t.k(":cmap<CR>");
    CHECK_EQ(t.msg(), "No mapping found");
}

TEST(vim_map_recursive_loop_is_stopped) {
    VT t("‸abc");
    t.k(":nmap a b<CR>:nmap b a<CR>");
    t.k("a");
    CHECK(t.msg().find("E223") != std::string::npos);
    CHECK_EQ(t.text(), "abc");
}

TEST(vim_map_rhs_starting_with_lhs) {
    VT t("‸abc");
    t.k(":nmap x xx<CR>");  // the first "x" of the rhs is not mapped again, the second is
    t.k("x");                // ... until "x" fails on the empty line
    CHECK_EQ(t.text(), "");
    CHECK(t.msg().find("E223") == std::string::npos);
}

TEST(vim_map_nop_and_map_bang) {
    VT t("‸abc");
    t.k(":nnoremap x <lt>Nop><CR>x");
    CHECK_EQ(t.text(), "abc");
    t.k(":map! ;; END<CR>");
    t.k("A;;<Esc>");
    CHECK_EQ(t.text(), "abcEND");
    t.k(":;;");  // also in Command-line mode
    CHECK_EQ(toUtf8(t.vim.cmdText), "END");
    t.k("<Esc>");
}

TEST(vim_map_not_applied_to_char_arguments) {
    VT t("‸axbx");
    t.k(":nnoremap x X<CR>");
    t.k("fx");  // the argument of "f" is not mapped
    CHECK_EQ(t.cur().x, 1);
    t.k("rx");
    CHECK_EQ(t.text(), "axbx");
}

TEST(vim_map_timeout_in_normal_mode) {
    VT t("‸abc");
    t.k(":nnoremap gx dd<CR>");
    t.k("g");
    CHECK(t.vim.waiting());
    t.vim.timeout();  // only "g": waits for the second key of a "g" command
    t.k("g");
    CHECK_EQ(t.cur().y, 0);
    t.k(":set notimeout<CR>");
    CHECK(!t.vim.opt.timeout);
}
