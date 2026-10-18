// Vim emulation: the Normal/Visual mode command parser, motions, operators, text objects
// and Visual mode.

#include <algorithm>
#include <cstring>

#include "i18n.h"
#include "utf8.h"
#include "vim.h"

namespace {

enum { P_MORE, P_OK, P_BAD, P_SEARCH };
enum { MT_EXCL, MT_INCL, MT_LINE };

bool isDigit(char32_t c) { return c >= '0' && c <= '9'; }
bool isBlank(char32_t c) { return c == ' ' || c == '\t'; }

bool isUpperC(char32_t c) { return fold(c) != c; }
char32_t toUpperC(char32_t c) {
    if (c >= 'a' && c <= 'z') return c - 32;
    if (c >= 0xE0 && c <= 0xFE && c != 0xF7) return c - 32;
    if (c >= 0x3B1 && c <= 0x3C9 && c != 0x3C2) return c - 32;
    if (c >= 0x430 && c <= 0x44F) return c - 32;
    if (c >= 0x450 && c <= 0x45F) return c - 80;
    return c;
}

// Operator at b[i]; returns its length (0 = none)
size_t opLen(const u32& b, size_t i, bool tildeop) {
    char32_t k = b[i];
    if (k == 'd' || k == 'c' || k == 'y' || k == '<' || k == '>' || k == '=' || k == '!') return 1;
    if (k == '~' && tildeop) return 1;
    if (k == 'g' && i + 1 < b.size()) {
        char32_t k2 = b[i + 1];
        if (k2 == '~' || k2 == 'u' || k2 == 'U' || k2 == '?' || k2 == 'q' || k2 == 'w') return 2;
    }
    return 0;
}

bool validObject(char32_t c) { return c < 128 && c && strchr("wWsp()b{}B[]<>t\"'`", int(c)); }

// Motion at b[i]
int parseMotion(const u32& b, size_t i, VimCmd& c, bool& needArg) {
    size_t n = b.size();
    char32_t k = b[i];
    static const u32 single = U"hjkl0^$wWbBeEGHML{}()%nN*#;,|-+_";
    bool simple = (k < 128 && single.find(k) != u32::npos) || k == VK::CR || k == VK::BS || k == ' ' ||
                  k == VK::ctrl('n') || k == VK::ctrl('p') || k == VK::NL || k == VK::ctrl('h');
    if (VK::special(k)) {
        int code = int((k - VK::SPECIAL) / 8);
        simple = code == K_UP || code == K_DOWN || code == K_LEFT || code == K_RIGHT || code == K_HOME || code == K_END;
    }
    if (simple) {
        c.key = u32(1, k);
        return P_OK;
    }
    if (k == 'f' || k == 'F' || k == 't' || k == 'T' || k == '`' || k == '\'') {
        if (i + 1 >= n) { needArg = true; return P_MORE; }
        c.key = u32(1, k);
        c.arg = b[i + 1];
        return P_OK;
    }
    if (k == '/' || k == '?') {
        c.key = u32(1, k);
        return P_SEARCH;
    }
    if (k == 'g') {
        if (i + 1 >= n) { needArg = true; return P_MORE; }
        char32_t k2 = b[i + 1];
        if (k2 < 128 && k2 && strchr("geE_0^$mMjko*#", int(k2))) {
            c.key = U"g" + u32(1, k2);
            return P_OK;
        }
        if (k2 == VK::UP || k2 == VK::DOWN || k2 == VK::HOME || k2 == VK::END) {
            c.key = U"g" + u32(1, k2);
            return P_OK;
        }
        if (k2 == '\'' || k2 == '`') {
            if (i + 2 >= n) { needArg = true; return P_MORE; }
            c.key = U"g" + u32(1, k2);
            c.arg = b[i + 2];
            return P_OK;
        }
        return P_BAD;
    }
    if (k == '[' || k == ']') {
        if (i + 1 >= n) { needArg = true; return P_MORE; }
        char32_t k2 = b[i + 1];
        if (k2 == '(' || k2 == ')' || k2 == '{' || k2 == '}' || k2 == '[' || k2 == ']') {
            c.key = u32(1, k) + u32(1, k2);
            return P_OK;
        }
        return P_BAD;
    }
    return P_BAD;
}

}  // namespace

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------

int Vim::parse(const u32& b, VimCmd& c, bool& needArg) const {
    needArg = false;
    c = VimCmd();
    size_t i = 0, n = b.size();
    bool vis = visualActive();
    for (;;) {
        if (i < n && b[i] == '"') {
            if (i + 1 >= n) { needArg = true; return P_MORE; }
            if (!validReg(b[i + 1], false)) return P_BAD;
            c.reg = b[i + 1];
            i += 2;
            continue;
        }
        if (i < n && isDigit(b[i]) && (b[i] != '0' || c.count1)) {
            c.count1 = std::min(c.count1 * 10 + int(b[i] - '0'), 99999999);
            i++;
            continue;
        }
        break;
    }
    if (i >= n) return P_MORE;
    char32_t k = b[i];
    if (k == 'g' && i + 1 >= n) { needArg = true; return P_MORE; }
    size_t ol = opLen(b, i, opt.tildeop);
    if (ol && !(vis && k == '~')) {
        c.op = b.substr(i, ol);
        i += ol;
        if (vis) return P_OK;
        while (i < n && isDigit(b[i]) && (b[i] != '0' || c.count2)) {
            c.count2 = std::min(c.count2 * 10 + int(b[i] - '0'), 99999999);
            i++;
        }
        if (i < n && (b[i] == 'v' || b[i] == 'V' || b[i] == VK::ctrl('v'))) c.force = b[i++];
        if (i >= n) return P_MORE;
        char32_t last = c.op.back();
        if (b[i] == last) {
            c.key = U"_";
            return P_OK;
        }
        if (c.op.size() == 2 && b[i] == 'g') {
            if (i + 1 >= n) { needArg = true; return P_MORE; }
            if (b[i + 1] == c.op[1]) {
                c.key = U"_";
                return P_OK;
            }
        }
        if (b[i] == 'i' || b[i] == 'a') {
            if (i + 1 >= n) { needArg = true; return P_MORE; }
            if (!validObject(b[i + 1])) return P_BAD;
            c.key = b.substr(i, 2);
            return P_OK;
        }
        if (b[i] == VK::ESC) return P_BAD;
        return parseMotion(b, i, c, needArg);
    }
    if (vis && (k == 'i' || k == 'a')) {
        if (i + 1 >= n) { needArg = true; return P_MORE; }
        if (!validObject(b[i + 1])) return P_BAD;
        c.key = b.substr(i, 2);
        return P_OK;
    }
    int r = parseMotion(b, i, c, needArg);
    if (r != P_BAD) return r;
    c.key.clear();
    c.arg = 0;
    // other commands
    auto withArg = [&](size_t keyLen) {
        if (i + keyLen >= n) { needArg = true; return P_MORE; }
        c.key = b.substr(i, keyLen);
        c.arg = b[i + keyLen];
        return P_OK;
    };
    switch (k) {
        case 'r': return withArg(1);
        case 'm': return withArg(1);
        case '@': return withArg(1);
        case 'q':
            if (recReg) { c.key = U"q"; return P_OK; }
            return withArg(1);
        case 'Z': case VK::ctrl('w'): return withArg(1);
        case 'z': {
            if (i + 1 >= n) { needArg = true; return P_MORE; }
            c.key = b.substr(i, 2);
            return P_OK;
        }
        case 'g': {
            char32_t k2 = b[i + 1];
            if (k2 == 'J' || k2 == 'p' || k2 == 'P' || k2 == 'i' || k2 == 'I' || k2 == 'v' || k2 == '&' ||
                k2 == 'a' || k2 == '8' || k2 == ';' || k2 == ',' || k2 == 'd' || k2 == 'D' || k2 == 'f' ||
                k2 == VK::ctrl('a') || k2 == VK::ctrl('x') || k2 == VK::ctrl('g') || k2 == 'x' || k2 == 't' ||
                k2 == 'T' || k2 == 'R' || k2 == 'r' || k2 == 'h' || k2 == 'n' || k2 == 'N' || k2 == 's' ||
                k2 == 'Q' || k2 == '<' || k2 == 'u' || k2 == 'U' || k2 == '~' || k2 == '?') {
                if (k2 == 'r') return withArg(2);
                c.key = b.substr(i, 2);
                return P_OK;
            }
            return P_BAD;
        }
        case '[': case ']': {
            char32_t k2 = b[i + 1];
            if (k2 == 'p' || k2 == 'P') { c.key = b.substr(i, 2); return P_OK; }
            return P_BAD;
        }
    }
    static const u32 cmds = U"xXsSDCYpPJiaIAoOuU.~&:vVRKQ";
    bool ok = (k < 128 && cmds.find(k) != u32::npos) || k == VK::ESC || k == VK::DEL || k == VK::INS ||
              k == VK::PGUP || k == VK::PGDN || k == VK::TAB || k == VK::NUL;
    if (!ok && k < 32) ok = true;  // control keys
    if (vis && (k == 'o' || k == 'O')) ok = true;
    if (VK::special(k)) {
        int code = int((k - VK::SPECIAL) / 8);
        if (code == K_PGUP || code == K_PGDN || code == K_DEL || code == K_INS) ok = true;
    }
    if (!ok) return P_BAD;
    c.key = u32(1, k);
    return P_OK;
}

// ---------------------------------------------------------------------------
// Key handling and execution
// ---------------------------------------------------------------------------

void Vim::normalKey(char32_t c) {
    // Esc or Ctrl-C cancels a partly typed command without an error
    if ((c == VK::ESC || c == VK::ctrl('c')) && !cmdbuf.empty()) {
        cmdbuf.clear();
        pendNeedArg = pendOp = false;
        afterCommand();
        return;
    }
    cmdbuf += c;
    VimCmd cmd;
    bool needArg;
    int r = parse(cmdbuf, cmd, needArg);
    if (r == P_MORE) {
        pendNeedArg = needArg;
        pendOp = !cmd.op.empty() && !visualActive();
        return;
    }
    cmdbuf.clear();
    pendNeedArg = pendOp = false;
    if (r == P_BAD) {
        beep();
        afterCommand();
        return;
    }
    if (r == P_SEARCH) {
        cmdPendingOp = cmd;
        cmdHasPendingOp = true;
        startCmdline(cmd.key[0]);
        return;
    }
    execute(cmd);
}

static bool isRepeatable(const VimCmd& c, bool visual) {
    if (!c.op.empty()) return c.op != U"y" && c.op != U"!";
    const u32& k = c.key;
    if (visual) {
        static const char32_t* V[] = {U"x", U"X", U"d", U"D", U"c", U"s", U"C", U"S", U"R", U"r", U"J", U"gJ",
                                      U"~", U"u", U"U", U"p", U"P", U"I", U"A", U"\x01", U"\x18", U"g\x01",
                                      U"g\x18", U">", U"<", U"=", U"\U000F0000"};
        for (auto v : V)
            if (k == v) return true;
        return k.size() == 1 && k[0] == VK::DEL;
    }
    static const char32_t* N[] = {U"x", U"X", U"D", U"C", U"s", U"S", U"r", U"~", U"J", U"gJ", U"p", U"P",
                                  U"gp", U"gP", U"]p", U"[p", U"]P", U"[P", U"i", U"a", U"I", U"A", U"gi",
                                  U"gI", U"o", U"O", U"R", U"\x01", U"\x18", U"&", U"gr"};
    for (auto v : N)
        if (k == v) return true;
    return (k.size() == 1 && (k[0] == VK::DEL || k[0] == VK::INS));
}

