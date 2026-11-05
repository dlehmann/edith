// Soft wrap (Options > Word Wrap, Vim's 'wrap') and the right margin (Options > Right Margin,
// Vim's 'colorcolumn'): the row layout of the Editor, drawing, keys, mouse and :set.

#include "apptest.h"
#include "i18n.h"
#include "menus.h"
#include "test.h"
#include "vimtest.h"

using namespace apptest;

static std::vector<int> rows(const char* s, int w) {
    Editor e;
    return e.wrapRows(U(s), w);
}

TEST(wrap_rows_break_at_blanks) {
    CHECK(rows("aaaa bbbb cccc", 10) == std::vector<int>({0, 10}));
    CHECK(rows("aaaa bbbbbb", 8) == std::vector<int>({0, 5}));    // the word moves to the next row
    CHECK(rows("abcdefghijkl", 5) == std::vector<int>({0, 5, 10}));  // a long word is cut
    CHECK(rows("ab cdefghijk", 5) == std::vector<int>({0, 3, 8}));
    CHECK(rows("abcd", 5) == std::vector<int>({0}));
    CHECK(rows("abcde", 5) == std::vector<int>({0, 5}));  // a full row: an empty row for the cursor
    CHECK(rows("", 5) == std::vector<int>({0}));
    CHECK(rows("日本語", 5) == std::vector<int>({0, 2}));  // wide characters are not split
    CHECK(rows("a\tb", 6) == std::vector<int>({0}));        // the tab reaches column 4, "b" fits after it
    CHECK(rows("abcde\tx", 6) == std::vector<int>({0, 5}));  // the tab does not fit
    CHECK(rows("a\tbcd", 6) == std::vector<int>({0, 2}));
    CHECK(rows("abcdefgh", 0) == std::vector<int>({0}));   // no wrapping
    S.linebreak = false;  // Vim's default: in the middle of a word
    CHECK(rows("aaaa bbbbbb", 8) == std::vector<int>({0, 8}));
}

TEST(wrap_row_positions) {
    Editor e;
    e.lines = {U"aaaa bbbbbb", U"abcde"};
    int r, c;
    e.rowCol(0, 4, 8, r, c);
    CHECK_EQ(r, 0);
    CHECK_EQ(c, 4);
    e.rowCol(0, 5, 8, r, c);
    CHECK_EQ(r, 1);
    CHECK_EQ(c, 0);
    e.rowCol(0, 7, 8, r, c);
    CHECK_EQ(r, 1);
    CHECK_EQ(c, 2);
    e.rowCol(1, 5, 5, r, c);  // the end of a full row is on the next row
    CHECK_EQ(r, 1);
    CHECK_EQ(c, 0);
    e.rowCol(0, 7, 0, r, c);  // without wrapping: the screen column
    CHECK_EQ(r, 0);
    CHECK_EQ(c, 7);
    CHECK_EQ(e.idxAtRow(0, 0, 7, 8), 4);  // at most the blank before the next row
    CHECK_EQ(e.idxAtRow(0, 1, 3, 8), 8);
    CHECK_EQ(e.idxAtRow(0, 1, 99, 8), 11);
    CHECK_EQ(e.idxAtRow(0, 0, 7, 0), 7);
    CHECK_EQ(e.rowCount(0, 8), 2);
    CHECK_EQ(e.rowCount(0, 0), 1);
}

