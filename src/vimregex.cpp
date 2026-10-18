// Regular expressions with Vim pattern syntax: parser and backtracking matcher (see vimregex.h).

#include "vimregex.h"

#include <cstring>

#include "i18n.h"
#include "utf8.h"

namespace {

enum {
    N_CHAR, N_ANY, N_SET, N_CLASS, N_BOL, N_EOL, N_BOW, N_EOW, N_BOF, N_EOF, N_NL, N_SEQ, N_ALT, N_AND,
    N_GROUP, N_REP, N_ZS, N_ZE, N_BREF, N_LOOK, N_LINE, N_COL, N_EMPTY
};

enum {
    C_ALNUM = 1, C_ALPHA = 2, C_BLANK = 4, C_CNTRL = 8, C_DIGIT = 16, C_GRAPH = 32, C_LOWER = 64,
    C_PRINT = 128, C_PUNCT = 256, C_SPACE = 512, C_UPPER = 1024, C_XDIGIT = 2048, C_RETURN = 4096,
    C_TAB = 8192, C_ESCAPE = 16384, C_BACKSPACE = 32768, C_IDENT = 65536, C_KEYWORD = 131072,
    C_FNAME = 262144
};

bool isKw(char32_t c) { return isWord(c); }

char32_t unfold(char32_t c) {
    if (c >= 'a' && c <= 'z') return c - 32;
    if (c >= 0xE0 && c <= 0xFE && c != 0xF7) return c - 32;
    if (c >= 0x3B1 && c <= 0x3C9 && c != 0x3C2) return c - 32;
    if (c >= 0x430 && c <= 0x44F) return c - 32;
    if (c >= 0x450 && c <= 0x45F) return c - 80;
    return c;
}

bool isUpperCh(char32_t c) { return fold(c) != c; }
bool isLowerCh(char32_t c) { return unfold(c) != c || (c >= 'a' && c <= 'z'); }

bool testNamed(int classes, char32_t c) {
    bool ascii = c < 0x80;
    auto any = [&](int f) { return (classes & f) != 0; };
    if (any(C_ALNUM) && ((c >= '0' && c <= '9') || isUpperCh(c) || isLowerCh(c) || (c >= 0x80 && isWord(c))))
        return true;
    if (any(C_ALPHA) && (isUpperCh(c) || isLowerCh(c) || (c >= 0x80 && isWord(c)))) return true;
    if (any(C_BLANK) && (c == ' ' || c == '\t')) return true;
    if (any(C_CNTRL) && (c < 32 || c == 127)) return true;
    if (any(C_DIGIT) && c >= '0' && c <= '9') return true;
    if (any(C_GRAPH) && c > 32 && c != 127) return true;
    if (any(C_LOWER) && isLowerCh(c)) return true;
    if (any(C_PRINT) && c >= 32 && c != 127) return true;
    if (any(C_PUNCT) && ascii && c > 32 && c < 127 && !isWord(c)) return true;
    if (any(C_SPACE) && (c == ' ' || (c >= 9 && c <= 13))) return true;
    if (any(C_UPPER) && isUpperCh(c)) return true;
    if (any(C_XDIGIT) && ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return true;
    if (any(C_RETURN) && c == '\r') return true;
    if (any(C_TAB) && c == '\t') return true;
    if (any(C_ESCAPE) && c == 27) return true;
    if (any(C_BACKSPACE) && c == 8) return true;
    if (any(C_IDENT) && (isWord(c))) return true;
    if (any(C_KEYWORD) && isKw(c)) return true;
    if (any(C_FNAME) && c > 32 && !strchr("\"'<>|*?;&`", int(c < 128 ? c : 'a'))) return true;
    return false;
}

// Character class escapes such as \s, \d, \w (Vim's definitions, mostly ASCII)
bool testClass(char32_t k, char32_t c) {
    bool r;
    switch (k | 0x20) {
        case 's': r = c == ' ' || c == '\t'; break;
        case 'd': r = c >= '0' && c <= '9'; break;
        case 'w': r = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; break;
        case 'a': r = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); break;
        case 'l': r = c >= 'a' && c <= 'z'; break;
        case 'u': r = c >= 'A' && c <= 'Z'; break;
        case 'x': r = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); break;
        case 'o': r = c >= '0' && c <= '7'; break;
        case 'h': r = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; break;
        case 'i': case 'k':
            r = isKw(c);
            if (k < 'a' && c >= '0' && c <= '9') r = false;  // \I, \K: without digits
            return r;
        case 'f':
            r = testNamed(C_FNAME, c);
            if (k < 'a' && c >= '0' && c <= '9') r = false;
            return r;
        case 'p':
            r = c >= 32 && c != 127;
            if (k < 'a' && c >= '0' && c <= '9') r = false;
            return r;
        default: r = false;
    }
    // Upper case letters are the negation (\S, \D, \W, ...)
    return k < 'a' ? !r : r;
}