void Vim::execute(VimCmd c) {
    bool vis = visualActive();
    if (vis) {
        ed.anchor.y = std::clamp(ed.anchor.y, 0, n() - 1);
        ed.anchor.x = std::clamp(ed.anchor.x, 0, len(ed.anchor.y));
    }
    // read-only registers (". : % /) cannot be written by operators and deletes
    if (c.reg && !validReg(c.reg, true)) {
        static const char32_t* W[] = {U"x", U"X", U"D", U"C", U"s", U"S", U"Y", U"y", U"d", U"c", U"r"};
        bool writes = !c.op.empty();
        for (auto w : W)
            if (c.key == w && (vis || (c.key != U"y" && c.key != U"d" && c.key != U"c"))) writes = true;
        if (writes) {
            beep();
            afterCommand();
            return;
        }
    }
    bool rep = isRepeatable(c, vis) && !replaying;
    bool changes = isRepeatable(c, vis) || (c.key == U"." && c.op.empty());
    if (rep) {
        dotNew = Dot();
        dotNew.valid = true;
        dotNew.cmd = c;
        dotRecording = true;
        if (vis) {
            Pos a, b;
            int c1, c2;
            visualBounds(a, b, c1, c2);
            dotNew.visual = true;
            dotNew.vmode = mode;
            dotNew.vlines = b.y - a.y + 1;
            dotNew.vcols = mode == VM_VBLOCK ? c2 - c1 + 1 : (a.y == b.y ? b.x - a.x + 1 : b.x + 1);
            dotNew.vdollar = blockDollar;
        }
    }
    if (changes) beginChange();
    bool done = false;
    if (vis) done = visualCommand(c);
    if (!done) {
        if (!c.op.empty()) {
            Pos start{ed.cy, ed.cx};
            Pos a = start, b;
            int mtype = MT_EXCL;
            bool incl = false, ok, isObj = c.key.size() == 2 && (c.key[0] == 'i' || c.key[0] == 'a');
            VimCmd mc = c;
            bool cwSpecial = false;
            if (c.op == U"c" && (c.key == U"w" || c.key == U"W") && len(ed.cy) > 0 &&
                ed.cx < len(ed.cy) && !isBlank(line(ed.cy)[size_t(ed.cx)])) {
                cwSpecial = true;
            }
            if (isObj) {
                ok = textObject(mc, a, b, mtype, incl);
                if (ok && mtype != MT_LINE) mtype = incl ? MT_INCL : MT_EXCL;
            } else if (cwSpecial) {
                // "cw" on a word works like "ce" (":help cw")
                Pos p = start;
                bool big = c.key == U"W";
                ok = true;
                if (!endWord(p, c.count(), big, true, false)) ok = p.y != start.y || p.x != start.x || true;
                b = p;
                mtype = MT_INCL;
            } else {
                bool jump;
                ok = motion(mc, b, mtype, jump, true);
                if (ok && jump) pushJump();
            }
            if (!ok) {
                beep();
                if (changes) endChange();
                dotRecording = false;
                afterCommand();
                return;
            }
            if (b < a) std::swap(a, b);
            if (c.force == 'v') {
                if (mtype == MT_LINE) {
                    mtype = MT_EXCL;
                    a = start.y <= b.y && !(b < start) ? a : a;
                } else {
                    mtype = mtype == MT_EXCL ? MT_INCL : MT_EXCL;
                }
            } else if (c.force == 'V') {
                mtype = MT_LINE;
            }
            bool block = c.force == VK::ctrl('v');
            int bc1 = 0, bc2 = 0;
            if (block) {
                int v1 = vcol(a.y, a.x), v2 = vcol(b.y, b.x);
                bc1 = std::min(v1, v2);
                bc2 = std::max(v1, v2);
                if (mtype != MT_EXCL) {
                    int y = v1 > v2 ? a.y : b.y, x = v1 > v2 ? a.x : b.x;
                    bc2 = std::max(bc2, ed.colOf(line(y), std::min(x + 1, len(y))) - 1);
                } else {
                    bc2 = std::max(bc1, bc2 - 1);
                }
            }
            // An exclusive motion that ends in column 0 ends at the end of the previous line
            bool noAdjust = isObj && c.key[1] == 't';  // a tag block keeps the line break
            if (!block && !noAdjust && mtype == MT_EXCL && b.x == 0 && b.y > a.y && c.force == 0) {
                b.y--;
                bool inIndent = true;
                for (int x = 0; x < a.x; x++)
                    if (!isBlank(line(a.y)[size_t(x)])) { inIndent = false; break; }
                if (inIndent && !(c.key == U"}" && false)) mtype = MT_LINE;
                else b.x = len(b.y);
            }
            if (mtype == MT_INCL) {
                if (b.x < len(b.y)) b.x++;
                else if (b.y + 1 < n() && len(b.y) > 0 && b.x >= len(b.y)) { b.y++; b.x = 0; }
                mtype = MT_EXCL;
            }
            b.x = std::min(b.x, len(b.y));
            applyOperator(c, a, b, mtype, block, bc1, bc2, false);
        } else {
            const u32& k = c.key;
            bool isMotion = false;
            {
                VimCmd t = c;
                bool na;
                u32 kb = k;
                if (c.arg) kb += c.arg;
                VimCmd tmp;
                int pr = kb.empty() ? P_BAD : parseMotion(kb, 0, tmp, na);
                isMotion = pr == P_OK || k == U"/" || k == U"?";
                (void)t;
            }
            if (vis && k.size() == 2 && (k[0] == 'i' || k[0] == 'a') && validObject(k[1])) {
                Pos a, b;
                int mt;
                bool incl;
                Pos cur{ed.cy, ed.cx};
                bool single = ed.anchor == cur;
                VimCmd oc = c;
                if (!single && cur < ed.anchor) {
                    beep();
                } else if ([&] {
                               // an existing selection is extended from the next character
                               char32_t o = c.key[1];
                               if (single || !(o == 'w' || o == 'W' || o == 's' || o == 'p'))
                                   return textObject(oc, a, b, mt, incl);
                               Pos save{ed.cy, ed.cx}, p = save;
                               if (o == 'p') { if (p.y + 1 >= n()) return false; p = {p.y + 1, 0}; }
                               else if (inc(p) == -1) return false;
                               if (p.x >= len(p.y) && len(p.y) > 0 && o != 'p') { if (inc(p) == -1) return false; }
                               ed.cy = p.y;
                               ed.cx = p.x;
                               bool r = textObject(oc, a, b, mt, incl);
                               ed.cy = save.y;
                               ed.cx = save.x;
                               if (r) a = std::min(a, ed.anchor, [](Pos x, Pos y) { return x < y; });
                               return r;
                           }()) {
                    Pos e = b;
                    if (!incl && mt != MT_LINE) {
                        if (e.x > 0) e.x--;
                        else if (e.y > a.y) { e.y--; e.x = len(e.y); }
                    }
                    if (single || a < ed.anchor) ed.anchor = a;
                    ed.cy = e.y;
                    ed.cx = e.x;
                    if (mt == MT_LINE && mode == VM_VISUAL) mode = VM_VLINE;
                    else if (mt != MT_LINE && mode == VM_VLINE) mode = VM_VISUAL;
                    if (mode == VM_VLINE) ed.cx = std::min(ed.cx, std::max(0, len(ed.cy) - 1));
                    curswant = vcol(ed.cy, ed.cx);
                } else {
                    beep();
                }
            } else if (isMotion) {
                Pos to;
                int mt;
                bool jump;
                mWant = -1;
                if (!motion(c, to, mt, jump, false)) {
                    beep();
                } else {
                    if (jump) pushJump();
                    ed.cy = to.y;
                    ed.cx = to.x;
                    if (mWant == -2) {}
                    else if (mWant == -1) curswant = vcol(ed.cy, std::min(ed.cx, len(ed.cy)));
                    else curswant = mWant;
                    if (mode == VM_VBLOCK && mWant != -2) blockDollar = mWant == INT_MAX;
                    if (mode == VM_VISUAL && curswant == INT_MAX) ed.cx = len(ed.cy);
                    else if (mode == VM_VISUAL && ed.cx >= len(ed.cy) && len(ed.cy) > 0 && mt != MT_LINE &&
                             (k == U"l" || k == U" " || k == u32(1, VK::RIGHT))) ed.cx = len(ed.cy);
                    else clampNormal();
                }
            } else {
                simpleCommand(c);
            }
        }
    }
    if (changes) endChange();
    if (rep && dotRecording && mode != VM_INSERT && mode != VM_REPLACE) {
        dot = dotNew;
        dotRecording = false;
    }
    afterCommand();
}

void Vim::afterCommand() {
    pendNeedArg = pendOp = false;
    ed.cy = std::clamp(ed.cy, 0, n() - 1);
    ed.cx = std::clamp(ed.cx, 0, len(ed.cy));
    if (mode == VM_NORMAL && !insOneCmd) clampNormal();
    if (insOneCmd && mode == VM_NORMAL && cmdbuf.empty()) {
        insOneCmd = false;
        clampNormal();
        if (insOneEol && ed.cx == len(ed.cy) - 1) ed.cx = len(ed.cy);
        startInsert('i', 1);
    }
}

// ---------------------------------------------------------------------------
// Motions
// ---------------------------------------------------------------------------

int Vim::cls(Pos p, bool big) const {
    if (p.x >= len(p.y)) return 0;
    char32_t c = line(p.y)[size_t(p.x)];
    if (c == ' ' || c == '\t' || c == 0 || c == 0x3000) return 0;
    if (big) return 1;
    if (c >= 0x3040 && c <= 0x30FF) return 3;   // kana
    if (c >= 0x3400 && c <= 0x9FFF) return 4;   // CJK ideographs
    if (c >= 0xAC00 && c <= 0xD7A3) return 5;   // Hangul
    if (c >= 0x1F000 && c <= 0x1FAFF) return 6; // emoji
    return isWord(c) ? 2 : 1;
}

int Vim::inc(Pos& p) const {
    if (p.x < len(p.y)) {
        p.x++;
        return p.x < len(p.y) ? 0 : 2;
    }
    if (p.y < n() - 1) {
        p.y++;
        p.x = 0;
        return 1;
    }
    return -1;
}

int Vim::dec(Pos& p) const {
    if (p.x > 0) {
        p.x = std::min(p.x - 1, len(p.y));
        return 0;
    }
    if (p.y > 0) {
        p.y--;
        p.x = len(p.y);
        return 1;
    }
    return -1;
}

bool Vim::fwdWord(Pos& p, int count, bool big, bool eol) const {
    while (--count >= 0) {
        int sclass = cls(p, big);
        bool lastLine = p.y == n() - 1;
        int i = inc(p);
        if (i == -1 || (i >= 1 && lastLine)) return false;
        if (i >= 1 && eol && count == 0) return true;
        if (sclass != 0) {
            while (cls(p, big) == sclass) {
                i = inc(p);
                if (i == -1 || (i >= 1 && eol && count == 0)) return true;
            }
        }
        while (cls(p, big) == 0) {
            if (p.x == 0 && len(p.y) == 0) break;
            i = inc(p);
            if (i == -1 || (i >= 1 && eol && count == 0)) return true;
        }
    }
    return true;
}

bool Vim::bckWord(Pos& p, int count, bool big, bool stop) const {
    while (--count >= 0) {
        int sclass = cls(p, big);
        if (dec(p) == -1) return false;
        bool finished = false;
        if (!stop || sclass == cls(p, big) || sclass == 0) {
            while (cls(p, big) == 0) {
                if (p.x == 0 && len(p.y) == 0) { finished = true; break; }
                if (dec(p) == -1) return true;
            }
            if (!finished) {
                int c = cls(p, big);
                while (cls(p, big) == c)
                    if (dec(p) == -1) return true;
            }
        }
        if (!finished) inc(p);
        stop = false;
    }
    return true;
}

bool Vim::endWord(Pos& p, int count, bool big, bool stop, bool empty) const {
    while (--count >= 0) {
        int sclass = cls(p, big);
        if (inc(p) == -1) return false;
        bool finished = false;
        if (cls(p, big) == sclass && sclass != 0) {
            while (cls(p, big) == sclass)
                if (inc(p) == -1) return false;
        } else if (!stop || sclass == 0) {
            while (cls(p, big) == 0) {
                if (p.x == 0 && len(p.y) == 0 && empty) { finished = true; break; }
                if (inc(p) == -1) return false;
            }
            if (!finished) {
                int c = cls(p, big);
                while (cls(p, big) == c)
                    if (inc(p) == -1) return false;
            }
        }
        if (!finished) dec(p);
        stop = false;
    }
    return true;
}

bool Vim::bckEndWord(Pos& p, int count, bool big, bool eol) const {
    while (--count >= 0) {
        int sclass = cls(p, big);
        int i = dec(p);
        if (i == -1) return false;
        if (eol && i == 1) return true;
        if (sclass != 0) {
            while (cls(p, big) == sclass) {
                i = dec(p);
                if (i == -1 || (eol && i == 1)) return true;
            }
        }
        while (cls(p, big) == 0) {
            if (p.x == 0 && len(p.y) == 0) break;
            i = dec(p);
            if (i == -1 || (eol && i == 1)) return true;
        }
    }
    return true;
}

bool Vim::findChar(Pos& p, char32_t cmd, char32_t ch, int count, bool repeat) const {
    bool fwd = cmd == 'f' || cmd == 't';
    bool till = cmd == 't' || cmd == 'T';
    const u32& l = line(p.y);
    int x = p.x, L = int(l.size()), d = fwd ? 1 : -1;
    if (till && repeat && count == 1) {
        int nx = x + d;
        if (nx >= 0 && nx < L && l[size_t(nx)] == ch) x = nx;
    }
    for (int k = 0; k < count; k++) {
        for (;;) {
            x += d;
            if (x < 0 || x >= L) return false;
            if (l[size_t(x)] == ch) break;
        }
    }
    if (till) x -= d;
    p.x = x;
    return true;
}

bool Vim::paragraph(Pos& p, int dir, int count, bool, bool& incl) const {
    int curr = p.y;
    incl = false;
    while (count--) {
        bool didSkip = false;
        for (bool first = true;; first = false) {
            if (len(curr) != 0) didSkip = true;
            bool startPS = len(curr) == 0 || line(curr)[0] == '\f';
            if (!first && didSkip && startPS) break;
            curr += dir;
            if (curr < 0 || curr > n() - 1) {
                if (count) return false;
                curr -= dir;
                break;
            }
        }
    }
    p.y = curr;
    if (curr == n() - 1 && dir > 0 && len(curr) != 0) {
        p.x = len(curr) - 1;
        incl = true;
    } else if (curr == n() - 1 && dir > 0) {
        p.x = 0;
    } else {
        p.x = 0;
    }
    return true;
}

bool Vim::sentence(Pos& p, int dir, int count) const {
    // A sentence starts at the first non-blank after '.', '!' or '?' (optionally followed by
    // ')', ']', '"' or '\'') and white space or a line break, at an empty line after text and
    // at the first non-blank after an empty line.
    auto isStart = [&](Pos q) -> bool {
        if (len(q.y) == 0) return q.x == 0 && (q.y == 0 || len(q.y - 1) != 0);
        if (q.x >= len(q.y) || isBlank(line(q.y)[size_t(q.x)])) return false;
        // walk back over blanks and line breaks
        Pos r = q;
        bool sawBlank = false;
        for (;;) {
            if (r.x > 0) {
                char32_t c = line(r.y)[size_t(r.x - 1)];
                if (isBlank(c)) { r.x--; sawBlank = true; continue; }
                if (!sawBlank) return false;
                int x = r.x - 1;
                while (x >= 0 && (line(r.y)[size_t(x)] == ')' || line(r.y)[size_t(x)] == ']' ||
                                  line(r.y)[size_t(x)] == '"' || line(r.y)[size_t(x)] == '\''))
                    x--;
                if (x < 0) return false;
                char32_t e = line(r.y)[size_t(x)];
                return e == '.' || e == '!' || e == '?';
            }
            if (r.y == 0) return true;
            if (len(r.y - 1) == 0) return true;
            r.y--;
            r.x = len(r.y);
            sawBlank = true;
        }
    };
    Pos q = p;
    for (int k = 0; k < count; k++) {
        bool found = false;
        if (dir > 0) {
            while (inc(q) != -1) {
                if (q.x >= len(q.y) && len(q.y) > 0) continue;
                if (isStart(q)) { found = true; break; }
            }
            if (!found) {
                // end of the buffer
                Pos e{n() - 1, std::max(0, len(n() - 1) - 1)};
                if (p == e || (k > 0 && q == e)) return k > 0;
                q = e;
                break;
            }
        } else {
            while (dec(q) != -1) {
                if (q.x >= len(q.y) && len(q.y) > 0) continue;
                if (isStart(q)) { found = true; break; }
            }
            if (!found) {
                if (p == Pos{0, 0}) return false;
                q = {0, 0};
                break;
            }
        }
    }
    p = q;
    return true;
}

bool Vim::matchPair(Pos& p) const {
    static const u32 pairs = U"(){}[]";
    const u32& l = line(p.y);
    int x = p.x;
    while (x < int(l.size()) && pairs.find(l[size_t(x)]) == u32::npos) x++;
    if (x >= int(l.size())) return false;
    char32_t c = l[size_t(x)];
    size_t pi = pairs.find(c);
    bool fwd = pi % 2 == 0;
    char32_t open = pairs[pi & ~size_t(1)], close = pairs[pi | 1];
    Pos q{p.y, x};
    int depth = 0;
    for (;;) {
        int r = fwd ? inc(q) : dec(q);
        if (r == -1) return false;
        if (q.x >= len(q.y)) continue;
        char32_t ch = line(q.y)[size_t(q.x)];
        if (ch == (fwd ? open : close)) depth++;
        else if (ch == (fwd ? close : open)) {
            if (depth == 0) break;
            depth--;
        }
    }
    p = q;
    return true;
}

