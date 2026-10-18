// Vim emulation: searching and the Ex commands of Command-line mode.

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>


#include "i18n.h"
#include "paths.h"
#include "process.h"
#include "utf8.h"
#include "vim.h"

namespace {

bool isBlank(char32_t c) { return c == ' ' || c == '\t'; }
bool isDigit(char32_t c) { return c >= '0' && c <= '9'; }
bool isAlpha(char32_t c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

u32 trim(const u32& s) {
    size_t a = 0, b = s.size();
    while (a < b && isBlank(s[a])) a++;
    while (b > a && isBlank(s[b - 1])) b--;
    return s.substr(a, b - a);
}

// Splits a pattern at the first unescaped delimiter; i is moved past it.
u32 takeUntil(const u32& s, size_t& i, char32_t delim, bool& found) {
    u32 r;
    found = false;
    while (i < s.size()) {
        char32_t c = s[i];
        if (c == '\\' && i + 1 < s.size()) {
            if (s[i + 1] == delim && delim != '\\') {
                // "\/" in a "/" pattern is a literal "/"
                r += '\\';
                r += delim;
            } else {
                r += c;
                r += s[i + 1];
            }
            i += 2;
            continue;
        }
        if (c == delim) {
            i++;
            found = true;
            return r;
        }
        r += c;
        i++;
    }
    return r;
}

struct ExName {
    const char* full;
    int min;
};
const ExName EXCMDS[] = {
    {"substitute", 1}, {"global", 1}, {"vglobal", 1}, {"delete", 1}, {"delmarks", 4}, {"display", 2},
    {"yank", 1}, {"put", 2}, {"pwd", 2}, {"print", 1}, {"move", 1}, {"marks", 5}, {"mark", 2}, {"map", 3},
    {"mapclear", 4}, {"copy", 2}, {"center", 2}, {"cmap", 2}, {"cnoremap", 3}, {"cunmap", 2},
    {"changes", 7}, {"cmapclear", 5}, {"t", 1}, {"join", 1}, {"jumps", 2}, {"normal", 4},
    {"nohlsearch", 3}, {"noremap", 2}, {"nmap", 2}, {"nnoremap", 2}, {"nunmap", 3}, {"number", 2},
    {"nmapclear", 5}, {"k", 1}, {"registers", 3}, {"read", 1}, {"redo", 3}, {"retab", 3}, {"right", 2},
    {"redraw", 4}, {"undo", 1}, {"update", 2}, {"unmap", 3}, {"write", 1}, {"wq", 2}, {"wall", 2},
    {"wqall", 3}, {"xit", 1}, {"xall", 2}, {"xmap", 2}, {"xnoremap", 2}, {"xunmap", 2}, {"xmapclear", 5},
    {"split", 2}, {"vsplit", 2}, {"vertical", 4}, {"new", 3}, {"vnew", 3}, {"close", 3}, {"only", 2},
    {"tabedit", 4}, {"tabnew", 6}, {"tabclose", 4}, {"tabonly", 4}, {"tabnext", 4}, {"tabNext", 4},
    {"tabprevious", 4}, {"tabfirst", 6}, {"tabrewind", 4}, {"tablast", 4}, {"tabs", 4}, {"tabmove", 4},
    {"resize", 3}, {"wincmd", 4}, {"Format", 6},
    {"exit", 3}, {"edit", 1}, {"echo", 2}, {"enew", 3}, {"execute", 3}, {"quit", 1}, {"qall", 2},
    {"quitall", 5}, {"saveas", 3}, {"set", 2}, {"setlocal", 4}, {"setglobal", 4}, {"sort", 3},
    {"syntax", 2}, {"startinsert", 4}, {"stopinsert", 5}, {"silent", 3}, {"file", 1}, {"filetype", 5},
    {"files", 5}, {"left", 2}, {"let", 3}, {"list", 1}, {"ls", 2}, {"buffers", 7}, {"help", 1},
    {"version", 2}, {"vmap", 2}, {"vnoremap", 2}, {"vunmap", 2}, {"vmapclear", 5}, {"omap", 2},
    {"onoremap", 3}, {"ounmap", 2}, {"omapclear", 5}, {"imap", 2}, {"inoremap", 3}, {"iunmap", 2},
    {"imapclear", 5}, {"smile", 3},
};

u32 resolveName(const u32& name) {
    std::string n = toUtf8(name);
    for (auto& e : EXCMDS)
        if (n == e.full) return name;
    for (auto& e : EXCMDS) {
        size_t L = strlen(e.full);
        if (int(n.size()) >= e.min && n.size() <= L && strncmp(e.full, n.c_str(), n.size()) == 0) return U(e.full);
    }
    return u32();
}

}  // namespace

// ---------------------------------------------------------------------------
// Searching
// ---------------------------------------------------------------------------

bool Vim::compilePat(const u32& pat, VimRegex& rx, bool useSmartcase) {
    if (!rx.compile(pat, opt.ignorecase, useSmartcase && opt.smartcase)) {
        error(rx.err);
        return false;
    }
    return true;
}

bool Vim::searchPat(const u32& pat, bool fwd, Pos from, int count, Pos& res, Pos* end, bool quiet, bool smart) {
    VimRegex rx;
    if (pat.empty()) {
        if (!quiet) error(tr(U"E35: No previous regular expression"));
        return false;
    }
    if (!compilePat(pat, rx, smart)) return false;
    Pos p = from;
    RxMatch m;
    bool wrappedAny = false;
    for (int k = 0; k < count; k++) {
        bool found = false, wrapped = false;
        if (fwd) {
            // matches that start after p, then the following lines, then from the top
            int nl = n();
            for (int i = 0; i <= nl && !found; i++) {
                int y = p.y + i;
                if (y >= nl) {
                    if (!opt.wrapscan) break;
                    y -= nl;
                    wrapped = true;
                }
                int x = i == 0 ? p.x + 1 : 0;
                if (i == nl) {
                    // the part of the start line before p
                    RxMatch t;
                    int xx = 0;
                    while (xx <= p.x && rx.findInLine(ed.lines, y, xx, t)) {
                        if (t.s.x > p.x || t.s.y != y) break;
                        m = t;
                        found = true;
                        break;
                    }
                    break;
                }
                if (x > len(y) + (i == 0 ? 0 : 0) && i == 0) continue;
                RxMatch t;
                int xx = x;
                while (rx.findInLine(ed.lines, y, xx, t)) {
                    if (i == 0 && !(p < t.s)) { xx = t.s.x + 1; if (xx > len(y)) break; continue; }
                    m = t;
                    found = true;
                    break;
                }
            }
        } else {
            int nl = n();
            for (int i = 0; i <= nl && !found; i++) {
                int y = p.y - i;
                if (y < 0) {
                    if (!opt.wrapscan) break;
                    y += nl;
                    wrapped = true;
                }
                RxMatch t;
                if (i == 0) {
                    if (rx.findLastInLine(ed.lines, y, p.x, t)) { m = t; found = true; }
                } else if (i == nl) {
                    // the part of the start line after p
                    RxMatch best;
                    bool any = false;
                    int xx = p.x + 1;
                    while (xx <= len(y) && rx.findInLine(ed.lines, y, xx, t) && t.s.y == y) {
                        best = t;
                        any = true;
                        xx = t.s.x + 1;
                    }
                    if (any) { m = best; found = true; }
                } else {
                    if (rx.findLastInLine(ed.lines, y, INT_MAX, t)) { m = t; found = true; }
                }
            }
        }
        if (!found) {
            if (!quiet) {
                if (!opt.wrapscan)
                    error((fwd ? tr(U"E385: Search hit BOTTOM without match for: ")
                               : tr(U"E384: Search hit TOP without match for: ")) +
                          pat);
                else
                    error(tr(U"E486: Pattern not found: ") + pat);
            }
            return false;
        }
        if (wrapped) wrappedAny = true;
        p = m.s;
    }
    res = p;
    if (end) *end = m.e;
    if (wrappedAny && !quiet) {
        message(fwd ? tr(U"search hit BOTTOM, continuing at TOP") : tr(U"search hit TOP, continuing at BOTTOM"));
        msgErr = true;
    }
    return true;
}

bool Vim::doSearch(const u32& spec, bool fwd, int count, Pos& to, int& mtype, bool& incl) {
    u32 pat, off;
    bool repeat = searchIsRepeat;
    if (!repeat) {
        size_t i = 0;
        bool found;
        pat = takeUntil(spec, i, fwd ? '/' : '?', found);
        off = found ? spec.substr(i) : u32();
        // "\/" was only needed to end the pattern
        u32 cleaned;
        for (size_t k = 0; k < pat.size(); k++) {
            if (pat[k] == '\\' && k + 1 < pat.size() && pat[k + 1] == (fwd ? '/' : '?')) continue;
            cleaned += pat[k];
        }
        pat = cleaned;
        if (pat.empty()) {
            pat = lastPat;
            if (!found) off = lastOffset;
        }
        if (pat.empty()) { error(tr(U"E35: No previous regular expression")); return false; }
        lastPat = pat;
        lastOffset = off;
        lastDirFwd = fwd;
    } else {
        pat = lastPat;
        off = lastOffset;
    }
    hlOn = true;
    // offset: [+-]N lines, e[+-N], s[+-N], b[+-N]
    char32_t ok = 0;
    long num = 0;
    {
        size_t i = 0;
        if (i < off.size() && (off[i] == 'e' || off[i] == 's' || off[i] == 'b')) ok = off[i++];
        bool sign = false, neg = false;
        if (i < off.size() && (off[i] == '+' || off[i] == '-')) { neg = off[i] == '-'; sign = true; i++; }
        bool dig = false;
        while (i < off.size() && isDigit(off[i])) {
            num = std::min(num * 10 + long(off[i] - '0'), 100000L);
            i++;
            dig = true;
        }
        if (sign && !dig) num = 1;
        if (neg) num = -num;
        if (!ok && (sign || dig)) ok = 'l';
    }
    Pos from{ed.cy, ed.cx};
    // with an end offset the search starts after the current match
    if (ok == 'e' && fwd) {
        Pos f = from;
        for (long i = 0; i < num + 0 && f.x > 0; i++) f.x--;
        from = f;
    } else if ((ok == 's' || ok == 'b') && num > 0 && fwd) {
        from.x = std::max(0, from.x - int(num));
    }
    if (ok == 'l') {
        // a line offset searches from the start/end of the line
        from = fwd ? Pos{ed.cy, len(ed.cy)} : Pos{ed.cy, 0};
        if (num < 0 && fwd) from = {std::min(n() - 1, ed.cy - int(num)), len(std::min(n() - 1, ed.cy - int(num)))};
    }
    Pos res, end;
    if (!searchPat(pat, fwd, from, count, res, &end, false)) return false;
    mtype = 0;
    incl = false;
    if (ok == 'l') {
        int y = std::clamp(res.y + int(num), 0, n() - 1);
        to = {y, firstNonBlank(y)};
        mtype = 2;
        return true;
    }
    if (ok == 'e') {
        Pos e = end;
        // the last matched character
        if (e.x > 0) e.x--;
        else if (e.y > res.y) { e.y--; e.x = std::max(0, len(e.y) - 1); }
        for (long i = 0; i < num; i++) inc(e);
        for (long i = 0; i > num; i--) dec(e);
        to = e;
        incl = true;
        return true;
    }
    if (ok == 's' || ok == 'b') {
        Pos s = res;
        for (long i = 0; i < num; i++) inc(s);
        for (long i = 0; i > num; i--) dec(s);
        to = s;
        incl = num > 0;
        return true;
    }
    to = res;
    return true;
}

bool Vim::starSearch(bool fwd, bool whole, int count, Pos& to) {
    const u32& l = line(ed.cy);
    int L = len(ed.cy);
    int s = ed.cx;
    while (s < L && !isWord(l[size_t(s)])) s++;
    bool kw = s < L;
    if (!kw) {
        s = ed.cx;
        while (s < L && isBlank(l[size_t(s)])) s++;
        if (s >= L) { error(tr(U"E348: No string under cursor")); return false; }
    }
    auto in = [&](char32_t c) { return kw ? isWord(c) : !isBlank(c); };
    while (s > 0 && in(l[size_t(s - 1)]) && (kw || s > ed.cx)) s--;
    int e = s;
    while (e < L && in(l[size_t(e)])) e++;
    u32 w = l.substr(size_t(s), size_t(e - s));
    u32 pat = rxEscape(w);
    if (whole && kw) pat = U"\\<" + pat + U"\\>";
    lastPat = pat;
    lastOffset.clear();
    lastDirFwd = fwd;
    hlOn = true;
    auto& h = hist['/'];
    h.erase(std::remove(h.begin(), h.end(), pat), h.end());
    h.push_back(pat);
    Pos from{ed.cy, s};  // like Vim: from the start of the word
    Pos res;
    if (!searchPat(pat, fwd, from, count, res, nullptr, false, false)) return false;
    to = res;
    return true;
}

// ---------------------------------------------------------------------------
// Ranges
// ---------------------------------------------------------------------------

bool Vim::parseAddr(const u32& s, size_t& i, int& ln, bool& got) {
    got = false;
    while (i < s.size() && isBlank(s[i])) i++;
    int base = ed.cy;
    if (i < s.size()) {
        char32_t c = s[i];
        if (isDigit(c)) {
            long v = 0;
            while (i < s.size() && isDigit(s[i])) { v = std::min(v * 10 + long(s[i] - '0'), 99999999L); i++; }
            base = int(v) - 1;
            got = true;
        } else if (c == '.') { i++; got = true; }
        else if (c == '$') { base = n() - 1; i++; got = true; }
        else if (c == '\'' && i + 1 < s.size()) {
            Pos p;
            if (!getMark(s[i + 1], p)) { error(tr(U"E20: Mark not set")); return false; }
            base = p.y;
            i += 2;
            got = true;
        } else if (c == '/' || c == '?') {
            i++;
            bool found;
            u32 pat = takeUntil(s, i, c, found);
            if (pat.empty()) pat = lastPat;
            else lastPat = pat;
            Pos res;
            Pos from = c == '/' ? Pos{ed.cy, len(ed.cy)} : Pos{ed.cy, 0};
            if (!searchPat(pat, c == '/', from, 1, res, nullptr, false)) return false;
            base = res.y;
            got = true;
        } else if (c == '\\' && i + 1 < s.size() && (s[i + 1] == '/' || s[i + 1] == '?' || s[i + 1] == '&')) {
            Pos res;
            bool f = s[i + 1] != '?';
            Pos from = f ? Pos{ed.cy, len(ed.cy)} : Pos{ed.cy, 0};
            if (!searchPat(lastPat, f, from, 1, res, nullptr, false)) return false;
            base = res.y;
            i += 2;
            got = true;
        }
    }
    // offsets
    for (;;) {
        while (i < s.size() && isBlank(s[i]) && i + 1 < s.size() && (s[i + 1] == '+' || s[i + 1] == '-')) i++;
        if (i >= s.size() || (s[i] != '+' && s[i] != '-' && !(got && isDigit(s[i]) && false))) break;
        bool neg = s[i] == '-';
        i++;
        long v = 0;
        bool dig = false;
        while (i < s.size() && isDigit(s[i])) { v = std::min(v * 10 + long(s[i] - '0'), 99999999L); i++; dig = true; }
        if (!dig) v = 1;
        base += int(neg ? -v : v);
        got = true;
    }
    ln = base;
    return true;
}

bool Vim::parseRange(const u32& s, size_t& i, int& l1, int& l2, int& naddr) {
    l1 = l2 = ed.cy;
    naddr = 0;
    while (i < s.size() && (isBlank(s[i]) || s[i] == ':')) i++;
    if (i < s.size() && s[i] == '%') {
        i++;
        l1 = 0;
        l2 = n() - 1;
        naddr = 2;
        return true;
    }
    if (i < s.size() && s[i] == '*') {
        i++;
        Pos a, b;
        if (!getMark('<', a) || !getMark('>', b)) { error(tr(U"E20: Mark not set")); return false; }
        l1 = a.y;
        l2 = b.y;
        naddr = 2;
        return true;
    }
    for (;;) {
        int ln;
        bool got;
        if (!parseAddr(s, i, ln, got)) return false;
        if (got) {
            l1 = l2;
            l2 = ln;
            naddr++;
        }
        while (i < s.size() && isBlank(s[i])) i++;
        if (i < s.size() && (s[i] == ',' || s[i] == ';')) {
            if (!got) {
                l1 = l2;
                l2 = ed.cy;
                naddr++;
            }
            if (s[i] == ';') {
                ed.cy = std::clamp(l2, 0, n() - 1);
                ed.cx = 0;
            }
            i++;
            // "1," means "1,."
            size_t j = i;
            int ln2;
            bool got2;
            size_t save = i;
            if (!parseAddr(s, j, ln2, got2)) return false;
            if (!got2) {
                l1 = l2;
                l2 = ed.cy;
                naddr++;
                i = save;
                break;
            }
            continue;
        }
        break;
    }
    if (naddr == 1) l1 = l2;
    return true;
}

// ---------------------------------------------------------------------------
// Ex commands
// ---------------------------------------------------------------------------

void Vim::exCommand(u32 cmd) {
    size_t i = 0;
    while (i < cmd.size() && (isBlank(cmd[i]) || cmd[i] == ':')) i++;
    if (i >= cmd.size()) return;
    if (cmd[i] == '"') return;  // comment
    int l1, l2, naddr;
    if (!parseRange(cmd, i, l1, l2, naddr)) return;
    while (i < cmd.size() && isBlank(cmd[i])) i++;
    // command name
    u32 name;
    if (i < cmd.size() && isAlpha(cmd[i])) {
        size_t s = i;
        while (i < cmd.size() && isAlpha(cmd[i])) i++;
        name = cmd.substr(s, i - s);
        // ":s#a#b#", ":ka", ":dl" ...: split known short forms with an argument
        if (name.size() > 1 && name[0] == 'k' && resolveName(name).empty()) {
            i = s + 1;
            name = U"k";
        }
        if (resolveName(name).empty() && name.size() > 1 && name[0] == 's' &&
            (name[1] == 'g' || name[1] == 'i' || name[1] == 'I' || name[1] == 'c' || name[1] == 'e' || name[1] == 'n' ||
             name[1] == 'r')) {
            // ":sg" = ":s//~/g" style abbreviations are not supported; treat as :s with flags
            i = s + 1;
            name = U"s";
        }
    } else if (i < cmd.size()) {
        name = cmd.substr(i, 1);
        i++;
        if ((name == U"&" && i < cmd.size() && cmd[i] == '&')) { name = U"&&"; i++; }
    }
    bool bang = false;
    if (i < cmd.size() && cmd[i] == '!' && name != U"!") { bang = true; i++; }
    u32 full = name.empty() ? u32() : (isAlpha(name[0]) ? resolveName(name) : name);
    if (name == U"Ni" && bang) {  // Vim's easter eggs stay in English in every UI language
        message(U"Do you demand a shrubbery?");
        return;
    }
    if (!name.empty() && full.empty()) {
        error(tr(U"E492: Not an editor command: ") + trim(cmd));
        return;
    }
    u32 args = cmd.substr(i);
    // a "|" separates commands, except for commands that take it as an argument
    bool takesBar = full == U"global" || full == U"vglobal" || full == U"normal" || full == U"!" ||
                    full.find(U"map") != u32::npos || full.find(U"noremap") != u32::npos || full == U"execute" ||
                    full == U"wincmd";
    u32 next;
    if (!takesBar) {
        for (size_t k = 0; k < args.size(); k++) {
            if (args[k] == '\\' && k + 1 < args.size()) { k++; continue; }
            if (args[k] == VK::ctrl('v') && k + 1 < args.size()) { k++; continue; }
            if (args[k] == '|') {
                next = args.substr(k + 1);
                args = args.substr(0, k);
                break;
            }
        }
    }
    u32 a = trim(args);
    if (l1 > l2) std::swap(l1, l2);
    // for these commands the "range" is a count (a height or a number), not a line
    bool countCmd = full == U"split" || full == U"vsplit" || full == U"new" || full == U"vnew" ||
                    full == U"resize" || full == U"wincmd" || full == U"tabclose" || full == U"tabonly" ||
                    full == U"tabnext" || full == U"tabprevious" || full == U"tabNext";
    if (naddr > 0 && !countCmd && (l1 < -1 || l2 >= n() + 0 || l2 < -1)) {
        if (!(full.empty() && l2 >= n())) {
            error(tr(U"E16: Invalid range"));
            return;
        }
    }
    l1 = std::max(l1, -1);
    auto clampL = [&](int v) { return std::clamp(v, 0, n() - 1); };
    auto countArg = [&](u32& rest) {
        // [x] [count] for :d and :y
        u32 r = trim(rest);
        char32_t reg = 0;
        if (!r.empty() && !isDigit(r[0]) && validReg(r[0], true)) { reg = r[0]; r = trim(r.substr(1)); }
        if (!r.empty() && isDigit(r[0])) {
            long c = 0;
            size_t k = 0;
            while (k < r.size() && isDigit(r[k])) { c = std::min(c * 10 + long(r[k] - '0'), 99999999L); k++; }
            l1 = l2;
            l2 = clampL(l1 + int(c) - 1);
        }
        rest = r;
        return reg;
    };

    if (full.empty()) {
        // ":N" goes to line N
        if (naddr) {
            int y = std::clamp(l2, 0, n() - 1);
            pushJump();
            toFirstNonBlank(y);
        }
    } else if (full == U"substitute" || full == U"&" || full == U"&&" || full == U"~") {
        if (full == U"&&") args = U"&" + args;
        exSubstitute(clampL(l1), clampL(l2), full == U"substitute" ? args : trim(args),
                     full == U"~" ? '~' : (full == U"substitute" ? 's' : '&'));
    } else if (full == U"global" || full == U"vglobal") {
        if (naddr == 0) { l1 = 0; l2 = n() - 1; }
        exGlobal(clampL(l1), clampL(l2), args, full == U"vglobal" || bang);
    } else if (full == U"delete" || full == U"yank") {
        u32 rest = args;
        char32_t reg = countArg(rest);
        l1 = clampL(l1);
        l2 = clampL(l2);
        VimReg r = textReg(Pos{l1, 0}, Pos{l2, 0}, RT_LINE);
        if (full == U"delete") {
            beginChange();
            setReg(reg, r, false, false);
            delLines(l1, l2);
            toFirstNonBlank(std::min(l1, n() - 1));
            recordChange(Pos{ed.cy, 0});
            endChange();
            if (l2 - l1 + 1 > opt.report) message(fmtU(trs("%d fewer lines").c_str(), l2 - l1 + 1));
        } else {
            setReg(reg, r, true, false);
            if (l2 - l1 + 1 > opt.report) message(fmtU(trs("%d lines yanked").c_str(), l2 - l1 + 1));
        }
    } else if (full == U"put") {
        char32_t reg = a.empty() ? 0 : a[0];
        VimReg r = getReg(reg);
        if (r.empty()) { error(tr(U"E353: Nothing in register ") + (reg ? u32(1, reg) : u32(U"\""))); return; }
        int after = naddr ? l2 : ed.cy;
        int y = bang ? after : after + 1;
        if (naddr && l2 < 0) y = 0;
        y = std::clamp(y, 0, n());
        beginChange();
        insLines(y, r.lines);
        toFirstNonBlank(y + int(r.lines.size()) - 1);
        recordChange(Pos{ed.cy, 0});
        endChange();
    } else if (full == U"move" || full == U"copy" || full == U"t") {
        size_t j = 0;
        int dest;
        bool got;
        if (!parseAddr(a, j, dest, got) || !got) { error(tr(U"E14: Invalid address")); return; }
        l1 = clampL(l1);
        l2 = clampL(l2);
        dest = std::clamp(dest, -1, n() - 1);
        if (full == U"move" && dest >= l1 && dest < l2) {
            error(tr(U"E134: Cannot move a range of lines into itself"));
            return;
        }
        std::vector<u32> ls(ed.lines.begin() + l1, ed.lines.begin() + l2 + 1);
        beginChange();
        if (full == U"move") {
            if (dest >= l1 - 1 && dest <= l2) { endChange(); toFirstNonBlank(l2); return; }
            delLines(l1, l2);
            int at = dest > l2 ? dest - int(ls.size()) + 1 : dest + 1;
            insLines(at, ls);
            toFirstNonBlank(at + int(ls.size()) - 1);
        } else {
            insLines(dest + 1, ls);
            toFirstNonBlank(dest + int(ls.size()));
        }
        recordChange(Pos{ed.cy, 0});
        endChange();
    } else if (full == U"join") {
        u32 rest = a;
        l1 = clampL(l1);
        l2 = clampL(l2);
        if (!rest.empty() && isDigit(rest[0])) {
            int c = std::stoi(toUtf8(rest));
            l1 = l2;
            l2 = l1 + c - 1;
        } else if (naddr < 2) {
            l2 = l1 + 1;
        }
        if (l2 >= n()) l2 = n() - 1;
        if (l2 <= l1) { if (l1 + 1 >= n()) return; l2 = l1 + 1; }
        beginChange();
        joinLines(l1, l2 - l1 + 1, !bang);
        endChange();
    } else if (full == U">" || full == U"<") {
        int amount = 1;
        size_t k = 0;
        while (k < a.size() && a[k] == name[0]) { amount++; k++; }
        u32 rest = trim(a.substr(k));
        l1 = clampL(l1);
        l2 = clampL(l2);
        if (!rest.empty() && isDigit(rest[0])) { l1 = l2; l2 = clampL(l1 + std::stoi(toUtf8(rest)) - 1); }
        beginChange();
        opShift(l1, l2, name[0] == '>' ? 1 : -1, amount);
        ed.cy = l2;
        toFirstNonBlank(l2);
        endChange();
    } else if (full == U"normal") {
        exNormal(clampL(l1), clampL(l2), naddr > 0, args.empty() ? args : args.substr(args[0] == ' ' ? 1 : 0), bang);
    } else if (full == U"k" || full == U"mark") {
        if (a.empty()) { error(tr(U"E471: Argument required")); return; }
        char32_t m = a[0];
        if (!(isAlpha(m) || m == '\'' || m == '`' || m == '[' || m == ']' || m == '<' || m == '>')) {
            error(tr(U"E191: Argument must be a letter or forward/backward quote"));
            return;
        }
        setMark(m, Pos{clampL(l2), 0});
    } else if (full == U"marks") {
        std::vector<u32> out{tr(U"mark line  col text")};
        std::vector<std::pair<char32_t, Pos>> ms;
        Pos p;
        for (char32_t m : u32(U"'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ\"[]^.<>")) {
            if (!a.empty() && a.find(m) == u32::npos) continue;
            if (m == '\'' && marks.find('\'') == marks.end()) continue;
            if (getMark(m, p)) ms.push_back({m, p});
        }
        for (auto& e : ms) {
            char b[64];
            snprintf(b, sizeof b, " %s %6d %4d ", toUtf8(u32(1, e.first)).c_str(), e.second.y + 1, e.second.x);
            out.push_back(U(b) + line(e.second.y).substr(0, 60));
        }
        host->showLines(tr(U"Marks"), out);
    } else if (full == U"delmarks") {
        if (bang) {
            for (char32_t m = 'a'; m <= 'z'; m++) marks.erase(m);
        } else {
            if (a.empty()) { error(tr(U"E471: Argument required")); return; }
            for (size_t k = 0; k < a.size(); k++) {
                if (k + 2 < a.size() && a[k + 1] == '-') {
                    for (char32_t m = a[k]; m <= a[k + 2]; m++) marks.erase(m);
                    k += 2;
                } else if (!isBlank(a[k])) {
                    marks.erase(a[k]);
                }
            }
        }
    } else if (full == U"registers" || full == U"display") {
        host->showLines(tr(U"Registers"), listRegisters(a));
    } else if (full == U"jumps") {
        std::vector<u32> out{tr(U" jump line  col file/text")};
        for (size_t k = 0; k < jumps.size(); k++) {
            int d = std::abs(int(k) - jumpIdx);
            char b[64];
            snprintf(b, sizeof b, "%s%4d %5d %4d ", int(k) == jumpIdx ? ">" : " ", d, jumps[k].y + 1, jumps[k].x);
            out.push_back(U(b) + (jumps[k].y < n() ? line(jumps[k].y).substr(0, 60) : u32()));
        }
        if (jumpIdx == int(jumps.size())) out.push_back(U">");
        host->showLines(tr(U"Jumps"), out);
    } else if (full == U"changes") {
        std::vector<u32> out{tr(U"change line  col text")};
        for (size_t k = 0; k < changes.size(); k++) {
            int d = std::abs(int(k) - changeIdx);
            char b[64];
            snprintf(b, sizeof b, "%s%5d %5d %4d ", int(k) == changeIdx ? ">" : " ", d, changes[k].y + 1, changes[k].x);
            out.push_back(U(b) + (changes[k].y < n() ? line(changes[k].y).substr(0, 60) : u32()));
        }
        if (changeIdx == int(changes.size())) out.push_back(U">");
        host->showLines(tr(U"Changes"), out);
    } else if (full == U"undo") {
        if (!ed.doUndo()) message(tr(U"Already at oldest change"));
        clampNormal();
    } else if (full == U"redo") {
        if (!ed.doRedo()) message(tr(U"Already at newest change"));
        clampNormal();
    } else if (full == U"write" || full == U"wq" || full == U"xit" || full == U"exit" || full == U"update" ||
               full == U"wall" || full == U"wqall" || full == U"xall") {
        bool append = false;
        u32 fa = a;
        if (fa.size() >= 2 && fa[0] == '>' && fa[1] == '>') { append = true; fa = trim(fa.substr(2)); }
        if (!fa.empty() && fa[0] == '!') {
            if (!shellAllowed()) return;
            // ":w !cmd" sends the lines to a command and shows its output
            int s = naddr ? clampL(l1) : 0, e = naddr ? clampL(l2) : n() - 1;
            std::string in;
            for (int y = s; y <= e; y++) in += toUtf8(line(y)) + "\n";
            showShellOutput(U":w !" + fa.substr(1), toUtf8(fa.substr(1)), in);
            return;
        }
        bool onlyIfModified = full == U"xit" || full == U"exit" || full == U"update" || full == U"xall";
        bool quitAfter = full == U"wq" || full == U"xit" || full == U"exit" || full == U"wqall" || full == U"xall";
        bool whole = naddr == 0;
        if (!onlyIfModified || ed.modified || (!fa.empty())) {
            if (!exWrite(toUtf8(fa), whole ? 0 : clampL(l1), whole ? n() - 1 : clampL(l2), whole, bang, append)) return;
        }
        if (full == U"wall" || full == U"wqall" || full == U"xall") {
            // the other buffers
            if (!layoutCmd("wall", 0)) return;
            if (full != U"wall") layoutCmd("qall", 0, u32(), true);
        } else if (quitAfter) {
            layoutCmd("quit", 0, u32(), true);
        }
    } else if (full == U"quit") {
        if (ed.modified && !bang && !host->bufferShownElsewhere()) {
            error(tr(U"E37: No write since last change (add ! to override)"));
            return;
        }
        layoutCmd("quit", 0, u32(), bang);
    } else if (full == U"qall" || full == U"quitall") {
        layoutCmd("qall", 0, u32(), bang);
    } else if (full == U"vertical") {
        // ":vertical {cmd}": a split side by side or a change of the width
        u32 inner = trim(args);
        size_t k = 0;
        while (k < inner.size() && isAlpha(inner[k])) k++;
        u32 nm = resolveName(inner.substr(0, k));
        u32 rest = trim(inner.substr(k));
        if (nm == U"split") layoutCmd("vsplit", 0, rest);
        else if (nm == U"new") layoutCmd("vnew", 0, rest);
        else if (nm == U"resize") {
            if (rest.empty()) layoutCmd("width", 0);
            else if (rest[0] == '+')
                layoutCmd("wider", std::stoi(toUtf8(rest.substr(1).empty() ? U"1" : rest.substr(1))));
            else if (rest[0] == '-')
                layoutCmd("narrower", std::stoi(toUtf8(rest.substr(1).empty() ? U"1" : rest.substr(1))));
            else layoutCmd("width", std::stoi(toUtf8(rest)));
        } else {
            exCommand(inner);
        }
    } else if (full == U"split" || full == U"vsplit" || full == U"new" || full == U"vnew") {
        layoutCmd(toUtf8(full), naddr ? l2 + 1 : 0, a);
    } else if (full == U"close") {
        layoutCmd("close", 0, u32(), bang);
    } else if (full == U"only") {
        layoutCmd("only", 0, u32(), bang);
    } else if (full == U"resize") {
        if (a.empty()) layoutCmd("height", naddr ? l2 + 1 : 0);
        else if (a[0] == '+' || a[0] == '-') {
            u32 num = a.substr(1);
            int v = num.empty() ? 1 : std::stoi(toUtf8(num));
            layoutCmd(a[0] == '+' ? "taller" : "shorter", v);
        } else if (isDigit(a[0])) {
            layoutCmd("height", std::stoi(toUtf8(a)));
        } else {
            error(tr(U"E474: Invalid argument: ") + a);
        }
    } else if (full == U"Format") {
        host->format();
    } else if (full == U"wincmd") {
        if (a.empty()) { error(tr(U"E471: Argument required")); return; }
        winCmd(a[0], naddr ? l2 + 1 : 0);
    } else if (full == U"tabedit" || full == U"tabnew") {
        layoutCmd("tabnew", 0, a);
    } else if (full == U"tabclose" || full == U"tabonly") {
        int nr = !a.empty() && isDigit(a[0]) ? std::stoi(toUtf8(a)) : (naddr ? l2 + 1 : 0);
        layoutCmd(toUtf8(full), nr, u32(), bang);
    } else if (full == U"tabnext") {
        int nr = !a.empty() && isDigit(a[0]) ? std::stoi(toUtf8(a)) : 0;
        layoutCmd("tabnext", nr);
    } else if (full == U"tabprevious" || full == U"tabNext") {
        int nr = !a.empty() && isDigit(a[0]) ? std::stoi(toUtf8(a)) : 0;
        layoutCmd("tabprev", nr);
    } else if (full == U"tabfirst" || full == U"tabrewind") {
        layoutCmd("tabfirst", 0);
    } else if (full == U"tablast") {
        layoutCmd("tablast", 0);
    } else if (full == U"tabs") {
        layoutCmd("tabs", 0);
    } else if (full == U"tabmove") {
        layoutCmd("tabmove", 0, a);
    } else if (full == U"edit") {
        exEdit(toUtf8(a), bang);
    } else if (full == U"enew") {
        bool elsewhere = host->bufferShownElsewhere();
        if (ed.modified && !bang && !elsewhere) {
            error(tr(U"E37: No write since last change (add ! to override)"));
            return;
        }
        if (elsewhere) layoutCmd("newbuf", 0);  // the other window keeps the buffer
        ed = Editor();
        reset();
        host->bufferLoaded();
    } else if (full == U"saveas") {
        if (a.empty()) { error(tr(U"E471: Argument required")); return; }
        std::string p = absPath(toUtf8(a));
        if (fileExists(p) && !bang) { error(tr(U"E13: File exists (add ! to override)")); return; }
        std::string old = ed.filename;
        ed.filename.clear();
        if (!exWrite(p, 0, n() - 1, true, true, false)) { ed.filename = old; return; }
        host->bufferLoaded();
    } else if (full == U"read") {
        exRead(naddr ? clampL(l2) : ed.cy, a);
        if (naddr && l2 < 0) {}
    } else if (full == U"file" || full == U"files" || full == U"ls" || full == U"buffers") {
        if (full == U"file" && !a.empty()) {
            ed.filename = absPath(toUtf8(a));
            ed.modified = true;
            host->bufferLoaded();
        }
        u32 nm = ed.filename.empty() ? u32(tr(U"[No Name]")) : U(ed.filename);
        if (full != U"file") {
            message(U"  1 %a   \"" + nm + U"\" " + fmtU(trs("line %d").c_str(), ed.cy + 1));
            return;
        }
        int pct = (ed.cy + 1) * 100 / std::max(1, n());
        message(U"\"" + nm + U"\"" + (ed.modified ? u32(tr(U" [Modified]")) : u32()) + U" " +
                fmtU((n() == 1 ? trs("%d line") : trs("%d lines")).c_str(), n()) + U" --" + fmtU("%d", pct) + U"%--");
    } else if (full == U"set" || full == U"setlocal" || full == U"setglobal") {
        exSet(a);
    } else if (full == U"nohlsearch") {
        hlOn = false;
    } else if (full == U"sort") {
        exSort(naddr ? clampL(l1) : 0, naddr ? clampL(l2) : n() - 1, a, bang);
    } else if (full == U"retab") {
        int ts = S.tabw;
        if (!a.empty() && isDigit(a[0])) {
            int v = std::stoi(toUtf8(a));
            if (v < 1 || v > 32) { error(tr(U"E487: Argument must be positive")); return; }
            ts = v;
        }
        int s = naddr ? clampL(l1) : 0, e = naddr ? clampL(l2) : n() - 1;
        beginChange();
        for (int y = s; y <= e; y++) {
            u32 l = line(y), out;
            int col = 0;
            size_t k = 0;
            while (k < l.size()) {
                if (l[k] == ' ' || l[k] == '\t') {
                    // a run of white space
                    int start = col;
                    bool hasTab = false;
                    size_t j = k;
                    while (j < l.size() && (l[j] == ' ' || l[j] == '\t')) {
                        if (l[j] == '\t') { hasTab = true; col += S.tabw - col % S.tabw; }
                        else col++;
                        j++;
                    }
                    if (S.expandtab) {
                        out += u32(size_t(col - start), ' ');
                    } else if (hasTab || bang) {
                        int c = start;
                        while ((c / ts + 1) * ts <= col && col - c > 1) { out += '\t'; c = (c / ts + 1) * ts; }
                        if ((c / ts + 1) * ts <= col && col - c == 1 && (c / ts + 1) * ts == col && hasTab) {
                            out += '\t';
                            c = col;
                        }
                        out += u32(size_t(col - c), ' ');
                    } else {
                        out += l.substr(k, j - k);
                    }
                    k = j;
                } else {
                    col += cw(l[k]);
                    out += l[k++];
                }
            }
            setLine(y, out);
        }
        S.tabw = ts;
        endChange();
    } else if (full == U"left" || full == U"right" || full == U"center") {
        int w = full == U"left" ? 0 : (opt.textwidth > 0 ? opt.textwidth : 80);
        if (!a.empty() && isDigit(a[0])) w = std::stoi(toUtf8(a));
        int s = clampL(l1), e = clampL(l2);
        beginChange();
        for (int y = s; y <= e; y++) {
            u32 t = trim(line(y));
            if (t.empty()) { setLine(y, u32()); continue; }
            int tw = strWidth(t);
            int ind = full == U"left" ? w : full == U"right" ? std::max(0, w - tw) : std::max(0, (w - tw) / 2);
            setLine(y, makeIndent(ind) + t);
        }
        endChange();
    } else if (full.find(U"map") != u32::npos || full.find(U"noremap") != u32::npos) {
        exMap(full, args, bang);
    } else if (full == U"help") {
        // Vim's easter eggs (in English, as in Vim's help), otherwise Edith's help
        if (bang && a.empty()) error(U"E478: Don't panic!");
        else if (a == U"42")
            host->showLines(U":help 42", {U"42 is the answer to the ultimate question of life, the universe",
                                          U"and everything. Sadly, nobody remembers the question.",
                                          U"Don't forget your towel."});
        else if (a == U"holy-grail") message(U"You found it, Arthur!");
        else if (a == U"spoon") message(U"There is no spoon.");
        else if (a == U"UserGettingBored") message(U"When the user presses the same key 42 times. Just kidding! :-)");
        else host->help();
    } else if (full == U"smile") {
        host->showLines(U":smile", {
            U"           .-''''''''''-.",
            U"        .'                '.",
            U"       /    __        __    \\",
            U"      |    (__)      (__)    |",
            U"      |                      |",
            U"      |   \\              /   |",
            U"       \\   '.          .'   /",
            U"        '.   '--------'   .'",
            U"          '-.__________.-'",
            U"",
            U"   Edith smiles back. Happy editing!",
        });
    } else if (full == U"syntax") {
        if (a == U"on" || a == U"enable") host->setSyntax(true);
        else if (a == U"off" || a == U"clear") host->setSyntax(false);
        else if (!a.empty()) error(tr(U"E410: Invalid :syntax subcommand: ") + a);
    } else if (full == U"filetype") {
        // detection is always on
    } else if (full == U"pwd") {
        message(U(getCwd()));
    } else if (full == U"version") {
        message(tr(U"Edith with Vim emulation"));
    } else if (full == U"startinsert") {
        if (bang) ed.cx = len(ed.cy);
        startInsert(bang ? 'A' : 'i', 1);
    } else if (full == U"stopinsert") {
        // only meaningful in mappings and scripts
    } else if (full == U"let") {
        size_t eq = a.find('=');
        if (eq == u32::npos) { error(tr(U"E15: Invalid expression: ") + a); return; }
        u32 var = trim(a.substr(0, eq)), val = trim(a.substr(eq + 1));
        if (val.size() >= 2 && (val[0] == '"' || val[0] == '\'') && val.back() == val[0])
            val = val.substr(1, val.size() - 2);
        if (var == U"mapleader" || var == U"g:mapleader") leader = vimParseKeys(val);
        else if (var == U"maplocalleader" || var == U"g:maplocalleader") {}
        else { error(tr(U"E121: Undefined variable: ") + var); return; }
    } else if (full == U"echo") {
        u32 v = a;
        if (v.size() >= 2 && (v[0] == '"' || v[0] == '\'') && v.back() == v[0]) v = v.substr(1, v.size() - 2);
        message(v);
    } else if (full == U"execute") {
        // only string literals joined by spaces, e.g. :exe "normal ihello\<Esc>"
        u32 out;
        size_t k = 0;
        while (k < a.size()) {
            if (a[k] == '"') {
                k++;
                while (k < a.size() && a[k] != '"') {
                    if (a[k] == '\\' && k + 1 < a.size()) {
                        char32_t e = a[k + 1];
                        if (e == '<') {
                            size_t c = a.find('>', k + 2);
                            if (c != u32::npos) {
                                out += vimParseKeys(a.substr(k + 1, c - k));
                                k = c + 1;
                                continue;
                            }
                        }
                        out += e == 'n'   ? char32_t('\n')
                               : e == 't' ? char32_t('\t')
                               : e == 'e' ? char32_t(27)
                               : e == 'r' ? char32_t('\r')
                                          : e;
                        k += 2;
                        continue;
                    }
                    out += a[k++];
                }
                k++;
            } else if (a[k] == '\'') {
                k++;
                while (k < a.size() && a[k] != '\'') out += a[k++];
                k++;
            } else {
                k++;
            }
        }
        exCommand(out);
    } else if (full == U"redraw") {
        host->redraw();
    } else if (full == U"print" || full == U"number" || full == U"#" || full == U"list") {
        std::vector<u32> out;
        bool num = full != U"print" && full != U"list";
        for (int y = clampL(l1); y <= clampL(l2); y++) out.push_back(num ? fmtU("%3d ", y + 1) + line(y) : line(y));
        if (inGlobal) printBuf.insert(printBuf.end(), out.begin(), out.end());
        else if (out.size() == 1) message(out[0]);
        else host->showLines(tr(U"Lines"), out);
        toFirstNonBlank(clampL(l2));
    } else if (full == U"=") {
        message(fmtU("%d", naddr ? l2 + 1 : n()));
    } else if (full == U"!") {
        if (!shellAllowed()) return;
        u32 c = args;
        // "!" in the command is replaced by the previous command
        u32 expanded;
        for (size_t k = 0; k < c.size(); k++) {
            if (c[k] == '\\' && k + 1 < c.size() && c[k + 1] == '!') { expanded += '!'; k++; continue; }
            if (c[k] == '!') { expanded += lastBangCmd; continue; }
            expanded += c[k];
        }
        lastBangCmd = expanded;
        if (naddr) {
            exFilter(clampL(l1), clampL(l2), toUtf8(expanded));
        } else {
            showShellOutput(U":!" + expanded, toUtf8(expanded), "");
        }
    } else if (full == U"silent") {
        exCommand(args);
        if (!msgErr) msg.clear();
    } else {
        error(tr(U"E492: Not an editor command: ") + trim(cmd));
        return;
    }
    if (!next.empty() && !msgErr) exCommand(next);
}

u32 Vim::expandReplacement(const u32& rep, const RxMatch& m, const std::vector<u32>& L) const {
    auto groupText = [&](int g) -> u32 {
        if (!m.g[g]) return u32();
        Pos a = m.gs[g], b = m.ge[g];
        u32 r;
        for (int y = a.y; y <= b.y && y < int(L.size()); y++) {
            int s = y == a.y ? a.x : 0, e = y == b.y ? b.x : int(L[size_t(y)].size());
            if (e > s) r += L[size_t(y)].substr(size_t(s), size_t(e - s));
            if (y < b.y) r += '\r';  // line break
        }
        return r;
    };
    u32 out;
    int caseMode = 0, one = 0;
    auto add = [&](const u32& t) {
        for (char32_t c : t) {
            if (c == '\r') { out += c; continue; }
            char32_t r = c;
            if (one == 1) {
                r = (c >= 'a' && c <= 'z')                                  ? c - 32
                    : (fold(c) == c && c >= 0xE0 && c <= 0xFE && c != 0xF7) ? c - 32
                                                                            : c;
                one = 0;
            } else if (one == 2) {
                r = fold(c);
                one = 0;
            } else if (caseMode == 1)
                r = (c >= 'a' && c <= 'z') ? c - 32 : (c >= 0xE0 && c <= 0xFE && c != 0xF7) ? c - 32 : c;
            else if (caseMode == 2) r = fold(c);
            out += r;
        }
    };
    for (size_t i = 0; i < rep.size(); i++) {
        char32_t c = rep[i];
        if (c == '\\' && i + 1 < rep.size()) {
            char32_t d = rep[++i];
            if (isDigit(d)) add(groupText(int(d - '0')));
            else if (d == 'n') out += char32_t(0);
            else if (d == 'r') out += '\r';
            else if (d == 't') add(U"\t");
            else if (d == 'u') one = 1;
            else if (d == 'l') one = 2;
            else if (d == 'U') caseMode = 1;
            else if (d == 'L') caseMode = 2;
            else if (d == 'e' || d == 'E') { caseMode = 0; one = 0; }
            else if (d == '\r') add(u32(1, char32_t(13)) == U"\r" ? u32() : u32());
            else add(u32(1, d));
        } else if (c == '&') {
            add(groupText(0));
        } else {
            add(u32(1, c));
        }
    }
    return out;
}

void Vim::exSubstitute(int l1, int l2, const u32& args, char32_t name) {
    u32 pat, rep, flagStr;
    size_t i = 0;
    bool newPattern = !args.empty() && !isAlpha(args[0]) && !isDigit(args[0]) && !isBlank(args[0]) &&
                      args[0] != '&' && args[0] != '"' && args[0] != '|' && args[0] != '\\' && name == 's';
    if (newPattern) {
        char32_t delim = args[0];
        i = 1;
        bool found;
        pat = takeUntil(args, i, delim, found);
        // remove the escaping of the delimiter
        u32 cleaned;
        for (size_t k = 0; k < pat.size(); k++) {
            if (pat[k] == '\\' && k + 1 < pat.size() && pat[k + 1] == delim && delim != '\\') continue;
            cleaned += pat[k];
        }
        pat = cleaned;
        if (found) {
            rep = takeUntil(args, i, delim, found);
            u32 r2;
            for (size_t k = 0; k < rep.size(); k++) {
                if (rep[k] == '\\' && k + 1 < rep.size() && rep[k + 1] == delim && delim != '\\' && delim != '&') {
                    r2 += delim;
                    k++;
                    continue;
                }
                r2 += rep[k];
            }
            rep = r2;
            flagStr = args.substr(i);
        }
        if (pat.empty()) pat = lastPat;
        if (pat.empty()) { error(tr(U"E35: No previous regular expression")); return; }
        // "~" in the replacement is the previous replacement string
        u32 r3;
        for (size_t k = 0; k < rep.size(); k++) {
            if (rep[k] == '\\' && k + 1 < rep.size()) { r3 += rep[k]; r3 += rep[k + 1]; k++; continue; }
            if (rep[k] == '~') { r3 += lastSubRep; continue; }
            r3 += rep[k];
        }
        rep = r3;
        lastSubPat = pat;
        lastSubRep = rep;
        lastSubSet = true;
        g_rxLastSub = rep;
        lastPat = pat;
        hlOn = true;
    } else {
        if (!lastSubSet) { error(tr(U"E35: No previous regular expression")); return; }
        pat = name == '~' ? lastPat : lastSubPat;
        rep = lastSubRep;
        flagStr = args;
        if (name == '~') lastSubPat = pat;
    }
    // flags
    bool g = false, conf = false, noErr = false, countOnly = false, ic = false, noIc = false, keep = false;
    size_t k = 0;
    if (k < flagStr.size() && flagStr[k] == '&') {
        keep = true;
        k++;
    }
    u32 flags;
    if (keep) flags = lastSubFlags;
    while (k < flagStr.size() && (isAlpha(flagStr[k]) || flagStr[k] == '#')) flags += flagStr[k++];
    for (char32_t f : flags) {
        switch (f) {
            case 'g': g = !g; break;
            case 'c': conf = true; break;
            case 'e': noErr = true; break;
            case 'n': countOnly = true; break;
            case 'i': ic = true; noIc = false; break;
            case 'I': noIc = true; ic = false; break;
            case 'r': case 'p': case '#': case 'l': break;
            default: error(tr(U"E488: Trailing characters: ") + flagStr); return;
        }
    }
    if (opt.gdefault) g = !g;
    lastSubFlags = flags;
    u32 rest = trim(flagStr.substr(k));
    if (!rest.empty() && isDigit(rest[0])) {
        int c = std::stoi(toUtf8(rest));
        if (c <= 0) { error(tr(U"E939: Positive count required")); return; }
        l1 = l2;
        l2 = std::min(n() - 1, l1 + c - 1);
    } else if (!rest.empty() && rest[0] != '"') {
        error(tr(U"E488: Trailing characters: ") + rest);
        return;
    }
    VimRegex rx;
    bool icase = ic ? true : noIc ? false : opt.ignorecase;
    if (!rx.compile(pat, icase, !ic && !noIc && opt.smartcase)) { error(rx.err); return; }
    beginChange();
    int count = 0, linesChanged = 0, lastLine = -1;
    bool stop = false;
    for (int y = l1; y <= l2 && y < n() && !stop; y++) {
        int x = 0;
        int skipEmptyAt = -1;
        bool lineCounted = false;
        while (!stop) {
            RxMatch m;
            if (x > len(y) || !rx.findInLine(ed.lines, y, x, m) || m.s.y != y) break;
            bool empty = m.s == m.e;
            if (empty && m.s.x == skipEmptyAt) {
                x = m.s.x + 1;
                continue;
            }
            if (conf) {
                ed.cy = m.s.y;
                ed.cx = m.s.x;
                ed.anch = true;
                ed.anchor = m.s;
                Pos sv = {ed.cy, ed.cx};
                ed.cy = m.e.y;
                ed.cx = std::max(m.s.x, m.e.x - 1);
                int ans = host->confirm(tr(U"replace with ") + rep + U" (y/n/a/q/l)?");
                ed.anch = false;
                ed.cy = sv.y;
                ed.cx = sv.x;
                if (ans == 'q' || ans == 27) { stop = true; break; }
                if (ans == 'n') {
                    x = empty ? m.s.x + 1 : m.e.y == y ? m.e.x : len(y) + 1;
                    if (!g) break;
                    continue;
                }
                if (ans == 'a') conf = false;
                if (ans == 'l') stop = true;
            }
            count++;
            if (!lineCounted) { linesChanged++; lineCounted = true; }
            lastLine = y;
            if (countOnly) {
                x = empty ? m.s.x + 1 : (m.e.y == y ? m.e.x : len(y) + 1);
                if (!empty) skipEmptyAt = x;
                if (!g) break;
                continue;
            }
            u32 r = expandReplacement(rep, m, ed.lines);
            for (auto& ch : r)
                if (ch == '\r') ch = '\n';
            int linesBefore = n();
            bool joined = m.e.y > m.s.y;
            delText(m.s, m.e);
            insText(m.s, r);
            Pos after{ed.cy, ed.cx};
            l2 += n() - linesBefore;
            y = after.y;
            lastLine = y;
            if (empty) {
                if (after.x >= len(y)) break;
                x = after.x + 1;
            } else {
                x = after.x;
                skipEmptyAt = x;
            }
            // like Vim: a line joined by the match is searched again even without "g"
            if (!g && !joined) break;
        }
    }
    endChange();
    if (count == 0) {
        if (!noErr) error(tr(U"E486: Pattern not found: ") + pat);
        return;
    }
    if (countOnly) {
        message(fmtU((count == 1 ? trs("%d match on %d line") : trs("%d matches on %d lines")).c_str(), count,
                     linesChanged));
        return;
    }
    toFirstNonBlank(std::clamp(lastLine, 0, n() - 1));
    recordChange(Pos{ed.cy, ed.cx});
    if (count > opt.report && !inGlobal)
        message(fmtU((count == 1 ? trs("%d substitution on %d line") : trs("%d substitutions on %d lines")).c_str(),
                     count, linesChanged));
}

void Vim::exGlobal(int l1, int l2, const u32& args, bool invert) {
    if (inGlobal) { error(tr(U"E147: Cannot do :global recursive")); return; }
    if (args.empty()) { error(tr(U"E35: No previous regular expression")); return; }
    char32_t delim = args[0];
    size_t i = 1;
    bool found;
    u32 pat = takeUntil(args, i, delim, found);
    u32 cleaned;
    for (size_t k = 0; k < pat.size(); k++) {
        if (pat[k] == '\\' && k + 1 < pat.size() && pat[k + 1] == delim) continue;
        cleaned += pat[k];
    }
    pat = cleaned;
    u32 cmd = found ? args.substr(i) : u32();
    if (trim(cmd).empty()) cmd = U"p";
    if (pat.empty()) pat = lastPat;
    VimRegex rx;
    if (!compilePat(pat, rx)) return;
    lastPat = pat;
    hlOn = true;
    std::vector<int> marked;
    for (int y = l1; y <= l2; y++)
        if (rx.lineMatches(ed.lines, y) != invert) marked.push_back(y);
    if (marked.empty()) {
        if (invert) message(tr(U"Pattern found in every line: ") + pat);
        else message(tr(U"Pattern not found: ") + pat);
        return;
    }
    inGlobal = true;
    printBuf.clear();
    trackLines = &marked;
    beginChange();
    for (size_t k = 0; k < marked.size(); k++) {
        int y = marked[k];
        if (y < 0 || y >= n()) continue;
        ed.cy = y;
        ed.cx = 0;
        exCommand(cmd);
        if (msgErr) break;
    }
    endChange();
    trackLines = nullptr;
    inGlobal = false;
    if (mode == VM_NORMAL) clampNormal();
    if (!printBuf.empty()) {
        if (printBuf.size() == 1) message(printBuf[0]);
        else host->showLines(tr(U"Lines"), printBuf);
        printBuf.clear();
    }
}

void Vim::exNormal(int l1, int l2, bool hasRange, const u32& args, bool bang) {
    if (args.empty()) { error(tr(U"E471: Argument required")); return; }
    if (execDepth > 50) { error(tr(U"E169: Command too recursive")); return; }
    beginChange();
    if (!hasRange) {
        runKeys(args, !bang);
    } else {
        std::vector<int> ls;
        for (int y = l1; y <= l2; y++) ls.push_back(y);
        std::vector<int>* old = trackLines;
        trackLines = &ls;
        for (size_t k = 0; k < ls.size(); k++) {
            if (ls[k] < 0 || ls[k] >= n()) continue;
            ed.cy = ls[k];
            ed.cx = 0;
            curswant = 0;
            runKeys(args, !bang);
        }
        trackLines = old;
    }
    endChange();
}

void Vim::exSort(int l1, int l2, const u32& args, bool bang) {
    bool num = false, ic = false, uniq = false, hex = false, useMatch = false;
    u32 pat;
    for (size_t k = 0; k < args.size(); k++) {
        char32_t c = args[k];
        if (c == 'n') num = true;
        else if (c == 'i') ic = true;
        else if (c == 'u') uniq = true;
        else if (c == 'x') hex = true;
        else if (c == 'r') useMatch = true;
        else if (c == 'f' || c == 'b' || c == 'o') num = true;
        else if (c == ' ') continue;
        else if (!isAlpha(c) && !isDigit(c) && c != '"') {
            size_t i = k + 1;
            bool found;
            pat = takeUntil(args, i, c, found);
            if (pat.empty()) pat = lastPat;
            k = i - 1;
        } else {
            error(tr(U"E474: Invalid argument"));
            return;
        }
    }
    VimRegex rx;
    if (!pat.empty() && !compilePat(pat, rx)) return;
    struct Item {
        u32 text, key;
        bool hasNum = false;
        long long v = 0;
        size_t idx;
    };
    std::vector<Item> items;
    for (int y = l1; y <= l2; y++) {
        Item it;
        it.text = line(y);
        it.idx = size_t(y);
        it.key = it.text;
        if (!pat.empty()) {
            RxMatch m;
            if (rx.findInLine(ed.lines, y, 0, m) && m.s.y == y) {
                int e = m.e.y == y ? m.e.x : len(y);
                it.key = useMatch ? it.text.substr(size_t(m.s.x), size_t(e - m.s.x)) : it.text.substr(size_t(e));
            } else if (useMatch) {
                it.key.clear();
            }
        }
        if (num || hex) {
            const u32& s = it.key;
            for (size_t i = 0; i < s.size(); i++) {
                bool d = hex ? ((s[i] >= '0' && s[i] <= '9') || ((s[i] | 0x20) >= 'a' && (s[i] | 0x20) <= 'f'))
                             : isDigit(s[i]);
                if (!d) continue;
                if (hex && i + 1 < s.size() && s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X')) i += 2;
                bool neg = !hex && i > 0 && s[i - 1] == '-';
                long long v = 0;
                while (i < s.size()) {
                    char32_t c = s[i];
                    int dv;
                    if (isDigit(c)) dv = int(c - '0');
                    else if (hex && (c | 0x20) >= 'a' && (c | 0x20) <= 'f') dv = int((c | 0x20) - 'a' + 10);
                    else break;
                    v = v * (hex ? 16 : 10) + dv;
                    if (v > (1LL << 60)) break;
                    i++;
                }
                it.v = neg ? -v : v;
                it.hasNum = true;
                break;
            }
        }
        if (ic) for (auto& c : it.key) c = fold(c);
        items.push_back(it);
    }
    std::stable_sort(items.begin(), items.end(), [&](const Item& a, const Item& b) {
        if (num || hex) {
            if (a.hasNum != b.hasNum) return !a.hasNum;
            return a.v < b.v;
        }
        return a.key < b.key;
    });
    if (bang) std::reverse(items.begin(), items.end());
    std::vector<u32> out;
    for (size_t k = 0; k < items.size(); k++) {
        if (uniq && k > 0) {
            bool same = (num || hex) ? (items[k].hasNum == items[k - 1].hasNum && items[k].v == items[k - 1].v &&
                                        (items[k].hasNum || items[k].key == items[k - 1].key))
                                     : items[k].key == items[k - 1].key;
            if (same) continue;
        }
        out.push_back(items[k].text);
    }
    beginChange();
    replaceLines(l1, l2, out);
    endChange();
    setCursor(l1, 0);
}

bool Vim::shellAllowed() {
    if (!restricted) return true;
    error(tr(U"E145: Shell commands and some functionality not allowed in rvim"));
    return false;
}

bool Vim::exWrite(const std::string& path, int l1, int l2, bool all, bool force, bool append) {
    if (!opt.write) {
        error(tr(U"E142: File not written: Writing is disabled by 'write' option"));
        return false;
    }
    std::string target = path.empty() ? ed.filename : absPath(path);
    if (target.empty()) { error(tr(U"E32: No file name")); return false; }
    bool isCurrent = target == ed.filename;
    bool same = isCurrent || ed.filename.empty();  // ":w name" names an unnamed buffer
    bool isNew = !fileExists(target);
    if (!isCurrent && !isNew && !force && !append) { error(tr(U"E13: File exists (add ! to override)")); return false; }
    std::string err;
    bool conv = false;
    long bytes = 0;
    int nl = l2 - l1 + 1;
    for (int y = l1; y <= l2; y++) bytes += long(toUtf8(line(y)).size()) + (ed.crlf ? 2 : 1);
    if (all && same && !append) {
        if (!ed.save(target, err, conv)) { error(tr(U"E212: Can't open file for writing: ") + U(err)); return false; }
    } else {
        Editor e;
        e.crlf = ed.crlf;
        e.bom = ed.bom && !append;
        e.latin1 = ed.latin1;
        e.lines.clear();
        if (append && !isNew) {
            Editor old;
            if (old.load(target, err)) e.lines = old.lines;
            if (!e.lines.empty() && e.lines.back().empty()) e.lines.pop_back();
        }
        for (int y = l1; y <= l2; y++) e.lines.push_back(line(y));
        e.lines.push_back(u32());  // the file ends with a line break
        if (!e.save(target, err, conv)) { error(tr(U"E212: Can't open file for writing: ") + U(err)); return false; }
        if (all && ed.filename.empty()) { ed.filename = target; ed.modified = false; }
    }
    u32 nm = U(baseOf(target));
    message(U"\"" + nm + U"\" " + (isNew ? u32(tr(U"[New] ")) : u32()) + fmtU("%dL, ", nl) + fmtU("%dB", int(bytes)) +
            u32(append ? tr(U" appended") : tr(U" written")));
    if (conv) message(msg + tr(U" [converted to UTF-8]"));
    host->bufferLoaded();
    return true;
}

bool Vim::exEdit(const std::string& path, bool force) {
    std::string p = path.empty() ? ed.filename : absPath(path);
    if (p.empty()) { error(tr(U"E32: No file name")); return false; }
    bool elsewhere = host->bufferShownElsewhere();
    if (ed.modified && !force && !elsewhere) {
        error(tr(U"E37: No write since last change (add ! to override)"));
        return false;
    }
    // the other window keeps the buffer, this one gets a new one
    if (elsewhere) layoutCmd("newbuf", 0);
    if (fileExists(p)) {
        Editor e;
        std::string err;
        if (!e.load(p, err)) { error(tr(U"E484: Can't open file ") + U(baseOf(p)) + U": " + U(err)); return false; }
        ed = std::move(e);
        long bytes = 0;
        for (auto& l : ed.lines) bytes += long(toUtf8(l).size()) + 1;
        message(U"\"" + U(baseOf(p)) + U"\" " + fmtU("%dL, ", n()) + fmtU("%dB", int(bytes)));
    } else {
        ed = Editor();
        ed.filename = p;
        message(U"\"" + U(baseOf(p)) + U"\" " + tr(U"[New]"));
    }
    u32 m = msg;
    reset();
    msg = m;
    host->bufferLoaded();
    return true;
}

static std::vector<u32> splitOutput(const std::string& out) {
    std::vector<u32> ls;
    u32 t = U(out);
    size_t s = 0;
    while (s < t.size()) {
        size_t q = t.find('\n', s);
        if (q == u32::npos) q = t.size();
        ls.push_back(t.substr(s, q - s));
        s = q + 1;
    }
    return ls;
}

void Vim::showShellOutput(const u32& title, const std::string& cmd, const std::string& input) {
    std::string out;
    int st = 0;
    if (!runShell(cmd, input, out, st)) { error(tr(U"E482: Can't create file")); return; }
    std::vector<u32> ls = splitOutput(out);
    if (st != 0) ls.push_back(fmtU(trs("shell returned %d").c_str(), st));
    if (!ls.empty()) host->showLines(title, ls);
}

void Vim::exFilter(int l1, int l2, const std::string& cmd) {
    if (!shellAllowed()) return;
    std::string in;
    for (int y = l1; y <= l2; y++) in += toUtf8(line(y)) + "\n";
    std::string out;
    int st = 0;
    if (!runShell(cmd, in, out, st)) { error(tr(U"E482: Can't create file")); return; }
    std::vector<u32> ls = splitOutput(out);
    beginChange();
    replaceLines(l1, l2, ls);
    endChange();
    toFirstNonBlank(std::min(l1, n() - 1));
    if (int(ls.size()) > opt.report || l2 - l1 + 1 > opt.report)
        message(fmtU(trs("%d lines filtered").c_str(), l2 - l1 + 1));
}

void Vim::exRead(int after, const u32& arg) {
    std::vector<u32> ls;
    u32 a = trim(arg);
    if (!a.empty() && a[0] == '!') {
        if (!shellAllowed()) return;
        std::string out;
        int st = 0;
        if (!runShell(toUtf8(a.substr(1)), "", out, st)) { error(tr(U"E482: Can't create file")); return; }
        ls = splitOutput(out);
    } else {
        std::string p = a.empty() ? ed.filename : absPath(toUtf8(a));
        Editor e;
        std::string err;
        if (p.empty() || !e.load(p, err)) { error(tr(U"E484: Can't open file ") + a); return; }
        ls = e.lines;
        if (ls.size() > 1 && ls.back().empty()) ls.pop_back();
    }
    if (ls.empty()) return;
    beginChange();
    int y = std::clamp(after + 1, 0, n());
    insLines(y, ls);
    toFirstNonBlank(y);
    endChange();
}

void Vim::exSet(const u32& args) {
    struct BoolOpt { const char* name; const char* abbr; bool* p; };
    bool nu = S.lineNums == 1 || (S.lineNums == 2 && numberAlso), rnu = S.lineNums == 2;
    bool syn = S.syntax;
    BoolOpt bools[] = {
        {"expandtab", "et", &S.expandtab}, {"autoindent", "ai", &S.autoindent}, {"number", "nu", &nu},
        {"relativenumber", "rnu", &rnu}, {"ignorecase", "ic", &opt.ignorecase}, {"smartcase", "scs", &opt.smartcase},
        {"wrapscan", "ws", &opt.wrapscan}, {"hlsearch", "hls", &opt.hlsearch}, {"incsearch", "is", &opt.incsearch},
        {"joinspaces", "js", &opt.joinspaces}, {"write", "write", &opt.write}, {"startofline", "sol", &opt.startofline},
        {"shiftround", "sr", &opt.shiftround}, {"tildeop", "top", &opt.tildeop}, {"gdefault", "gd", &opt.gdefault},
        {"timeout", "to", &opt.timeout}, {"list", "list", &S.invisibles},
    };
    struct NumOpt { const char* name; const char* abbr; int* p; int lo, hi; };
    NumOpt nums[] = {
        {"tabstop", "ts", &S.tabw, 1, 32}, {"shiftwidth", "sw", &opt.shiftwidth, 0, 32},
        {"textwidth", "tw", &opt.textwidth, 0, 10000}, {"scrolloff", "so", &opt.scrolloff, 0, 999},
        {"timeoutlen", "tm", &opt.timeoutlen, 0, 100000}, {"report", "report", &opt.report, 0, 100000},
    };
    struct StrOpt { const char* name; const char* abbr; std::string* p; };
    std::string ft, synStr;
    StrOpt strs[] = {
        {"clipboard", "cb", &opt.clipboard}, {"nrformats", "nf", &opt.nrformats}, {"filetype", "ft", &ft},
        {"syntax", "syn", &synStr},
    };
    static const char* const IGNORED[] = {"wrap", "ruler", "showcmd", "showmode", "compatible", "cp",
                                          "hidden", "lazyredraw", "lz", "magic", "backup", "swapfile", "undofile",
                                          "modeline", "ml", "cursorline", "cul", "smartindent", "si", "cindent",
                                          "cin", "smarttab", "sta", "paste", "mouse", "encoding", "enc",
                                          "fileencoding", "fenc", "softtabstop", "sts", "laststatus", "ls",
                                          "background", "bg", "termguicolors", "tgc", "belloff", "bo", "wildmenu",
                                          "wmnu", "visualbell", "vb", "errorbells", "eb", "spell", "foldenable",
                                          "fen", "linebreak", "lbr", "showmatch", "sm", "autoread", "ar",
                                          "writebackup", "wb", "title", "ttimeout", "ttimeoutlen", "ttm",
                                          "listchars", "lcs"};
    std::vector<u32> shown;
    auto items = std::vector<u32>();
    {
        u32 cur;
        for (size_t i = 0; i < args.size(); i++) {
            if (args[i] == '\\' && i + 1 < args.size()) { cur += args[i + 1]; i++; continue; }
            if (isBlank(args[i])) { if (!cur.empty()) items.push_back(cur); cur.clear(); continue; }
            cur += args[i];
        }
        if (!cur.empty()) items.push_back(cur);
    }
    if (items.empty() || (items.size() == 1 && items[0] == U"all")) {
        std::vector<u32> out;
        for (auto& b : bools) out.push_back(U(std::string(*b.p ? "  " : "no") + b.name));
        for (auto& nn : nums) out.push_back(U(std::string("  ") + nn.name + "=" + std::to_string(*nn.p)));
        for (auto& s : strs) {
            std::string v = s.p == &ft ? "" : *s.p;
            out.push_back(U(std::string("  ") + s.name + "=" + v));
        }
        host->showLines(tr(U"Options"), out);
        return;
    }
    for (u32 item : items) {
        std::string it = toUtf8(item);
        size_t op = it.find_first_of("=:+-^!&?");
        std::string nm = op == std::string::npos ? it : it.substr(0, op);
        std::string rest = op == std::string::npos ? "" : it.substr(op);
        bool handled = false;
        // boolean options
        for (auto& b : bools) {
            std::string base = nm;
            bool neg = false, inv = false;
            if (base.compare(0, 2, "no") == 0 && (base.substr(2) == b.name || base.substr(2) == b.abbr)) {
                neg = true;
                base = base.substr(2);
            } else if (base.compare(0, 3, "inv") == 0 && (base.substr(3) == b.name || base.substr(3) == b.abbr)) {
                inv = true;
                base = base.substr(3);
            }
            if (base != b.name && base != b.abbr) continue;
            handled = true;
            if (rest == "?") { message(U(std::string(*b.p ? "  " : "no") + b.name)); break; }
            if (rest == "!") inv = true;
            else if (rest == "&") {
                *b.p = (b.p == &S.autoindent || b.p == &opt.wrapscan || b.p == &opt.joinspaces ||
                        b.p == &opt.startofline || b.p == &opt.timeout || b.p == &opt.write);
                break;
            } else if (!rest.empty()) {
                error(tr(U"E474: Invalid argument: ") + item);
                return;
            }
            if (inv) *b.p = !*b.p;
            else *b.p = !neg;
            break;
        }
        if (!handled) {
            for (auto& nn : nums) {
                if (nm != nn.name && nm != nn.abbr) continue;
                handled = true;
                if (rest.empty() || rest == "?") {
                    message(U(std::string("  ") + nn.name + "=" + std::to_string(*nn.p)));
                    break;
                }
                if (rest == "&") {
                    *nn.p = nn.p == &S.tabw ? 4 : nn.p == &opt.timeoutlen ? 1000 : nn.p == &opt.report ? 2 : 0;
                    break;
                }
                size_t vstart = 1;
                char mode = '=';
                if (rest[0] == '+' || rest[0] == '-' || rest[0] == '^') {
                    mode = rest[0];
                    vstart = 2;
                    if (rest.size() < 2 || rest[1] != '=') {
                        error(tr(U"E474: Invalid argument: ") + item);
                        return;
                    }
                } else if (rest[0] != '=' && rest[0] != ':') {
                    error(tr(U"E474: Invalid argument: ") + item);
                    return;
                }
                std::string v = rest.substr(vstart);
                if (v.empty() || v.find_first_not_of("0123456789") != std::string::npos) {
                    error(tr(U"E521: Number required after =: ") + item);
                    return;
                }
                long val = std::min(std::stol(v), 1000000L);
                long nv = mode == '+' ? *nn.p + val : mode == '-' ? *nn.p - val : mode == '^' ? long(*nn.p) * val : val;
                if (nv < nn.lo || nv > nn.hi) { error(tr(U"E487: Argument must be positive: ") + item); return; }
                *nn.p = int(nv);
                break;
            }
        }
        if (!handled) {
            for (auto& s : strs) {
                if (nm != s.name && nm != s.abbr) continue;
                handled = true;
                if (rest.empty() || rest == "?") {
                    std::string v = s.p == &ft ? "" : *s.p;
                    message(U(std::string("  ") + s.name + "=" + v));
                    break;
                }
                if (rest == "&") { *s.p = s.p == &opt.nrformats ? "bin,hex" : ""; break; }
                size_t vstart = 1;
                char mode = '=';
                if (rest[0] == '+' || rest[0] == '-' || rest[0] == '^') { mode = rest[0]; vstart = 2; }
                std::string v = rest.substr(vstart);
                if (mode == '=') *s.p = v;
                else if (mode == '+') *s.p = s.p->empty() ? v : *s.p + "," + v;
                else if (mode == '^') *s.p = s.p->empty() ? v : v + "," + *s.p;
                else {
                    size_t p = s.p->find(v);
                    if (p != std::string::npos) {
                        s.p->erase(p, v.size());
                        if (p < s.p->size() && (*s.p)[p] == ',') s.p->erase(p, 1);
                        else if (p > 0 && (*s.p)[p - 1] == ',') s.p->erase(p - 1, 1);
                    }
                }
                if (s.p == &ft || s.p == &synStr) {
                    if (s.p == &synStr && (v == "off" || v.empty())) syn = false;
                    else if (!host->setFiletype(*s.p)) { error(tr(U"E474: Invalid argument: ") + item); return; }
                    else syn = true;
                }
                break;
            }
        }
        if (!handled) {
            for (auto ig : IGNORED) {
                std::string b = nm;
                if (b.compare(0, 2, "no") == 0 && b.substr(2) == ig) b = ig;
                if (b == ig) { handled = true; break; }
            }
        }
        if (!handled) { error(tr(U"E518: Unknown option: ") + U(nm)); return; }
    }
    numberAlso = nu;
    S.lineNums = rnu ? 2 : nu ? 1 : 0;
    if (syn != S.syntax) host->setSyntax(syn);
}

void Vim::exMap(const u32& name, const u32& args, bool bang) {
    std::string n = toUtf8(name);
    bool unmap = n.find("unmap") != std::string::npos;
    bool clear = n.find("mapclear") != std::string::npos;
    bool nore = n.find("noremap") != std::string::npos;
    int modes;
    switch (n[0]) {
        case 'n':
            modes = (n.compare(0, 2, "no") == 0 && n != "nmap" && n != "nnoremap" && n != "nunmap" && n != "nmapclear")
                        ? 0
                        : MM_N;
            break;
        case 'v': case 'x': modes = MM_X; break;
        case 'o': modes = MM_O; break;
        case 'i': modes = MM_I; break;
        case 'c': modes = MM_C; break;
        default: modes = 0;
    }
    if (modes == 0) modes = bang ? (MM_I | MM_C) : (MM_N | MM_X | MM_O);
    if (clear) {
        for (auto it = maps.begin(); it != maps.end();) {
            it->modes &= ~modes;
            if (!it->modes) it = maps.erase(it);
            else ++it;
        }
        return;
    }
    // options such as <silent>
    u32 a = trim(args);
    for (;;) {
        bool any = false;
        for (const char32_t* o :
             {U"<silent>", U"<buffer>", U"<nowait>", U"<unique>", U"<script>", U"<special>", U"<expr>"}) {
            u32 os = o;
            if (a.size() >= os.size()) {
                bool eq = true;
                for (size_t k = 0; k < os.size(); k++)
                    if (fold(a[k]) != fold(os[k])) { eq = false; break; }
                if (eq) { a = trim(a.substr(os.size())); any = true; }
            }
        }
        if (!any) break;
    }
    auto expandLeader = [&](u32 s) {
        for (const char32_t* ld : {U"<leader>", U"<localleader>"}) {
            u32 l = ld;
            for (size_t p = 0; p + l.size() <= s.size(); p++) {
                bool eq = true;
                for (size_t k = 0; k < l.size(); k++)
                    if (fold(s[p + k]) != fold(l[k])) { eq = false; break; }
                if (eq) { s.replace(p, l.size(), leader); p += leader.size() - 1; }
            }
        }
        return vimParseKeys(s);
    };
    size_t i = 0;
    u32 lhs;
    while (i < a.size() && !isBlank(a[i])) {
        if (a[i] == VK::ctrl('v') && i + 1 < a.size()) i++;
        lhs += a[i++];
    }
    u32 rhs = trim(a.substr(i));
    // an unescaped "|" ends the right-hand side
    for (size_t k = 0; k < rhs.size(); k++) {
        if ((rhs[k] == '\\' || rhs[k] == VK::ctrl('v')) && k + 1 < rhs.size()) { k++; continue; }
        if (rhs[k] == '|') { rhs = rhs.substr(0, k); break; }
    }
    u32 L = expandLeader(lhs);
    if (unmap) {
        bool found = false;
        for (auto it = maps.begin(); it != maps.end();) {
            if (it->lhs == L && (it->modes & modes)) {
                found = true;
                it->modes &= ~modes;
                if (!it->modes) { it = maps.erase(it); continue; }
            }
            ++it;
        }
        if (!found) error(tr(U"E31: No such mapping"));
        return;
    }
    if (rhs.empty() && a.find(U"<Nop>") == u32::npos && a.find(U"<nop>") == u32::npos) {
        // list the mappings
        std::vector<u32> out;
        for (auto& m : maps) {
            if (!(m.modes & modes)) continue;
            if (!L.empty() && m.lhs.compare(0, L.size(), L) != 0) continue;
            u32 md = (m.modes & MM_N) && (m.modes & MM_X) && (m.modes & MM_O) ? U" " :
                     (m.modes & MM_I) && (m.modes & MM_C) ? U"!" :
                     (m.modes & MM_N) ? U"n" : (m.modes & MM_X) ? U"x" : (m.modes & MM_O) ? U"o" :
                     (m.modes & MM_I) ? U"i" : U"c";
            u32 l = vimKeyNames(m.lhs);
            while (l.size() < 12) l += ' ';
            out.push_back(md + U"  " + l + (m.noremap ? U"* " : U"  ") + vimKeyNames(m.rhs));
        }
        if (out.empty()) message(tr(U"No mapping found"));
        else host->showLines(tr(U"Mappings"), out);
        return;
    }
    if (L.empty()) { error(tr(U"E474: Invalid argument")); return; }
    for (auto it = maps.begin(); it != maps.end();) {
        if (it->lhs == L) {
            it->modes &= ~modes;
            if (!it->modes) { it = maps.erase(it); continue; }
        }
        ++it;
    }
    VimMap m;
    m.lhs = L;
    m.rhs = expandLeader(rhs);
    m.noremap = nore;
    m.modes = modes;
    maps.push_back(m);
}

std::vector<u32> Vim::listRegisters(const u32& which) {
    std::vector<u32> out{tr(U"Type Name Content")};
    auto show = [&](char32_t r) {
        if (!which.empty() && which.find(r) == u32::npos) return;
        VimReg v = getReg(r);
        if (v.empty()) return;
        u32 t;
        for (size_t i = 0; i < v.lines.size(); i++) {
            if (i) t += U"^J";
            t += vimKeyNames(v.lines[i]);
        }
        if (v.type == RT_LINE) t += U"^J";
        if (t.size() > 70) t = t.substr(0, 70);
        const char32_t* ty = v.type == RT_LINE ? U"  l  \"" : v.type == RT_BLOCK ? U"  b  \"" : U"  c  \"";
        out.push_back(ty + u32(1, r) + U"   " + t);
    };
    show('"');
    for (char32_t r = '0'; r <= '9'; r++) show(r);
    for (char32_t r = 'a'; r <= 'z'; r++) show(r);
    for (char32_t r : u32(U"-*+.:%/")) {
        if (r == '*') continue;
        show(r);
    }
    return out;
}