struct Tok {
    char32_t c = 0;
    bool magic = false;
    bool end = true;
};

struct Parser {
    const u32& p;
    std::vector<VimRegex::Node>& nodes;
    int& ngroups;
    size_t i = 0;
    int mode = 2;  // 0 = \V, 1 = \M, 2 = \m (default), 3 = \v
    u32 err;

    Parser(const u32& pat, std::vector<VimRegex::Node>& n, int& g) : p(pat), nodes(n), ngroups(g) {}

    int add(VimRegex::Node n) {
        nodes.push_back(std::move(n));
        return int(nodes.size()) - 1;
    }
    int mk(int t, char32_t c = 0) {
        VimRegex::Node n;
        n.t = t;
        n.c = c;
        return add(n);
    }

    void skipModes() {
        while (i + 1 < p.size() && p[i] == '\\') {
            char32_t c = p[i + 1];
            if (c == 'v') mode = 3;
            else if (c == 'm') mode = 2;
            else if (c == 'M') mode = 1;
            else if (c == 'V') mode = 0;
            else if (c == 'c' || c == 'C') {}
            else break;
            i += 2;
        }
    }

    bool plainMagic(char32_t c) const {
        if (mode == 3) return c < 128 && strchr("()|&+=?{@%<>*.[~^$", int(c)) && c;
        if (mode == 2) return c < 128 && strchr("*.[~^$", int(c)) && c;
        return c == '^' || c == '$';
    }

    Tok read() {
        skipModes();
        Tok t;
        if (i >= p.size()) return t;
        t.end = false;
        char32_t c = p[i];
        if (c == '\\') {
            if (i + 1 >= p.size()) { i++; t.c = '\\'; return t; }
            char32_t x = p[i + 1];
            i += 2;
            t.c = x;
            if ((x >= 'a' && x <= 'z') || (x >= 'A' && x <= 'Z') || (x >= '0' && x <= '9') || x == '_') {
                t.magic = true;
                return t;
            }
            if (plainMagic(x)) return t;  // the backslash makes a magic character literal
            if (x < 128 && strchr("()|&+=?{@%<>", int(x)) && x) t.magic = true;
            else if (mode <= 1 && x < 128 && strchr("*.[~", int(x)) && x) t.magic = true;
            return t;
        }
        i++;
        t.c = c;
        t.magic = plainMagic(c);
        return t;
    }

    Tok peek() {
        size_t si = i;
        int sm = mode;
        Tok t = read();
        i = si;
        mode = sm;
        return t;
    }

    bool isEndTok(const Tok& t) {
        return t.end || (t.magic && (t.c == '|' || t.c == '&' || t.c == ')'));
    }

    int parseAlt() {
        std::vector<int> br{parseAnd()};
        for (;;) {
            Tok t = peek();
            if (t.end || !t.magic || t.c != '|') break;
            read();
            br.push_back(parseAnd());
        }
        if (br.size() == 1) return br[0];
        int n = mk(N_ALT);
        nodes[n].kids = br;
        return n;
    }

    int parseAnd() {
        std::vector<int> parts{parseConcat()};
        for (;;) {
            Tok t = peek();
            if (t.end || !t.magic || t.c != '&') break;
            read();
            parts.push_back(parseConcat());
        }
        if (parts.size() == 1) return parts[0];
        int n = mk(N_AND);
        nodes[n].kids = parts;
        return n;
    }