bool Vim::unmatched(Pos& p, char32_t open, char32_t close, bool fwd, int count) const {
    Pos q = p;
    int depth = 0;
    for (;;) {
        int r = fwd ? inc(q) : dec(q);
        if (r == -1) return false;
        if (q.x >= len(q.y)) continue;
        char32_t ch = line(q.y)[size_t(q.x)];
        char32_t same = fwd ? close : open, other = fwd ? open : close;
        if (ch == other) depth++;
        else if (ch == same) {
            if (depth == 0) {
                if (--count == 0) break;
            } else {
                depth--;
            }
        }
    }
    p = q;
    return true;
}

bool Vim::section(Pos& p, char32_t brace, bool fwd, int count) const {
    int y = p.y;
    for (int k = 0; k < count; k++) {
        for (;;) {
            y += fwd ? 1 : -1;
            if (y < 0) { y = 0; break; }
            if (y >= n()) { y = n() - 1; break; }
            if (len(y) > 0 && line(y)[0] == brace) break;
        }
    }
    if (y == p.y && ((fwd && y == n() - 1) || (!fwd && y == 0)) && (fwd ? p.x >= std::max(0, len(y) - 1) : p.x == 0))
        return false;
    p.y = y;
    p.x = (fwd && y == n() - 1 && !(len(y) > 0 && line(y)[0] == brace)) ? std::max(0, len(y) - 1) : 0;
    return true;
}

