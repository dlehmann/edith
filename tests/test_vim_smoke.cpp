// Quick checks of the basic Vim machinery.

#include "vimtest.h"

TEST(vim_smoke_insert_and_escape) {
    VT t("");
    t.k("ihello<Esc>");
    CHECK_EQ(t.state(), "hell‸o");
    CHECK_EQ(t.mode(), VM_NORMAL);
}

TEST(vim_smoke_dw) {
    VT t("one ‸two three");
    t.k("dw");
    CHECK_EQ(t.state(), "one ‸three");
    CHECK_EQ(t.reg('"'), "two ");
}

TEST(vim_smoke_dd_p) {
    VT t("a\n‸b\nc");
    t.k("ddp");
    CHECK_EQ(t.state(), "a\nc\n‸b");
}

TEST(vim_smoke_undo) {
    VT t("‸abc");
    t.k("xx");
    CHECK_EQ(t.text(), "c");
    t.k("u");
    CHECK_EQ(t.text(), "bc");
    t.k("u");
    CHECK_EQ(t.text(), "abc");
    t.k("<C-r>");
    CHECK_EQ(t.text(), "bc");
}

TEST(vim_smoke_search) {
    VT t("‸foo bar\nbaz bar");
    t.k("/bar<CR>");
    CHECK_EQ(t.cur(), Pos({0, 4}));
    t.k("n");
    CHECK_EQ(t.cur(), Pos({1, 4}));
}

TEST(vim_smoke_substitute) {
    VT t("a a a\nb a");
    t.k(":%s/a/x/g<CR>");
    CHECK_EQ(t.text(), "x x x\nb x");
}