    int parseConcat() {
        std::vector<int> seq;
        bool atStart = true;
        for (;;) {
            Tok t = peek();
            if (isEndTok(t)) break;
            int piece = parsePiece(atStart);
            if (piece < 0) return -1;
            seq.push_back(piece);
            const VimRegex::Node& pn = nodes[piece];
            atStart = pn.t == N_BOL && atStart;  // "^*" matches a literal star
        }
        if (seq.size() == 1) return seq[0];
        int n = mk(N_SEQ);
        nodes[n].kids = seq;
        return n;
    }

    int readNum(int& v) {
        int d = 0;
        v = 0;
        while (i < p.size() && p[i] >= '0' && p[i] <= '9') {
            v = std::min(v * 10 + int(p[i] - '0'), 1000000);
            i++;
            d++;
        }
        return d;
    }

    int parsePiece(bool atStart) {
        int a = parseAtom(atStart);
        if (a < 0) return -1;
        if (nodes[a].t == N_BOL) return a;  // "^*": the star is a literal character
        for (;;) {
            Tok t = peek();
            if (t.end || !t.magic) break;
            int mn, mx;
            bool greedy = true;
            if (t.c == '*') { read(); mn = 0; mx = -1; }
            else if (t.c == '+') { read(); mn = 1; mx = -1; }
            else if (t.c == '=' || t.c == '?') { read(); mn = 0; mx = 1; }
            else if (t.c == '{') {
                read();
                if (i < p.size() && p[i] == '-') { greedy = false; i++; }
                int lo = 0, hi = 0;
                int dl = readNum(lo);
                bool comma = false;
                if (i < p.size() && p[i] == ',') { comma = true; i++; }
                int dh = readNum(hi);
                if (i < p.size() && p[i] == '\\') i++;
                if (i >= p.size() || p[i] != '}') { err = tr(U"E554: Syntax error in \\{...}"); return -1; }
                i++;
                if (!comma) { mn = dl ? lo : 0; mx = dl ? lo : -1; }
                else { mn = dl ? lo : 0; mx = dh ? hi : -1; }
                if (mx >= 0 && mn > mx) std::swap(mn, mx);
            } else if (t.c == '@') {
                read();
                int dummy;
                readNum(dummy);
                int kind = -1;
                if (i < p.size()) {
                    char32_t c = p[i++];
                    if (c == '=') kind = 0;
                    else if (c == '!') kind = 1;
                    else if (c == '>') kind = 4;
                    else if (c == '<' && i < p.size()) {
                        char32_t c2 = p[i++];
                        kind = c2 == '=' ? 2 : c2 == '!' ? 3 : -1;
                    }
                }
                if (kind < 0) { err = tr(U"E64: @ follows nothing"); return -1; }
                int n = mk(N_LOOK);
                nodes[n].a = kind;
                nodes[n].kids = {a};
                a = n;
                continue;
            } else {
                break;
            }
            int n = mk(N_REP);
            nodes[n].a = mn;
            nodes[n].b = mx;
            nodes[n].greedy = greedy;
            nodes[n].kids = {a};
            a = n;
        }
        return a;
    }

    char32_t readCode(int base, int maxd) {
        char32_t v = 0;
        int d = 0;
        while (i < p.size() && d < maxd) {
            char32_t c = p[i];
            int x = (c >= '0' && c <= '9') ? int(c - '0') : (c >= 'a' && c <= 'f') ? int(c - 'a' + 10)
                    : (c >= 'A' && c <= 'F') ? int(c - 'A' + 10) : 99;
            if (x >= base) break;
            v = v * base + x;
            i++;
            d++;
        }
        return v;
    }

