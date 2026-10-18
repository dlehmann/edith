// The Vim pattern engine (":help pattern").

#include "test.h"
#include "vimregex.h"

// first match in a single line: "start-end" or "none"
static std::string m1(const char* pat, const char* text, bool ic = false, bool scs = false) {
    VimRegex rx;
    if (!rx.compile(U(pat), ic, scs)) return "error: " + toUtf8(rx.err);
    std::vector<u32> L;
    u32 t = U(text);
    size_t s = 0;
    for (;;) {
        size_t q = t.find('\n', s);
        L.push_back(t.substr(s, q == u32::npos ? u32::npos : q - s));
        if (q == u32::npos) break;
        s = q + 1;
    }
    for (int y = 0; y < int(L.size()); y++) {
        RxMatch m;
        if (rx.findInLine(L, y, 0, m)) {
            if (m.s.y == m.e.y && m.s.y == 0) return std::to_string(m.s.x) + "-" + std::to_string(m.e.x);
            return show(m.s) + "-" + show(m.e);
        }
    }
    return "none";
}

static std::string grp(const char* pat, const char* text, int g) {
    VimRegex rx;
    if (!rx.compile(U(pat))) return "error";
    std::vector<u32> L{U(text)};
    RxMatch m;
    if (!rx.findInLine(L, 0, 0, m) || !m.g[g]) return "none";
    return toUtf8(L[0].substr(size_t(m.gs[g].x), size_t(m.ge[g].x - m.gs[g].x)));
}

TEST(regex_literals_and_dot) {
    CHECK_EQ(m1("abc", "xxabcx"), "2-5");
    CHECK_EQ(m1("a.c", "abc"), "0-3");
    CHECK_EQ(m1("a\\.c", "abc a.c"), "4-7");
    CHECK_EQ(m1("x", "abc"), "none");
    CHECK_EQ(m1("", "abc"), "0-0");
}

TEST(regex_multis) {
    CHECK_EQ(m1("ab*", "abbbc"), "0-4");
    CHECK_EQ(m1("ab\\+", "ac abb"), "3-6");
    CHECK_EQ(m1("ab\\=c", "ac"), "0-2");
    CHECK_EQ(m1("ab\\?c", "abc"), "0-3");
    CHECK_EQ(m1("a\\{2,3}", "aaaa"), "0-3");
    CHECK_EQ(m1("a\\{2}", "a aa"), "2-4");
    CHECK_EQ(m1("a\\{-1,}", "aaa"), "0-1");
    CHECK_EQ(m1("a.\\{-}b", "axxbxb"), "0-4");
    CHECK_EQ(m1("a\\{,2}", "aaa"), "0-2");
    CHECK_EQ(m1("*a", "x*a"), "1-3");  // "*" at the start is literal
    CHECK_EQ(m1("^*", "*x"), "0-1");
}

TEST(regex_anchors_and_word_boundaries) {
    CHECK_EQ(m1("^a", "ba\nab"), "(1,0)-(1,1)");
    CHECK_EQ(m1("a$", "ab\nba"), "(1,1)-(1,2)");
    CHECK_EQ(m1("a^", "a^"), "0-2");  // literal when not at the start
    CHECK_EQ(m1("$a", "x$a"), "1-3");
    CHECK_EQ(m1("\\<is\\>", "this is"), "5-7");
    CHECK_EQ(m1("\\<ä", "xä ä"), "3-4");
    CHECK_EQ(m1("\\%^a", "a"), "0-1");
}

TEST(regex_groups_alternation_backrefs) {
    CHECK_EQ(m1("foo\\|bar", "xbar"), "1-4");
    CHECK_EQ(m1("\\(ab\\)\\+", "ababx"), "0-4");
    CHECK_EQ(grp("\\(a\\)\\(b\\)", "ab", 2), "b");
    CHECK_EQ(m1("\\(a\\)\\1", "a aa"), "2-4");
    CHECK_EQ(m1("\\%(ab\\)\\{2}", "abab"), "0-4");
    CHECK_EQ(grp("\\%(x\\)\\(y\\)", "xy", 1), "y");
    CHECK_EQ(m1("\\(\\w\\+\\) \\1", "the the"), "0-7");
    CHECK_EQ(m1(".*bar\\&foo.*", "foobar"), "0-6");
}

TEST(regex_very_magic_and_nomagic) {
    CHECK_EQ(m1("\\v(a|b)+", "xabba"), "1-5");
    CHECK_EQ(m1("\\v<is>", "this is"), "5-7");
    CHECK_EQ(m1("\\va{2}", "aa"), "0-2");
    CHECK_EQ(m1("\\M.", "a.b"), "1-2");
    CHECK_EQ(m1("\\Ma*", "a*"), "0-2");
    CHECK_EQ(m1("\\V.*", "ab.*"), "2-4");
    CHECK_EQ(m1("\\Va\\.b", "axb"), "0-3");
}

TEST(regex_classes) {
    CHECK_EQ(m1("\\d\\+", "ab123c"), "2-5");
    CHECK_EQ(m1("\\D", "12a"), "2-3");
    CHECK_EQ(m1("\\s", "ab c"), "2-3");
    CHECK_EQ(m1("\\S\\+", "  ab "), "2-4");
    CHECK_EQ(m1("\\w\\+", "--a_1--"), "2-5");
    CHECK_EQ(m1("\\a\\+", "12ab3"), "2-4");
    CHECK_EQ(m1("\\u", "abC"), "2-3");
    CHECK_EQ(m1("\\l", "ABc"), "2-3");
    CHECK_EQ(m1("\\x\\+", "zz0aFg"), "2-5");
    CHECK_EQ(m1("\\k\\+", "!ünï!"), "1-4");
    CHECK_EQ(m1("\\h\\w*", "1 _x1"), "2-5");
}

