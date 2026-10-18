// The fallback lexer: line-by-line highlighting for languages without a tree-sitter grammar.

#include "lexer.h"

#include <unordered_set>
#include <vector>

#include "languages.h"
#include "utf8.h"

struct LangRT {
    const LangDef* d = nullptr;
    unsigned flags = 0;
    std::unordered_set<u32> kw, ty;
    std::vector<u32> lc, sc;
    u32 b1s, b1e, b2s, b2e, str;
    struct ML {
        u32 open, close;
        bool esc;
    };
    std::vector<ML> ml;
};

static const LangRT& langRT(int i) {
    static std::vector<LangRT> rt;
    if (rt.empty()) {
        rt.resize(NLANGS);
        for (int k = 0; k < NLANGS; k++) {
            const LangDef& d = LANGS[k];
            LangRT& r = rt[k];
            r.d = &d;
            r.flags = d.flags;
            bool nc = d.flags & F_NOCASE;
            for (auto& w : splitWords(d.kw)) r.kw.insert(nc ? lower(w) : w);
            for (auto& w : splitWords(d.ty)) r.ty.insert(nc ? lower(w) : w);
            r.lc = splitWords(d.lineCmt);
            r.sc = splitWords(d.startCmt);
            auto b1 = splitWords(d.blk1), b2 = splitWords(d.blk2);
            if (b1.size() == 2) { r.b1s = b1[0]; r.b1e = b1[1]; }
            if (b2.size() == 2) { r.b2s = b2[0]; r.b2e = b2[1]; }
            r.str = U(d.str);
            auto ml = splitWords(d.mlstr);
            for (size_t j = 0; j + 2 < ml.size(); j += 3) r.ml.push_back({ml[j], ml[j + 1], ml[j + 2] == U"1"});
        }
    }
    return rt[i];
}

static bool at(const u32& s, size_t i, const u32& t, bool nc = false) {
    if (t.empty() || i + t.size() > s.size()) return false;
    for (size_t k = 0; k < t.size(); k++)
        if (nc ? fold(s[i + k]) != fold(t[k]) : s[i + k] != t[k]) return false;
    return true;
}

static size_t findFrom(const u32& s, size_t from, const u32& t, bool esc) {
    for (size_t j = from; j < s.size(); j++) {
        if (esc && s[j] == '\\') { j++; continue; }
        if (at(s, j, t)) return j;
    }
    return u32::npos;
}