    int parseCollection(bool nl) {
        size_t j = i;
        VimRegex::Node n;
        n.t = N_SET;
        n.nl = nl;
        if (j < p.size() && p[j] == '^') { n.neg = true; j++; }
        if (j < p.size() && p[j] == ']') { n.ranges.push_back({']', ']'}); j++; }
        while (j < p.size() && p[j] != ']') {
            if (p[j] == '[' && j + 1 < p.size() && p[j + 1] == ':') {
                size_t e = p.find(U":]", j + 2);
                if (e != u32::npos) {
                    std::string name = toUtf8(p.substr(j + 2, e - j - 2));
                    static const struct { const char* n; int f; } NAMES[] = {
                        {"alnum", C_ALNUM}, {"alpha", C_ALPHA}, {"blank", C_BLANK}, {"cntrl", C_CNTRL},
                        {"digit", C_DIGIT}, {"graph", C_GRAPH}, {"lower", C_LOWER}, {"print", C_PRINT},
                        {"punct", C_PUNCT}, {"space", C_SPACE}, {"upper", C_UPPER}, {"xdigit", C_XDIGIT},
                        {"return", C_RETURN}, {"tab", C_TAB}, {"escape", C_ESCAPE}, {"backspace", C_BACKSPACE},
                        {"ident", C_IDENT}, {"keyword", C_KEYWORD}, {"fname", C_FNAME},
                    };
                    bool found = false;
                    for (auto& nm : NAMES)
                        if (name == nm.n) { n.classes |= nm.f; found = true; }
                    if (found) { j = e + 2; continue; }
                }
            }
            if (p[j] == '[' && j + 1 < p.size() && p[j + 1] == '=' && j + 4 < p.size() && p[j + 3] == '=' &&
                p[j + 4] == ']') {
                n.ranges.push_back({p[j + 2], p[j + 2]});
                j += 5;
                continue;
            }
            char32_t c = p[j];
            if (c == '\\' && j + 1 < p.size()) {
                char32_t e = p[j + 1];
                bool known = true;
                size_t save = i;
                switch (e) {
                    case 'e': c = 27; j += 2; break;
                    case 't': c = 9; j += 2; break;
                    case 'r': c = 13; j += 2; break;
                    case 'b': c = 8; j += 2; break;
                    case 'n': n.nl = true; j += 2; continue;
                    case '\\': case ']': case '^': case '-': c = e; j += 2; break;
                    case 'd': i = j + 2; c = readCode(10, 10); j = i; i = save; break;
                    case 'o': i = j + 2; c = readCode(8, 4); j = i; i = save; break;
                    case 'x': i = j + 2; c = readCode(16, 2); j = i; i = save; break;
                    case 'u': i = j + 2; c = readCode(16, 4); j = i; i = save; break;
                    case 'U': i = j + 2; c = readCode(16, 8); j = i; i = save; break;
                    default: known = false;
                }
                if (!known) j++;  // a literal backslash
            } else {
                j++;
            }
            char32_t hi = c;
            if (j + 1 < p.size() && p[j] == '-' && p[j + 1] != ']') {
                hi = p[j + 1];
                j += 2;
                if (hi == '\\' && j < p.size()) hi = p[j++];
                if (hi < c) { err = tr(U"E944: Reverse range in character class"); return -1; }
            }
            n.ranges.push_back({c, hi});
        }
        if (j >= p.size()) return -2;  // no closing ']': the '[' is literal
        i = j + 1;
        return add(n);
    }

