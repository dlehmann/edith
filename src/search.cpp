// Searching with regular expressions in the Find and Change dialogs.

#include "search.h"

// = & ~ @ % are special in Vim's very magic mode but plain characters in the usual regular
// expressions (a=b, user@host, 100%): they are escaped outside of [...].
static u32 plainSpecials(const u32& p) {
    u32 r;
    bool coll = false;
    for (size_t i = 0; i < p.size(); i++) {
        char32_t c = p[i];
        if (c == '\\' && i + 1 < p.size()) {
            r += c;
            r += p[++i];
            continue;
        }
        if (coll) {
            if (c == ']') coll = false;
        } else if (c == '[') {
            coll = true;
            r += c;
            if (i + 1 < p.size() && p[i + 1] == '^') r += p[++i];
            if (i + 1 < p.size() && p[i + 1] == ']') r += p[++i];  // "]" first is a character
            continue;
        } else if (c == '=' || c == '&' || c == '~' || c == '@' || c == '%') {
            r += '\\';
        }
        r += c;
    }
    return r;
}

bool compileSearch(VimRegex& rx, const u32& pattern, bool cs, bool whole, u32& err) {
    u32 pat = plainSpecials(pattern);
    u32 p = U"\\v" + (whole ? U"<%(" + pat + U")>" : pat);
    if (!rx.compile(p, !cs)) {
        err = rx.err;
        return false;
    }
    return true;
}

bool findRx(const Editor& ed, const VimRegex& rx, Pos from, RxMatch& m) {
    int cnt = ed.n();
    for (int k = 0; k <= cnt; k++) {
        int y = (from.y + k) % cnt;
        int x = k == 0 ? from.x : 0;
        int len = int(ed.lines[size_t(y)].size());
        while (x <= len && rx.findInLine(ed.lines, y, x, m)) {
            if (k == cnt && m.s.x >= from.x) return false;  // back at the start
            if (!(k == 0 && m.s == m.e && m.s == from)) return true;
            x = m.s.x + 1;
        }
    }
    return false;
}

u32 expandReplacement(const Editor& ed, const RxMatch& m, const u32& r) {
    u32 out;
    for (size_t i = 0; i < r.size(); i++) {
        if (r[i] != '\\' || i + 1 >= r.size()) {
            out += r[i];
            continue;
        }
        char32_t c = r[++i];
        if (c >= '0' && c <= '9') {
            int g = int(c - '0');
            if (g == 0) out += ed.getText(m.s, m.e);
            else if (m.g[g]) out += ed.getText(m.gs[g], m.ge[g]);
        } else if (c == 'n') {
            out += '\n';
        } else if (c == 't') {
            out += '\t';
        } else if (c == '\\') {
            out += '\\';
        } else {
            out += '\\';
            out += c;
        }
    }
    return out;
}

int replaceAllRx(Editor& ed, const VimRegex& rx, const u32& r) {
    int n = ed.n();
    Pos p{0, 0}, last{0, 0};
    u32 out;
    int cnt = 0;
    RxMatch m;
    while (p.y < n) {
        if (!rx.findInLine(ed.lines, p.y, p.x, m)) {
            p = {p.y + 1, 0};
            continue;
        }
        out += ed.getText(last, m.s) + expandReplacement(ed, m, r);
        last = m.e;
        cnt++;
        p = m.e;
        if (m.s == m.e) {  // an empty match: go on after the next character
            if (p.x < int(ed.lines[size_t(p.y)].size())) p.x++;
            else p = {p.y + 1, 0};
        }
    }
    if (!cnt) return 0;  // without a match neither the undo nor the redo list changes
    out += ed.getText(last, {n - 1, int(ed.lines[size_t(n - 1)].size())});
    std::vector<u32> result(1);
    for (char32_t c : out) {
        if (c == '\n') result.emplace_back();
        else result.back() += c;
    }
    ed.checkpoint(0);
    ed.lines = std::move(result);
    ed.touch(0);
    ed.modified = true;
    ed.anch = false;
    ed.clampCursor();
    return cnt;
}