static bool isDigit(char32_t c) { return c >= '0' && c <= '9'; }
static bool isHex(char32_t c) { return isDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

// Length of a number starting at position i (0 = no number)
static size_t numberLen(const u32& s, size_t i) {
    size_t n = s.size(), j = i;
    if (j < n && (s[j] == '-' || s[j] == '+')) j++;
    if (!(j < n && (isDigit(s[j]) || (s[j] == '.' && j + 1 < n && isDigit(s[j + 1]))))) return 0;
    bool hex = j + 1 < n && s[j] == '0' && (s[j + 1] == 'x' || s[j + 1] == 'X');
    while (j < n) {
        char32_t c = s[j];
        if (isWord(c) || c == '.') j++;
        else if ((c == '+' || c == '-') && !hex && (s[j - 1] == 'e' || s[j - 1] == 'E')) j++;
        else break;
    }
    return j - i;
}

static int lexCode(const LangRT& R, const u32& s, int st, uint8_t* c) {
    const size_t n = s.size();
    const unsigned fl = R.flags;
    const bool nc = fl & F_NOCASE;
    auto mark = [&](size_t a, size_t b, int k) {
        for (size_t j = a; j < b && j < n; j++) c[j] = uint8_t(k);
    };
    auto identStart = [&](char32_t ch) {
        return (isWord(ch) && !isDigit(ch)) || ((fl & F_DOTID) && (ch == '.' || ch == '%'));
    };
    auto identChar = [&](char32_t ch) {
        if (isWord(ch)) return true;
        if ((fl & (F_DASHID | F_LISP)) && ch == '-') return true;
        if ((fl & F_LISP) && (ch == '?' || ch == '!' || ch == '*' || ch == '+' || ch == '<' || ch == '>' ||
                              ch == '=' || ch == '/'))
            return true;
        if ((fl & F_DOTID) && ch == '.') return true;
        return false;
    };
    mark(0, n, H_TEXT);
    size_t i = 0;

    // Continuation of multi-line constructs from the previous line
    if (st == 1 || st == 2) {
        const u32& e = st == 1 ? R.b1e : R.b2e;
        size_t p = findFrom(s, 0, e, false);
        if (p == u32::npos) { mark(0, n, H_COMMENT); return st; }
        mark(0, p + e.size(), H_COMMENT);
        i = p + e.size();
    } else if (st >= 3) {
        const auto& m = R.ml[size_t(st - 3)];
        size_t p = findFrom(s, 0, m.close, m.esc);
        if (p == u32::npos) { mark(0, n, H_STRING); return st; }
        mark(0, p + m.close.size(), H_STRING);
        i = p + m.close.size();
    }

    size_t fns = 0;
    while (fns < n && (s[fns] == ' ' || s[fns] == '\t')) fns++;
    bool firstWord = (fl & F_FIRSTWORD) != 0;
    if (i == 0 && fns < n) {
        size_t f2 = fns;  // skip a line number (BASIC)
        while (f2 < n && isDigit(s[f2])) f2++;
        if (f2 > fns && f2 < n && s[f2] == ' ') {
            mark(fns, f2, H_NUMBER);
            while (f2 < n && s[f2] == ' ') f2++;
        } else {
            f2 = fns;
        }
        for (auto& t : R.sc) {
            bool word = isWord(t[0]);
            if (at(s, f2, t, nc) && (!word || f2 + t.size() >= n || !isWord(s[f2 + t.size()]))) {
                mark(f2, n, H_COMMENT);
                return 0;
            }
        }
        if ((fl & F_BATLABEL) && s[fns] == ':' && fns + 1 < n && s[fns + 1] != ':') {
            mark(fns, n, H_TYPE);
            return 0;
        }
        if ((fl & F_LABELS) && identStart(s[fns])) {
            size_t j = fns;
            while (j < n && (identChar(s[j]) || s[j] == '-' || s[j] == '/' || s[j] == '%')) j++;
            if (j < n && s[j] == ':' && (j + 1 >= n || s[j + 1] != '=')) {
                mark(fns, j + 1, H_TYPE);
                i = j + 1;
            }
        }
        if ((fl & F_PREPROC) && s[fns] == '#') {
            size_t j = fns;
            while (j < n && !at(s, j, U"//") && !at(s, j, U"/*")) j++;
            mark(fns, j, H_PREPROC);
            i = j;
        }
    }

    while (i < n) {
        char32_t ch = s[i];
        // Block comments
        if (at(s, i, R.b1s)) {
            size_t p = findFrom(s, i + R.b1s.size(), R.b1e, false);
            if (p == u32::npos) { mark(i, n, H_COMMENT); return 1; }
            mark(i, p + R.b1e.size(), H_COMMENT);
            i = p + R.b1e.size();
            continue;
        }
        if (at(s, i, R.b2s)) {
            size_t p = findFrom(s, i + R.b2s.size(), R.b2e, false);
            if (p == u32::npos) { mark(i, n, H_COMMENT); return 2; }
            mark(i, p + R.b2e.size(), H_COMMENT);
            i = p + R.b2e.size();
            continue;
        }
        // Line comment
        bool lc = false;
        for (auto& t : R.lc) {
            if (!at(s, i, t, nc)) continue;
            if ((fl & F_HASHSPACE) && t == U"#" && i > 0 && s[i - 1] != ' ' && s[i - 1] != '\t') continue;
            if (isWord(t[0]) && ((i > 0 && isWord(s[i - 1])) || (i + t.size() < n && isWord(s[i + t.size()]))))
                continue;
            lc = true;
            break;
        }
        if (lc) { mark(i, n, H_COMMENT); return 0; }
        // Multi-line strings
        bool found = false;
        for (size_t k = 0; k < R.ml.size(); k++) {
            const auto& m = R.ml[k];
            if (!at(s, i, m.open)) continue;
            size_t p = findFrom(s, i + m.open.size(), m.close, m.esc);
            if (p == u32::npos) { mark(i, n, H_STRING); return 3 + int(k); }
            mark(i, p + m.close.size(), H_STRING);
            i = p + m.close.size();
            found = true;
            break;
        }
        if (found) continue;
        // Single-line strings
        if (R.str.find(ch) != u32::npos) {
            if (ch == '\'' && (fl & F_CHARSTRICT)) {
                bool lit = (i + 2 < n && s[i + 2] == '\'') || (i + 1 < n && s[i + 1] == '\\');
                if (!lit) { i++; continue; }
            }
            size_t j = i + 1;
            bool esc = !(fl & F_NOESC);
            while (j < n) {
                if (esc && s[j] == '\\') { j += 2; continue; }
                if (s[j] == ch) { j++; break; }
                j++;
            }
            j = std::min(j, n);
            int k = H_STRING;
            if (fl & F_KEYSTR) {
                size_t q = j;
                while (q < n && (s[q] == ' ' || s[q] == '\t')) q++;
                if (q < n && s[q] == ':') k = H_TYPE;
            }
            mark(i, j, k);
            i = j;
            continue;
        }
        // Variables
        if (ch == '$' && (fl & F_DOLLAR) && i + 1 < n) {
            size_t j = i + 1;
            if (s[j] == '{') {
                while (j < n && s[j] != '}') j++;
                j = std::min(n, j + 1);
            } else if (s[j] == '(' && (fl & F_MAKE)) {
                while (j < n && s[j] != ')') j++;
                j = std::min(n, j + 1);
            } else if (isWord(s[j])) {
                while (j < n && isWord(s[j])) j++;
            } else if (u32(U"@*#?$!-<^%").find(s[j]) != u32::npos) {
                j++;
            }
            if (j > i + 1) { mark(i, j, H_TYPE); i = j; continue; }
        }
        if (ch == '%' && (fl & F_PERCENT) && i + 1 < n) {
            size_t j = i + 1;
            if (s[j] == '~' || isDigit(s[j]) || s[j] == '*') {
                while (j < n && (isWord(s[j]) || s[j] == '~')) j++;
                if (j == i + 1) j++;
            } else {
                size_t q = s.find('%', j);
                if (q != u32::npos && q > j && q - j < 64) j = q + 1;
                else j = i;
            }
            if (j > i) { mark(i, j, H_TYPE); i = j; continue; }
        }
        if (ch == '@' && (fl & (F_DECOR | F_ATVAR)) && i + 1 < n && (isWord(s[i + 1]) || s[i + 1] == '@')) {
            size_t j = i + 1;
            while (j < n && (isWord(s[j]) || s[j] == '@' || ((fl & F_DECOR) && (s[j] == '.' || s[j] == '-')))) j++;
            mark(i, j, (fl & F_ATVAR) ? H_TYPE : H_PREPROC);
            i = j;
            continue;
        }
        if (ch == '#' && (fl & F_HEXCOLOR) && i + 1 < n && isHex(s[i + 1])) {
            size_t j = i + 1;
            while (j < n && isWord(s[j])) j++;
            mark(i, j, H_NUMBER);
            i = j;
            continue;
        }
        if (ch == '\\' && (fl & F_BSCMD) && i + 1 < n) {
            size_t j = i + 1;
            while (j < n && ((s[j] >= 'a' && s[j] <= 'z') || (s[j] >= 'A' && s[j] <= 'Z') || s[j] == '@')) j++;
            if (j == i + 1) j++;
            mark(i, j, H_KEYWORD);
            i = j;
            continue;
        }
        if (ch == ':' && (fl & F_SYMBOL) && i + 1 < n && identStart(s[i + 1]) &&
            (i == 0 || (!isWord(s[i - 1]) && s[i - 1] != ':'))) {
            size_t j = i + 1;
            while (j < n && (isWord(s[j]) || s[j] == '?' || s[j] == '!')) j++;
            mark(i, j, H_NUMBER);
            i = j;
            continue;
        }
        // Numbers
        if ((isDigit(ch) || (ch == '.' && i + 1 < n && isDigit(s[i + 1]))) && (i == 0 || !identChar(s[i - 1]))) {
            size_t len = numberLen(s, i);
            if (len) {
                mark(i, i + len, H_NUMBER);
                i += len;
                continue;
            }
        }
        // Identifiers and keywords
        if (identStart(ch)) {
            size_t j = i + 1;
            while (j < n && identChar(s[j])) j++;
            while (j > i + 1 && s[j - 1] == '-') j--;
            u32 w = s.substr(i, j - i);
            if (nc) w = lower(w);
            int k = H_TEXT;
            if (R.kw.count(w)) k = H_KEYWORD;
            else if (R.ty.count(w)) k = H_TYPE;
            else if (firstWord) k = H_KEYWORD;
            firstWord = false;
            mark(i, j, k);
            i = j;
            continue;
        }
        i++;
    }
    return 0;
}

// HTML/XML. States: 1 = inside a tag, 2 = inside a comment, 3/4 = attribute value "…" / '…' spanning lines
static int lexMarkup(const u32& s, int st, uint8_t* c) {
    const size_t n = s.size();
    auto mark = [&](size_t a, size_t b, int k) {
        for (size_t j = a; j < b && j < n; j++) c[j] = uint8_t(k);
    };
    mark(0, n, H_TEXT);
    size_t i = 0;
    int tagKind = H_KEYWORD;
    while (i < n) {
        if (st == 2) {
            size_t p = findFrom(s, i, U"-->", false);
            if (p == u32::npos) { mark(i, n, H_COMMENT); return 2; }
            mark(i, p + 3, H_COMMENT);
            i = p + 3;
            st = 0;
            continue;
        }
        if (st == 3 || st == 4) {
            size_t p = s.find(st == 3 ? U'"' : U'\'', i);
            if (p == u32::npos) { mark(i, n, H_STRING); return st; }
            mark(i, p + 1, H_STRING);
            i = p + 1;
            st = 1;
            continue;
        }
        if (st == 1) {
            char32_t ch = s[i];
            if (ch == '>' || (ch == '/' && i + 1 < n && s[i + 1] == '>') ||
                (ch == '?' && i + 1 < n && s[i + 1] == '>')) {
                size_t e = ch == '>' ? i + 1 : i + 2;
                mark(i, e, tagKind);
                i = e;
                st = 0;
                continue;
            }
            if (ch == '"' || ch == '\'') {
                size_t p = s.find(ch, i + 1);
                if (p == u32::npos) { mark(i, n, H_STRING); return ch == '"' ? 3 : 4; }
                mark(i, p + 1, H_STRING);
                i = p + 1;
                continue;
            }
            if (isWord(ch) || ch == ':' || ch == '@' || ch == '-') {
                size_t j = i;
                while (j < n && (isWord(s[j]) || s[j] == ':' || s[j] == '-' || s[j] == '.' || s[j] == '@')) j++;
                mark(i, j, H_TYPE);
                i = j;
                continue;
            }
            i++;
            continue;
        }
        // Text
        if (at(s, i, U"<!--")) { st = 2; continue; }
        if (s[i] == '<' && i + 1 < n && (isWord(s[i + 1]) || s[i + 1] == '/' || s[i + 1] == '!' || s[i + 1] == '?')) {
            tagKind = (s[i + 1] == '!' || s[i + 1] == '?') ? H_PREPROC : H_KEYWORD;
            size_t j = i + 1;
            if (s[j] == '/' || s[j] == '!' || s[j] == '?') j++;
            while (j < n && (isWord(s[j]) || s[j] == ':' || s[j] == '-' || s[j] == '.')) j++;
            mark(i, j, tagKind);
            i = j;
            st = 1;
            continue;
        }
        if (s[i] == '&') {
            size_t p = s.find(';', i);
            if (p != u32::npos && p - i <= 10) {
                mark(i, p + 1, H_NUMBER);
                i = p + 1;
                continue;
            }
        }
        i++;
    }
    return st;
}

// Inline elements in Markdown: `code`, **bold**, [link](url)
static void lexMdInline(const u32& s, size_t i, uint8_t* c) {
    const size_t n = s.size();
    auto mark = [&](size_t a, size_t b, int k) {
        for (size_t j = a; j < b && j < n; j++) c[j] = uint8_t(k);
    };
    while (i < n) {
        if (s[i] == '`') {
            size_t p = s.find('`', i + 1);
            if (p != u32::npos) { mark(i, p + 1, H_STRING); i = p + 1; continue; }
        }
        if ((at(s, i, U"**") || at(s, i, U"__"))) {
            size_t p = s.find(s.substr(i, 2), i + 2);
            if (p != u32::npos) { mark(i, p + 2, H_KEYWORD); i = p + 2; continue; }
        }
        if (s[i] == '[') {
            size_t p = s.find(']', i + 1);
            if (p != u32::npos && p + 1 < n && s[p + 1] == '(') {
                size_t q = s.find(')', p + 2);
                if (q != u32::npos) {
                    mark(i, p + 1, H_TYPE);
                    mark(p + 1, q + 1, H_NUMBER);
                    i = q + 1;
                    continue;
                }
            }
        }
        i++;
    }
}

// Markdown. State 1 = inside a ```code block```
static int lexMarkdown(const u32& s, int st, uint8_t* c) {
    const size_t n = s.size();
    auto mark = [&](size_t a, size_t b, int k) {
        for (size_t j = a; j < b && j < n; j++) c[j] = uint8_t(k);
    };
    mark(0, n, H_TEXT);
    size_t f = 0;
    while (f < n && s[f] == ' ') f++;
    if (at(s, f, U"```") || at(s, f, U"~~~")) {
        mark(0, n, H_PREPROC);
        return st == 1 ? 0 : 1;
    }
    if (st == 1) { mark(0, n, H_STRING); return 1; }
    if (f < n && s[f] == '#') { mark(0, n, H_KEYWORD); return 0; }
    if (f < n && s[f] == '>') { mark(0, n, H_COMMENT); return 0; }
    if (f >= 4 || (n > 0 && s[0] == '\t')) { mark(0, n, H_STRING); return 0; }
    if (f + 1 < n && (s[f] == '-' || s[f] == '*' || s[f] == '+') && s[f + 1] == ' ') {
        if (s.find_first_not_of(U" -*", f) == u32::npos && n - f >= 3) { mark(0, n, H_PREPROC); return 0; }
        mark(f, f + 1, H_NUMBER);
    } else if (f < n && isDigit(s[f])) {
        size_t j = f;
        while (j < n && isDigit(s[j])) j++;
        if (j + 1 < n && (s[j] == '.' || s[j] == ')') && s[j + 1] == ' ') mark(f, j + 1, H_NUMBER);
    } else if (f < n && (s[f] == '=' || s[f] == '-') && s.find_first_not_of(s[f], f) == u32::npos) {
        mark(0, n, H_KEYWORD);
        return 0;
    }
    lexMdInline(s, f, c);
    return 0;
}

// Values in INI/TOML/YAML: strings, numbers, true/false, trailing comment
static void lexValue(const u32& s, size_t i, uint8_t* c, bool semicolonCmt) {
    const size_t n = s.size();
    auto mark = [&](size_t a, size_t b, int k) {
        for (size_t j = a; j < b && j < n; j++) c[j] = uint8_t(k);
    };
    static const std::unordered_set<u32> consts = {U"true", U"false", U"yes", U"no", U"on", U"off", U"null",
                                                   U"none", U"nil", U"~"};
    while (i < n) {
        char32_t ch = s[i];
        if ((ch == '#' || (semicolonCmt && ch == ';')) && (i == 0 || s[i - 1] == ' ' || s[i - 1] == '\t')) {
            mark(i, n, H_COMMENT);
            return;
        }
        if (ch == '"' || ch == '\'') {
            size_t j = i + 1;
            while (j < n && s[j] != ch) {
                if (ch == '"' && s[j] == '\\') j++;
                j++;
            }
            mark(i, std::min(n, j + 1), H_STRING);
            i = j + 1;
            continue;
        }
        if ((isDigit(ch) || ch == '-' || ch == '+' || ch == '.') && (i == 0 || !isWord(s[i - 1]))) {
            size_t len = numberLen(s, i);
            if (len && (i + len >= n || !isWord(s[i + len]))) {
                mark(i, i + len, H_NUMBER);
                i += len;
                continue;
            }
        }
        if (isWord(ch) || ch == '~') {
            size_t j = i;
            while (j < n && (isWord(s[j]) || s[j] == '~')) j++;
            if (consts.count(lower(s.substr(i, j - i)))) mark(i, j, H_KEYWORD);
            i = j;
            continue;
        }
        i++;
    }
}

static int lexIni(const u32& s, int, uint8_t* c) {
    const size_t n = s.size();
    auto mark = [&](size_t a, size_t b, int k) {
        for (size_t j = a; j < b && j < n; j++) c[j] = uint8_t(k);
    };
    mark(0, n, H_TEXT);
    size_t f = 0;
    while (f < n && (s[f] == ' ' || s[f] == '\t')) f++;
    if (f >= n) return 0;
    if (s[f] == ';' || s[f] == '#') { mark(f, n, H_COMMENT); return 0; }
    if (s[f] == '[') {
        size_t p = s.rfind(']');
        size_t e = p == u32::npos ? n : p + 1;
        mark(f, e, H_KEYWORD);
        lexValue(s, e, c, true);
        return 0;
    }
    size_t eq = s.find_first_of(U"=:", f);
    if (eq == u32::npos) { lexValue(s, f, c, true); return 0; }
    mark(f, eq, H_TYPE);
    lexValue(s, eq + 1, c, true);
    return 0;
}

static int lexYaml(const u32& s, int, uint8_t* c) {
    const size_t n = s.size();
    auto mark = [&](size_t a, size_t b, int k) {
        for (size_t j = a; j < b && j < n; j++) c[j] = uint8_t(k);
    };
    mark(0, n, H_TEXT);
    size_t f = 0;
    while (f < n && s[f] == ' ') f++;
    if (f >= n) return 0;
    if (s[f] == '#') { mark(f, n, H_COMMENT); return 0; }
    if (f == 0 && (at(s, 0, U"---") || at(s, 0, U"..."))) { mark(0, 3, H_PREPROC); lexValue(s, 3, c, false); return 0; }
    while (f + 1 < n && s[f] == '-' && s[f + 1] == ' ') {  // list items "- "
        mark(f, f + 1, H_NUMBER);
        f += 2;
        while (f < n && s[f] == ' ') f++;
    }
    // Key: up to ":" followed by a space or the end of the line
    size_t j = f;
    if (j < n && (s[j] == '"' || s[j] == '\'')) {
        size_t q = s.find(s[j], j + 1);
        j = q == u32::npos ? n : q + 1;
    } else {
        while (j < n && s[j] != ':' && s[j] != '#' && s[j] != '"' && s[j] != '\'') j++;
    }
    if (j < n && s[j] == ':' && (j + 1 >= n || s[j + 1] == ' ') && j > f) {
        mark(f, j + 1, H_TYPE);
        f = j + 1;
    }
    for (size_t k = f; k < n; k++) {  // &anchor and *alias
        if ((s[k] == '&' || s[k] == '*') && (k == 0 || s[k - 1] == ' ') && k + 1 < n && isWord(s[k + 1])) {
            size_t e = k + 1;
            while (e < n && (isWord(s[e]) || s[e] == '-')) e++;
            mark(k, e, H_PREPROC);
        }
    }
    lexValue(s, f, c, false);
    return 0;
}

static int lexDiff(const u32& s, int, uint8_t* c) {
    int k = H_TEXT;
    if (at(s, 0, U"+++") || at(s, 0, U"---") || at(s, 0, U"diff ") || at(s, 0, U"index ")) k = H_KEYWORD;
    else if (at(s, 0, U"@@")) k = H_TYPE;
    else if (!s.empty() && s[0] == '+') k = H_STRING;
    else if (!s.empty() && s[0] == '-') k = H_PREPROC;
    else if (!s.empty() && s[0] == '\\') k = H_COMMENT;
    for (size_t j = 0; j < s.size(); j++) c[j] = uint8_t(k);
    return 0;
}

int highlightLine(int lang, const u32& s, int st, uint8_t* c) {
    const LangRT& R = langRT(lang);
    switch (R.d->kind) {
        case LK_MARKUP: return lexMarkup(s, st, c);
        case LK_MARKDOWN: return lexMarkdown(s, st, c);
        case LK_INI: return lexIni(s, st, c);
        case LK_YAML: return lexYaml(s, st, c);
        case LK_DIFF: return lexDiff(s, st, c);
    }
    return lexCode(R, s, st, c);
}
