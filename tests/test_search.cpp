// Regular expressions in the Find and Change dialogs (src/search.*).

#include "search.h"
#include "test.h"

static Editor ed(const std::vector<const char32_t*>& ls) {
    Editor e;
    e.lines.clear();
    for (auto l : ls) e.lines.push_back(l);
    return e;
}

static bool rx(VimRegex& r, const char32_t* p, bool cs = true, bool whole = false) {
    u32 err;
    return compileSearch(r, p, cs, whole, err);
}

TEST(search_regex_syntax) {
    Editor e = ed({U"color colour Colr", U"a1 b22 c333"});
    VimRegex r;
    RxMatch m;
    CHECK(rx(r, U"colou?r"));  // ? + ( ) | without backslashes
    CHECK(findRx(e, r, {0, 0}, m));
    CHECK_EQ(m.s, Pos({0, 0}));
    CHECK(findRx(e, r, m.e, m));
    CHECK_EQ(m.s, Pos({0, 6}));
    CHECK_EQ(m.e, Pos({0, 12}));
    CHECK(findRx(e, r, m.e, m));  // wraps around
    CHECK_EQ(m.s, Pos({0, 0}));
    CHECK(rx(r, U"colr", false));  // ignoring case
    CHECK(findRx(e, r, {0, 0}, m));
    CHECK_EQ(m.s, Pos({0, 13}));
    CHECK(rx(r, U"[a-c](\\d{2,})"));
    CHECK(findRx(e, r, {0, 0}, m));
    CHECK_EQ(m.s, Pos({1, 3}));
    CHECK_EQ(e.getText(m.gs[1], m.ge[1]), u32(U"22"));
    CHECK(rx(r, U"b|c", true, true) == true);  // whole words: neither "b" nor "c" alone
    CHECK(!findRx(e, r, {0, 0}, m));
    CHECK(rx(r, U"a1|c333", true, true));
    CHECK(findRx(e, r, {0, 0}, m));
    CHECK_EQ(m.s, Pos({1, 0}));
    u32 err;
    CHECK(!compileSearch(r, U"(a", true, false, err));
    CHECK(err.find(U"E54") == 0);
}

TEST(search_regex_empty_and_multiline) {
    Editor e = ed({U"ab", U"cd"});
    VimRegex r;
    RxMatch m;
    CHECK(rx(r, U"^"));  // an empty match at the cursor is skipped
    CHECK(findRx(e, r, {0, 0}, m));
    CHECK_EQ(m.s, Pos({1, 0}));
    CHECK(rx(r, U"b\\nc"));  // over a line break
    CHECK(findRx(e, r, {0, 0}, m));
    CHECK_EQ(m.s, Pos({0, 1}));
    CHECK_EQ(m.e, Pos({1, 1}));
}

TEST(search_replace_all_regex) {
    Editor e = ed({U"x=1, y=22", U"z=333"});
    VimRegex r;
    CHECK(rx(r, U"(\\w)=(\\d+)"));
    CHECK_EQ(replaceAllRx(e, r, U"\\2:\\1 \\\\ \\t"), 3);
    CHECK_EQ(e.lines[0], u32(U"1:x \\ \t, 22:y \\ \t"));
    CHECK(e.modified);
    CHECK(e.doUndo());
    CHECK_EQ(e.lines[0], u32(U"x=1, y=22"));
    CHECK(rx(r, U"^"));  // empty matches: one per line
    CHECK_EQ(replaceAllRx(e, r, U"> "), 2);
    CHECK_EQ(e.lines[1], u32(U"> z=333"));
    CHECK(rx(r, U"1, |\\n"));  // the line break goes, a new one comes from \n
    CHECK_EQ(replaceAllRx(e, r, U"\\n"), 2);
    CHECK_EQ(int(e.lines.size()), 3);
    CHECK_EQ(e.lines[0], u32(U"> x="));
    CHECK_EQ(e.lines[1], u32(U"y=22"));
    CHECK(rx(r, U"nothing"));
    CHECK_EQ(replaceAllRx(e, r, U""), 0);
    RxMatch m;
    m.s = {1, 0};
    m.e = {1, 1};
    CHECK_EQ(expandReplacement(e, m, U"[\\0\\5\\q]"), u32(U"[y\\q]"));
}

TEST(search_regex_email) {
    Editor e = ed({U"Mail: anna.berg@example.org, tom_k+x@mail.co.uk.", U"no@ mail"});
    VimRegex r;
    RxMatch m;
    CHECK(rx(r, U"[A-Za-z0-9._%+-]+\\@[A-Za-z0-9.-]+\\.[A-Za-z]{2,}"));
    CHECK(findRx(e, r, {0, 0}, m));
    CHECK_EQ(e.getText(m.s, m.e), u32(U"anna.berg@example.org"));
    CHECK(findRx(e, r, m.e, m));
    CHECK_EQ(e.getText(m.s, m.e), u32(U"tom_k+x@mail.co.uk"));
    CHECK(findRx(e, r, m.e, m));
    CHECK_EQ(m.s, Pos({0, 6}));  // only these two
    CHECK(rx(r, U"[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\\.[A-Za-z]{2,}"));  // @ without a backslash
    CHECK(findRx(e, r, {0, 0}, m));
    CHECK_EQ(e.getText(m.s, m.e), u32(U"anna.berg@example.org"));
    Editor c = ed({U"if (a==b && x~y) p = 100%;"});  // = & ~ % are plain characters as well
    CHECK(rx(r, U"a==b && x~y"));
    CHECK(findRx(c, r, {0, 0}, m));
    CHECK_EQ(m.s, Pos({0, 4}));
    CHECK(rx(r, U"\\d+%"));
    CHECK(findRx(c, r, {0, 0}, m));
    CHECK_EQ(c.getText(m.s, m.e), u32(U"100%"));
    CHECK(rx(r, U"[]=]+"));  // in a collection everything stays as it is
    CHECK(findRx(c, r, {0, 0}, m));
    CHECK_EQ(c.getText(m.s, m.e), u32(U"=="));
}