TEST(wrap_scrolling_and_cursor_keys) {
    S.wrap = true;
    Editor e;
    e.lines.assign(10, u32(25, 'x'));  // three rows of 10 columns each
    e.cx = 3;
    e.moveKey(K_DOWN, 0, 5, 10);
    CHECK_EQ(e.cy, 0);
    CHECK_EQ(e.cx, 13);
    e.moveKey(K_DOWN, 0, 5, 10);
    CHECK_EQ(e.cx, 23);
    e.moveKey(K_DOWN, 0, 5, 10);  // the next line, the same column
    CHECK_EQ(e.cy, 1);
    CHECK_EQ(e.cx, 3);
    e.moveKey(K_UP, 0, 5, 10);
    CHECK_EQ(e.cy, 0);
    CHECK_EQ(e.cx, 23);
    // the view scrolls by rows
    e.left = 5;
    e.cy = 3;
    e.cx = 20;
    e.scrollTo(5, 10);
    CHECK_EQ(e.left, 0);
    CHECK_EQ(e.top, 2);
    CHECK_EQ(e.topRow, 1);
    e.cy = 0;
    e.cx = 0;
    e.scrollTo(5, 10);
    CHECK_EQ(e.top, 0);
    CHECK_EQ(e.topRow, 0);
    e.cy = 5;
    e.cx = 10;
    e.scrollTo(5, 10, true);  // centered
    CHECK_EQ(e.top, 4);
    CHECK_EQ(e.topRow, 2);
    CHECK_EQ(e.rowFromTop(5, 1, 10, 5), 2);
    CHECK_EQ(e.rowFromTop(4, 1, 10, 5), -1);
    // the last page stays full
    e.top = e.topRow = 0;
    e.scrollRows(100, 5, 10);
    CHECK_EQ(e.top, 8);
    CHECK_EQ(e.topRow, 1);
    e.scrollRows(-1, 5, 10);
    CHECK_EQ(e.top, 8);
    CHECK_EQ(e.topRow, 0);
    // PgDn moves the cursor and the view by a page of rows
    e.top = e.topRow = 0;
    e.cy = e.cx = 0;
    e.wantCol = -1;
    e.moveKey(K_PGDN, 0, 5, 10);
    CHECK_EQ(e.cy, 1);
    CHECK_EQ(e.cx, 10);
    CHECK_EQ(e.top, 1);
    CHECK_EQ(e.topRow, 1);
    // Ctrl+Down scrolls by a row and keeps the cursor in the view
    e.top = e.topRow = 0;
    e.cy = e.cx = 0;
    e.moveKey(K_DOWN, M_CTRL, 5, 10);
    CHECK_EQ(e.top, 0);
    CHECK_EQ(e.topRow, 1);
    CHECK_EQ(e.cy, 0);
    CHECK_EQ(e.cx, 10);
    // without soft wrap Up and Down move by lines
    S.wrap = false;
    e.cy = 0;
    e.cx = 3;
    e.wantCol = -1;
    e.moveKey(K_DOWN, 0, 5, 10);
    CHECK_EQ(e.cy, 1);
    CHECK_EQ(e.cx, 3);
}

TEST(app_word_wrap) {
    std::string longLine;
    for (int i = 0; i < 30; i++) longLine += "word" + std::to_string(i % 10) + " ";
    AppT t(longLine + "\nend");
    int y0 = t.a().ty(), x0 = t.a().tx(), w = t.a().tw();
    CHECK(t.row(y0 + 1).find("end") != std::string::npos);
    t.keys(alt('o') + alt('w'));  // Options > Word Wrap
    CHECK(S.wrap);
    // 180 columns in rows of whole words: "word0 ... word2 " fits 13 words in 78 columns
    std::vector<int> r = t.ed().wrapRows(t.ed().lines[0], w);
    CHECK_EQ(int(r.size()), 3);
    CHECK_EQ(r[1] % 6, 0);  // a row starts at a word
    CHECK(t.row(y0).find("│word0 word1") == 0);
    CHECK(t.row(y0 + 1).find("│word") == 0);
    CHECK(t.row(y0 + 3).find("│end") == 0);
    // ↓ moves to the next screen row of the same line, End to the last row
    t.keys(DOWN);
    CHECK_EQ(t.ed().cy, 0);
    CHECK_EQ(t.ed().cx, r[1]);
    CHECK_EQ(t.a().scr.cy, y0 + 1);
    CHECK_EQ(t.a().scr.cx, x0);
    t.keys("\x1b[F");  // End
    CHECK_EQ(t.a().scr.cy, y0 + 2);
    t.keys(DOWN);
    CHECK_EQ(t.ed().cy, 1);
    CHECK_EQ(t.a().scr.cy, y0 + 3);
    // a click on a continuation row places the cursor in it
    t.keys(click(x0 + 2, y0 + 1) + click(x0 + 2, y0 + 1, 0, true));
    CHECK_EQ(t.ed().cy, 0);
    CHECK_EQ(t.ed().cx, r[1] + 2);
    // the line numbers are shown on the first row of a line only
    S.lineNums = 1;
    t.draw();
    CHECK(t.row(y0).find("│  1  word0") == 0);
    CHECK(t.row(y0 + 1).find("│     word") == 0);
    CHECK(t.row(y0 + 3).find("│  2  end") == 0);
    t.keys(alt('o') + alt('w') + "\x1b[H");  // and Home: the view scrolls back to column 0
    CHECK(!S.wrap);
    CHECK(t.row(y0 + 1).find("│  2  end") == 0);  // without soft wrap every row has its number
    S.lineNums = 0;
    t.draw();
    CHECK(t.row(y0 + 1).find("│end") == 0);
}

