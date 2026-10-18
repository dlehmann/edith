// Vim emulation: key codes, the input queue with mappings, registers, buffer primitives,
// Insert/Replace mode and Command-line mode.

#include "vim.h"

#include <algorithm>
#include <cstring>

#include "i18n.h"
#include "utf8.h"

// ---------------------------------------------------------------------------
// Key codes and key notation
// ---------------------------------------------------------------------------

char32_t vimKeyCode(const Key& k) {
    switch (k.code) {
        case K_CHAR: {
            if (k.mods & M_ALT) return 0;
            char32_t c = k.ch;
            if (k.mods & M_CTRL) {
                if (c >= 'a' && c <= 'z') return c - 'a' + 1;
                if (c >= 'A' && c <= 'Z') return c - 'A' + 1;
                switch (c) {
                    case '@': case ' ': case '2': return VK::NUL;
                    case '[': return 27;
                    case '\\': return 28;
                    case ']': return 29;
                    case '^': case '6': return 30;
                    case '_': case '-': return 31;
                }
                return 0;
            }
            return c;
        }
        case K_ENTER: return VK::CR;
        case K_TAB: return (k.mods & M_SHIFT) ? VK::STAB : VK::TAB;
        case K_BKSP: return VK::BS;
        case K_ESC: return VK::ESC;
        case K_UP: case K_DOWN: case K_LEFT: case K_RIGHT: case K_HOME: case K_END:
        case K_PGUP: case K_PGDN: case K_INS: case K_DEL:
            return VK::key(k.code, k.mods & (M_SHIFT | M_CTRL));
    }
    return 0;
}

namespace {
struct KeyName {
    const char* name;
    char32_t code;
};
const KeyName KEYNAMES[] = {
    {"Esc", VK::ESC}, {"CR", VK::CR}, {"NL", VK::NL}, {"BS", VK::BS}, {"Tab", VK::TAB}, {"Space", ' '},
    {"lt", '<'}, {"Bar", '|'}, {"Bslash", '\\'}, {"Del", VK::DEL}, {"Insert", VK::INS}, {"Home", VK::HOME},
    {"End", VK::END}, {"PageUp", VK::PGUP}, {"PageDown", VK::PGDN}, {"Up", VK::UP}, {"Down", VK::DOWN},
    {"Left", VK::LEFT}, {"Right", VK::RIGHT}, {"Nul", VK::NUL}, {"Enter", VK::CR}, {"Return", VK::CR},
    {"Ins", VK::INS}, {"kDel", VK::DEL}, {"Escape", VK::ESC}, {"LF", VK::NL},
};

bool ieq(const std::string& a, const char* b) {
    if (a.size() != strlen(b)) return false;
    for (size_t i = 0; i < a.size(); i++)
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
    return true;
}

char32_t namedKey(std::string s) {
    int mods = 0;
    bool ctrl = false;
    while (s.size() > 2 && s[1] == '-') {
        char m = char(toupper((unsigned char)s[0]));
        if (m == 'C') ctrl = true;
        else if (m == 'S') mods |= M_SHIFT;
        else return 0;
        s = s.substr(2);
    }
    for (auto& kn : KEYNAMES) {
        if (!ieq(s, kn.name)) continue;
        char32_t c = kn.code;
        if (VK::special(c) && c != VK::NUL) {
            int code = int((c - VK::SPECIAL) / 8);
            return VK::key(code, mods | (ctrl ? M_CTRL : 0));
        }
        if (c == VK::TAB && (mods & M_SHIFT)) return VK::STAB;
        if (ctrl && c == ' ') return VK::NUL;
        return c;
    }
    if (s.size() >= 2 && (s[0] == 'F' || s[0] == 'f') && isdigit((unsigned char)s[1])) return 0;
    if (ctrl && s.size() == 1) {
        Key k;
        k.code = K_CHAR;
        k.ch = char32_t((unsigned char)s[0]);
        k.mods = M_CTRL;
        return vimKeyCode(k);
    }
    if (mods == M_SHIFT && s.size() == 1) return char32_t(toupper((unsigned char)s[0]));
    return 0;
}
}  // namespace

u32 vimParseKeys(const u32& n) {
    u32 r;
    for (size_t i = 0; i < n.size(); i++) {
        if (n[i] == '<') {
            size_t e = n.find('>', i + 1);
            if (e != u32::npos && e > i + 1 && e - i < 20) {
                std::string name = toUtf8(n.substr(i + 1, e - i - 1));
                if (ieq(name, "Nop")) { i = e; continue; }
                char32_t c = namedKey(name);
                if (c) {
                    r += c;
                    i = e;
                    continue;
                }
            }
        }
        r += n[i];
    }
    return r;
}

u32 vimKeyNames(const u32& codes) {
    u32 r;
    for (char32_t c : codes) {
        const char* nm = nullptr;
        if (c == VK::ESC) nm = "Esc";
        else if (c == VK::CR) nm = "CR";
        else if (c == VK::NL) nm = "NL";
        else if (c == VK::BS) nm = "BS";
        else if (c == VK::TAB) nm = "Tab";
        else if (c == VK::NUL) nm = "Nul";
        else if (c == VK::STAB) nm = "S-Tab";
        if (nm) { r += U"<" + U(nm) + U">"; continue; }
        if (c < 32) {
            r += U"<C-";
            r += char32_t('a' + c - 1 + (c > 26 ? 0 : 0));
            if (c > 26) { r.pop_back(); r += char32_t('@' + c); }
            r += '>';
            continue;
        }
        if (VK::special(c)) {
            int code = int((c - VK::SPECIAL) / 8), mods = int((c - VK::SPECIAL) % 8);
            u32 name = U"?";
            for (auto& kn : KEYNAMES)
                if (VK::special(kn.code) && int((kn.code - VK::SPECIAL) / 8) == code) { name = U(kn.name); break; }
            r += U"<" + u32((mods & M_CTRL) ? U"C-" : U"") + u32((mods & M_SHIFT) ? U"S-" : U"") + name + U">";
            continue;
        }
        r += c;
    }
    return r;
}

u32 VimReg::text() const {
    u32 r;
    for (size_t i = 0; i < lines.size(); i++) {
        if (i) r += '\n';
        r += lines[i];
    }
    if (type == RT_LINE) r += '\n';
    return r;
}

static VimReg regFromText(const u32& t) {
    VimReg r;
    if (t.empty()) return r;
    size_t s = 0;
    for (;;) {
        size_t q = t.find('\n', s);
        if (q == u32::npos) { r.lines.push_back(t.substr(s)); break; }
        r.lines.push_back(t.substr(s, q - s));
        s = q + 1;
    }
    if (r.lines.size() > 1 && r.lines.back().empty()) {
        r.lines.pop_back();
        r.type = RT_LINE;
    }
    return r;
}