    int parseAtom(bool atStart) {
        Tok t = read();
        if (!t.magic) return mk(N_CHAR, t.c);
        switch (t.c) {
            case '^': return atStart ? mk(N_BOL) : mk(N_CHAR, '^');
            case '$': {
                Tok nx = peek();
                if (isEndTok(nx) || (nx.magic && nx.c == 'n')) return mk(N_EOL);
                return mk(N_CHAR, '$');
            }
            case '.': return mk(N_ANY);
            case '[': {
                size_t si = i;
                int r = parseCollection(false);
                if (r == -2) { i = si; return mk(N_CHAR, '['); }
                return r;
            }
            case '~': {
                int n = mk(N_SEQ);
                for (char32_t c : g_rxLastSub) {
                    int k = mk(N_CHAR, c);
                    nodes[n].kids.push_back(k);
                }
                return n;
            }
            case '(': {
                int g = ++ngroups;
                int kid = parseAlt();
                if (kid < 0) return -1;
                Tok cl = read();
                if (cl.end || !cl.magic || cl.c != ')') { err = tr(U"E54: Unmatched \\("); return -1; }
                int n = mk(N_GROUP);
                nodes[n].a = g <= 9 ? g : -1;
                nodes[n].kids = {kid};
                return n;
            }
            case ')': err = tr(U"E55: Unmatched \\)"); return -1;
            case '%': {
                if (i >= p.size()) { err = tr(U"E71: Invalid character after \\%"); return -1; }
                char32_t c = p[i++];
                switch (c) {
                    case '(': {
                        int kid = parseAlt();
                        if (kid < 0) return -1;
                        Tok cl = read();
                        if (cl.end || !cl.magic || cl.c != ')') { err = tr(U"E53: Unmatched \\%("); return -1; }
                        int n = mk(N_GROUP);
                        nodes[n].a = -1;
                        nodes[n].kids = {kid};
                        return n;
                    }
                    case '^': return mk(N_BOF);
                    case '$': return mk(N_EOF);
                    case 'V': case '#': return mk(N_EMPTY);
                    case 'd': return mk(N_CHAR, readCode(10, 10));
                    case 'x': return mk(N_CHAR, readCode(16, 2));
                    case 'u': return mk(N_CHAR, readCode(16, 4));
                    case 'U': return mk(N_CHAR, readCode(16, 8));
                    case 'o': return mk(N_CHAR, readCode(8, 4));
                }
                int cmp = 0;
                if (c == '<') cmp = 1;
                else if (c == '>') cmp = 2;
                else i--;
                int v;
                if (readNum(v) && i < p.size() && (p[i] == 'l' || p[i] == 'c' || p[i] == 'v')) {
                    int n = mk(p[i] == 'l' ? N_LINE : N_COL);
                    nodes[n].a = v;
                    nodes[n].b = cmp;
                    i++;
                    return n;
                }
                err = tr(U"E71: Invalid character after \\%");
                return -1;
            }
            case '<': return mk(N_BOW);
            case '>': return mk(N_EOW);
            case 'z': {
                if (i < p.size() && p[i] == 's') { i++; return mk(N_ZS); }
                if (i < p.size() && p[i] == 'e') { i++; return mk(N_ZE); }
                err = tr(U"E68: Invalid character after \\z");
                return -1;
            }
            case '_': {
                if (i >= p.size()) { err = tr(U"E63: Invalid use of \\_"); return -1; }
                char32_t c = p[i++];
                if (c == '^') return mk(N_BOL);
                if (c == '$') return mk(N_EOL);
                if (c == '.') { int n = mk(N_ANY); nodes[n].nl = true; return n; }
                if (c == '[') {
                    size_t si = i;
                    int r = parseCollection(true);
                    if (r == -2) { i = si; return mk(N_CHAR, '['); }
                    return r;
                }
                if (c < 128 && strchr("sSdDwWaAlLuUxXoOhHiIkKfFpP", int(c))) {
                    int n = mk(N_CLASS, c);
                    nodes[n].nl = true;
                    return n;
                }
                err = tr(U"E63: Invalid use of \\_");
                return -1;
            }
            case 'n': return mk(N_NL);
            case 'e': return mk(N_CHAR, 27);
            case 't': return mk(N_CHAR, 9);
            case 'r': return mk(N_CHAR, 13);
            case 'b': return mk(N_CHAR, 8);
        }
        if (t.c >= '1' && t.c <= '9') {
            int n = mk(N_BREF);
            nodes[n].a = int(t.c - '0');
            return n;
        }
        if (t.c < 128 && strchr("sSdDwWaAlLuUxXoOhHiIkKfFpP", int(t.c))) return mk(N_CLASS, t.c);
        // "*" at the start, "\{" without an atom ...: a literal character
        return mk(N_CHAR, t.c);
    }
};

// Continuation without heap allocation
struct Cont {
    const void* obj;
    bool (*fn)(const void*, Pos);
    bool operator()(Pos p) const { return fn(obj, p); }
};
template <class F>
Cont cont(const F& f) {
    return Cont{&f, [](const void* o, Pos p) { return (*static_cast<const F*>(o))(p); }};
}