TEST(app_word_wrap_scrolls_by_rows) {
    std::string s;
    for (int i = 0; i < 40; i++) s += std::string(100, char('a' + i % 26)) + "\n";  // two rows each
    AppT t(s);
    S.wrap = true;
    t.keys("\x1b[<65;10;10M");  // the wheel scrolls by rows
    CHECK_EQ(t.ed().top, 1);
    CHECK_EQ(t.ed().topRow, 1);
    CHECK(t.row(t.a().ty()).find(std::string(20, 'b')) != std::string::npos);
    t.keys("\x1b[<64;10;10M");
    CHECK_EQ(t.ed().top, 0);
    CHECK_EQ(t.ed().topRow, 0);
    // the cursor stays visible at the bottom of the window
    for (int i = 0; i < 30; i++) t.keys(DOWN);
    CHECK_EQ(t.ed().cy, 15);
    CHECK(t.a().scr.cvis);
    CHECK_EQ(t.a().scr.cy, t.a().ty() + t.a().th() - 1);
    t.keys(ctrl('a'));  // Select All: the selection is drawn on every row
    CHECK_EQ(int(t.cell(t.a().tx() + 5, t.a().ty()).bg), int(Col::SEL.bg));
}

TEST(app_right_margin) {
    AppT t("short\n" + std::string(85, 'x'));
    int y0 = t.a().ty(), x0 = t.a().tx();
    int x = x0 + 80;
    CHECK(!S.showMargin);
    t.keys(alt('o') + alt('m'));  // Options > Right Margin
    CHECK(S.showMargin);
    CHECK_EQ(S.margin, 80);
    // the window is 78 columns wide: column 80 is visible only when the view scrolls
    CHECK_EQ(t.a().marginX(x0, 0, t.a().tw()), -1);
    t.keys(alt('o') + alt('c') + "\x7f\x7f" + "40\r");  // Options > Right Margin Column...
    CHECK_EQ(S.margin, 40);
    x = x0 + 40;
    CHECK_EQ(t.a().scr.buf[size_t(y0 * 80 + x)].ch, U'│');  // after the text: the line
    CHECK_EQ(int(t.cell(x, y0).fg), int(Col::MARGIN.fg));
    CHECK_EQ(t.a().scr.buf[size_t((y0 + 5) * 80 + x)].ch, U'│');  // below the end of the text
    CHECK_EQ(t.a().scr.buf[size_t((y0 + 1) * 80 + x)].ch, U'x');  // the text it crosses is not highlighted
    CHECK_EQ(int(t.cell(x, y0 + 1).bg), int(Col::TEXT.bg));
    CHECK_EQ(int(t.cell(x, y0 + 1).fg), int(t.cell(x - 1, y0 + 1).fg));
    // drag it with the mouse; the cursor stays where it is
    t.keys(click(x, y0 + 3) + click(x + 5, y0 + 3, 32) + click(x + 5, y0 + 3, 0, true));
    CHECK_EQ(S.margin, 45);
    CHECK(t.row(24).find("Right margin after column 45") != std::string::npos);
    CHECK_EQ(t.ed().cy, 0);
    CHECK_EQ(t.ed().cx, 0);
    CHECK_EQ(t.a().scr.buf[size_t(y0 * 80 + x0 + 45)].ch, U'│');
    // a click on the text at the margin's column places the cursor
    t.keys(click(x0 + 45, y0 + 1) + click(x0 + 45, y0 + 1, 0, true));
    CHECK_EQ(S.margin, 45);
    CHECK_EQ(t.ed().cy, 1);
    CHECK_EQ(t.ed().cx, 45);
    // invalid columns
    t.keys(alt('o') + alt('c') + "\x7f\x7f" + "0\r" + "\r");
    CHECK_EQ(S.margin, 45);
    // with soft wrap the margin is drawn on every row at the same column
    S.wrap = true;
    t.draw();
    CHECK_EQ(t.a().scr.buf[size_t((y0 + 1) * 80 + x0 + 45)].ch, U'x');
    CHECK_EQ(int(t.cell(x0 + 45, y0 + 1).bg), int(Col::TEXT.bg));
    CHECK_EQ(t.a().scr.buf[size_t((y0 + 2) * 80 + x0 + 45)].ch, U'│');  // the second row has 7 columns
    t.keys(alt('o') + alt('m'));
    CHECK(!S.showMargin);
    CHECK_EQ(t.a().scr.buf[size_t(y0 * 80 + x0 + 45)].ch, U' ');
}