bool Vim::motion(const VimCmd& c, Pos& to, int& mtype, bool& jump, bool forOp) {
    int cnt = c.count();
    bool has = c.hasCount();
    Pos p{ed.cy, ed.cx};
    to = p;
    mtype = MT_EXCL;
    jump = false;
    mWant = -1;
    u32 k = c.key;
    // keys with the same meaning as a letter
    if (k.size() == 1) {
        char32_t k0 = k[0];
        if (VK::special(k0)) {
            int code = int((k0 - VK::SPECIAL) / 8), mods = int((k0 - VK::SPECIAL) % 8);
            if (code == K_LEFT) k = mods ? U"b" : U"h";
            else if (code == K_RIGHT) k = mods ? U"w" : U"l";
            else if (code == K_UP) k = U"k";
            else if (code == K_DOWN) k = U"j";
            else if (code == K_HOME) k = (mods & M_CTRL) ? U"gg" : U"0";
            else if (code == K_END) k = (mods & M_CTRL) ? U"G" : U"$";
        } else if (k0 == VK::CR) k = U"+";
        else if (k0 == VK::ctrl('n') || k0 == VK::NL) k = U"j";
        else if (k0 == VK::ctrl('p')) k = U"k";
    }
    int y = p.y;
    auto vertTarget = [&](int ny) {
        int L = len(ny);
        int x = curswant == INT_MAX ? std::max(0, L - 1) : std::min(idxAtCol(ny, curswant), std::max(0, L - 1));
        return Pos{ny, x};
    };
    if (k == U"h" || k == u32(1, VK::BS) || k == u32(1, VK::ctrl('h'))) {
        if (k == U"h") {
            if (p.x == 0) return false;
            to.x = std::max(0, p.x - cnt);
            return true;
        }
        Pos q = p;
        bool moved = false;
        for (int i = 0; i < cnt; i++) {
            if (q.x > 0) q.x--;
            else if (q.y > 0) { q.y--; q.x = forOp ? len(q.y) : std::max(0, len(q.y) - 1); }
            else break;
            moved = true;
        }
        to = q;
        return moved;
    }
    if (k == U"l" || k == U" ") {
        int L = len(y);
        if (k == U"l") {
            int limit = forOp ? L : std::max(0, L - 1);
            if (visualActive()) limit = std::max(0, L - 1);
            if (p.x >= limit) return false;
            to.x = std::min(p.x + cnt, limit);
            return true;
        }
        Pos q = p;
        bool moved = false;
        for (int i = 0; i < cnt; i++) {
            int lim = forOp ? len(q.y) : len(q.y) - 1;
            if (q.x < lim) q.x++;
            else if (q.y < n() - 1) { q.y++; q.x = 0; }
            else break;
            moved = true;
        }
        to = q;
        return moved;
    }
    if (k == U"j" || k == U"k" || k == U"gj" || k == U"gk" || k == U"g" + u32(1, VK::DOWN) ||
        k == U"g" + u32(1, VK::UP)) {
        bool down = k == U"j" || k == U"gj" || k == U"g" + u32(1, VK::DOWN);
        if (down ? y >= n() - 1 : y <= 0) return false;
        int ny = std::clamp(y + (down ? cnt : -cnt), 0, n() - 1);
        to = vertTarget(ny);
        mtype = MT_LINE;
        mWant = -2;
        return true;
    }
    if (k == U"+" || k == U"-" || k == U"_") {
        int d = k == U"+" ? cnt : k == U"-" ? -cnt : cnt - 1;
        if (d > 0 && y >= n() - 1) return false;
        if (d < 0 && y <= 0) return false;
        int ny = std::clamp(y + d, 0, n() - 1);
        to = {ny, firstNonBlank(ny)};
        mtype = MT_LINE;
        return true;
    }
    if (k == U"0") { to.x = 0; return true; }
    if (k == U"^") { to.x = firstNonBlank(y); return true; }
    if (k == U"$" || k == U"g$" || k == U"g" + u32(1, VK::END)) {
        if (y + cnt - 1 > n() - 1) return false;
        int ny = y + cnt - 1;
        to = {ny, std::max(0, len(ny) - 1)};
        mtype = MT_INCL;
        mWant = INT_MAX;
        return true;
    }
    if (k == U"g_") {
        if (y + cnt - 1 > n() - 1) return false;
        int ny = y + cnt - 1;
        int x = len(ny) - 1;
        while (x > 0 && isBlank(line(ny)[size_t(x)])) x--;
        to = {ny, std::max(0, x)};
        mtype = MT_INCL;
        return true;
    }
    if (k == U"g0" || k == U"g^" || k == U"g" + u32(1, VK::HOME)) {
        to.x = k == U"g^" ? firstNonBlank(y) : 0;
        return true;
    }
    if (k == U"gm") {
        to.x = std::min(idxAtCol(y, ed.left + host->viewWidth() / 2), std::max(0, len(y) - 1));
        return true;
    }
    if (k == U"gM") {
        int w = vcol(y, len(y));
        to.x = std::min(idxAtCol(y, has ? w * std::min(cnt, 100) / 100 : w / 2), std::max(0, len(y) - 1));
        return true;
    }
    if (k == U"|") {
        to.x = std::min(idxAtCol(y, cnt - 1), std::max(0, len(y) - 1));
        mWant = cnt - 1;
        return true;
    }
    if (k == U"w" || k == U"W") {
        Pos q = p;
        bool big = k == U"W";
        bool ok = fwdWord(q, cnt, big, forOp);
        if (!ok && !forOp) return false;
        if (!forOp && q.x >= len(q.y) && len(q.y) > 0) q.x = len(q.y) - 1;
        to = q;
        return true;
    }
    if (k == U"b" || k == U"B") {
        Pos q = p;
        if (!bckWord(q, cnt, k == U"B", false)) return false;
        to = q;
        return true;
    }
    if (k == U"e" || k == U"E") {
        Pos q = p;
        bool ok = endWord(q, cnt, k == U"E", false, false);
        if (!ok && !forOp) return false;
        if (q.x >= len(q.y) && len(q.y) > 0) q.x = len(q.y) - 1;
        to = q;
        mtype = MT_INCL;
        return true;
    }
    if (k == U"ge" || k == U"gE") {
        Pos q = p;
        if (!bckEndWord(q, cnt, k == U"gE", false)) return false;
        if (q.x >= len(q.y) && len(q.y) > 0) q.x = len(q.y) - 1;
        to = q;
        mtype = MT_INCL;
        return true;
    }
    if (k == U"f" || k == U"F" || k == U"t" || k == U"T" || k == U";" || k == U",") {
        char32_t cmd, ch;
        bool repeat = false;
        if (k == U";" || k == U",") {
            if (!lastFindCmd) return false;
            cmd = lastFindCmd;
            ch = lastFind;
            repeat = true;
            if (k == U",") cmd = cmd == 'f' ? 'F' : cmd == 'F' ? 'f' : cmd == 't' ? 'T' : 't';
        } else {
            cmd = k[0];
            ch = c.arg;
            if (ch == VK::ESC) return false;
            lastFindCmd = cmd;
            lastFind = ch;
        }
        Pos q = p;
        if (!findChar(q, cmd, ch, cnt, repeat)) return false;
        to = q;
        mtype = (cmd == 'f' || cmd == 't') ? MT_INCL : MT_EXCL;
        return true;
    }
    if (k == U"G" || k == U"gg") {
        int ln = has ? cnt : (k == U"G" ? n() : 1);
        int ny = std::clamp(ln - 1, 0, n() - 1);
        to = opt.startofline ? Pos{ny, firstNonBlank(ny)} : vertTarget(ny);
        if (!opt.startofline) mWant = -2;
        mtype = MT_LINE;
        jump = true;
        return true;
    }
    if (k == U"%") {
        if (has) {
            if (cnt > 100) return false;
            int ny = std::clamp((cnt * n() + 99) / 100 - 1, 0, n() - 1);
            to = {ny, firstNonBlank(ny)};
            mtype = MT_LINE;
            jump = true;
            return true;
        }
        Pos q = p;
        if (!matchPair(q)) return false;
        to = q;
        mtype = MT_INCL;
        jump = true;
        return true;
    }
    if (k == U"H" || k == U"L" || k == U"M") {
        int h = host->viewHeight();
        // the view may still show lines that a macro or mapping just deleted
        ed.top = std::clamp(ed.top, 0, n() - 1);
        int top = ed.top, bot = std::min(n() - 1, ed.top + h - 1);
        int ny;
        if (k == U"H") ny = std::min(top + cnt - 1, bot);
        else if (k == U"L") ny = std::max(bot - cnt + 1, top);
        else ny = top + (bot - top) / 2;
        to = {ny, firstNonBlank(ny)};
        mtype = MT_LINE;
        jump = true;
        return true;
    }
    if (k == U"{" || k == U"}") {
        Pos q = p;
        bool incl;
        if (!paragraph(q, k == U"}" ? 1 : -1, cnt, forOp, incl)) return false;
        to = q;
        mtype = incl ? MT_INCL : MT_EXCL;
        jump = true;
        return true;
    }
    if (k == U"(" || k == U")") {
        Pos q = p;
        if (!sentence(q, k == U")" ? 1 : -1, cnt)) return false;
        to = q;
        jump = true;
        return true;
    }
    if (k == U"n" || k == U"N" || k == U"/" || k == U"?") {
        bool incl = false;
        bool ok;
        if (k == U"n" || k == U"N") {
            if (lastPat.empty()) { error(tr(U"E35: No previous regular expression")); return false; }
            bool fwd = k == U"n" ? lastDirFwd : !lastDirFwd;
            u32 spec = lastPat;
            searchIsRepeat = true;
            ok = doSearch(u32(), fwd, cnt, to, mtype, incl);
            searchIsRepeat = false;
            (void)spec;
        } else {
            bool fwd = k == U"/";
            ok = doSearch(c.pat, fwd, cnt, to, mtype, incl);
        }
        if (!ok) return false;
        if (incl && mtype == MT_EXCL) mtype = MT_INCL;
        jump = true;
        return true;
    }
    if (k == U"*" || k == U"#" || k == U"g*" || k == U"g#") {
        if (!starSearch(k == U"*" || k == U"g*", k.size() == 1, cnt, to)) return false;
        jump = true;
        return true;
    }
    if (k == U"`" || k == U"'" || k == U"g`" || k == U"g'") {
        Pos m;
        if (c.arg == VK::ESC) return false;
        if (!getMark(c.arg, m)) {
            error(tr(U"E20: Mark not set"));
            return false;
        }
        if (k.back() == '\'') {
            to = {m.y, firstNonBlank(m.y)};
            mtype = MT_LINE;
        } else {
            to = m;
            if (!forOp && to.x >= len(to.y)) to.x = std::max(0, len(to.y) - 1);
        }
        jump = k[0] != 'g';
        return true;
    }
    if (k == U"[(" || k == U"[{" || k == U"])" || k == U"]}") {
        Pos q = p;
        char32_t open = (k[1] == '(' || k[1] == ')') ? '(' : '{', close = open == '(' ? ')' : '}';
        if (!unmatched(q, open, close, k[0] == ']', cnt)) return false;
        to = q;
        if (k[0] == ']') mtype = forOp ? MT_EXCL : MT_EXCL;
        jump = true;
        return true;
    }
    if (k == U"[[" || k == U"]]" || k == U"[]" || k == U"][") {
        Pos q = p;
        bool fwd = k[0] == ']';
        char32_t brace = (k == U"[[" || k == U"]]") ? '{' : '}';
        if (!section(q, brace, fwd, cnt)) return false;
        to = q;
        jump = true;
        return true;
    }
    if (k == U"go") {
        long want = has ? cnt : 1, off = 1;
        for (int yy = 0; yy < n(); yy++) {
            for (int x = 0; x <= len(yy); x++) {
                long bytes = 1;
                if (x < len(yy)) {
                    char32_t ch = line(yy)[size_t(x)];
                    bytes = ch < 0x80 ? 1 : ch < 0x800 ? 2 : ch < 0x10000 ? 3 : 4;
                } else if (yy == n() - 1) {
                    break;
                }
                if (off + bytes > want) {
                    to = {yy, std::min(x, std::max(0, len(yy) - 1))};
                    jump = true;
                    return true;
                }
                off += bytes;
            }
        }
        to = {n() - 1, std::max(0, len(n() - 1) - 1)};
        jump = true;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Text objects
// ---------------------------------------------------------------------------

bool Vim::textObject(const VimCmd& c, Pos& a, Pos& b, int& mtype, bool& incl) {
    char32_t kind = c.key[0], obj = c.key[1];
    bool around = kind == 'a';
    int cnt = c.count();
    int y = ed.cy, x = ed.cx;
    const u32& l = line(y);
    int L = int(l.size());
    incl = false;
    mtype = MT_EXCL;
    if (obj == 'w' || obj == 'W') {
        bool big = obj == 'W';
        if (L == 0) {
            if (!around) { a = b = {y, 0}; return true; }
            return false;
        }
        x = std::min(x, L - 1);
        auto runClass = [&](int i) { return cls(Pos{y, i}, big); };
        int s = x;
        int cl = runClass(x);
        while (s > 0 && runClass(s - 1) == cl) s--;
        int e = x;
        auto endRun = [&](int i) {
            int c0 = runClass(i);
            while (i < L && runClass(i) == c0) i++;
            return i;
        };
        if (!around) {
            e = s;
            for (int k = 0; k < cnt && e < L; k++) e = endRun(e);
        } else {
            e = s;
            bool startedOnBlank = cl == 0;
            for (int k = 0; k < cnt && e < L; k++) {
                if (startedOnBlank) {
                    e = endRun(e);            // white space
                    if (e < L) e = endRun(e); // and the following word
                } else {
                    e = endRun(e);            // the word
                    if (e < L && runClass(e) == 0) e = endRun(e);  // trailing white space
                    else if (k == cnt - 1 && e >= L) {
                        // no trailing white space: include the white space before the word
                        while (s > 0 && runClass(s - 1) == 0) s--;
                    }
                }
            }
        }
        a = {y, s};
        b = {y, e};
        return true;
    }
    if (obj == 's') {
        Pos s{y, x};
        // start of the current sentence
        Pos t = s;
        if (!sentence(t, 1, 1)) t = {n() - 1, len(n() - 1)};
        Pos st = t;
        if (!sentence(st, -1, 1)) st = {0, 0};
        if (s < st) {
            st = s;
            sentence(st, -1, 1);
        }
        Pos en = st;
        for (int k = 0; k < cnt; k++)
            if (!sentence(en, 1, 1)) { en = {n() - 1, len(n() - 1)}; break; }
        if (en == Pos{n() - 1, std::max(0, len(n() - 1) - 1)}) en = {n() - 1, len(n() - 1)};
        if (!around) {
            // without the trailing white space
            Pos q = en;
            while (true) {
                Pos r = q;
                if (dec(r) == -1) break;
                if (r < st) break;
                if (r.x < len(r.y) && !isBlank(line(r.y)[size_t(r.x)])) break;
                q = r;
            }
            en = q;
        }
        a = st;
        b = en;
        return true;
    }
    if (obj == 'p') {
        auto blank = [&](int yy) {
            const u32& s = line(yy);
            return std::all_of(s.begin(), s.end(), [](char32_t ch) { return isBlank(ch); });
        };
        bool white = blank(y);
        int s = y, e = y;
        while (s > 0 && blank(s - 1) == white) s--;
        while (e < n() - 1 && blank(e + 1) == white) e++;
        bool curWhite = white;
        for (int k = 1; k < cnt; k++) {
            if (e >= n() - 1) return false;
            curWhite = blank(e + 1);
            e++;
            while (e < n() - 1 && blank(e + 1) == curWhite) e++;
        }
        if (around) {
            if (e < n() - 1) {
                bool nw = blank(e + 1);
                if (!curWhite || nw != curWhite) {
                    e++;
                    while (e < n() - 1 && blank(e + 1) == nw) e++;
                }
            } else if (!white) {
                while (s > 0 && blank(s - 1)) s--;
            }
        }
        a = {s, 0};
        b = {e, 0};
        mtype = MT_LINE;
        return true;
    }
    if (obj == '"' || obj == '\'' || obj == '`') {
        char32_t q = obj;
        auto escaped = [&](int i) {
            int bs = 0;
            while (i - 1 - bs >= 0 && l[size_t(i - 1 - bs)] == '\\') bs++;
            return bs % 2 == 1;
        };
        std::vector<int> qs;
        for (int i = 0; i < L; i++)
            if (l[size_t(i)] == q && !escaped(i)) qs.push_back(i);
        int open = -1, close = -1;
        if (x < L && l[size_t(x)] == q && !escaped(x)) {
            size_t idx = std::find(qs.begin(), qs.end(), x) - qs.begin();
            if (idx % 2 == 0) {
                if (idx + 1 >= qs.size()) return false;
                open = x;
                close = qs[idx + 1];
            } else {
                open = qs[idx - 1];
                close = x;
            }
        } else {
            int before = -1, after = -1;
            for (int i : qs) {
                if (i < x) before = i;
                else if (after < 0) after = i;
            }
            if (before >= 0 && after >= 0) {
                open = before;
                close = after;
            } else if (before < 0 && after >= 0) {
                size_t idx = std::find(qs.begin(), qs.end(), after) - qs.begin();
                if (idx + 1 >= qs.size()) return false;
                open = after;
                close = qs[idx + 1];
            } else {
                return false;
            }
        }
        int s, e;
        if (around) {
            s = open;
            e = close + 1;
            if (e < L && isBlank(l[size_t(e)])) {
                while (e < L && isBlank(l[size_t(e)])) e++;
            } else {
                while (s > 0 && isBlank(l[size_t(s - 1)])) s--;
            }
        } else if (cnt >= 2) {
            s = open;
            e = close + 1;
        } else {
            s = open + 1;
            e = close;
        }
        a = {y, s};
        b = {y, e};
        return true;
    }
    if (obj == 't') {
        // all tags of the buffer as (start, end, name, closing)
        struct Tag { Pos s, e; u32 name; bool close; };
        std::vector<Tag> tags;
        for (int yy = 0; yy < n(); yy++) {
            const u32& s = line(yy);
            for (int i = 0; i < len(yy); i++) {
                if (s[size_t(i)] != '<') continue;
                int j = i + 1;
                bool closing = j < len(yy) && s[size_t(j)] == '/';
                if (closing) j++;
                int ns = j;
                while (j < len(yy) &&
                       (isWord(s[size_t(j)]) || s[size_t(j)] == '-' || s[size_t(j)] == ':' || s[size_t(j)] == '.'))
                    j++;
                if (j == ns) continue;
                u32 name = s.substr(size_t(ns), size_t(j - ns));
                // find the closing '>' (may be on a later line)
                Pos e{yy, j};
                bool found = false, selfClose = false;
                while (!found) {
                    const u32& t = line(e.y);
                    while (e.x < len(e.y)) {
                        if (t[size_t(e.x)] == '>') {
                            selfClose = e.x > 0 && t[size_t(e.x - 1)] == '/';
                            found = true;
                            break;
                        }
                        e.x++;
                    }
                    if (found) break;
                    if (e.y + 1 >= n()) break;
                    e = {e.y + 1, 0};
                }
                if (!found || selfClose) continue;
                tags.push_back({Pos{yy, i}, Pos{e.y, e.x + 1}, name, closing});
            }
        }
        struct Pair { Pos os, oe, cs, ce; };
        std::vector<Pair> pairs;
        std::vector<size_t> st;
        for (size_t i = 0; i < tags.size(); i++) {
            if (!tags[i].close) { st.push_back(i); continue; }
            for (size_t k = st.size(); k-- > 0;) {
                if (fold(tags[st[k]].name[0]) == fold(tags[i].name[0]) &&
                    tags[st[k]].name.size() == tags[i].name.size()) {
                    bool same = true;
                    for (size_t m = 0; m < tags[i].name.size(); m++)
                        if (fold(tags[st[k]].name[m]) != fold(tags[i].name[m])) same = false;
                    if (!same) continue;
                    const Tag& o = tags[st[k]];
                    pairs.push_back({o.s, o.e, tags[i].s, tags[i].e});
                    st.resize(k);
                    break;
                }
            }
        }
        Pos cur{y, x};
        std::vector<const Pair*> enc;
        for (auto& pr : pairs)
            if (!(cur < pr.os) && cur < pr.ce) enc.push_back(&pr);
        std::sort(enc.begin(), enc.end(), [](const Pair* p1, const Pair* p2) { return p2->os < p1->os; });
        if (int(enc.size()) < cnt) return false;
        const Pair* pr = enc[size_t(cnt - 1)];
        if (around) { a = pr->os; b = pr->ce; }
        else { a = pr->oe; b = pr->cs; }
        return true;
    }
    // blocks
    char32_t open, close;
    switch (obj) {
        case '(': case ')': case 'b': open = '('; close = ')'; break;
        case '{': case '}': case 'B': open = '{'; close = '}'; break;
        case '[': case ']': open = '['; close = ']'; break;
        default: open = '<'; close = '>'; break;
    }
    Pos cur{y, x};
    bool inVisual = visualActive() && !(ed.anchor == cur);
    for (int extra = 0; extra < 2; extra++) {
        Pos op = cur;
        bool found;
        int need = cnt + extra;
        if (x < L && l[size_t(x)] == open) {
            found = true;
            if (need > 1) found = unmatched(op, open, close, false, need - 1);
        } else if (x < L && l[size_t(x)] == close) {
            Pos m = cur;
            found = matchPair(m);
            op = m;
            if (found && need > 1) found = unmatched(op, open, close, false, need - 1);
        } else {
            found = unmatched(op, open, close, false, need);
        }
        if (!found) return false;
        Pos cl = op;
        {
            // the matching close bracket
            Pos q = op;
            int depth = 0;
            bool ok = false;
            while (inc(q) != -1) {
                if (q.x >= len(q.y)) continue;
                char32_t ch = line(q.y)[size_t(q.x)];
                if (ch == open) depth++;
                else if (ch == close) {
                    if (depth == 0) { ok = true; break; }
                    depth--;
                }
            }
            if (!ok) return false;
            cl = q;
        }
        if (around) {
            a = op;
            b = {cl.y, cl.x + 1};
        } else {
            Pos s = op;
            if (s.x + 1 < len(s.y)) s.x++;
            else if (s.y + 1 < n()) s = {s.y + 1, 0};
            else s.x++;
            // a closing bracket preceded only by white space: exclude its line
            bool sol = true;
            for (int i = 0; i < cl.x; i++)
                if (!isBlank(line(cl.y)[size_t(i)])) { sol = false; break; }
            Pos e = cl;
            if (sol && cl.y > s.y) e = {cl.y, 0};
            else if (sol && cl.y == s.y) e = cl;
            if (e < s) e = s;
            a = s;
            b = e;
        }
        // In Visual mode a selection that already covers the block selects the next outer block
        if (inVisual && extra == 0) {
            Pos va = ed.anchor, vb = cur;
            if (vb < va) std::swap(va, vb);
            Pos be = b;
            if (be.x > 0) be.x--;
            if (!(a < va) && !(vb < be)) continue;
        }
        return true;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Operators
// ---------------------------------------------------------------------------

void Vim::applyOperator(const VimCmd& c, Pos a, Pos b, int mtype, bool block, int bc1, int bc2, bool dollar) {
    if (block) {
        blockOp(c, a.y, b.y, bc1, bc2, dollar);
        return;
    }
    const u32& op = c.op;
    if (op == U"d" || op == U"c") {
        opDelete(c, a, b, mtype, op == U"c");
    } else if (op == U"y") {
        opYank(c.reg, a, b, mtype, true);
    } else if (op == U">" || op == U"<") {
        int y2 = b.y;
        if (mtype != MT_LINE && b.x == 0 && b.y > a.y) y2--;
        opShift(a.y, y2, op == U">" ? 1 : -1, visualActive() ? std::max(1, c.count()) : 1);
    } else if (op == U"=") {
        opReindent(a.y, b.y);
    } else if (op == U"g~" || op == U"gu" || op == U"gU" || op == U"g?" || op == U"~") {
        opCase(op, a, b, mtype);
    } else if (op == U"gq" || op == U"gw") {
        opFormat(a.y, b.y, op == U"gw");
    } else if (op == U"!") {
        if (!shellAllowed()) return;
        int cnt = b.y - a.y;
        ed.cy = a.y;
        u32 range = cnt ? U".,.+" + fmtU("%d", cnt) : u32(U".");
        startCmdline(':', range + U"!");
        return;
    }
}

static bool specialDeleteMotion(const u32& k) {
    return k == U"%" || k == U"(" || k == U")" || k == U"`" || k == U"/" || k == U"?" || k == U"n" ||
           k == U"N" || k == U"{" || k == U"}";
}

void Vim::opDelete(const VimCmd& c, Pos a, Pos b, int mtype, bool change) {
    char32_t reg = c.reg;
    if (!change && mtype != MT_LINE && a.y != b.y && c.force == 0) {
        // d{motion} over lines with only blanks before the start and after the end is linewise
        bool before = true, after = true;
        for (int x = 0; x < a.x; x++)
            if (!isBlank(line(a.y)[size_t(x)])) before = false;
        for (int x = b.x; x < len(b.y); x++)
            if (!isBlank(line(b.y)[size_t(x)])) after = false;
        if (before && after) mtype = MT_LINE;
    }
    if (mtype == MT_LINE) {
        setReg(reg, textReg(a, b, RT_LINE), false, false);
        int cnt = b.y - a.y + 1;
        if (change) {
            u32 in = S.autoindent ? indentOf(a.y) : u32();
            if (b.y > a.y) delLines(a.y + 1, b.y);
            setLine(a.y, in);
            ed.cy = a.y;
            ed.cx = int(in.size());
            startInsert('c', 1);
            aiLine = in.empty() ? -1 : a.y;
        } else {
            delLines(a.y, b.y);
            toFirstNonBlank(std::min(a.y, n() - 1));
            if (cnt > opt.report) message(fmtU(trs("%d fewer lines").c_str(), cnt));
        }
        setMark('[', Pos{std::min(a.y, n() - 1), 0});
        setMark(']', Pos{std::min(a.y, n() - 1), 0});
        recordChange(Pos{std::min(a.y, n() - 1), 0});
        return;
    }
    bool small = a.y == b.y && !specialDeleteMotion(c.key) && reg == 0;
    setReg(reg, textReg(a, b, RT_CHAR), false, small);
    delText(a, b);
    ed.cy = a.y;
    ed.cx = a.x;
    setMark('[', a);
    setMark(']', a);
    recordChange(a);
    if (change) startInsert('c', 1);
    else clampNormal();
    curswant = vcol(ed.cy, ed.cx);
}

void Vim::opYank(char32_t reg, Pos a, Pos b, int mtype, bool moveCursor) {
    setReg(reg, textReg(a, b, mtype == MT_LINE ? RT_LINE : RT_CHAR), true, false);
    setMark('[', mtype == MT_LINE ? Pos{a.y, 0} : a);
    setMark(']', mtype == MT_LINE ? Pos{b.y, std::max(0, len(b.y) - 1)} : Pos{b.y, std::max(0, b.x - 1)});
    int cnt = b.y - a.y + 1;
    if (mtype == MT_LINE && cnt > opt.report) message(fmtU(trs("%d lines yanked").c_str(), cnt));
    if (moveCursor) {
        if (mtype == MT_LINE) {
            if (ed.cy != a.y) {
                ed.cy = a.y;
                ed.cx = std::min(idxAtCol(a.y, curswant == INT_MAX ? 0 : curswant), std::max(0, len(a.y) - 1));
            }
        } else {
            ed.cy = a.y;
            ed.cx = a.x;
        }
        clampNormal();
    }
}

static char32_t caseOp(const u32& op, char32_t c) {
    if (op == U"gu") return fold(c);
    if (op == U"gU") return toUpperC(c);
    if (op == U"g?") {
        if (c >= 'a' && c <= 'z') return 'a' + (c - 'a' + 13) % 26;
        if (c >= 'A' && c <= 'Z') return 'A' + (c - 'A' + 13) % 26;
        return c;
    }
    return isUpperC(c) ? fold(c) : toUpperC(c);
}

void Vim::opCase(const u32& op, Pos a, Pos b, int mtype) {
    if (mtype == MT_LINE) {
        a.x = 0;
        b = {b.y, len(b.y)};
    }
    for (int y = a.y; y <= b.y; y++) {
        u32 l = line(y);
        int s = y == a.y ? a.x : 0, e = y == b.y ? b.x : int(l.size());
        for (int x = s; x < e && x < int(l.size()); x++) l[size_t(x)] = caseOp(op, l[size_t(x)]);
        setLine(y, l);
    }
    setMark('[', a);
    setMark(']', b);
    recordChange(a);
    ed.cy = a.y;
    ed.cx = a.x;
    clampNormal();
    curswant = vcol(ed.cy, ed.cx);
}

void Vim::opShift(int y1, int y2, int dir, int amount) {
    int sw = shiftWidth();
    for (int y = y1; y <= y2; y++) {
        if (len(y) == 0) continue;
        int w = indentWidth(y);
        int nw;
        if (opt.shiftround) nw = dir > 0 ? (w / sw + amount) * sw : std::max(0, ((w + sw - 1) / sw - amount) * sw);
        else nw = dir > 0 ? w + sw * amount : std::max(0, w - sw * amount);
        if (dir < 0 && w == 0) continue;
        setIndent(y, nw);
    }
    toFirstNonBlank(y1);
    setMark('[', Pos{y1, 0});
    setMark(']', Pos{y2, 0});
    recordChange(Pos{y1, ed.cx});
    int cnt = y2 - y1 + 1;
    if (cnt > opt.report)
        message(fmtU((dir > 0 ? trs("%d lines >ed %d time(s)") : trs("%d lines <ed %d time(s)")).c_str(), cnt, amount));
}

void Vim::opReindent(int y1, int y2) {
    int sw = shiftWidth();
    // the application's rules (the syntax tree of the language)
    std::vector<int> hw;
    if (host->reindent(y1, y2, hw) && int(hw.size()) >= y2 - y1 + 1) {
        for (int y = y1; y <= y2; y++) {
            int w = hw[size_t(y - y1)];
            if (std::all_of(line(y).begin(), line(y).end(), [](char32_t ch) { return isBlank(ch); })) {
                setLine(y, u32());
                continue;
            }
            if (w >= 0) setIndent(y, w);
        }
        toFirstNonBlank(y1);
        recordChange(Pos{y1, 0});
        int cnt = y2 - y1 + 1;
        if (cnt > opt.report) message(fmtU(trs("%d lines indented ").c_str(), cnt));
        return;
    }
    auto lastCode = [&](int y) -> char32_t {
        const u32& l = line(y);
        int x = len(y) - 1;
        // ignore a trailing // comment
        size_t cm = l.find(U"//");
        if (cm != u32::npos) x = int(cm) - 1;
        while (x >= 0 && isBlank(l[size_t(x)])) x--;
        return x >= 0 ? l[size_t(x)] : 0;
    };
    for (int y = y1; y <= y2; y++) {
        if (len(y) == 0) continue;
        int p = y - 1;
        while (p >= 0 && std::all_of(line(p).begin(), line(p).end(), [](char32_t ch) { return isBlank(ch); })) p--;
        int w = 0;
        if (p >= 0) {
            w = indentWidth(p);
            char32_t lc = lastCode(p);
            if (lc == '{' || lc == '(' || lc == '[') w += sw;
        }
        const u32& l = line(y);
        size_t i = 0;
        while (i < l.size() && isBlank(l[i])) i++;
        if (i < l.size() && (l[i] == '}' || l[i] == ')' || l[i] == ']')) w = std::max(0, w - sw);
        if (i == l.size()) { setLine(y, u32()); continue; }
        setIndent(y, w);
    }
    toFirstNonBlank(y1);
    recordChange(Pos{y1, 0});
    int cnt = y2 - y1 + 1;
    if (cnt > opt.report) message(fmtU(trs("%d lines indented ").c_str(), cnt));
}

void Vim::opFormat(int y1, int y2, bool keepCursor) {
    Pos save{ed.cy, ed.cx};
    int tw = opt.textwidth > 0 ? opt.textwidth : 79;
    int y = y1;
    int end = y2;
    int lastLine = y1;
    while (y <= end) {
        if (std::all_of(line(y).begin(), line(y).end(), [](char32_t ch) { return isBlank(ch); })) { y++; continue; }
        int s = y;
        while (y + 1 <= end &&
               !std::all_of(line(y + 1).begin(), line(y + 1).end(), [](char32_t ch) { return isBlank(ch); }))
            y++;
        // paragraph s..y
        u32 in = indentOf(s);
        std::vector<u32> words;
        for (int k = s; k <= y; k++) {
            const u32& l = line(k);
            size_t i = 0;
            while (i < l.size()) {
                while (i < l.size() && isBlank(l[i])) i++;
                size_t j = i;
                while (j < l.size() && !isBlank(l[j])) j++;
                if (j > i) words.push_back(l.substr(i, j - i));
                i = j;
            }
        }
        std::vector<u32> out;
        u32 cur = in;
        bool empty = true;
        for (auto& w : words) {
            u32 sep = U" ";
            if (!empty && opt.joinspaces && !cur.empty() &&
                (cur.back() == '.' || cur.back() == '!' || cur.back() == '?'))
                sep = U"  ";
            if (!empty && ed.colOf(cur + sep + w, int((cur + sep + w).size())) > tw) {
                out.push_back(cur);
                cur = in + w;
            } else {
                cur += empty ? w : sep + w;
            }
            empty = false;
        }
        out.push_back(cur);
        replaceLines(s, y, out);
        int diff = int(out.size()) - (y - s + 1);
        end += diff;
        y = s + int(out.size());
        lastLine = y - 1;
    }
    // delLines on a one-line buffer leaves an empty line behind
    if (keepCursor) {
        setCursor(save.y, save.x);
        clampNormal();
    } else {
        int ly = std::min(lastLine, n() - 1);
        toFirstNonBlank(ly);
    }
    recordChange(Pos{y1, 0});
}

void Vim::joinLines(int y, int count, bool spaces) {
    int cnt = std::max(2, count);
    if (y + 1 >= n()) { beep(); return; }
    int last = std::min(n() - 1, y + cnt - 1);
    int col = 0;
    for (int k = y + 1; k <= last; k++) {
        u32 a = line(y);
        u32 b = line(y + 1);
        if (spaces) {
            size_t i = 0;
            while (i < b.size() && isBlank(b[i])) i++;
            b = b.substr(i);
            u32 sep;
            if (!b.empty() && !a.empty() && !isBlank(a.back()) && b[0] != ')') {
                sep = U" ";
                if (opt.joinspaces && (a.back() == '.' || a.back() == '!' || a.back() == '?')) sep = U"  ";
            }
            col = int(a.size()) + (sep.empty() ? 0 : 0);
            if (sep.empty() && !a.empty() && b.empty()) col = int(a.size()) - (isBlank(a.back()) ? 1 : 0);
            if (!sep.empty()) col = int(a.size());
            if (sep.empty() && !a.empty() && !b.empty()) col = int(a.size()) - (isBlank(a.back()) ? 1 : 0);
            if (a.empty()) col = 0;
            a += sep + b;
        } else {
            col = int(a.size());
            a += b;
        }
        setLine(y, a);
        delLines(y + 1, y + 1);
    }
    ed.cy = y;
    ed.cx = col;
    clampNormal();
    curswant = vcol(ed.cy, ed.cx);
    setMark('[', Pos{y, 0});
    setMark(']', Pos{y, ed.cx});
    recordChange(Pos{y, ed.cx});
}

// ---------------------------------------------------------------------------
// Visual mode
// ---------------------------------------------------------------------------

void Vim::enterVisual(VimMode m) {
    mode = m;
    ed.anch = true;
    ed.anchor = {ed.cy, ed.cx};
    blockDollar = m == VM_VBLOCK && curswant == INT_MAX;
}

void Vim::exitVisual() {
    if (!visualActive()) return;
    lastVStart = ed.anchor;
    lastVEnd = {ed.cy, ed.cx};
    lastVMode = mode;
    mode = VM_NORMAL;
    ed.anch = false;
    clampNormal();
    curswant = vcol(ed.cy, ed.cx);
}

void Vim::visualBounds(Pos& a, Pos& b, int& c1, int& c2) const {
    Pos s = ed.anchor, e{ed.cy, ed.cx};
    s.y = std::clamp(s.y, 0, n() - 1);
    s.x = std::clamp(s.x, 0, len(s.y));
    a = s;
    b = e;
    if (b < a) std::swap(a, b);
    auto endCol = [&](Pos p) {
        if (p.x >= len(p.y)) return vcol(p.y, p.x);
        return ed.colOf(line(p.y), p.x + 1) - 1;
    };
    int v1 = vcol(s.y, s.x), v2 = vcol(e.y, e.x);
    c1 = std::min(v1, v2);
    c2 = std::max(endCol(s), endCol(e));
}

void Vim::blockSpan(int y, int c1, int c2, int& s, int& e) const {
    const u32& l = line(y);
    int L = int(l.size());
    s = idxAtCol(y, c1);
    if (s >= L) { s = e = L; return; }
    e = std::min(L, idxAtCol(y, c2) + 1);
    if (e < s) e = s;
}

void Vim::expandTabsIn(int y, int c1, int c2) {
    u32 l = line(y);
    bool changed = false;
    int col = 0;
    u32 out;
    for (char32_t ch : l) {
        int w = ch == '\t' ? S.tabw - col % S.tabw : cw(ch);
        bool straddle = w > 1 && ((col < c1 && col + w > c1) || (col <= c2 && col + w > c2 + 1));
        if (ch == '\t' && straddle) {
            out += u32(size_t(w), ' ');
            changed = true;
        } else {
            out += ch;
        }
        col += w;
    }
    if (changed) setLine(y, out);
}

void Vim::blockOp(const VimCmd& c, int y1, int y2, int c1, int c2, bool dollar) {
    const u32& op = c.op;
    const u32& k = c.key;
    int right = dollar ? INT_MAX / 2 : c2;
    auto yankBlock = [&](char32_t reg) {
        VimReg r;
        r.type = RT_BLOCK;
        for (int y = y1; y <= y2; y++) {
            u32 l = line(y);
            u32 piece;
            int col = 0;
            for (char32_t ch : l) {
                int w = ch == '\t' ? S.tabw - col % S.tabw : cw(ch);
                int s = std::max(col, c1), e = std::min(col + w - 1, right);
                if (e >= s) {
                    if (ch == '\t' && (s != col || e != col + w - 1)) piece += u32(size_t(e - s + 1), ' ');
                    else piece += ch;
                }
                col += w;
            }
            r.lines.push_back(piece);
        }
        setReg(reg, r, op == U"y", false);
    };
    auto delBlock = [&] {
        for (int y = y1; y <= y2; y++) {
            expandTabsIn(y, c1, right);
            int s, e;
            blockSpan(y, c1, right, s, e);
            if (e > s) {
                u32 l = line(y);
                l.erase(size_t(s), size_t(e - s));
                setLine(y, l);
            }
        }
    };
    if (op == U"y") {
        yankBlock(c.reg);
        setCursor(y1, idxAtCol(y1, c1));
        clampNormal();
        return;
    }
    if (op == U"d" || op == U"c") {
        yankBlock(c.reg);
        delBlock();
        setCursor(y1, idxAtCol(y1, c1));
        recordChange(Pos{y1, ed.cx});
        if (op == U"c") {
            blk = BlockIns();
            blk.y1 = y1;
            blk.y2 = y2;
            blk.col = c1;
            blk.dollar = false;
            blk.append = false;
            blk.before = line(y1);
            u32 l = line(y1);
            int lw = ed.colOf(l, int(l.size()));
            if (lw < c1) { setLine(y1, l + u32(size_t(c1 - lw), ' ')); blk.before = line(y1); }
            ed.cx = idxAtCol(y1, c1);
            startInsert('c', 1);
            blk.on = true;
        } else {
            clampNormal();
        }
        return;
    }
    if (op == U"I" || op == U"A") {
        blk = BlockIns();
        blk.y1 = y1;
        blk.y2 = y2;
        blk.append = op == U"A";
        blk.dollar = dollar && op == U"A";
        blk.col = op == U"A" ? c2 + 1 : c1;
        u32 l = line(y1);
        int lw = ed.colOf(l, int(l.size()));
        if (blk.dollar) {
            ed.cx = len(y1);
        } else {
            if (lw < blk.col && op == U"A") setLine(y1, l + u32(size_t(blk.col - lw), ' '));
            expandTabsIn(y1, blk.col, blk.col);
            ed.cx = lw < blk.col && op == U"I" ? len(y1) : idxAtCol(y1, blk.col);
        }
        ed.cy = y1;
        blk.before = line(y1);
        startInsert(op == U"I" ? 'I' : 'A', 1);
        blk.on = true;
        return;
    }
    if (op == U"r") {
        for (int y = y1; y <= y2; y++) {
            expandTabsIn(y, c1, right);
            int s, e;
            blockSpan(y, c1, right, s, e);
            u32 l = line(y);
            for (int x = s; x < e; x++) l[size_t(x)] = c.arg;
            setLine(y, l);
        }
        setCursor(y1, idxAtCol(y1, c1));
        clampNormal();
        recordChange(Pos{y1, ed.cx});
        return;
    }
    if (op == U"g~" || op == U"gu" || op == U"gU" || op == U"g?" || op == U"~") {
        for (int y = y1; y <= y2; y++) {
            int s, e;
            blockSpan(y, c1, right, s, e);
            u32 l = line(y);
            for (int x = s; x < e; x++) l[size_t(x)] = caseOp(op, l[size_t(x)]);
            setLine(y, l);
        }
        setCursor(y1, idxAtCol(y1, c1));
        clampNormal();
        recordChange(Pos{y1, ed.cx});
        return;
    }
    if (op == U">" || op == U"<") {
        int amount = std::max(1, c.count()) * shiftWidth();
        for (int y = y1; y <= y2; y++) {
            int s, e;
            blockSpan(y, c1, c1, s, e);
            if (s >= len(y)) continue;
            u32 l = line(y);
            if (op == U">") {
                l.insert(size_t(s), u32(size_t(amount), ' '));
            } else {
                int x = s, removed = 0;
                while (x < int(l.size()) && isBlank(l[size_t(x)]) && removed < amount) {
                    removed += l[size_t(x)] == '\t' ? S.tabw : 1;
                    x++;
                }
                l.erase(size_t(s), size_t(x - s));
            }
            setLine(y, l);
        }
        setCursor(y1, idxAtCol(y1, c1));
        clampNormal();
        recordChange(Pos{y1, ed.cx});
        return;
    }
    (void)k;
}

bool Vim::visualCommand(VimCmd& c) {
    Pos a, b;
    int c1, c2;
    visualBounds(a, b, c1, c2);
    VimMode vm = mode;
    u32 k = c.key;
    int cnt = c.count();
    auto finish = [&] {
        lastVStart = ed.anchor;
        lastVEnd = {ed.cy, ed.cx};
        lastVMode = vm;
        blockDollar = vm == VM_VBLOCK ? blockDollar : false;
        mode = VM_NORMAL;
        ed.anch = false;
    };
    // exclusive end of a character selection (includes the line break when the end is past the line)
    auto charEnd = [&]() -> Pos {
        Pos e = b;
        if (e.x < len(e.y)) e.x++;
        else if (e.y + 1 < n()) e = {e.y + 1, 0};
        else e.x = len(e.y);
        return e;
    };
    // map Visual commands to operators
    u32 op = c.op;
    bool lineForce = false;
    if (op.empty()) {
        if (k == U"x" || k == U"d" || k == u32(1, VK::DEL) ||
            (VK::special(k[0]) && int((k[0] - VK::SPECIAL) / 8) == K_DEL))
            op = U"d";
        else if (k == U"X" || k == U"D") { op = U"d"; if (vm != VM_VBLOCK) lineForce = true; else blockDollar = true; }
        else if (k == U"y") op = U"y";
        else if (k == U"Y") { op = U"y"; if (vm != VM_VBLOCK) lineForce = true; }
        else if (k == U"c" || k == U"s") op = U"c";
        else if (k == U"C" || k == U"S" || k == U"R") {
            op = U"c";
            if (vm != VM_VBLOCK || k != U"C") lineForce = true;
            else blockDollar = true;
        } else if (k == U"~") op = U"~";
        else if (k == U"u") op = U"gu";
        else if (k == U"U") op = U"gU";
        else if (k == U"r") op = U"r";
        else if (k == U"I") op = U"I";
        else if (k == U"A") op = U"A";
    }
    if (op == U"!") {
        exitVisual();
        if (!shellAllowed()) return true;
        startCmdline(':', U"'<,'>!");
        return true;
    }
    if (!op.empty()) {
        if (lineForce && vm == VM_VISUAL) vm = VM_VLINE;
        bool dollar = blockDollar;
        VimCmd oc = c;
        oc.op = op;
        if (op == U"r" && c.arg == VK::ESC) { exitVisual(); return true; }
        if (vm == VM_VBLOCK) {
            finish();
            int cc2 = c2;
            if (dollar) {
                cc2 = 0;
                for (int y = a.y; y <= b.y; y++) cc2 = std::max(cc2, vcol(y, len(y)) - 1);
            }
            if (op == U"J") {}
            blockOp(oc, a.y, b.y, c1, cc2, dollar);
            return true;
        }
        if (op == U"I" || op == U"A") {
            finish();
            vm = VM_VLINE;  // like Vim: "I" and "A" make a non-block selection linewise
            if (op == U"I") {
                if (vm == VM_VLINE) setCursor(a.y, 0);
                else setCursor(a.y, a.x);
                startInsert('i', 1);
            } else {
                if (vm == VM_VLINE) setCursor(b.y, len(b.y));
                else setCursor(b.y, std::min(b.x + 1, len(b.y)));
                startInsert('a', 1);
            }
            return true;
        }
        if (op == U"r") {
            finish();
            Pos e = vm == VM_VLINE ? Pos{b.y, len(b.y)} : charEnd();
            Pos s = vm == VM_VLINE ? Pos{a.y, 0} : a;
            for (int y = s.y; y <= e.y && y < n(); y++) {
                u32 l = line(y);
                int xs = y == s.y ? s.x : 0, xe = y == e.y ? e.x : int(l.size());
                for (int x = xs; x < xe && x < int(l.size()); x++) l[size_t(x)] = c.arg;
                setLine(y, l);
            }
            setCursor(s.y, s.x);
            clampNormal();
            recordChange(s);
            return true;
        }
        finish();
        if (vm == VM_VLINE) {
            if (op == U">" || op == U"<") { opShift(a.y, b.y, op == U">" ? 1 : -1, cnt); return true; }
            applyOperator(oc, Pos{a.y, 0}, Pos{b.y, len(b.y)}, MT_LINE, false, 0, 0, false);
        } else {
            if (op == U">" || op == U"<") { opShift(a.y, b.y, op == U">" ? 1 : -1, cnt); return true; }
            Pos e = charEnd();
            if (op == U"=" || op == U"gq" || op == U"gw") {
                applyOperator(oc, a, e, MT_LINE, false, 0, 0, false);
                return true;
            }
            oc.force = 'v';  // no linewise exceptions in Visual mode
            applyOperator(oc, a, e, MT_EXCL, false, 0, 0, false);
        }
        return true;
    }
    if (k == U"o" || (k == U"O" && vm != VM_VBLOCK)) {
        Pos cur{ed.cy, ed.cx};
        ed.cy = ed.anchor.y;
        ed.cx = ed.anchor.x;
        ed.anchor = cur;
        curswant = vcol(ed.cy, ed.cx);
        return true;
    }
    if (k == U"O") {
        // other corner in the same line
        int ac = vcol(ed.anchor.y, ed.anchor.x), cc = vcol(ed.cy, ed.cx);
        ed.anchor.x = idxAtCol(ed.anchor.y, cc);
        ed.cx = idxAtCol(ed.cy, ac);
        curswant = vcol(ed.cy, ed.cx);
        return true;
    }
    if (k == u32(1, VK::ESC) || k == u32(1, VK::ctrl('c'))) {
        exitVisual();
        return true;
    }
    if (k == U"v" || k == U"V" || k == u32(1, VK::ctrl('v'))) {
        VimMode nm = k == U"v" ? VM_VISUAL : k == U"V" ? VM_VLINE : VM_VBLOCK;
        if (nm == mode) exitVisual();
        else {
            mode = nm;
            blockDollar = nm == VM_VBLOCK && curswant == INT_MAX;
        }
        return true;
    }
    if (k == U"gv") {
        if (lastVStart.y < 0) { beep(); return true; }
        Pos s = lastVStart, e = lastVEnd;
        VimMode lm = lastVMode;
        lastVStart = ed.anchor;
        lastVEnd = {ed.cy, ed.cx};
        lastVMode = mode;
        mode = lm;
        ed.anchor = s;
        setCursor(e.y, e.x);
        return true;
    }
    if (k == U":") {
        exitVisual();
        startCmdline(':', U"'<,'>");
        return true;
    }
    if (k == U"J" || k == U"gJ") {
        finish();
        joinLines(a.y, std::max(2, b.y - a.y + 1), k == U"J");
        return true;
    }
    if (k == U"p" || k == U"P") {
        VimReg r = getReg(c.reg);
        if (r.empty()) {
            finish();
            error(tr(U"E353: Nothing in register ") + u32(1, c.reg ? c.reg : U'"'));
            return true;
        }
        finish();
        // delete the selection: "p" puts it into the unnamed register, "P" keeps that
        VimCmd dc;
        dc.op = U"d";
        dc.force = 'v';
        dc.reg = k == U"P" ? '_' : 0;
        if (vm == VM_VBLOCK) {
            blockOp(dc, a.y, b.y, c1, c2, blockDollar);
            if (r.type == RT_CHAR && r.lines.size() == 1) {
                // a single line of text is put in every line of the block
                for (int y = a.y; y <= b.y; y++) {
                    u32 l = line(y);
                    int idx = idxAtCol(y, c1);
                    int lw = ed.colOf(l, int(l.size()));
                    if (lw < c1) { l += u32(size_t(c1 - lw), ' '); idx = int(l.size()); }
                    l.insert(size_t(idx), r.lines[0]);
                    setLine(y, l);
                }
                setCursor(a.y, idxAtCol(a.y, c1));
                return true;
            }
            regs['\x01'] = r;
            setCursor(a.y, idxAtCol(a.y, c1));
            put('\x01', cnt, false, false, false);
            regs.erase('\x01');
            return true;
        }
        if (vm == VM_VLINE) {
            setReg(dc.reg, textReg(Pos{a.y, 0}, Pos{b.y, 0}, RT_LINE), false, false);
            std::vector<u32> ls;
            for (int i = 0; i < cnt; i++) ls.insert(ls.end(), r.lines.begin(), r.lines.end());
            replaceLines(a.y, b.y, ls);
            toFirstNonBlank(std::min(a.y, n() - 1));
            recordChange(Pos{ed.cy, ed.cx});
            return true;
        }
        Pos e = charEnd();
        opDelete(dc, a, e, MT_EXCL, false);
        setCursor(a.y, a.x);
        if (r.type == RT_LINE) {
            u32 t = U"\n" + r.text();
            insText(Pos{a.y, a.x}, t);
            toFirstNonBlank(a.y + 1);
        } else {
            u32 one = r.text(), t;
            for (int i = 0; i < cnt; i++) t += one;
            insText(Pos{a.y, a.x}, t);
            if (t.find('\n') != u32::npos) setCursor(a.y, a.x);
            else setCursor(ed.cy, std::max(0, ed.cx - 1));
        }
        clampNormal();
        return true;
    }
    if (k == u32(1, VK::ctrl('a')) || k == u32(1, VK::ctrl('x')) || k == U"g\x01" || k == U"g\x18") {
        finish();
        bool progressive = k[0] == 'g';
        int delta = (k.back() == VK::ctrl('a')) ? cnt : -cnt;
        int step = 0;
        for (int y = a.y; y <= b.y; y++) {
            int x1, x2;
            if (vm == VM_VLINE) { x1 = 0; x2 = len(y); }
            else if (vm == VM_VBLOCK) blockSpan(y, c1, blockDollar ? INT_MAX / 2 : c2, x1, x2);
            else { x1 = y == a.y ? a.x : 0; x2 = y == b.y ? std::min(b.x + 1, len(y)) : len(y); }
            if (increment(progressive ? delta * (step + 1) : delta, y, x1, x2, progressive, step)) step++;
        }
        setCursor(a.y, a.x);
        clampNormal();
        return true;
    }
    return false;
}

// Ctrl-A / Ctrl-X on the number at or after x1 in line y (before x2)
bool Vim::increment(int delta, int y, int x1, int x2, bool, int) {
    u32 l = line(y);
    int L = int(l.size());
    x2 = std::min(x2, L);
    bool hexOn = opt.nrformats.find("hex") != std::string::npos;
    bool binOn = opt.nrformats.find("bin") != std::string::npos;
    bool alpha = opt.nrformats.find("alpha") != std::string::npos;
    auto isHex = [](char32_t c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); };
    int s = -1, e = -1, base = 10;
    // a hex or binary number containing the cursor
    {
        int i = x1;
        while (i > 0 && (isHex(l[size_t(i)]) || l[size_t(i)] == 'x' || l[size_t(i)] == 'X')) i--;
        for (int j = std::max(0, i); j <= x1 && j + 1 < L; j++) {
            if (l[size_t(j)] == '0' && (l[size_t(j + 1)] == 'x' || l[size_t(j + 1)] == 'X') && hexOn && j + 2 < L &&
                isHex(l[size_t(j + 2)])) {
                int k = j + 2;
                while (k < L && isHex(l[size_t(k)])) k++;
                if (x1 < k) { s = j; e = k; base = 16; break; }
            }
            if (l[size_t(j)] == '0' && (l[size_t(j + 1)] == 'b' || l[size_t(j + 1)] == 'B') && binOn && j + 2 < L &&
                (l[size_t(j + 2)] == '0' || l[size_t(j + 2)] == '1')) {
                int k = j + 2;
                while (k < L && (l[size_t(k)] == '0' || l[size_t(k)] == '1')) k++;
                if (x1 < k) { s = j; e = k; base = 2; break; }
            }
        }
    }
    if (s < 0) {
        int i = x1;
        while (i < x2 && !isDigit(l[size_t(i)]) &&
               !(alpha && i < L && ((l[size_t(i)] | 0x20) >= 'a' && (l[size_t(i)] | 0x20) <= 'z')))
            i++;
        if (i >= x2 || i >= L) return false;
        if (!isDigit(l[size_t(i)])) {
            char32_t ch = l[size_t(i)];
            bool up = ch < 'a';
            int v = int((ch | 0x20) - 'a') + delta;
            v = std::clamp(v, 0, 25);
            l[size_t(i)] = char32_t((up ? 'A' : 'a') + v);
            setLine(y, l);
            ed.cy = y;
            ed.cx = i;
            return true;
        }
        // back to the start of the digits
        while (i > x1 && i > 0 && isDigit(l[size_t(i - 1)])) i--;
        while (i > 0 && isDigit(l[size_t(i - 1)]) && i - 1 >= 0 && x1 > i - 1 && false) i--;
        s = i;
        while (s > 0 && isDigit(l[size_t(s - 1)])) s--;
        e = s;
        while (e < L && isDigit(l[size_t(e)])) e++;
        // "0x1f" / "0b101" after the cursor
        if (l[size_t(s)] == '0' && s + 2 < L && (l[size_t(s + 1)] | 0x20) == 'x' && hexOn && isHex(l[size_t(s + 2)])) {
            e = s + 2;
            while (e < L && isHex(l[size_t(e)])) e++;
            base = 16;
        } else if (l[size_t(s)] == '0' && s + 2 < L && (l[size_t(s + 1)] | 0x20) == 'b' && binOn &&
                   (l[size_t(s + 2)] == '0' || l[size_t(s + 2)] == '1')) {
            e = s + 2;
            while (e < L && (l[size_t(e)] == '0' || l[size_t(e)] == '1')) e++;
            base = 2;
        }
        // "0x10" where only the digits after "0x" were found
        if (base == 10 && hexOn && s >= 2 && (l[size_t(s - 1)] == 'x' || l[size_t(s - 1)] == 'X') &&
            l[size_t(s - 2)] == '0') {
            s -= 2;
            e = s + 2;
            while (e < L && isHex(l[size_t(e)])) e++;
            base = 16;
        }
    }
    bool neg = false;
    int numStart = s;
    if (base == 10 && s > 0 && l[size_t(s - 1)] == '-' && opt.nrformats.find("unsigned") == std::string::npos) {
        neg = true;
        numStart = s - 1;
    }
    u32 digits = base == 10 ? l.substr(size_t(s), size_t(e - s)) : l.substr(size_t(s + 2), size_t(e - s - 2));
    unsigned long long v = 0;
    for (char32_t ch : digits) {
        int d = ch <= '9' ? int(ch - '0') : int((ch | 0x20) - 'a' + 10);
        v = v * unsigned(base) + unsigned(d);
    }
    u32 out;
    if (base == 10) {
        long long sv = neg ? -(long long)v : (long long)v;
        sv += delta;
        bool nneg = sv < 0;
        unsigned long long av = nneg ? (unsigned long long)(-sv) : (unsigned long long)sv;
        std::string ds = std::to_string(av);
        // keep leading zeros
        if (digits.size() > 1 && digits[0] == '0' && ds.size() < digits.size())
            ds = std::string(digits.size() - ds.size(), '0') + ds;
        out = U(std::string(nneg ? "-" : "") + ds);
    } else {
        unsigned long long nv = v + (unsigned long long)(long long)delta;
        if (base == 16) {
            bool upper = false;
            for (char32_t ch : digits) if (ch >= 'A' && ch <= 'F') upper = true;
            char buf[40];
            snprintf(buf, sizeof buf, upper ? "%llX" : "%llx", nv);
            std::string hs = buf;
            if (hs.size() < digits.size()) hs = std::string(digits.size() - hs.size(), '0') + hs;
            out = l.substr(size_t(s), 2) + U(hs);
        } else {
            std::string bs;
            for (unsigned long long t = nv; t; t >>= 1) bs.insert(bs.begin(), char('0' + (t & 1)));
            if (bs.empty()) bs = "0";
            if (bs.size() < digits.size()) bs = std::string(digits.size() - bs.size(), '0') + bs;
            out = l.substr(size_t(s), 2) + U(bs);
        }
    }
    l.replace(size_t(numStart), size_t(e - numStart), out);
    setLine(y, l);
    ed.cy = y;
    ed.cx = numStart + int(out.size()) - 1;
    return true;
}

// ---------------------------------------------------------------------------
// Simple Normal mode commands
// ---------------------------------------------------------------------------

void Vim::scrollCmd(const u32& k, int count) {
    int h = std::max(1, host->viewHeight());
    int maxTop = std::max(0, n() - 1);
    ed.top = std::clamp(ed.top, 0, maxTop);
    auto keepCursorVisible = [&] {
        if (ed.cy < ed.top) ed.cy = ed.top;
        if (ed.cy > ed.top + h - 1) ed.cy = std::min(n() - 1, ed.top + h - 1);
        ed.cx = std::min(idxAtCol(ed.cy, curswant == INT_MAX ? INT_MAX / 2 : curswant), std::max(0, len(ed.cy) - 1));
    };
    char32_t k0 = k[0];
    if (k0 == VK::ctrl('e')) {
        ed.top = std::min(maxTop, ed.top + count);
        keepCursorVisible();
    } else if (k0 == VK::ctrl('y')) {
        ed.top = std::max(0, ed.top - count);
        keepCursorVisible();
    } else if (k0 == VK::ctrl('d') || k0 == VK::ctrl('u')) {
        if (count > 0 && count != INT_MIN) scrollAmount = count;  // a count sets 'scroll'
        int amt = scrollAmount > 0 ? scrollAmount : std::max(1, h / 2);
        if (k0 == VK::ctrl('d')) {
            if (ed.cy >= n() - 1) { beep(); return; }
            ed.top = std::min(std::max(0, n() - h), ed.top + amt);
            ed.cy = std::min(n() - 1, ed.cy + amt);
        } else {
            if (ed.cy <= 0) { beep(); return; }
            ed.top = std::max(0, ed.top - amt);
            ed.cy = std::max(0, ed.cy - amt);
        }
        toFirstNonBlank(ed.cy);
    } else if (k0 == VK::ctrl('f') || k0 == VK::PGDN || (VK::special(k0) && int((k0 - VK::SPECIAL) / 8) == K_PGDN)) {
        if (ed.top >= maxTop || ed.cy >= n() - 1) {
            if (ed.cy >= n() - 1) { beep(); return; }
        }
        ed.top = std::min(maxTop, ed.top + std::max(1, (h - 2)) * count);
        ed.cy = std::max(ed.cy, ed.top);
        if (ed.cy > n() - 1) ed.cy = n() - 1;
        toFirstNonBlank(ed.cy);
    } else if (k0 == VK::ctrl('b') || k0 == VK::PGUP || (VK::special(k0) && int((k0 - VK::SPECIAL) / 8) == K_PGUP)) {
        if (ed.top == 0 && ed.cy == 0) { beep(); return; }
        ed.top = std::max(0, ed.top - std::max(1, (h - 2)) * count);
        ed.cy = std::min(ed.cy, ed.top + h - 1);
        if (ed.top == 0 && ed.cy > h - 1) ed.cy = h - 1;
        toFirstNonBlank(std::min(ed.cy, n() - 1));
    } else if (k0 == 'z') {
        char32_t z = k[1];
        if (z == 't' || z == VK::CR || z == 'z' || z == '.' || z == 'b' || z == '-') {
            if (count > 0 && count != INT_MIN) {
                ed.cy = std::clamp(count - 1, 0, n() - 1);
                ed.cx = std::min(idxAtCol(ed.cy, curswant == INT_MAX ? INT_MAX / 2 : curswant),
                                 std::max(0, len(ed.cy) - 1));
            }
            if (z == 't' || z == VK::CR) ed.top = ed.cy;
            else if (z == 'z' || z == '.') ed.top = std::max(0, ed.cy - h / 2);
            else ed.top = std::max(0, ed.cy - h + 1);
            if (z == VK::CR || z == '.' || z == '-') toFirstNonBlank(ed.cy);
        } else if (z == 'h' || z == VK::LEFT) {
            ed.left = std::max(0, ed.left - std::max(1, count));
        } else if (z == 'l' || z == VK::RIGHT) {
            ed.left += std::max(1, count);
        } else if (z == 'H') {
            ed.left = std::max(0, ed.left - host->viewWidth() / 2);
        } else if (z == 'L') {
            ed.left += host->viewWidth() / 2;
        } else if (z == 's') {
            ed.left = vcol(ed.cy, ed.cx);
        } else if (z == 'e') {
            ed.left = std::max(0, vcol(ed.cy, ed.cx) - host->viewWidth() + 1);
        } else {
            beep();
            return;
        }
        // keep the cursor inside the horizontally scrolled view
        if (z == 'h' || z == 'l' || z == 'H' || z == 'L' || z == VK::LEFT || z == VK::RIGHT) {
            int col = vcol(ed.cy, ed.cx);
            int w = host->viewWidth();
            if (col < ed.left) ed.cx = std::min(idxAtCol(ed.cy, ed.left), std::max(0, len(ed.cy) - 1));
            else if (col >= ed.left + w)
                ed.cx = std::min(idxAtCol(ed.cy, ed.left + w - 1), std::max(0, len(ed.cy) - 1));
        }
    }
}

bool Vim::winCmd(char32_t c, int count) {
    const char* cmd = nullptr;
    switch (c) {
        case 's': case 'S': case VK::ctrl('s'): cmd = "split"; break;
        case 'v': case VK::ctrl('v'): cmd = "vsplit"; break;
        case 'n': case VK::ctrl('n'): cmd = "new"; break;
        case 'c': cmd = "close"; break;
        case 'q': case VK::ctrl('q'):
            if (ed.modified && !host->bufferShownElsewhere()) {
                error(tr(U"E37: No write since last change (add ! to override)"));
                return false;
            }
            cmd = "quit";
            break;
        case 'o': case VK::ctrl('o'): cmd = "only"; break;
        case 'w': case VK::ctrl('w'): cmd = "next"; break;
        case 'W': cmd = "prev"; break;
        case 'h': case VK::BS: case VK::LEFT: cmd = "left"; break;  // Ctrl-H is <BS>
        case 'j': case VK::ctrl('j'): case VK::DOWN: cmd = "down"; break;
        case 'k': case VK::ctrl('k'): case VK::UP: cmd = "up"; break;
        case 'l': case VK::ctrl('l'): case VK::RIGHT: cmd = "right"; break;
        case 't': case VK::ctrl('t'): cmd = "top"; break;
        case 'b': case VK::ctrl('b'): cmd = "bottom"; break;
        case 'p': case VK::ctrl('p'): cmd = "last"; break;
        case '=': cmd = "equal"; break;
        case '+': cmd = "taller"; break;
        case '-': cmd = "shorter"; break;
        case '_': case VK::ctrl('_'): cmd = "height"; break;
        case '|': cmd = "width"; break;
        case '>': cmd = "wider"; break;
        case '<': cmd = "narrower"; break;
        case 'T': cmd = "tonewtab"; break;
        case VK::ESC: case VK::ctrl('c'): return true;
    }
    if (!cmd) {
        beep();
        return false;
    }
    return layoutCmd(cmd, count);
}

void Vim::repeatDot(int count) {
    if (!dot.valid) { beep(); return; }
    Dot d = dot;
    VimCmd c = d.cmd;
    if (count) {
        c.count1 = count;
        c.count2 = 0;
    }
    // "1p... puts the next numbered register each time
    if (c.reg >= '1' && c.reg < '9' && c.op.empty() &&
        (c.key == U"p" || c.key == U"P" || c.key == U"gp" || c.key == U"gP")) {
        c.reg++;
        dot.cmd.reg = c.reg;
    }
    replaying = true;
    if (d.visual) {
        // the same amount of text from the cursor
        mode = d.vmode;
        ed.anch = true;
        ed.anchor = {ed.cy, ed.cx};
        int ey = std::min(n() - 1, ed.cy + d.vlines - 1);
        int ex;
        if (d.vmode == VM_VBLOCK) {
            ex = idxAtCol(ey, vcol(ed.cy, ed.cx) + d.vcols - 1);
            blockDollar = d.vdollar;
        } else if (d.vlines == 1) {
            ex = ed.cx + d.vcols - 1;
        } else {
            ex = d.vcols - 1;
        }
        ed.cy = ey;
        ed.cx = std::clamp(ex, 0, std::max(0, len(ey) - 1));
        if (d.vmode == VM_VISUAL && ex >= len(ey)) ed.cx = len(ey);
        if (count) { c.count1 = 0; c.count2 = 0; }
    }
    execute(c);
    if (!d.ins.empty() && (mode == VM_INSERT || mode == VM_REPLACE)) {
        for (char32_t ch : d.ins) {
            if (mode != VM_INSERT && mode != VM_REPLACE) break;
            insertKey(ch);
        }
    }
    replaying = false;
    if (count && !d.visual) {
        dot.cmd.count1 = count;
        dot.cmd.count2 = 0;
    }
}

bool Vim::simpleCommand(VimCmd& c) {
    const u32& k = c.key;
    int cnt = c.count();
    bool has = c.hasCount();
    char32_t k0 = k.empty() ? 0 : k[0];
    if (visualActive()) {
        // only these Normal mode commands also work in Visual mode
        bool ok = k == U"m" || k == U"q" || k == U"z" || k == U"ga" || k == U"g8" || k0 == VK::ctrl('e') ||
                  k0 == VK::ctrl('w') || k == U"gt" || k == U"gT" ||
                  k0 == VK::ctrl('y') || k0 == VK::ctrl('d') || k0 == VK::ctrl('u') || k0 == VK::ctrl('f') ||
                  k0 == VK::ctrl('b') || k0 == VK::ctrl('l') || k0 == VK::ctrl('g') || k0 == 'z' ||
                  k0 == VK::PGUP || k0 == VK::PGDN;
        if (!ok) {
            beep();
            return false;
        }
    }
    auto runOp = [&](const char32_t* op, const char32_t* motion) {
        VimCmd m = c;
        m.op = op;
        m.key = motion;
        bool savedRec = dotRecording;
        bool savedRep = replaying;
        replaying = true;  // the dot command keeps the original command
        execute(m);
        replaying = savedRep;
        dotRecording = savedRec;
    };
    if (k == U"x" || k0 == VK::DEL || (VK::special(k0) && int((k0 - VK::SPECIAL) / 8) == K_DEL)) {
        if (len(ed.cy) == 0) { beep(); return true; }
        runOp(U"d", U"l");
        return true;
    }
    if (k == U"X") { runOp(U"d", U"h"); return true; }
    if (k == U"D") { runOp(U"d", U"$"); return true; }
    if (k == U"C") { runOp(U"c", U"$"); return true; }
    if (k == U"Y") { runOp(U"y", U"_"); return true; }
    if (k == U"s") {
        if (len(ed.cy) == 0) {
            startInsert('s', 1);
            return true;
        }
        runOp(U"c", U"l");
        return true;
    }
    if (k == U"S") { runOp(U"c", U"_"); return true; }
    if (k == U"~") {
        if (len(ed.cy) == 0) { beep(); return true; }
        u32 l = line(ed.cy);
        int e = std::min(int(l.size()), ed.cx + cnt);
        for (int x = ed.cx; x < e; x++) l[size_t(x)] = caseOp(U"g~", l[size_t(x)]);
        setLine(ed.cy, l);
        setMark('[', Pos{ed.cy, ed.cx});
        recordChange(Pos{ed.cy, ed.cx});
        ed.cx = std::min(e, int(l.size()) - 1);
        curswant = vcol(ed.cy, ed.cx);
        return true;
    }
    if (k == U"r") {
        if (c.arg == VK::ESC || c.arg == VK::ctrl('c')) return true;
        if (ed.cx + cnt > len(ed.cy)) { beep(); return true; }
        u32 l = line(ed.cy);
        if (c.arg == VK::CR || c.arg == VK::NL) {
            u32 head = l.substr(0, size_t(ed.cx)), tail = l.substr(size_t(ed.cx + cnt));
            u32 in = S.autoindent ? indentOf(ed.cy) : u32();
            size_t i = 0;
            while (i < tail.size() && isBlank(tail[i])) i++;
            if (S.autoindent) tail = tail.substr(i);
            while (!head.empty() && isBlank(head.back())) head.pop_back();
            setLine(ed.cy, head);
            insLines(ed.cy + 1, {in + tail});
            ed.cy++;
            ed.cx = int(in.size());
            clampNormal();
        } else {
            for (int x = ed.cx; x < ed.cx + cnt; x++) l[size_t(x)] = c.arg;
            setLine(ed.cy, l);
            ed.cx += cnt - 1;
        }
        recordChange(Pos{ed.cy, ed.cx});
        curswant = vcol(ed.cy, ed.cx);
        return true;
    }
    if (k == U"J" || k == U"gJ") { joinLines(ed.cy, has ? cnt : 2, k == U"J"); return true; }
    if (k == U"p" || k == U"P" || k == U"gp" || k == U"gP" || k == U"]p" || k == U"[p" || k == U"]P" || k == U"[P") {
        bool after = k == U"p" || k == U"gp" || k == U"]p";
        bool adj = k[0] == '[' || k[0] == ']';
        if (adj && getReg(c.reg).type != RT_LINE) adj = false;
        put(c.reg, cnt, after, k[0] == 'g', adj);
        return true;
    }
    if (k == U"i" || k == U"a" || k == U"I" || k == U"A" || k == U"gI" || k == U"gi" || k0 == VK::INS ||
        (VK::special(k0) && int((k0 - VK::SPECIAL) / 8) == K_INS)) {
        int L = len(ed.cy);
        if (k == U"a") ed.cx = L ? std::min(ed.cx + 1, L) : 0;
        else if (k == U"A") ed.cx = L;
        else if (k == U"I") {
            ed.cx = firstNonBlank(ed.cy);
            if (L && ed.cx == L - 1 && isBlank(line(ed.cy).back())) ed.cx = L;
        } else if (k == U"gI") ed.cx = 0;
        else if (k == U"gi") {
            Pos m;
            if (getMark('^', m)) { ed.cy = m.y; ed.cx = m.x; }
        }
        startInsert(k == U"gi" || k == U"gI" ? 'i' : (k0 == VK::INS || VK::special(k0) ? 'i' : k0), cnt);
        return true;
    }
    if (k == U"o" || k == U"O") {
        u32 in = S.autoindent ? indentOf(ed.cy) : u32();
        int y = k == U"o" ? ed.cy + 1 : ed.cy;
        insLines(y, {in});
        ed.cy = y;
        ed.cx = int(in.size());
        startInsert(k0, cnt);
        aiLine = in.empty() ? -1 : y;
        return true;
    }
    if (k == U"R") { startInsert('R', cnt, true); return true; }
    if (k == U"u" || k == U"U") {
        int done = 0;
        for (int i = 0; i < cnt; i++)
            if (ed.doUndo()) done++;
        if (!done) message(tr(U"Already at oldest change"));
        else message(fmtU((done == 1 ? trs("%d change undone") : trs("%d changes undone")).c_str(), done));
        ed.anch = false;
        clampNormal();
        curswant = vcol(ed.cy, ed.cx);
        return true;
    }
    if (k0 == VK::ctrl('r')) {
        int done = 0;
        for (int i = 0; i < cnt; i++)
            if (ed.doRedo()) done++;
        if (!done) message(tr(U"Already at newest change"));
        else message(fmtU((done == 1 ? trs("%d change redone") : trs("%d changes redone")).c_str(), done));
        ed.anch = false;
        clampNormal();
        curswant = vcol(ed.cy, ed.cx);
        return true;
    }
    if (k == U".") { repeatDot(has ? cnt : 0); return true; }
    if (k == U"q") {
        if (recReg) {
            u32 keys = recBuf;
            if (!keys.empty() && keys.back() == 'q') keys.pop_back();
            VimReg r;
            r.type = RT_CHAR;
            r.lines = {keys};
            char32_t reg = recReg;
            recReg = 0;
            recBuf.clear();
            if (reg == '"') reg = '0';
            if (reg >= 'A' && reg <= 'Z') {
                VimReg& d = regs[reg - 'A' + 'a'];
                if (d.empty()) d = r;
                else d.lines.back() += keys;
            } else {
                regs[reg] = r;
            }
            return true;
        }
        char32_t r = c.arg;
        if (!((r >= 'a' && r <= 'z') || (r >= 'A' && r <= 'Z') || (r >= '0' && r <= '9') || r == '"')) {
            beep();
            return true;
        }
        recReg = r;
        recBuf.clear();
        return true;
    }
    if (k == U"@") {
        char32_t r = c.arg;
        if (r == '@') {
            if (!lastExecReg) { error(tr(U"E748: No previously used register")); return true; }
            r = lastExecReg;
        }
        if (r == ':') {
            if (lastExCmd.empty()) { error(tr(U"E30: No previous command line")); return true; }
            lastExecReg = ':';
            for (int i = 0; i < cnt; i++) exCommand(lastExCmd);
            return true;
        }
        if (!validReg(r, false)) { beep(); return true; }
        lastExecReg = r;
        VimReg reg = getReg(r);
        u32 keys = reg.text();
        if (r == '+' || r == '*') keys = vimParseKeys(keys);
        u32 all;
        for (int i = 0; i < cnt; i++) all += keys;
        if (all.size() > 1000000) all.resize(1000000);
        for (size_t i = all.size(); i-- > 0;) q.push_front({all[i], false, false});
        return true;
    }
    if (k == U"m") {
        char32_t m = c.arg;
        if ((m >= 'a' && m <= 'z') || (m >= 'A' && m <= 'Z') || m == '\'' || m == '`' || m == '[' || m == ']' ||
            m == '<' || m == '>') {
            setMark(m, Pos{ed.cy, ed.cx});
        } else {
            beep();
        }
        return true;
    }
    if (k == U"v" || k == U"V" || k0 == VK::ctrl('v')) {
        enterVisual(k == U"v" ? VM_VISUAL : k == U"V" ? VM_VLINE : VM_VBLOCK);
        return true;
    }
    if (k == U"gv") {
        if (lastVStart.y < 0) { beep(); return true; }
        mode = lastVMode;
        ed.anch = true;
        ed.anchor = {std::clamp(lastVStart.y, 0, n() - 1), lastVStart.x};
        ed.anchor.x = std::clamp(ed.anchor.x, 0, len(ed.anchor.y));
        setCursor(lastVEnd.y, lastVEnd.x);
        return true;
    }
    if (k == U":") {
        if (has) startCmdline(':', cnt == 1 ? u32(U".") : U".,.+" + fmtU("%d", cnt - 1));
        else startCmdline(':');
        return true;
    }
    if (k == U"&") { exCommand(U"s"); return true; }
    if (k == U"g&") { exCommand(U"%s//~/&"); return true; }
    if (k0 == VK::ctrl('a') || k0 == VK::ctrl('x')) {
        if (!increment(k0 == VK::ctrl('a') ? cnt : -cnt, ed.cy, ed.cx, len(ed.cy), false, 0)) { beep(); return true; }
        recordChange(Pos{ed.cy, ed.cx});
        curswant = vcol(ed.cy, ed.cx);
        return true;
    }
    if (k0 == VK::ctrl('g')) {
        u32 name = ed.filename.empty() ? u32(tr(U"[No Name]")) : U(ed.filename);
        int pct = n() ? (ed.cy + 1) * 100 / n() : 0;
        u32 m = U"\"" + name + U"\"" + (ed.modified ? u32(tr(U" [Modified]")) : u32()) + U" " +
                fmtU((n() == 1 ? trs("%d line") : trs("%d lines")).c_str(), n()) + U" --" + fmtU("%d", pct) + U"%--";
        message(m);
        return true;
    }
    if (k == U"g\x07") {
        long words = 0, chars = 0;
        for (int y = 0; y < n(); y++) {
            chars += len(y) + 1;
            bool in = false;
            for (char32_t ch : line(y)) {
                bool w = !isBlank(ch);
                if (w && !in) words++;
                in = w;
            }
        }
        message(fmtU(trs("Line %d of %d").c_str(), ed.cy + 1, n()) + U"; " + fmtU(trs("%d words").c_str(), int(words)) +
                U"; " + fmtU(trs("%d characters").c_str(), int(chars)));
        return true;
    }
    if (k == U"ga") {
        if (ed.cx >= len(ed.cy)) { message(U"NUL"); return true; }
        char32_t ch = line(ed.cy)[size_t(ed.cx)];
        char b[80];
        snprintf(b, sizeof b, "<%s> %u, Hex %0*x, Oct %o", toUtf8(u32(1, ch < 32 ? char32_t('?') : ch)).c_str(),
                 unsigned(ch), ch > 0xFFFF ? 8 : ch > 0xFF ? 4 : 2, unsigned(ch), unsigned(ch));
        message(U(b));
        return true;
    }
    if (k == U"g8") {
        if (ed.cx >= len(ed.cy)) { message(U"NUL"); return true; }
        std::string s;
        putUtf8(s, line(ed.cy)[size_t(ed.cx)]);
        std::string out;
        char b[8];
        for (unsigned char ch : s) { snprintf(b, sizeof b, "%s%02x", out.empty() ? "" : " ", ch); out += b; }
        message(U(out));
        return true;
    }
    if (k0 == VK::ctrl('l')) { host->redraw(); return true; }
    // tab pages: gt, gT, Ctrl+PageDown, Ctrl+PageUp
    if (k == U"gt" || (VK::special(k0) && k0 == VK::key(K_PGDN, M_CTRL))) {
        if (has) layoutCmd("tabnext", cnt);
        else layoutCmd("tabnext", 0);
        return true;
    }
    if (k == U"gT" || (VK::special(k0) && k0 == VK::key(K_PGUP, M_CTRL))) {
        layoutCmd("tabprev", has ? cnt : 0);
        return true;
    }
    if (k0 == VK::ctrl('w')) {
        winCmd(c.arg, has ? cnt : 0);
        return true;
    }
    if (k0 == VK::ctrl('e') || k0 == VK::ctrl('y') || k0 == VK::ctrl('d') || k0 == VK::ctrl('u') ||
        k0 == VK::ctrl('f') || k0 == VK::ctrl('b') || k0 == VK::PGUP || k0 == VK::PGDN || k0 == 'z' ||
        (VK::special(k0) && (int((k0 - VK::SPECIAL) / 8) == K_PGUP || int((k0 - VK::SPECIAL) / 8) == K_PGDN))) {
        int count = has ? cnt : (k0 == VK::ctrl('d') || k0 == VK::ctrl('u') || k0 == 'z') ? INT_MIN : 1;
        if (k0 == VK::ctrl('d') || k0 == VK::ctrl('u') || k0 == 'z') count = has ? cnt : 0;
        scrollCmd(k, count);
        return true;
    }
    if (k0 == VK::ctrl('o') || k0 == VK::TAB) {
        if (k0 == VK::ctrl('o')) {
            if (jumpIdx == int(jumps.size())) {
                pushJump();
                jumpIdx = int(jumps.size()) - 1;
            }
            int target = jumpIdx - cnt;
            if (target < 0) { beep(); return true; }
            jumpIdx = target;
        } else {
            int target = jumpIdx + cnt;
            if (target >= int(jumps.size())) { beep(); return true; }
            jumpIdx = target;
        }
        Pos p = jumps[size_t(jumpIdx)];
        marks['\''] = {ed.cy, ed.cx};
        setCursor(p.y, p.x);
        clampNormal();
        curswant = vcol(ed.cy, ed.cx);
        return true;
    }
    if (k == U"g;" || k == U"g,") {
        if (changes.empty()) { error(tr(U"E664: Changelist is empty")); return true; }
        int target = changeIdx + (k == U"g;" ? -cnt : cnt);
        if (target < 0) { error(tr(U"E662: At start of changelist")); return true; }
        if (target >= int(changes.size())) { error(tr(U"E663: At end of changelist")); return true; }
        changeIdx = target;
        Pos p = changes[size_t(target)];
        setCursor(p.y, p.x);
        clampNormal();
        curswant = vcol(ed.cy, ed.cx);
        return true;
    }
    if (k == U"Z") {
        if (c.arg == 'Z') exCommand(U"x");
        else if (c.arg == 'Q') exCommand(U"q!");
        else beep();
        return true;
    }
    if (k == U"gd" || k == U"gD") {
        const u32& l = line(ed.cy);
        int s = ed.cx, e;
        while (s < len(ed.cy) && !isWord(l[size_t(s)])) s++;
        if (s >= len(ed.cy)) { error(tr(U"E348: No string under cursor")); return true; }
        while (s > 0 && isWord(l[size_t(s - 1)])) s--;
        e = s;
        while (e < len(ed.cy) && isWord(l[size_t(e)])) e++;
        u32 pat = U"\\<" + rxEscape(l.substr(size_t(s), size_t(e - s))) + U"\\>";
        Pos res;
        if (searchPat(pat, true, Pos{n() - 1, len(n() - 1)}, 1, res, nullptr, true, false)) {
            pushJump();
            lastPat = pat;
            lastDirFwd = true;
            hlOn = true;
            setCursor(res.y, res.x);
            curswant = vcol(ed.cy, ed.cx);
        } else {
            beep();
        }
        return true;
    }
    if (k == U"gf") {
        const u32& l = line(ed.cy);
        auto fc = [](char32_t ch) {
            return ch > 32 && ch != '"' && ch != '\'' && ch != '<' && ch != '>' && ch != '(' && ch != ')' && ch != ',';
        };
        int s = ed.cx, e;
        if (s >= len(ed.cy) || !fc(l[size_t(s)])) { error(tr(U"E446: No file name under cursor")); return true; }
        while (s > 0 && fc(l[size_t(s - 1)])) s--;
        e = s;
        while (e < len(ed.cy) && fc(l[size_t(e)])) e++;
        exEdit(toUtf8(l.substr(size_t(s), size_t(e - s))), false);
        return true;
    }
    if (k0 == VK::ESC) { beeps++; return true; }
    if (k0 == VK::ctrl('c')) return true;
    if (k0 == VK::ctrl('^')) { error(tr(U"E23: No alternate file")); return true; }
    if (k0 == VK::ctrl(']')) { error(tr(U"E433: No tags file")); return true; }
    if (k0 == VK::ctrl('z') || k0 == VK::ctrl('s') || k0 == VK::ctrl('q')) return true;
    beep();
    return false;
}