struct Matcher {
    const VimRegex& R;
    const std::vector<u32>& L;
    Pos gs[10], ge[10];
    bool g[10] = {};
    Pos zs, ze;
    bool zsSet = false, zeSet = false;
    long& steps;  // shared by all start positions of one search (see RX_MAX_STEPS)

    Matcher(const VimRegex& r, const std::vector<u32>& l, long& st) : R(r), L(l), steps(st) {}

    int len(int y) const { return int(L[y].size()); }

    bool eqc(char32_t a, char32_t b) const { return a == b || (R.icase && fold(a) == fold(b)); }

    bool inSet(const VimRegex::Node& n, char32_t c) const {
        auto t = [&](char32_t ch) {
            for (auto& r : n.ranges)
                if (ch >= r.first && ch <= r.second) return true;
            return n.classes && testNamed(n.classes, ch);
        };
        bool r = t(c) || (R.icase && (t(fold(c)) || t(unfold(c))));
        return n.neg ? !r : r;
    }

    // Single character nodes; q = position after the character
    bool one(const VimRegex::Node& n, Pos p, Pos& q) const {
        if (p.x < len(p.y)) {
            char32_t c = L[p.y][p.x];
            bool ok;
            switch (n.t) {
                case N_CHAR: ok = eqc(c, n.c); break;
                case N_ANY: ok = true; break;
                case N_SET: ok = inSet(n, c); break;
                case N_CLASS: ok = testClass(n.c, c); break;
                default: return false;
            }
            if (!ok) return false;
            q = {p.y, p.x + 1};
            return true;
        }
        // End of the line: the line break matches \_x, [\n] and "[^...]" with \_
        if (n.nl && p.y + 1 < int(L.size()) && n.t != N_CHAR) {
            q = {p.y + 1, 0};
            return true;
        }
        return false;
    }

    static bool single(const VimRegex::Node& n) {
        return n.t == N_CHAR || n.t == N_ANY || n.t == N_SET || n.t == N_CLASS;
    }

    bool seq(const VimRegex::Node& n, size_t i, Pos p, Cont k) {
        if (i == n.kids.size()) return k(p);
        auto nx = [&](Pos q) { return seq(n, i + 1, q, k); };
        return m(n.kids[i], p, cont(nx));
    }

    bool rep(const VimRegex::Node& n, int count, Pos p, Cont k) {
        const VimRegex::Node& kid = R.nodes[n.kids[0]];
        if (single(kid)) {
            std::vector<Pos> pos{p};
            Pos cur = p;
            while (n.b < 0 || int(pos.size()) - 1 < n.b) {
                Pos q;
                if (!one(kid, cur, q)) break;
                pos.push_back(q);
                cur = q;
            }
            if (n.greedy) {
                for (int c = int(pos.size()) - 1; c >= n.a; c--)
                    if (k(pos[c])) return true;
            } else {
                for (int c = n.a; c < int(pos.size()); c++)
                    if (k(pos[c])) return true;
            }
            return false;
        }
        auto more = [&](Pos q) {
            if (q == p) return false;  // an empty iteration would loop forever
            return rep(n, count + 1, q, k);
        };
        bool canMore = n.b < 0 || count < n.b;
        if (n.greedy) {
            if (canMore && m(n.kids[0], p, cont(more))) return true;
            return count >= n.a && k(p);
        }
        if (count >= n.a && k(p)) return true;
        return canMore && m(n.kids[0], p, cont(more));
    }