TEST(app_right_margin_german) {
    g_uiLang = UiLang::German;
    buildMenus();
    AppT t("x");
    t.keys(alt('o') + alt('d'));  // Optionen > Rechter Ran&d
    CHECK(S.showMargin);
    t.keys(alt('o') + alt('u'));  // Optionen > Zeilen&umbruch
    CHECK(S.wrap);
    t.keys(alt('o') + alt('p') + "\x7f\x7f" + "30\r");  // Optionen > S&palte des rechten Rands...
    CHECK_EQ(S.margin, 30);
}

TEST(vim_set_wrap_and_colorcolumn) {
    VT t("‸abc");
    t.k(":set wrap<CR>");
    CHECK(S.wrap);
    t.k(":set wrap?<CR>");
    CHECK_EQ(t.msg(), "  wrap");
    t.k(":set nowrap<CR>");
    CHECK(!S.wrap);
    t.k(":set nolbr<CR>");
    CHECK(!S.linebreak);
    t.k(":set lbr&<CR>");  // Edith breaks at blanks by default
    CHECK(S.linebreak);
    t.k(":set cc=81<CR>");
    CHECK(S.showMargin);
    CHECK_EQ(S.margin, 80);
    t.k(":set cc?<CR>");
    CHECK_EQ(t.msg(), "  colorcolumn=81");
    t.k(":set colorcolumn=101,121<CR>");  // only the first column is shown
    CHECK_EQ(S.margin, 100);
    t.k(":set cc=<CR>");
    CHECK(!S.showMargin);
    CHECK_EQ(S.margin, 100);
    t.k(":set tw=72 cc=+1<CR>");  // relative to 'textwidth'
    CHECK(S.showMargin);
    CHECK_EQ(S.margin, 72);
    t.k(":set tw=0 cc=+1<CR>");  // no 'textwidth': no margin
    CHECK(!S.showMargin);
    t.k(":set cc=81<CR>");
    t.k(":set cc&<CR>");
    CHECK(!S.showMargin);
    t.k(":set cc=abc<CR>");
    CHECK_EQ(t.msg(), "E474: Invalid argument: cc=abc");
}

// Random lines and widths: the rows cover the line in order, every index maps back to its row
TEST(wrap_rows_random) {
    unsigned seed = 12345;
    auto rnd = [&](int n) { seed = seed * 1103515245u + 12345u; return int((seed >> 16) % unsigned(n)); };
    const char32_t chars[] = {'a', 'b', ' ', '\t', U'日', 0x200B, 'x', ' '};
    for (int run = 0; run < 2000; run++) {
        S.linebreak = rnd(2);
        S.tabw = 1 + rnd(8);
        Editor e;
        u32 l;
        for (int k = rnd(60); k > 0; k--) l += chars[rnd(8)];
        e.lines = {l, U"z"};
        int w = 1 + rnd(12);
        std::vector<int> r = e.wrapRows(l, w);
        CHECK_EQ(r[0], 0);
        for (size_t k = 1; k < r.size(); k++) CHECK(r[k] > r[k - 1] && r[k] <= int(l.size()));
        for (int x = 0; x <= int(l.size()); x++) {
            int row, col;
            e.rowCol(0, x, w, row, col);
            CHECK(row >= 0 && row < int(r.size()) && r[size_t(row)] <= x);
            CHECK(col >= 0);
            int idx = e.idxAtRow(0, row, col, w);
            CHECK(idx >= r[size_t(row)] && idx <= int(l.size()));
        }
        int y = 0, rr = 0;
        int all = int(r.size()) + e.rowCount(1, w);  // "z" has two rows when w is 1
        CHECK_EQ(e.stepRows(y, rr, 1000, w), all - 1);  // to the last row of "z"
        CHECK_EQ(y, 1);
        CHECK_EQ(e.stepRows(y, rr, -1000, w), -(all - 1));
        CHECK_EQ(y, 0);
        CHECK_EQ(rr, 0);
    }
}