bool VimHost::layout(const std::string& cmd, int, const u32&, bool, u32& err) {
    if (cmd == "quit" || cmd == "qall") {
        quit();
        return true;
    }
    err = tr(U"E476: Invalid command");
    return false;
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void Vim::reset() {
    mode = VM_NORMAL;
    q.clear();
    cmdbuf.clear();
    pendNeedArg = pendOp = false;
    marks.clear();
    jumps.clear();
    jumpIdx = 0;
    changes.clear();
    changeIdx = 0;
    lastVStart = lastVEnd = {-1, 0};
    insOneCmd = false;
    insPending = cmdPending = 0;
    blk.on = false;
    cmpl.on = false;
    changeDepth = 0;
    cmdHasPendingOp = false;
    if (recReg) { recReg = 0; recBuf.clear(); }
    ed.anch = false;
    clampNormal();
    curswant = vcol(ed.cy, ed.cx);
}

void Vim::leaveWindow() {
    if (mode == VM_INSERT || mode == VM_REPLACE) insEsc(true);
    if (mode == VM_CMDLINE) {
        mode = VM_NORMAL;
        cmdHasPendingOp = false;
    }
    if (visualActive()) exitVisual();
    cmdbuf.clear();
    pendNeedArg = pendOp = false;
    insOneCmd = false;
    blk.on = false;
    ed.anch = false;
}

void Vim::swapBufState(VimBufState& s) {
    std::swap(marks, s.marks);
    std::swap(changes, s.changes);
    std::swap(changeIdx, s.changeIdx);
    std::swap(lastVStart, s.lastVStart);
    std::swap(lastVEnd, s.lastVEnd);
    int m = int(lastVMode);
    std::swap(m, s.lastVMode);
    lastVMode = VimMode(m);
}

void Vim::swapWinState(VimWinState& s) {
    std::swap(jumps, s.jumps);
    std::swap(jumpIdx, s.jumpIdx);
}

bool Vim::layoutCmd(const std::string& cmd, int count, const u32& arg, bool bang) {
    u32 err;
    leaveWindow();
    if (host->layout(cmd, count, arg, bang, err)) {
        clampNormal();
        return true;
    }
    if (!err.empty()) error(err);
    else beep();
    return false;
}

bool Vim::handleKey(const Key& k) {
    char32_t c = vimKeyCode(k);
    if (!c) return false;
    // Ctrl-S (save) and Ctrl-Q (exit) stay with the application
    if ((c == VK::ctrl('s') || c == VK::ctrl('q')) && insPending != 1 && cmdPending != 1) return false;
    feed(u32(1, c));
    return true;
}

void Vim::feed(const u32& keys) {
    if (!msgKeep) {
        msg.clear();
        msgErr = false;
    }
    msgKeep = false;
    for (char32_t c : keys) q.push_back({c, false, true});
    process();
}

void Vim::feedNotation(const char* keys) { feed(vimParseKeys(U(keys))); }

bool Vim::waiting() const { return !q.empty(); }

void Vim::timeout() {
    if (q.empty()) return;
    bool old = forceMapResolve;
    forceMapResolve = true;
    process();
    forceMapResolve = old;
}

void Vim::process() {
    while (!q.empty()) {
        if (!q.front().noremap) {
            int r = tryMap();
            if (r == 2) return;
            if (r == 1) continue;
        }
        QKey k = q.front();
        q.pop_front();
        if (k.typed && recReg) recBuf += k.c;
        exec(k.c);
    }
    mapDepth = 0;
}

void Vim::exec(char32_t c) {
    switch (mode) {
        case VM_INSERT: case VM_REPLACE: insertKey(c); break;
        case VM_CMDLINE: cmdKey(c); break;
        default: normalKey(c); break;
    }
}

int Vim::mapMode() const {
    switch (mode) {
        case VM_INSERT: case VM_REPLACE: return insPending ? 0 : MM_I;
        case VM_CMDLINE: return cmdPending ? 0 : MM_C;
        case VM_VISUAL: case VM_VLINE: case VM_VBLOCK: return pendNeedArg ? 0 : MM_X;
        default:
            if (pendNeedArg) return 0;
            return pendOp ? MM_O : MM_N;
    }
}

// 0 = no mapping, 1 = a mapping was expanded, 2 = wait for more keys
int Vim::tryMap() {
    int mm = mapMode();
    if (!mm || maps.empty()) return 0;
    size_t avail = 0;
    while (avail < q.size() && !q[avail].noremap) avail++;
    const VimMap* best = nullptr;
    bool longer = false;
    for (auto& m : maps) {
        if (!(m.modes & mm) || m.lhs.empty()) continue;
        size_t L = m.lhs.size(), k = 0;
        while (k < L && k < avail && q[k].c == m.lhs[k]) k++;
        if (k == L) {
            if (!best || L > best->lhs.size()) best = &m;
        } else if (k == avail) {
            longer = true;
        }
    }
    if (longer && !forceMapResolve && (best || true)) return 2;
    if (!best) return 0;
    if (++mapDepth > 1000) {
        error(tr(U"E223: Recursive mapping"));
        q.clear();
        return 1;
    }
    u32 lhs = best->lhs, rhs = best->rhs;
    bool nore = best->noremap;
    for (size_t i = 0; i < lhs.size(); i++) {
        if (q.front().typed && recReg) recBuf += q.front().c;
        q.pop_front();
    }
    bool startsWithLhs = rhs.compare(0, lhs.size(), lhs) == 0;
    for (size_t i = rhs.size(); i-- > 0;) q.push_front({rhs[i], nore || (startsWithLhs && i < lhs.size()), false});
    return 1;
}

void Vim::runKeys(const u32& keys, bool remap) {
    std::deque<QKey> saved;
    saved.swap(q);
    bool oldForce = forceMapResolve;
    forceMapResolve = true;
    execDepth++;
    for (char32_t c : keys) q.push_back({c, !remap, false});
    process();
    abortPending();
    execDepth--;
    q.swap(saved);
    forceMapResolve = oldForce;
}

void Vim::abortPending() {
    if (mode == VM_INSERT || mode == VM_REPLACE) insEsc(true);
    if (mode == VM_CMDLINE) {
        mode = VM_NORMAL;
        cmdHasPendingOp = false;
    }
    cmdbuf.clear();
    pendNeedArg = pendOp = false;
    if (visualActive()) exitVisual();
    insOneCmd = false;
}

void Vim::beep() {
    beeps++;
    q.clear();  // like Vim: flush the typeahead, which also ends a macro
    cmdbuf.clear();
    pendNeedArg = pendOp = false;
}

void Vim::error(const u32& m) {
    msg = m;
    msgErr = true;
    msgKeep = false;
    // like beep(), but the failing command beeps itself
    q.clear();
    cmdbuf.clear();
    pendNeedArg = pendOp = false;
}

void Vim::message(const u32& m) {
    msg = m;
    msgErr = false;
}

u32 Vim::showCmd() const { return vimKeyNames(cmdbuf); }

u32 Vim::statusText() const {
    if (!msg.empty()) return msg;
    u32 s;
    switch (mode) {
        case VM_INSERT: s = tr(U"-- INSERT --"); break;
        case VM_REPLACE: s = tr(U"-- REPLACE --"); break;
        case VM_VISUAL: s = insOneCmd ? tr(U"-- (insert) VISUAL --") : tr(U"-- VISUAL --"); break;
        case VM_VLINE: s = insOneCmd ? tr(U"-- (insert) VISUAL LINE --") : tr(U"-- VISUAL LINE --"); break;
        case VM_VBLOCK: s = insOneCmd ? tr(U"-- (insert) VISUAL BLOCK --") : tr(U"-- VISUAL BLOCK --"); break;
        default: if (insOneCmd) s = tr(U"-- (insert) --"); break;
    }
    if (mode == VM_INSERT && insPending == 1) s += U" ^V";
    if (recReg) {
        if (!s.empty()) s += U" ";
        s += tr(U"recording @") + u32(1, recReg);
    }
    return s;
}

u32 Vim::cmdlineText() const { return u32(1, cmdType) + cmdText; }

int Vim::cmdlineCursor() const { return 1 + strWidth(cmdText.substr(0, cmdPos)); }

int Vim::cursorShape() const {
    switch (mode) {
        case VM_INSERT: case VM_CMDLINE: return 5;  // bar
        case VM_REPLACE: return 3;                  // underline
        default: return 1;                          // block
    }
}

bool Vim::visualSpan(int y, int& s, int& e) const {
    if (!visualActive()) return false;
    Pos a, b;
    int c1, c2;
    visualBounds(a, b, c1, c2);
    if (y < a.y || y > b.y) return false;
    int L = len(y);
    switch (mode) {
        case VM_VLINE: s = 0; e = L + 1; return true;
        case VM_VISUAL:
            s = y == a.y ? a.x : 0;
            e = y == b.y ? std::min(b.x + 1, L + 1) : L + 1;
            if (y == b.y && b.x >= L) e = L + 1;
            return true;
        default:
            blockSpan(y, c1, blockDollar ? INT_MAX / 2 : c2, s, e);
            if (blockDollar) e = L + 1;
            return e > s;
    }
}

void Vim::searchSpansIn(const std::vector<u32>& L, int y, std::vector<std::pair<int, int>>& out) const {
    if (!opt.hlsearch || !hlOn || lastPat.empty()) return;
    if (hlRxPat != lastPat || hlRxIc != (opt.ignorecase ? 1 : 0) + (opt.smartcase ? 2 : 0)) {
        hlRxPat = lastPat;
        hlRxIc = (opt.ignorecase ? 1 : 0) + (opt.smartcase ? 2 : 0);
        hlRxOk = hlRx.compile(lastPat, opt.ignorecase, opt.smartcase);
    }
    if (!hlRxOk) return;
    int x = 0;
    RxMatch m;
    int guard = 0;
    while (x <= int(L[size_t(y)].size()) && guard++ < 1000 && hlRx.findInLine(L, y, x, m)) {
        if (m.s.y != y) break;
        int e = m.e.y == y ? m.e.x : int(L[size_t(y)].size()) + 1;
        out.push_back({m.s.x, std::max(e, m.s.x + 1)});
        x = std::max(m.e.y == y ? m.e.x : int(L[size_t(y)].size()) + 1, m.s.x + 1);
    }
}

void Vim::visualFromSelection(Pos anchor, Pos cur) {
    if (anchor == cur) return;
    // the end is exclusive in the selection and inclusive in Visual mode: go back one character,
    // over a line break to the end of the line before
    auto prev = [&](Pos p) { return p.x > 0 ? Pos{p.y, p.x - 1} : Pos{p.y - 1, len(p.y - 1)}; };
    if (anchor < cur) cur = prev(cur);
    else anchor = prev(anchor);
    mode = VM_VISUAL;
    ed.anch = true;
    ed.anchor = anchor;
    ed.cy = cur.y;
    ed.cx = cur.x;
    curswant = vcol(ed.cy, ed.cx);
}

bool Vim::selectionFromVisual(Pos& anchor, Pos& cur) const {
    if (!visualActive()) return false;
    anchor = ed.anchor;
    cur = {ed.cy, ed.cx};
    auto next = [&](Pos p) {  // the position after p (after its line break when p is at the end of the line)
        if (p.x < len(p.y)) return Pos{p.y, p.x + 1};
        return p.y + 1 < n() ? Pos{p.y + 1, 0} : Pos{p.y, len(p.y)};
    };
    bool fwd = !(cur < anchor);
    if (mode == VM_VLINE) {  // whole lines
        Pos& s = fwd ? anchor : cur;
        Pos& e = fwd ? cur : anchor;
        s.x = 0;
        e.x = len(e.y);
        e = next(e);
    } else if (fwd) {
        cur = next(cur);
    } else {
        anchor = next(anchor);
    }
    return !(anchor == cur);
}

void Vim::afterExternal() {
    if (mode == VM_CMDLINE) return;
    bool sel = ed.anch && !(ed.anchor == Pos{ed.cy, ed.cx});
    if (visualActive()) {
        if (!ed.anch) {
            mode = VM_NORMAL;
            blockDollar = false;
        }
    } else if (sel) {
        if (mode == VM_INSERT || mode == VM_REPLACE) {
            Pos a = ed.anchor, c{ed.cy, ed.cx};
            insEsc(true);
            ed.anch = true;
            ed.anchor = a;
            ed.cy = c.y;
            ed.cx = c.x;
        }
        mode = VM_VISUAL;  // the character under the mouse pointer is included
    } else {
        ed.anch = false;
    }
    ed.clampCursor();
    if (mode != VM_INSERT && mode != VM_REPLACE) clampNormal();
    curswant = vcol(ed.cy, ed.cx);
    cmdbuf.clear();
    pendNeedArg = pendOp = false;
}

void Vim::pasteText(const u32& t) {
    if (t.empty()) return;
    switch (mode) {
        case VM_CMDLINE: {
            u32 s = t.substr(0, t.find('\n'));
            cmdText.insert(size_t(cmdPos), s);
            cmdPos += int(s.size());
            return;
        }
        case VM_INSERT: case VM_REPLACE:
            insText(Pos{ed.cy, ed.cx}, t);
            insTyped += t;
            return;
        default: break;
    }
    beginChange();
    if (visualActive()) {
        VimCmd c;
        c.reg = '_';
        c.op = U"d";
        visualCommand(c);
    }
    Pos p{ed.cy, ed.cx};
    insText(p, t);
    setMark('[', p);
    setMark(']', Pos{ed.cy, std::max(0, ed.cx - 1)});
    recordChange(Pos{ed.cy, ed.cx});
    if (ed.cx > 0) ed.cx--;
    endChange();
    clampNormal();
    curswant = vcol(ed.cy, ed.cx);
}

void Vim::menuCopy(bool cut) {
    if (!visualActive()) return;
    VimCmd c;
    c.reg = '+';
    c.op = cut ? U"d" : U"y";
    beginChange();
    visualCommand(c);
    endChange();
}

// ---------------------------------------------------------------------------
// Buffer primitives
// ---------------------------------------------------------------------------

void Vim::setLine(int y, const u32& s) {
    if (ed.lines[y] == s) return;
    ed.lines[y] = s;
    ed.touch(y);
    ed.modified = true;
}

void Vim::insLines(int y, const std::vector<u32>& ls) {
    if (ls.empty()) return;
    y = std::clamp(y, 0, n());
    ed.lines.insert(ed.lines.begin() + y, ls.begin(), ls.end());
    ed.touch(std::max(0, y - 1));
    ed.modified = true;
    adjustMarks(y, int(ls.size()));
}

void Vim::delLines(int y1, int y2) {
    y1 = std::max(0, y1);
    y2 = std::min(n() - 1, y2);
    if (y2 < y1) return;
    int k = y2 - y1 + 1;
    if (k >= n()) {
        ed.lines.assign(1, u32());
    } else {
        ed.lines.erase(ed.lines.begin() + y1, ed.lines.begin() + y2 + 1);
    }
    ed.touch(std::max(0, y1 - 1));
    ed.modified = true;
    adjustMarks(y1, -k, y1, y2);
    ed.clampCursor();
}

void Vim::replaceLines(int y1, int y2, const std::vector<u32>& ls) {
    int old = y2 - y1 + 1, k = int(ls.size());
    int common = std::min(old, k);
    for (int i = 0; i < common; i++) setLine(y1 + i, ls[size_t(i)]);
    if (k > old) insLines(y1 + old, std::vector<u32>(ls.begin() + old, ls.end()));
    else if (k < old) {
        if (k == 0 && old == n()) {
            ed.lines.assign(1, u32());
            ed.touch(0);
            ed.modified = true;
            adjustMarks(0, -old, 0, y2);
        } else {
            delLines(y1 + k, y2);
        }
    }
}

void Vim::insText(Pos p, const u32& t) {
    if (t.empty()) {
        ed.cy = p.y;
        ed.cx = p.x;
        return;
    }
    bool anch = ed.anch;
    Pos anchor = ed.anchor;
    ed.cy = p.y;
    ed.cx = std::min(p.x, len(p.y));
    ed.insert(t);
    int nl = int(std::count(t.begin(), t.end(), U'\n'));
    if (nl) adjustMarks(p.x == 0 && t.back() == '\n' ? p.y : p.y + 1, nl);
    ed.anch = anch;
    ed.anchor = anchor;
}

void Vim::delText(Pos a, Pos b) {
    if (!(a < b)) return;
    bool anch = ed.anch;
    Pos anchor = ed.anchor;
    ed.deleteRange(a, b);
    if (b.y > a.y) adjustMarks(a.y + 1, -(b.y - a.y), a.y + 1, b.y);
    ed.anch = anch;
    ed.anchor = anchor;
}

void Vim::adjustMarks(int y, int delta, int delFrom, int delTo) {
    auto adj = [&](Pos& p) -> bool {  // false = the position was deleted
        if (delta > 0) {
            if (p.y >= y) p.y += delta;
        } else if (delta < 0) {
            if (p.y >= delFrom && p.y <= delTo) {
                p.y = std::max(0, delFrom - 1 >= 0 ? delFrom : 0);
                return false;
            }
            if (p.y > delTo) p.y += delta;
        }
        return true;
    };
    for (auto it = marks.begin(); it != marks.end();) {
        bool keep = adj(it->second);
        char32_t m = it->first;
        if (!keep && ((m >= 'a' && m <= 'z') || (m >= 'A' && m <= 'Z'))) it = marks.erase(it);
        else ++it;
    }
    for (auto& p : jumps) adj(p);
    for (auto& p : changes) adj(p);
    if (lastVStart.y >= 0) { adj(lastVStart); adj(lastVEnd); }
    if (trackLines) {
        for (int& l : *trackLines) {
            if (l < 0) continue;
            Pos p{l, 0};
            if (!adj(p)) l = -1;
            else l = p.y;
        }
    }
}

void Vim::beginChange() {
    if (changeDepth++ == 0) {
        savedRedo = std::move(ed.redo);
        ed.redo.clear();
        savedModified = ed.modified;
        ed.checkpoint(0);
        ed.redo.clear();
    }
}

void Vim::endChange() {
    if (changeDepth <= 0) { changeDepth = 0; return; }
    if (--changeDepth == 0) {
        if (!ed.undo.empty() && ed.undo.back().lines == ed.lines) {
            ed.undo.pop_back();
            ed.redo = std::move(savedRedo);
            ed.modified = savedModified;
        }
        savedRedo.clear();
        ed.lastEdit = 0;
    }
}

void Vim::setCursor(int y, int x) {
    ed.cy = std::clamp(y, 0, n() - 1);
    ed.cx = std::clamp(x, 0, len(ed.cy));
}

void Vim::clampNormal() {
    ed.cy = std::clamp(ed.cy, 0, n() - 1);
    int L = len(ed.cy);
    ed.cx = std::clamp(ed.cx, 0, std::max(0, L - 1));
}

int Vim::firstNonBlank(int y) const {
    const u32& l = line(y);
    int x = 0;
    while (x < int(l.size()) && (l[x] == ' ' || l[x] == '\t')) x++;
    if (x >= int(l.size())) x = std::max(0, int(l.size()) - 1);
    return x;
}

void Vim::toFirstNonBlank(int y) {
    ed.cy = std::clamp(y, 0, n() - 1);
    ed.cx = firstNonBlank(ed.cy);
    curswant = vcol(ed.cy, ed.cx);
}

int Vim::idxAtCol(int y, int col) const { return ed.idxOf(line(y), col); }

u32 Vim::indentOf(int y) const {
    const u32& l = line(y);
    size_t i = 0;
    while (i < l.size() && (l[i] == ' ' || l[i] == '\t')) i++;
    return l.substr(0, i);
}

int Vim::indentWidth(int y) const { return ed.colOf(line(y), int(indentOf(y).size())); }

u32 Vim::makeIndent(int w) const {
    w = std::max(0, w);
    if (S.expandtab) return u32(size_t(w), ' ');
    return u32(size_t(w / S.tabw), '\t') + u32(size_t(w % S.tabw), ' ');
}

void Vim::setIndent(int y, int w) {
    u32 old = indentOf(y);
    u32 ni = makeIndent(w);
    setLine(y, ni + line(y).substr(old.size()));
}

void Vim::setMark(char32_t m, Pos p) {
    if (m == '`') m = '\'';
    if (m == '<') { lastVStart = p; return; }
    if (m == '>') { lastVEnd = p; return; }
    marks[m] = p;
}

bool Vim::getMark(char32_t m, Pos& p) {
    if (m == '`') m = '\'';
    if (m == '<' || m == '>') {
        if (lastVStart.y < 0) return false;
        Pos a = lastVStart, b = lastVEnd;
        if (b < a) std::swap(a, b);
        if (lastVMode == VM_VLINE) {
            a.x = 0;
            b.x = std::max(0, len(std::min(b.y, n() - 1)) - 1);
        }
        p = m == '<' ? a : b;
    } else {
        auto it = marks.find(m);
        if (it == marks.end()) {
            if (m == '\'') { p = {0, 0}; return true; }
            return false;
        }
        p = it->second;
    }
    p.y = std::clamp(p.y, 0, n() - 1);
    p.x = std::clamp(p.x, 0, len(p.y));
    return true;
}

void Vim::pushJump() {
    Pos c{ed.cy, ed.cx};
    marks['\''] = c;
    for (size_t i = 0; i < jumps.size(); i++)
        if (jumps[i].y == c.y) { jumps.erase(jumps.begin() + long(i)); break; }
    jumps.push_back(c);
    if (jumps.size() > 100) jumps.erase(jumps.begin());
    jumpIdx = int(jumps.size());
}

void Vim::recordChange(Pos p) {
    marks['.'] = p;
    if (changes.empty() || changes.back().y != p.y) changes.push_back(p);
    else changes.back() = p;
    if (changes.size() > 100) changes.erase(changes.begin());
    changeIdx = int(changes.size());
}

// ---------------------------------------------------------------------------
// Registers
// ---------------------------------------------------------------------------

bool Vim::validReg(char32_t r, bool write) const {
    if ((r >= 'a' && r <= 'z') || (r >= 'A' && r <= 'Z') || (r >= '0' && r <= '9')) return true;
    if (r == '"' || r == '-' || r == '*' || r == '+' || r == '_') return true;
    if (!write && (r == '.' || r == ':' || r == '/' || r == '%')) return true;
    return false;
}

VimReg Vim::getReg(char32_t r) {
    if (r == 0 || r == '"') {
        if (opt.clipboard.find("unnamed") != std::string::npos) r = '+';
        else return regs['"'];
    }
    if (r >= 'A' && r <= 'Z') r = r - 'A' + 'a';
    VimReg v;
    switch (r) {
        case '+': case '*': {
            u32 t = host->clipboardGet();
            auto it = regs.find('+');
            if (it != regs.end() && it->second.text() == t) return it->second;  // keeps a block type
            return regFromText(t);
        }
        case '.': v.lines = {lastInserted}; break;
        case ':': v.lines = {lastExCmd}; break;
        case '/': v.lines = {lastPat}; break;
        case '%': v.lines = {U(ed.filename)}; break;
        case '_': return v;
        default: {
            auto it = regs.find(r);
            if (it != regs.end()) return it->second;
            return v;
        }
    }
    if (v.lines.size() == 1 && v.lines[0].empty()) v.lines.clear();
    return v;
}

void Vim::setReg(char32_t r, const VimReg& v, bool yank, bool smallDelete) {
    if (r == '_') return;
    bool clipSync = opt.clipboard.find("unnamed") != std::string::npos;
    auto shift = [&] {
        for (char32_t i = '9'; i > '1'; i--) regs[i] = regs[i - 1];
        regs['1'] = v;
    };
    if (r == 0 || r == '"') {
        if (yank) regs['0'] = v;
        else if (smallDelete) regs['-'] = v;
        else shift();
        regs['"'] = v;
        if (clipSync) {
            regs['+'] = v;
            host->clipboardSet(v.text());
        }
        return;
    }
    if (r >= 'A' && r <= 'Z') {
        VimReg& d = regs[r - 'A' + 'a'];
        if (d.empty()) {
            d = v;
        } else if (v.type == RT_LINE || d.type == RT_LINE) {
            d.type = RT_LINE;
            d.lines.insert(d.lines.end(), v.lines.begin(), v.lines.end());
        } else {
            d.lines.back() += v.lines.empty() ? u32() : v.lines[0];
            for (size_t i = 1; i < v.lines.size(); i++) d.lines.push_back(v.lines[i]);
        }
        regs['"'] = d;
    } else if (r == '+' || r == '*') {
        regs['+'] = v;
        regs['"'] = v;
        host->clipboardSet(v.text());
    } else {
        regs[r] = v;
        regs['"'] = v;
    }
    if (!yank && !smallDelete) shift();
}

VimReg Vim::textReg(Pos a, Pos b, int type) const {
    VimReg r;
    r.type = type;
    if (type == RT_LINE) {
        for (int y = a.y; y <= b.y; y++) r.lines.push_back(line(y));
        return r;
    }
    if (a.y == b.y) {
        r.lines.push_back(line(a.y).substr(size_t(a.x), size_t(std::max(0, b.x - a.x))));
        return r;
    }
    r.lines.push_back(line(a.y).substr(size_t(std::min(a.x, len(a.y)))));
    for (int y = a.y + 1; y < b.y; y++) r.lines.push_back(line(y));
    r.lines.push_back(line(b.y).substr(0, size_t(b.x)));
    return r;
}

void Vim::put(char32_t reg, int count, bool after, bool gp, bool adjustIndent) {
    VimReg r = getReg(reg);
    if (r.empty()) {
        u32 name = reg ? u32(1, reg) : u32(U"\"");
        error(tr(U"E353: Nothing in register ") + name);
        return;
    }
    count = std::max(1, count);
    beginChange();
    if (r.type == RT_LINE) {
        int y = after ? ed.cy + 1 : ed.cy;
        std::vector<u32> ls;
        for (int i = 0; i < count; i++) ls.insert(ls.end(), r.lines.begin(), r.lines.end());
        if (adjustIndent) {
            int from = -1;
            for (auto& l : ls) {
                size_t i = 0;
                while (i < l.size() && (l[i] == ' ' || l[i] == '\t')) i++;
                if (i < l.size()) { from = ed.colOf(l, int(i)); break; }
            }
            int delta = from < 0 ? 0 : indentWidth(ed.cy) - from;
            for (auto& l : ls) {
                size_t i = 0;
                while (i < l.size() && (l[i] == ' ' || l[i] == '\t')) i++;
                if (i == l.size()) continue;
                l = makeIndent(ed.colOf(l, int(i)) + delta) + l.substr(i);
            }
        }
        insLines(y, ls);
        setMark('[', Pos{y, 0});
        setMark(']', Pos{y + int(ls.size()) - 1, 0});
        if (gp) {
            int ny = y + int(ls.size());
            if (ny >= n()) ny = n() - 1;
            setCursor(ny, 0);
            curswant = 0;
        } else {
            toFirstNonBlank(y);
        }
        recordChange(Pos{y, 0});
    } else if (r.type == RT_CHAR) {
        Pos p{ed.cy, ed.cx};
        if (after && len(p.y) > 0) p.x = std::min(p.x + 1, len(p.y));
        u32 one = r.text(), t;
        for (int i = 0; i < count; i++) t += one;
        insText(p, t);
        Pos end{ed.cy, ed.cx};
        setMark('[', p);
        setMark(']', Pos{end.y, std::max(0, end.x - 1)});
        if (gp) {
            ed.cy = end.y;
            ed.cx = end.x;
            if (ed.cx >= len(ed.cy) && ed.cy + 1 < n() && t.find('\n') != u32::npos) { ed.cy++; ed.cx = 0; }
        } else if (t.find('\n') != u32::npos) {
            ed.cy = p.y;
            ed.cx = p.x;
        } else {
            ed.cy = end.y;
            ed.cx = std::max(0, end.x - 1);
        }
        recordChange(end);
        curswant = vcol(ed.cy, ed.cx);
    } else {
        int col = vcol(ed.cy, ed.cx);
        if (after && len(ed.cy) > 0) col += std::max(1, ed.colOf(line(ed.cy), ed.cx + 1) - col);
        int width = 0;
        for (auto& l : r.lines) width = std::max(width, strWidth(l));
        int y0 = ed.cy;
        for (size_t i = 0; i < r.lines.size(); i++) {
            int y = y0 + int(i);
            if (y >= n()) insLines(n(), {u32()});
            expandTabsIn(y, col, col);
            u32 l = line(y);
            int lw = ed.colOf(l, int(l.size()));
            if (lw < col) l += u32(size_t(col - lw), ' ');
            int idx = idxAtCol(y, col);
            if (lw < col) idx = int(l.size());
            u32 piece;
            for (int k = 0; k < count; k++) {
                piece += r.lines[i];
                bool last = k == count - 1;
                if (!last || idx < int(l.size())) piece += u32(size_t(width - strWidth(r.lines[i])), ' ');
            }
            l.insert(size_t(idx), piece);
            setLine(y, l);
        }
        int idx0 = idxAtCol(y0, col);
        setMark('[', Pos{y0, idx0});
        setMark(']', Pos{y0 + int(r.lines.size()) - 1, idx0});
        if (gp) {
            int ly = y0 + int(r.lines.size()) - 1;
            setCursor(ly, idxAtCol(ly, col + width * count));
        } else {
            setCursor(y0, idx0);
        }
        recordChange(Pos{y0, idx0});
        curswant = vcol(ed.cy, ed.cx);
    }
    endChange();
    if (mode == VM_NORMAL && !gp) clampNormal();
    else if (mode == VM_NORMAL) {
        if (ed.cx > len(ed.cy)) ed.cx = len(ed.cy);
        if (ed.cx >= len(ed.cy)) clampNormal();
    }
}

// ---------------------------------------------------------------------------
// Insert and Replace mode
// ---------------------------------------------------------------------------

void Vim::startInsert(char32_t cmd, int count, bool replace) {
    mode = replace ? VM_REPLACE : VM_INSERT;
    insCount = std::max(1, count);
    insCmd = cmd;
    insKeys.clear();
    insTyped.clear();
    insStart = {ed.cy, ed.cx};
    replStack.clear();
    insPending = 0;
    cmpl.on = false;
    ed.anch = false;
    beginChange();
}

static const char32_t DIGRAPHS[][3] = {
    {'a', ':', U'ä'}, {'o', ':', U'ö'}, {'u', ':', U'ü'}, {'A', ':', U'Ä'}, {'O', ':', U'Ö'}, {'U', ':', U'Ü'},
    {'e', ':', U'ë'}, {'i', ':', U'ï'}, {'y', ':', U'ÿ'}, {'E', ':', U'Ë'}, {'I', ':', U'Ï'}, {'s', 's', U'ß'},
    {'a', '\'', U'á'}, {'e', '\'', U'é'}, {'i', '\'', U'í'}, {'o', '\'', U'ó'}, {'u', '\'', U'ú'}, {'y', '\'', U'ý'},
    {'A', '\'', U'Á'}, {'E', '\'', U'É'}, {'I', '\'', U'Í'}, {'O', '\'', U'Ó'}, {'U', '\'', U'Ú'}, {'Y', '\'', U'Ý'},
    {'a', '!', U'à'}, {'e', '!', U'è'}, {'i', '!', U'ì'}, {'o', '!', U'ò'}, {'u', '!', U'ù'},
    {'A', '!', U'À'}, {'E', '!', U'È'}, {'I', '!', U'Ì'}, {'O', '!', U'Ò'}, {'U', '!', U'Ù'},
    {'a', '>', U'â'}, {'e', '>', U'ê'}, {'i', '>', U'î'}, {'o', '>', U'ô'}, {'u', '>', U'û'},
    {'A', '>', U'Â'}, {'E', '>', U'Ê'}, {'I', '>', U'Î'}, {'O', '>', U'Ô'}, {'U', '>', U'Û'},
    {'a', '?', U'ã'}, {'n', '?', U'ñ'}, {'o', '?', U'õ'}, {'A', '?', U'Ã'}, {'N', '?', U'Ñ'}, {'O', '?', U'Õ'},
    {'c', ',', U'ç'}, {'C', ',', U'Ç'}, {'a', 'a', U'å'}, {'A', 'A', U'Å'}, {'a', 'e', U'æ'}, {'A', 'E', U'Æ'},
    {'o', '/', U'ø'}, {'O', '/', U'Ø'}, {'E', 'u', U'€'}, {'=', 'e', U'€'}, {'P', 'd', U'£'}, {'Y', 'e', U'¥'},
    {'C', 'o', U'©'}, {'R', 'g', U'®'}, {'D', 'G', U'°'}, {'+', '-', U'±'}, {'<', '<', U'«'}, {'>', '>', U'»'},
    {'!', 'I', U'¡'}, {'?', 'I', U'¿'}, {'M', 'y', U'µ'}, {'S', 'E', U'§'}, {'P', 'I', U'¶'}, {'*', 'X', U'×'},
    {'-', ':', U'÷'}, {'a', '*', U'α'}, {'b', '*', U'β'}, {'g', '*', U'γ'}, {'d', '*', U'δ'}, {'e', '*', U'ε'},
    {'p', '*', U'π'}, {'l', '*', U'λ'}, {'m', '*', U'μ'}, {'s', '*', U'σ'}, {'w', '*', U'ω'}, {'O', 'K', U'✓'},
    {'-', '>', U'→'}, {'<', '-', U'←'}, {'1', '2', U'½'}, {'1', '4', U'¼'}, {'3', '4', U'¾'}, {'1', 'S', U'¹'},
    {'2', 'S', U'²'}, {'3', 'S', U'³'}, {'N', 'S', 0xA0}, {'-', 'N', U'–'}, {'-', 'M', U'—'}, {'"', '6', U'“'},
    {'"', '9', U'”'}, {'\'', '6', U'‘'}, {'\'', '9', U'’'}, {'.', 'M', U'·'}, {':', '-', U'÷'},
};

void Vim::insDigraph(char32_t a, char32_t b) {
    for (auto& d : DIGRAPHS)
        if ((d[0] == a && d[1] == b) || (d[0] == b && d[1] == a)) { insChar(d[2]); return; }
    insChar(b);
}

void Vim::insertKey(char32_t c) {
    if (insPending) {
        int p = insPending;
        if (p == 1) {  // Ctrl-V: literal character or code
            if (insPendBuf.empty()) {
                if (c == 'x' || c == 'X' || c == 'u' || c == 'U' || c == 'o' || c == 'O' || (c >= '0' && c <= '9')) {
                    insPendBuf += c;
                    insKeys += c;
                    return;
                }
                insPending = 0;
                insKeys += c;
                if (VK::special(c) && c != VK::NUL) {
                    for (char32_t ch : vimKeyNames(u32(1, c))) insChar(ch);
                } else {
                    insChar(c == VK::NUL ? 0 : c);
                }
                return;
            }
            char32_t k = insPendBuf[0];
            int base = (k == 'x' || k == 'X' || k == 'u' || k == 'U') ? 16 : (k == 'o' || k == 'O') ? 8 : 10;
            int maxd = (k == 'x' || k == 'X') ? 2 : k == 'u' ? 4 : k == 'U' ? 8 : 3;
            u32 digits = base == 10 ? insPendBuf : insPendBuf.substr(1);
            auto isd = [&](char32_t ch) {
                if (base == 16) return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
                if (base == 8) return ch >= '0' && ch <= '7';
                return ch >= '0' && ch <= '9';
            };
            auto finish = [&] {
                u32 dg = base == 10 ? insPendBuf : insPendBuf.substr(1);
                insPending = 0;
                insPendBuf.clear();
                if (dg.empty()) {
                    insChar(k);
                    return;
                }
                char32_t v = 0;
                for (char32_t ch : dg) v = v * base + char32_t(ch <= '9' ? ch - '0' : (ch | 0x20) - 'a' + 10);
                if (base == 10 && v > 255) v = 255;
                insChar(v);
            };
            if (isd(c) && int(digits.size()) < maxd) {
                insPendBuf += c;
                insKeys += c;
                digits += c;
                if (int(digits.size()) >= maxd) finish();
                return;
            }
            finish();
            // the key that ended the code is handled normally
        } else if (p == 2) {  // Ctrl-R {register}
            insKeys += c;
            if (c == VK::ctrl('r') || c == VK::ctrl('o') || c == VK::ctrl('p')) return;
            insPending = 0;
            if (!validReg(c, false) && c != '"') { beep(); return; }
            VimReg r = getReg(c);
            u32 t = r.text();
            if (!t.empty()) {
                insText(Pos{ed.cy, ed.cx}, t);
                insTyped += t;
            }
            return;
        } else if (p == 3) {  // Ctrl-K first character
            insKeys += c;
            if (c == VK::ESC) { insPending = 0; return; }
            insPendBuf = u32(1, c);
            insPending = 4;
            return;
        } else if (p == 4) {
            insKeys += c;
            insPending = 0;
            if (c == VK::ESC) return;
            insDigraph(insPendBuf[0], c);
            return;
        } else if (p == 5) {  // Ctrl-G
            insKeys += c;
            insPending = 0;
            if (c == 'u') {
                endChange();
                beginChange();
            }
            return;
        }
    }
    if (cmpl.on && c != VK::ctrl('n') && c != VK::ctrl('p')) cmpl.on = false;
    bool movement = c == VK::UP || c == VK::DOWN || c == VK::LEFT || c == VK::RIGHT || c == VK::HOME ||
                    c == VK::END || c == VK::PGUP || c == VK::PGDN ||
                    (VK::special(c) && c != VK::DEL && c != VK::INS && c != VK::NUL && c != VK::STAB);
    if (c != VK::ESC && c != VK::ctrl('c') && c != VK::ctrl('o') && !movement) insKeys += c;
    switch (c) {
        case VK::ESC: insEsc(false); return;
        case VK::ctrl('c'): insEsc(true); return;
        case VK::CR: case VK::NL: insNewline(); return;
        case VK::BS: insBackspace(); return;
        case VK::DEL: {
            if (ed.cx < len(ed.cy)) delText(Pos{ed.cy, ed.cx}, Pos{ed.cy, ed.cx + 1});
            else if (ed.cy + 1 < n()) delText(Pos{ed.cy, ed.cx}, Pos{ed.cy + 1, 0});
            return;
        }
        case VK::TAB: {
            if (S.expandtab) {
                int col = vcol(ed.cy, ed.cx);
                int k = S.tabw - col % S.tabw;
                for (int i = 0; i < k; i++) insChar(' ');
            } else {
                insChar('\t');
            }
            return;
        }
        case VK::STAB: return;
        case VK::ctrl('w'): {
            if (ed.cx == 0) { insBackspace(); return; }
            const u32& l = line(ed.cy);
            int x = ed.cx;
            while (x > 0 && (l[x - 1] == ' ' || l[x - 1] == '\t')) x--;
            if (x > 0 && isWord(l[x - 1])) {
                while (x > 0 && isWord(l[x - 1])) x--;
            } else if (x > 0) {
                while (x > 0 && !isWord(l[x - 1]) && l[x - 1] != ' ' && l[x - 1] != '\t') x--;
            }
            if (ed.cy == insStart.y && insStart.x < ed.cx && x < insStart.x && ed.cx > insStart.x) x = insStart.x;
            delText(Pos{ed.cy, x}, Pos{ed.cy, ed.cx});
            if (insStart.y == ed.cy && insStart.x > x) insStart.x = x;
            insTyped.clear();
            return;
        }
        case VK::ctrl('u'): {
            int x;
            if (ed.cy == insStart.y && insStart.x < ed.cx) x = insStart.x;
            else {
                int ind = int(indentOf(ed.cy).size());
                x = ed.cx > ind ? ind : 0;
            }
            if (ed.cx == 0) { insBackspace(); return; }
            delText(Pos{ed.cy, x}, Pos{ed.cy, ed.cx});
            if (insStart.y == ed.cy) insStart.x = std::min(insStart.x, x);
            insTyped.clear();
            return;
        }
        case VK::ctrl('t'): case VK::ctrl('d'): {
            const u32& l = line(ed.cy);
            int old = int(indentOf(ed.cy).size());
            if (c == VK::ctrl('d') && ed.cx > 0 && (l[ed.cx - 1] == '0' || l[ed.cx - 1] == '^') &&
                !insTyped.empty() && (insTyped.back() == '0' || insTyped.back() == '^')) {
                delText(Pos{ed.cy, ed.cx - 1}, Pos{ed.cy, ed.cx});
                insTyped.pop_back();
                old = int(indentOf(ed.cy).size());
                int keep = ed.cx - old;
                setIndent(ed.cy, 0);
                ed.cx = std::max(0, keep);
                return;
            }
            int w = indentWidth(ed.cy), sw = shiftWidth();
            int nw = c == VK::ctrl('t') ? (w / sw + 1) * sw : std::max(0, (w - 1) / sw * sw);
            int rel = ed.cx - old;
            setIndent(ed.cy, nw);
            int ni = int(indentOf(ed.cy).size());
            ed.cx = std::max(ni, ni + rel);
            if (rel < 0) ed.cx = ni;
            aiLine = -1;
            return;
        }
        case VK::ctrl('r'): insPending = 2; return;
        case VK::ctrl('v'): case VK::ctrl('q'): insPending = 1; insPendBuf.clear(); return;
        case VK::ctrl('k'): insPending = 3; return;
        case VK::ctrl('g'): insPending = 5; return;
        case VK::ctrl('x'): return;
        case VK::ctrl('a'): {
            u32 t = lastInserted;
            for (char32_t ch : t) {
                if (ch == '\n') insNewline();
                else insChar(ch);
            }
            return;
        }
        case VK::NUL: {
            u32 t = lastInserted;
            for (char32_t ch : t) {
                if (ch == '\n') insNewline();
                else insChar(ch);
            }
            insEsc(false);
            return;
        }
        case VK::ctrl('e'): case VK::ctrl('y'): {
            int y = ed.cy + (c == VK::ctrl('e') ? 1 : -1);
            if (y < 0 || y >= n()) { beep(); return; }
            int col = vcol(ed.cy, ed.cx);
            int idx = idxAtCol(y, col);
            if (idx >= len(y) || ed.colOf(line(y), idx) != col) { beep(); return; }
            insChar(line(y)[size_t(idx)]);
            return;
        }
        case VK::ctrl('o'): {
            insOneCmd = true;
            insOneEol = ed.cx >= len(ed.cy) && len(ed.cy) > 0;
            if (dotRecording && !replaying) {
                dotNew.ins = insKeys + VK::ESC;
                dot = dotNew;
            }
            dotRecording = false;
            lastInserted = insTyped;
            endChange();
            mode = VM_NORMAL;
            if (insOneEol) ed.cx = len(ed.cy) - 1;
            curswant = vcol(ed.cy, ed.cx);
            return;
        }
        case VK::ctrl('n'): insComplete(true); return;
        case VK::ctrl('p'): insComplete(false); return;
        case VK::INS: mode = mode == VM_INSERT ? VM_REPLACE : VM_INSERT; replStack.clear(); return;
    }
    if (movement) {
        int y = ed.cy, x = ed.cx;
        int code = int((c - VK::SPECIAL) / 8), mods = int((c - VK::SPECIAL) % 8);
        ed.moveKey(code, mods & M_CTRL, host->viewHeight());
        ed.anch = false;
        if (!(Pos{y, x} == Pos{ed.cy, ed.cx})) {
            // moving the cursor starts a new insert (undo step and repeat)
            lastInserted = insTyped;
            insKeys.clear();
            insTyped.clear();
            insCount = 1;
            insStart = {ed.cy, ed.cx};
            dotRecording = false;
            endChange();
            beginChange();
            aiLine = -1;
        }
        return;
    }
    if (c >= 32 && !VK::special(c)) insChar(c);
    else if (c > 0 && c < 32 && c != 27) insChar(c);  // other control characters are inserted as is
}

void Vim::insChar(char32_t c) {
    int y = ed.cy, x = ed.cx;
    if (mode == VM_REPLACE && x < len(y)) {
        u32 l = line(y);
        replStack.push_back({Pos{y, x}, int(l[size_t(x)])});
        l[size_t(x)] = c;
        setLine(y, l);
        ed.cx = x + 1;
    } else {
        if (mode == VM_REPLACE) replStack.push_back({Pos{y, x}, -1});
        u32 l = line(y);
        l.insert(size_t(x), 1, c);
        setLine(y, l);
        ed.cx = x + 1;
    }
    insTyped += c;
    if (aiLine == y) aiLine = -1;
    // automatic line break at 'textwidth'
    if (opt.textwidth > 0 && c != ' ' && c != '\t' && vcol(y, ed.cx) > opt.textwidth) {
        const u32& l = line(y);
        int ind = int(indentOf(y).size());
        int b = -1;
        for (int i = ed.cx - 1; i > ind; i--)
            if ((l[size_t(i)] == ' ' || l[size_t(i)] == '\t') && vcol(y, i) <= opt.textwidth) { b = i; break; }
        if (b > 0) {
            int s = b;
            while (s > ind && (l[size_t(s - 1)] == ' ' || l[size_t(s - 1)] == '\t')) s--;
            int e = b + 1;
            int rel = ed.cx - e;
            u32 head = l.substr(0, size_t(s)), tail = l.substr(size_t(e));
            u32 in = S.autoindent ? indentOf(y) : u32();
            setLine(y, head);
            insLines(y + 1, {in + tail});
            ed.cy = y + 1;
            ed.cx = int(in.size()) + rel;
        }
    }
}

void Vim::insNewline() {
    int y = ed.cy;
    u32 l = line(y);
    u32 head = l.substr(0, size_t(ed.cx)), tail = l.substr(size_t(ed.cx));
    u32 in;
    if (S.autoindent) {
        in = indentOf(y);
        if (in.size() > head.size()) in = in.substr(0, head.size());
        size_t i = 0;
        while (i < tail.size() && (tail[i] == ' ' || tail[i] == '\t')) i++;
        tail = tail.substr(i);
    }
    if (aiLine == y && std::all_of(head.begin(), head.end(), [](char32_t ch) { return ch == ' ' || ch == '\t'; }))
        head.clear();
    if (mode == VM_REPLACE) replStack.clear();
    setLine(y, head);
    insLines(y + 1, {in + tail});
    ed.cy = y + 1;
    ed.cx = int(in.size());
    aiLine = in.empty() ? -1 : y + 1;
    insTyped += '\n';
}

void Vim::insBackspace() {
    if (mode == VM_REPLACE) {
        if (ed.cx == 0) {
            if (ed.cy > 0 && !(insStart.y == ed.cy && insStart.x == 0)) { ed.cy--; ed.cx = len(ed.cy); }
            return;
        }
        if (!replStack.empty() && replStack.back().first == Pos{ed.cy, ed.cx - 1}) {
            auto e = replStack.back();
            replStack.pop_back();
            u32 l = line(ed.cy);
            if (e.second < 0) l.erase(size_t(ed.cx - 1), 1);
            else l[size_t(ed.cx - 1)] = char32_t(e.second);
            setLine(ed.cy, l);
        }
        ed.cx--;
        if (!insTyped.empty()) insTyped.pop_back();
        return;
    }
    if (ed.cx > 0) {
        delText(Pos{ed.cy, ed.cx - 1}, Pos{ed.cy, ed.cx});
        if (!insTyped.empty()) insTyped.pop_back();
        if (insStart.y == ed.cy && insStart.x > ed.cx) insStart.x = ed.cx;
    } else if (ed.cy > 0) {
        int py = ed.cy - 1, px = len(py);
        delText(Pos{py, px}, Pos{ed.cy, 0});
        if (!insTyped.empty()) insTyped.pop_back();
        if (insStart.y > py) insStart = {py, px};
    }
}

void Vim::insReplay(const u32& keys) {
    for (char32_t c : keys) insertKey(c);
}

void Vim::insEsc(bool interrupt) {
    insPending = 0;
    cmpl.on = false;
    if (!interrupt && insCount > 1) {
        u32 keys = insKeys;
        int cnt = insCount;
        insCount = 1;
        for (int i = 1; i < cnt; i++) {
            if (insCmd == 'o' || insCmd == 'O') {
                ed.cx = len(ed.cy);
                insNewline();
            }
            insReplay(keys);
        }
        insKeys = keys;
    }
    if (blk.on) {
        blk.on = false;
        // the text typed in the first line is inserted in all other lines of the block
        u32 now = line(blk.y1);
        int startIdx = blk.dollar ? int(blk.before.size()) : idxAtCol(blk.y1, blk.col);
        if (!blk.dollar && ed.colOf(blk.before, int(blk.before.size())) < blk.col) startIdx = int(blk.before.size());
        if (ed.cy == blk.y1 && int(now.size()) >= int(blk.before.size()) && now.find('\n') == u32::npos &&
            now.substr(0, size_t(startIdx)) ==
                blk.before.substr(0, size_t(std::min<size_t>(startIdx, blk.before.size()))) &&
            now.substr(now.size() - (blk.before.size() - size_t(std::min<size_t>(startIdx, blk.before.size())))) ==
                blk.before.substr(size_t(std::min<size_t>(startIdx, blk.before.size())))) {
            size_t ins = now.size() - blk.before.size();
            u32 t = now.substr(size_t(startIdx), ins);
            if (!t.empty()) {
                for (int y = blk.y1 + 1; y <= blk.y2 && y < n(); y++) {
                    if (blk.dollar) {
                        setLine(y, line(y) + t);
                        continue;
                    }
                    int lw = ed.colOf(line(y), len(y));
                    if (lw < blk.col) {
                        if (!blk.append) continue;  // short lines are not changed by "I"
                        setLine(y, line(y) + u32(size_t(blk.col - lw), ' ') + t);
                        continue;
                    }
                    expandTabsIn(y, blk.col, blk.col);
                    u32 l = line(y);
                    l.insert(size_t(idxAtCol(y, blk.col)), t);
                    setLine(y, l);
                }
            }
        }
        ed.cy = blk.y1;
        ed.cx = std::min(startIdx + 1, len(blk.y1));
    }
    if (aiLine == ed.cy && ed.cy < n()) {
        const u32& l = line(ed.cy);
        if (std::all_of(l.begin(), l.end(), [](char32_t ch) { return ch == ' ' || ch == '\t'; })) {
            setLine(ed.cy, u32());
            ed.cx = 0;
        }
    }
    aiLine = -1;
    lastInserted = insTyped;
    setMark('^', Pos{ed.cy, ed.cx});
    setMark(']', Pos{ed.cy, std::max(0, ed.cx - 1)});
    if (!(insStart == Pos{ed.cy, ed.cx}) || !insTyped.empty()) recordChange(Pos{ed.cy, std::max(0, ed.cx - 1)});
    mode = VM_NORMAL;
    if (ed.cx > 0) ed.cx--;
    clampNormal();
    curswant = vcol(ed.cy, ed.cx);
    endChange();
    if (dotRecording && !replaying) {
        dotNew.ins = insKeys + VK::ESC;
        dot = dotNew;
    }
    dotRecording = false;
    replStack.clear();
}

void Vim::insComplete(bool fwd) {
    if (!cmpl.on) {
        const u32& l = line(ed.cy);
        int x = ed.cx;
        while (x > 0 && isWord(l[size_t(x - 1)])) x--;
        cmpl.start = {ed.cy, x};
        cmpl.prefix = l.substr(size_t(x), size_t(ed.cx - x));
        cmpl.cands.clear();
        std::vector<std::pair<long, u32>> found;
        long cur = long(ed.cy) * 100000 + x;
        for (int y = 0; y < n(); y++) {
            const u32& s = line(y);
            for (int i = 0; i < len(y);) {
                if (!isWord(s[size_t(i)]) || (i > 0 && isWord(s[size_t(i - 1)]))) { i++; continue; }
                int e = i;
                while (e < len(y) && isWord(s[size_t(e)])) e++;
                u32 w = s.substr(size_t(i), size_t(e - i));
                if (!(y == ed.cy && i == x) && w.size() > cmpl.prefix.size() &&
                    w.compare(0, cmpl.prefix.size(), cmpl.prefix) == 0) {
                    long pos = long(y) * 100000 + i;
                    long dist = fwd ? (pos > cur ? pos - cur : pos - cur + 1000000000L)
                                    : (pos < cur ? cur - pos : cur - pos + 1000000000L);
                    found.push_back({dist, w});
                }
                i = e;
            }
        }
        std::stable_sort(found.begin(), found.end(), [](auto& a, auto& b) { return a.first < b.first; });
        for (auto& f : found)
            if (std::find(cmpl.cands.begin(), cmpl.cands.end(), f.second) == cmpl.cands.end())
                cmpl.cands.push_back(f.second);
        if (cmpl.cands.empty()) {
            message(tr(U"-- Keyword completion: Pattern not found"));
            msgErr = true;
            return;
        }
        cmpl.on = true;
        cmpl.idx = -1;
    }
    int nc = int(cmpl.cands.size());
    // cycling includes the original text at the end
    cmpl.idx = cmpl.idx + 1 > nc ? 0 : cmpl.idx + 1;
    u32 w = cmpl.idx == nc ? cmpl.prefix : cmpl.cands[size_t(cmpl.idx)];
    delText(cmpl.start, Pos{ed.cy, ed.cx});
    insText(cmpl.start, w);
    insTyped += w;
}

// ---------------------------------------------------------------------------
// Command-line mode
// ---------------------------------------------------------------------------

void Vim::startCmdline(char32_t type, const u32& init) {
    cmdFromMode = mode;
    mode = VM_CMDLINE;
    cmdType = type;
    cmdText = init;
    cmdPos = int(init.size());
    histIdx = -1;
    cmdPending = 0;
    cmdStartCur = {ed.cy, ed.cx};
}

void Vim::cmdKey(char32_t c) {
    auto ins = [&](const u32& t) {
        cmdText.insert(size_t(cmdPos), t);
        cmdPos += int(t.size());
    };
    if (cmdPending == 1) {
        cmdPending = 0;
        if (VK::special(c) && c != VK::NUL) ins(vimKeyNames(u32(1, c)));
        else ins(u32(1, c == VK::NUL ? 0 : c));
        return;
    }
    if (cmdPending == 2) {
        cmdPending = 0;
        if (c == VK::ctrl('w') || c == VK::ctrl('a')) {
            const u32& l = line(ed.cy);
            int s = ed.cx, e = ed.cx;
            auto in = [&](char32_t ch) { return c == VK::ctrl('w') ? isWord(ch) : (ch != ' ' && ch != '\t'); };
            while (s < len(ed.cy) && !in(l[size_t(s)])) s++;
            e = s;
            while (s > 0 && in(l[size_t(s - 1)])) s--;
            while (e < len(ed.cy) && in(l[size_t(e)])) e++;
            ins(l.substr(size_t(s), size_t(e - s)));
            return;
        }
        if (c == VK::ctrl('l')) { ins(line(ed.cy)); return; }
        if (!validReg(c, false)) return;
        u32 t = getReg(c).text();
        if (!t.empty() && t.back() == '\n') t.pop_back();
        for (auto& ch : t)
            if (ch == '\n') ch = '\r';
        ins(t);
        return;
    }
    auto cancel = [&] {
        mode =
            (cmdFromMode == VM_VISUAL || cmdFromMode == VM_VLINE || cmdFromMode == VM_VBLOCK) ? cmdFromMode : VM_NORMAL;
        if (!visualActive()) ed.anch = false;
        cmdHasPendingOp = false;
        if (cmdType != ':') {
            ed.cy = cmdStartCur.y;
            ed.cx = cmdStartCur.x;
        }
        afterCommand();
    };
    switch (c) {
        case VK::ESC: case VK::ctrl('c'): cancel(); return;
        case VK::CR: case VK::NL: cmdExecute(); return;
        case VK::BS:
            if (cmdText.empty()) { cancel(); return; }
            if (cmdPos > 0) {
                cmdText.erase(size_t(cmdPos - 1), 1);
                cmdPos--;
            }
            return;
        case VK::DEL:
            if (cmdPos < int(cmdText.size())) cmdText.erase(size_t(cmdPos), 1);
            else if (cmdPos > 0) { cmdText.erase(size_t(cmdPos - 1), 1); cmdPos--; }
            return;
        case VK::ctrl('w'): {
            int x = cmdPos;
            while (x > 0 && cmdText[size_t(x - 1)] == ' ') x--;
            if (x > 0 && isWord(cmdText[size_t(x - 1)])) while (x > 0 && isWord(cmdText[size_t(x - 1)])) x--;
            else if (x > 0) x--;
            cmdText.erase(size_t(x), size_t(cmdPos - x));
            cmdPos = x;
            return;
        }
        case VK::ctrl('u'): cmdText.erase(0, size_t(cmdPos)); cmdPos = 0; return;
        case VK::LEFT: if (cmdPos > 0) cmdPos--; return;
        case VK::RIGHT: if (cmdPos < int(cmdText.size())) cmdPos++; return;
        case VK::HOME: case VK::ctrl('b'): cmdPos = 0; return;
        case VK::END: case VK::ctrl('e'): cmdPos = int(cmdText.size()); return;
        case VK::UP: case VK::DOWN: case VK::ctrl('p'): case VK::ctrl('n'): {
            auto& h = hist[cmdType == '?' ? '/' : cmdType];
            if (h.empty()) return;
            if (histIdx < 0) { histPrefix = cmdText; histIdx = int(h.size()); }
            bool up = c == VK::UP || c == VK::ctrl('p');
            int i = histIdx;
            for (;;) {
                i += up ? -1 : 1;
                if (i < 0) return;
                if (i >= int(h.size())) {
                    histIdx = int(h.size());
                    cmdText = histPrefix;
                    cmdPos = int(cmdText.size());
                    return;
                }
                if ((c == VK::ctrl('p') || c == VK::ctrl('n')) ||
                    h[size_t(i)].compare(0, histPrefix.size(), histPrefix) == 0)
                    break;
            }
            histIdx = i;
            cmdText = h[size_t(i)];
            cmdPos = int(cmdText.size());
            return;
        }
        case VK::ctrl('v'): case VK::ctrl('q'): cmdPending = 1; return;
        case VK::ctrl('r'): cmdPending = 2; return;
        case VK::TAB: case VK::STAB: return;
    }
    if (!VK::special(c) && c >= 32) ins(u32(1, c));
}

void Vim::cmdExecute() {
    u32 text = cmdText;
    char32_t t = cmdType;
    if (!text.empty()) {
        auto& h = hist[t == '?' ? '/' : t];
        h.erase(std::remove(h.begin(), h.end(), text), h.end());
        h.push_back(text);
        if (h.size() > 50) h.erase(h.begin());
    }
    VimMode from = cmdFromMode;
    bool fromVisual = from == VM_VISUAL || from == VM_VLINE || from == VM_VBLOCK;
    if (t == ':') {
        mode = VM_NORMAL;
        ed.anch = false;
        if (!text.empty()) lastExCmd = text;
        exCommand(text);
        if (mode == VM_NORMAL) clampNormal();
        afterCommand();
        return;
    }
    mode = fromVisual ? from : VM_NORMAL;
    if (!fromVisual) ed.anch = false;
    ed.cy = cmdStartCur.y;
    ed.cx = cmdStartCur.x;
    VimCmd c = cmdPendingOp;
    cmdHasPendingOp = false;
    c.pat = text;
    execute(c);
}