    bool m(int ni, Pos p, Cont k) {
        if (++steps > RX_MAX_STEPS) return false;  // give up on catastrophic backtracking
        const VimRegex::Node& n = R.nodes[ni];
        switch (n.t) {
            case N_CHAR: case N_ANY: case N_SET: case N_CLASS: {
                Pos q;
                return one(n, p, q) && k(q);
            }
            case N_BOL: return p.x == 0 && k(p);
            case N_EOL: return p.x == len(p.y) && k(p);
            case N_BOF: return p.y == 0 && p.x == 0 && k(p);
            case N_EOF: return p.y == int(L.size()) - 1 && p.x == len(p.y) && k(p);
            case N_BOW: {
                const u32& l = L[p.y];
                return p.x < int(l.size()) && isKw(l[p.x]) && (p.x == 0 || !isKw(l[p.x - 1])) && k(p);
            }
            case N_EOW: {
                const u32& l = L[p.y];
                return p.x > 0 && isKw(l[p.x - 1]) && (p.x == int(l.size()) || !isKw(l[p.x])) && k(p);
            }
            case N_NL: return p.x == len(p.y) && p.y + 1 < int(L.size()) && k(Pos{p.y + 1, 0});
            case N_EMPTY: return k(p);
            case N_SEQ: return seq(n, 0, p, k);
            case N_ALT:
                for (int kid : n.kids)
                    if (m(kid, p, k)) return true;
                return false;
            case N_AND: {
                auto yes = [](Pos) { return true; };
                for (size_t i = 0; i + 1 < n.kids.size(); i++)
                    if (!m(n.kids[i], p, cont(yes))) return false;
                return m(n.kids.back(), p, k);
            }
            case N_GROUP: {
                int gi = n.a;
                if (gi < 0) return m(n.kids[0], p, k);
                Pos os = gs[gi], oe = ge[gi];
                bool og = g[gi];
                gs[gi] = p;
                auto close = [&](Pos e) {
                    Pos oe2 = ge[gi];
                    bool og2 = g[gi];
                    ge[gi] = e;
                    g[gi] = true;
                    if (k(e)) return true;
                    ge[gi] = oe2;
                    g[gi] = og2;
                    return false;
                };
                if (m(n.kids[0], p, cont(close))) return true;
                gs[gi] = os;
                ge[gi] = oe;
                g[gi] = og;
                return false;
            }
            case N_REP: return rep(n, 0, p, k);
            case N_ZS: {
                Pos o = zs;
                bool os = zsSet;
                zs = p;
                zsSet = true;
                if (k(p)) return true;
                zs = o;
                zsSet = os;
                return false;
            }
            case N_ZE: {
                Pos o = ze;
                bool os = zeSet;
                ze = p;
                zeSet = true;
                if (k(p)) return true;
                ze = o;
                zeSet = os;
                return false;
            }
            case N_BREF: {
                int gi = n.a;
                if (!g[gi]) return k(p);
                Pos a = gs[gi], b = ge[gi], q = p;
                while (a < b) {
                    if (a.x >= len(a.y)) {
                        if (q.x != len(q.y) || q.y + 1 >= int(L.size())) return false;
                        a = {a.y + 1, 0};
                        q = {q.y + 1, 0};
                        continue;
                    }
                    if (q.x >= len(q.y) || !eqc(L[q.y][q.x], L[a.y][a.x])) return false;
                    a.x++;
                    q.x++;
                }
                return k(q);
            }
            case N_LOOK: {
                auto yes = [](Pos) { return true; };
                switch (n.a) {
                    case 0: return m(n.kids[0], p, cont(yes)) && k(p);
                    case 1: return !m(n.kids[0], p, cont(yes)) && k(p);
                    case 2: case 3: {
                        bool found = false;
                        auto atP = [&](Pos e) { return e == p; };
                        for (int x = p.x; x >= 0 && !found; x--)
                            if (m(n.kids[0], Pos{p.y, x}, cont(atP))) found = true;
                        if (!found && p.y > 0) {
                            for (int x = len(p.y - 1); x >= 0 && !found; x--)
                                if (m(n.kids[0], Pos{p.y - 1, x}, cont(atP))) found = true;
                        }
                        return (n.a == 2 ? found : !found) && k(p);
                    }
                    default: {
                        Pos e;
                        auto take = [&](Pos q) { e = q; return true; };
                        if (!m(n.kids[0], p, cont(take))) return false;
                        return k(e);
                    }
                }
            }
            case N_LINE: {
                int v = p.y + 1;
                bool ok = n.b == 0 ? v == n.a : n.b == 1 ? v < n.a : v > n.a;
                return ok && k(p);
            }
            case N_COL: {
                int v = p.x + 1;
                bool ok = n.b == 0 ? v == n.a : n.b == 1 ? v < n.a : v > n.a;
                return ok && k(p);
            }
        }
        return false;
    }
};

}  // namespace