TEST(regex_collections) {
    CHECK_EQ(m1("[abc]\\+", "xxbcay"), "2-5");
    CHECK_EQ(m1("[^a-c]", "abcd"), "3-4");
    CHECK_EQ(m1("[]x]", "a]"), "1-2");
    CHECK_EQ(m1("[a-]", "x-"), "1-2");
    CHECK_EQ(m1("[[:digit:]]\\+", "ab42"), "2-4");
    CHECK_EQ(m1("[[:upper:][:space:]]", "ab C"), "2-3");
    CHECK_EQ(m1("[\\t]", "a\tb"), "1-2");
    CHECK_EQ(m1("[äöü]", "xö"), "1-2");
    CHECK_EQ(m1("[a", "x[a"), "1-3");  // no closing bracket: literal
    CHECK_EQ(m1("[\\d65]", "zA"), "1-2");
}

TEST(regex_case) {
    CHECK_EQ(m1("abc", "ABC"), "none");
    CHECK_EQ(m1("abc", "ABC", true), "0-3");
    CHECK_EQ(m1("Abc", "abc", true, true), "none");  // smartcase
    CHECK_EQ(m1("abc", "ABC", true, true), "0-3");
    CHECK_EQ(m1("abc\\c", "ABC"), "0-3");
    CHECK_EQ(m1("\\Cabc", "ABC", true), "none");
    CHECK_EQ(m1("ä", "Ä", true), "0-1");
    CHECK_EQ(m1("[a-c]", "B", true), "0-1");
}

TEST(regex_zs_ze_lookaround) {
    CHECK_EQ(m1("foo\\zsbar", "foobar"), "3-6");
    CHECK_EQ(m1("foo\\zebar", "foo foobar"), "4-7");
    CHECK_EQ(m1("foo\\(bar\\)\\@=", "foo foobar"), "4-7");
    CHECK_EQ(m1("foo\\(bar\\)\\@!", "foobar foox"), "7-10");
    CHECK_EQ(m1("\\(foo\\)\\@<=bar", "xbar foobar"), "8-11");
    CHECK_EQ(m1("\\(foo\\)\\@<!bar", "foobar xbar"), "8-11");
    CHECK_EQ(m1("\\(a*\\)\\@>a", "aaa"), "none");
}

TEST(regex_multiline) {
    CHECK_EQ(m1("a\\nb", "xa\nbx"), "(0,1)-(1,1)");
    CHECK_EQ(m1("a\\_s*b", "a\n  b"), "(0,0)-(1,3)");
    CHECK_EQ(m1("x\\_.*y", "x\nq\ny"), "(0,0)-(2,1)");
    CHECK_EQ(m1("a.*b", "a\nb"), "none");  // "." does not match a line break
    CHECK_EQ(m1("a\\n", "a"), "none");     // no line break after the last line
    CHECK_EQ(m1("a\\_[x\\n]*b", "ax\nxb"), "(0,0)-(1,2)");
}

TEST(regex_special_atoms) {
    CHECK_EQ(m1("\\%d97", "xa"), "1-2");
    CHECK_EQ(m1("\\%x41", "xA"), "1-2");
    CHECK_EQ(m1("\\%u20ac", "x€"), "1-2");
    CHECK_EQ(m1("\\%2l.", "a\nb"), "(1,0)-(1,1)");
    CHECK_EQ(m1("\\%3c.", "abcd"), "2-3");
    CHECK_EQ(m1("a\\tb", "a\tb"), "0-3");
    CHECK_EQ(m1("\\e", "a\x1b"), "1-2");
}

TEST(regex_tilde_last_substitute) {
    g_rxLastSub = U"xy";
    CHECK_EQ(m1("a~", "axy"), "0-3");
    CHECK_EQ(m1("a\\~", "a~"), "0-2");
    g_rxLastSub.clear();
}

TEST(regex_errors) {
    CHECK(m1("\\(a", "a").find("E54") != std::string::npos);
    CHECK(m1("a\\)", "a").find("E55") != std::string::npos);
    CHECK(m1("\\%(a", "a").find("E53") != std::string::npos);
    CHECK(m1("a\\{1", "a").find("E554") != std::string::npos);
    CHECK(m1("[b-a]", "a").find("E944") != std::string::npos);
    CHECK(m1("\\zq", "a").find("E68") != std::string::npos);
    CHECK(m1("a\\@x", "a").find("E64") != std::string::npos);
}

TEST(regex_backtracking_limit) {
    // a catastrophic pattern returns instead of hanging, also in a long line (the step budget is
    // shared by all start positions, not spent again at every index)
    std::string s(5000, 'a');
    VimRegex rx;
    CHECK(rx.compile(U"\\(a*\\)*b"));
    std::vector<u32> L{U(s)};
    RxMatch m;
    CHECK(!rx.findInLine(L, 0, 0, m));
}

TEST(regex_find_last_and_escape) {
    VimRegex rx;
    CHECK(rx.compile(U"a"));
    std::vector<u32> L{U"a.a.a"};
    RxMatch m;
    CHECK(rx.findLastInLine(L, 0, 4, m));
    CHECK_EQ(m.s.x, 2);
    CHECK(rx.findLastInLine(L, 0, INT_MAX, m));
    CHECK_EQ(m.s.x, 4);
    CHECK(!rx.findLastInLine(L, 0, 0, m));
    CHECK_EQ(toUtf8(rxEscape(U"a.b*c/[d]~$^")), "a\\.b\\*c\\/\\[d]\\~\\$\\^");
}