u32 g_rxLastSub;

bool VimRegex::compile(const u32& pat, bool ignorecase, bool smartcase) {
    nodes.clear();
    root = -1;
    ngroups = 0;
    err.clear();
    // \c and \C anywhere in the pattern override 'ignorecase'
    int cflag = 0;
    bool upper = false;
    for (size_t i = 0; i < pat.size(); i++) {
        if (pat[i] == '\\' && i + 1 < pat.size()) {
            if (pat[i + 1] == 'c') cflag = 1;
            else if (pat[i + 1] == 'C' && cflag == 0) cflag = 2;
            i++;
            continue;
        }
        if (isUpperCh(pat[i])) upper = true;
    }
    icase = cflag == 1 || (cflag == 0 && ignorecase && !(smartcase && upper));
    Parser ps(pat, nodes, ngroups);
    int r = ps.parseAlt();
    if (r >= 0 && ps.i < pat.size()) {
        Tok t = ps.peek();
        if (!t.end) {
            r = -1;
            if (ps.err.empty()) ps.err = t.c == ')' ? tr(U"E55: Unmatched \\)") : tr(U"E476: Invalid command");
        }
    }
    if (r < 0) {
        err = ps.err.empty() ? u32(tr(U"E383: Invalid search string")) : ps.err;
        nodes.clear();
        return false;
    }
    root = r;
    return true;
}

bool VimRegex::matchAt(const std::vector<u32>& L, Pos p, RxMatch& out) const {
    long steps = 0;
    return matchAt(L, p, out, steps);
}

bool VimRegex::matchAt(const std::vector<u32>& L, Pos p, RxMatch& out, long& steps) const {
    if (root < 0 || p.y < 0 || p.y >= int(L.size()) || steps > RX_MAX_STEPS) return false;
    Matcher mt(*this, L, steps);
    Pos end;
    auto fin = [&](Pos e) {
        end = e;
        for (int i = 0; i < 10; i++) { out.gs[i] = mt.gs[i]; out.ge[i] = mt.ge[i]; out.g[i] = mt.g[i]; }
        return true;
    };
    if (!mt.m(root, p, cont(fin))) return false;
    out.s = mt.zsSet ? mt.zs : p;
    out.e = mt.zeSet ? mt.ze : end;
    if (out.e < out.s) out.e = out.s;
    out.gs[0] = out.s;
    out.ge[0] = out.e;
    out.g[0] = true;
    return true;
}

bool VimRegex::findInLine(const std::vector<u32>& L, int y, int x, RxMatch& m) const {
    if (root < 0 || y < 0 || y >= int(L.size())) return false;
    int n = int(L[y].size());
    const Node& r = nodes[root];
    // one budget for the whole line: a catastrophic pattern must not be retried at every index
    long steps = 0;
    for (int i = std::max(0, x); i <= n && steps <= RX_MAX_STEPS; i++) {
        // quick reject for patterns that start with a literal character
        if (r.t == N_CHAR && !icase && (i >= n || L[y][i] != r.c)) continue;
        if (r.t == N_BOL && i > 0) return false;
        if (matchAt(L, {y, i}, m, steps)) return true;
    }
    return false;
}

bool VimRegex::findLastInLine(const std::vector<u32>& L, int y, int x, RxMatch& m) const {
    bool found = false;
    RxMatch t;
    int from = 0;
    while (findInLine(L, y, from, t)) {
        if (!(t.s.y == y && t.s.x < x) && !(t.s.y < y)) break;
        m = t;
        found = true;
        from = t.s.x + 1;
        if (t.s.y != y) break;
    }
    return found;
}

u32 rxEscape(const u32& s) {
    u32 r;
    for (char32_t c : s) {
        if (c == '\\' || c == '/' || c == '.' || c == '*' || c == '$' || c == '^' || c == '~' || c == '[' ||
            c == '?')
            r += '\\';
        r += c;
    }
    return r;
}
