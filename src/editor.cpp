// The text buffer: editing, undo/redo, search and reading and writing files.

#include "editor.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "i18n.h"
#include "paths.h"
#include "terminal.h"
#include "utf8.h"

Settings S;

int Editor::colOf(const u32& l, int idx) const {
    int c = 0;
    for (int i = 0; i < idx && i < int(l.size()); i++) c += l[i] == '\t' ? S.tabw - c % S.tabw : cw(l[i]);
    return c;
}

int Editor::idxOf(const u32& l, int col) const {
    int c = 0;
    for (int i = 0; i < int(l.size()); i++) {
        int nc = l[i] == '\t' ? c + S.tabw - c % S.tabw : c + cw(l[i]);
        if (nc > col) return i;
        c = nc;
    }
    return int(l.size());
}

bool Editor::sel(Pos& a, Pos& b) const {
    if (!anch) return false;
    Pos c{cy, cx};
    if (anchor == c) return false;
    if (anchor < c) { a = anchor; b = c; }
    else { a = c; b = anchor; }
    return true;
}

bool Editor::hasSel() const {
    Pos a, b;
    return sel(a, b);
}

void Editor::clampCursor() {
    cy = std::clamp(cy, 0, n() - 1);
    cx = std::clamp(cx, 0, int(lines[cy].size()));
}

void Editor::setCursor(int y, int x, bool select) {
    if (select) {
        if (!anch) { anch = true; anchor = {cy, cx}; }
    } else {
        anch = false;
    }
    cy = std::clamp(y, 0, n() - 1);
    cx = std::clamp(x, 0, int(lines[cy].size()));
    lastEdit = 0;
}

void Editor::checkpoint(int kind) {
    if (kind && kind == lastEdit) return;
    undo.push_back({lines, cy, cx});
    if (undo.size() > 300) undo.erase(undo.begin());
    redo.clear();
    lastEdit = kind;
}

bool Editor::doUndo() {
    if (undo.empty()) return false;
    redo.push_back({std::move(lines), cy, cx});
    Snap s = std::move(undo.back());
    undo.pop_back();
    lines = std::move(s.lines);
    cy = s.cy; cx = s.cx;
    anch = false; modified = true; lastEdit = 0;
    touch(0);
    clampCursor();
    return true;
}

bool Editor::doRedo() {
    if (redo.empty()) return false;
    undo.push_back({std::move(lines), cy, cx});
    Snap s = std::move(redo.back());
    redo.pop_back();
    lines = std::move(s.lines);
    cy = s.cy; cx = s.cx;
    anch = false; modified = true; lastEdit = 0;
    touch(0);
    clampCursor();
    return true;
}

void Editor::insert(const u32& t) {
    touch(cy);
    u32 head = lines[cy].substr(0, cx), tail = lines[cy].substr(cx);
    if (t.find('\n') == u32::npos) {
        lines[cy] = head + t + tail;
        cx += int(t.size());
    } else {
        std::vector<u32> nl;
        size_t s = 0;
        for (;;) {
            size_t q = t.find('\n', s);
            if (q == u32::npos) { nl.push_back(t.substr(s)); break; }
            nl.push_back(t.substr(s, q - s));
            s = q + 1;
        }
        nl.front() = head + nl.front();
        int lastLen = int(nl.back().size());
        nl.back() += tail;
        lines.erase(lines.begin() + cy);
        lines.insert(lines.begin() + cy, nl.begin(), nl.end());
        cy += int(nl.size()) - 1;
        cx = lastLen;
    }
    modified = true;
    anch = false;
}

void Editor::deleteRange(Pos a, Pos b) {
    touch(a.y);
    if (a.y == b.y) {
        lines[a.y].erase(a.x, b.x - a.x);
    } else {
        lines[a.y] = lines[a.y].substr(0, a.x) + lines[b.y].substr(b.x);
        lines.erase(lines.begin() + a.y + 1, lines.begin() + b.y + 1);
    }
    cy = a.y; cx = a.x;
    modified = true;
    anch = false;
}

u32 Editor::getText(Pos a, Pos b) const {
    if (a.y == b.y) return lines[a.y].substr(a.x, b.x - a.x);
    u32 r = lines[a.y].substr(a.x);
    for (int y = a.y + 1; y < b.y; y++) r += U"\n" + lines[y];
    r += U"\n" + lines[b.y].substr(0, b.x);
    return r;
}

bool Editor::deleteSel() {
    Pos a, b;
    if (!sel(a, b)) { anch = false; return false; }
    deleteRange(a, b);
    return true;
}

void Editor::typeChar(char32_t c) {
    checkpoint(1);
    if (!deleteSel() && S.overwrite && cx < int(lines[cy].size())) lines[cy].erase(cx, 1);
    insert(u32(1, c));
}

void Editor::backspace() {
    if (hasSel()) { checkpoint(0); deleteSel(); return; }
    anch = false;
    if (cx > 0) {
        checkpoint(2);
        deleteRange({cy, cx - 1}, {cy, cx});
    } else if (cy > 0) {
        checkpoint(0);
        deleteRange({cy - 1, int(lines[cy - 1].size())}, {cy, 0});
    }
}

void Editor::backspaceWord(bool toLineStart) {
    if (hasSel()) { checkpoint(0); deleteSel(); return; }
    anch = false;
    int y = cy, x = cx;
    if (toLineStart && x > 0) x = 0;
    else wordLeft(y, x);
    if (y == cy && x == cx) return;
    checkpoint(0);
    deleteRange({y, x}, {cy, cx});
}

void Editor::delFwd() {
    if (hasSel()) { checkpoint(0); deleteSel(); return; }
    anch = false;
    if (cx < int(lines[cy].size())) {
        checkpoint(3);
        deleteRange({cy, cx}, {cy, cx + 1});
    } else if (cy < n() - 1) {
        checkpoint(0);
        deleteRange({cy, cx}, {cy + 1, 0});
    }
}

void Editor::newline() {
    checkpoint(0);
    deleteSel();
    u32 ind;
    if (S.autoindent) {
        const u32& l = lines[cy];
        size_t i = 0;
        while (i < l.size() && int(i) < cx && (l[i] == ' ' || l[i] == '\t')) i++;
        ind = l.substr(0, i);
    }
    insert(U"\n" + ind);
}

void Editor::indent(bool out) {
    Pos a, b;
    bool s = sel(a, b);
    int y1 = cy, y2 = cy;
    if (s) {
        y1 = a.y; y2 = b.y;
        if (b.x == 0 && y2 > y1) y2--;
    }
    checkpoint(0);
    touch(y1);
    u32 unit = S.expandtab ? u32(S.tabw, ' ') : u32(U"\t");
    int delta = 0;
    for (int y = y1; y <= y2; y++) {
        u32& l = lines[y];
        if (!out) {
            if (!l.empty()) {
                l.insert(0, unit);
                if (y == cy) delta = int(unit.size());
            }
        } else {
            int r = 0;
            if (!l.empty() && l[0] == '\t') r = 1;
            else while (r < S.tabw && r < int(l.size()) && l[r] == ' ') r++;
            l.erase(0, r);
            if (y == cy) delta = -r;
        }
    }
    modified = true;
    if (s) {
        anch = true;
        anchor = {y1, 0};
        cy = y2;
        cx = int(lines[y2].size());
    } else {
        cx = std::max(0, cx + delta);
        clampCursor();
    }
}

void Editor::tab(bool shift) {
    Pos a, b;
    bool s = sel(a, b);
    if (shift || (s && a.y != b.y)) { indent(shift); return; }
    checkpoint(1);
    deleteSel();
    if (S.expandtab) {
        int c = colOf(lines[cy], cx);
        insert(u32(S.tabw - c % S.tabw, ' '));
    } else {
        insert(U"\t");
    }
}

void Editor::paste(const u32& t) {
    if (t.empty()) return;
    checkpoint(0);
    deleteSel();
    insert(t);
}

void Editor::selectAll() {
    anch = true;
    anchor = {0, 0};
    cy = n() - 1;
    cx = int(lines[cy].size());
    lastEdit = 0;
}

void Editor::selectWord(int y, int x) {
    const u32& l = lines[y];
    if (x >= int(l.size()) || !isWord(l[x])) {
        if (x > 0 && x <= int(l.size()) && isWord(l[x - 1])) x--;
        else { setCursor(y, x, false); return; }
    }
    int s = x, e = x;
    while (s > 0 && isWord(l[s - 1])) s--;
    while (e < int(l.size()) && isWord(l[e])) e++;
    anch = true;
    anchor = {y, s};
    cy = y; cx = e;
    lastEdit = 0; wantCol = -1;
}

void Editor::wordLeft(int& y, int& x) const {
    if (x == 0) {
        if (y > 0) { y--; x = int(lines[y].size()); }
        return;
    }
    const u32& l = lines[y];
    while (x > 0 && !isWord(l[x - 1])) x--;
    while (x > 0 && isWord(l[x - 1])) x--;
}

void Editor::wordRight(int& y, int& x) const {
    const u32& l = lines[y];
    if (x >= int(l.size())) {
        if (y < n() - 1) { y++; x = 0; }
        return;
    }
    while (x < int(l.size()) && isWord(l[x])) x++;
    while (x < int(l.size()) && !isWord(l[x])) x++;
}

void Editor::moveKey(int code, int mods, int th) {
    bool s = mods & M_SHIFT, c = mods & M_CTRL;
    int y = cy, x = cx;
    const u32& l = lines[y];
    bool vert = false;
    int col = 0;
    switch (code) {
        case K_LEFT:
            if (c) wordLeft(y, x);
            else if (x > 0) x--;
            else if (y > 0) { y--; x = int(lines[y].size()); }
            break;
        case K_RIGHT:
            if (c) wordRight(y, x);
            else if (x < int(l.size())) x++;
            else if (y < n() - 1) { y++; x = 0; }
            break;
        case K_UP: case K_DOWN: case K_PGUP: case K_PGDN: {
            col = wantCol >= 0 ? wantCol : colOf(l, x);
            if (c && (code == K_UP || code == K_DOWN)) {  // scroll the view
                top = std::clamp(top + (code == K_UP ? -1 : 1), 0, std::max(0, n() - 1));
                if (cy >= top && cy < top + th) return;
                y = cy < top ? top : top + th - 1;
            } else {
                int d = code == K_UP ? -1 : code == K_DOWN ? 1 : code == K_PGUP ? -(th - 1) : th - 1;
                int ny = std::clamp(y + d, 0, n() - 1);
                if (code == K_PGUP || code == K_PGDN) top = std::clamp(top + (ny - y), 0, std::max(0, n() - th));
                y = ny;
            }
            y = std::clamp(y, 0, n() - 1);
            x = idxOf(lines[y], col);
            vert = true;
            break;
        }
        case K_HOME:
            if (c) { y = 0; x = 0; }
            else {
                int ind = 0;
                while (ind < int(l.size()) && (l[ind] == ' ' || l[ind] == '\t')) ind++;
                x = (x == ind) ? 0 : ind;
            }
            break;
        case K_END:
            if (c) { y = n() - 1; x = int(lines[y].size()); }
            else x = int(l.size());
            break;
    }
    setCursor(y, x, s);
    wantCol = vert ? col : -1;
}

void Editor::scrollTo(int th, int tw, bool center) {
    clampCursor();
    if (center && (cy < top || cy >= top + th)) top = std::max(0, cy - th / 2);
    if (cy < top) top = cy;
    else if (cy >= top + th) top = cy - th + 1;
    int col = colOf(lines[cy], cx);
    if (col < left) left = col;
    else if (col >= left + tw) left = col - tw + 1;
}

bool Editor::matchAt(const u32& l, size_t i, const u32& t, bool cs, bool whole) {
    if (i + t.size() > l.size()) return false;
    for (size_t k = 0; k < t.size(); k++) {
        char32_t a = l[i + k], b = t[k];
        if (cs ? a != b : fold(a) != fold(b)) return false;
    }
    if (whole) {
        if (i > 0 && isWord(l[i - 1])) return false;
        if (i + t.size() < l.size() && isWord(l[i + t.size()])) return false;
    }
    return true;
}

bool Editor::find(const u32& t, bool cs, bool whole, Pos from, Pos& rs, Pos& re) const {
    if (t.empty()) return false;
    int cnt = n();
    for (int k = 0; k <= cnt; k++) {
        int y = (from.y + k) % cnt;
        const u32& l = lines[y];
        size_t st = k == 0 ? size_t(from.x) : 0;
        for (size_t i = st; i + t.size() <= l.size(); i++) {
            if (k == cnt && int(i) >= from.x) break;
            if (matchAt(l, i, t, cs, whole)) {
                rs = {y, int(i)};
                re = {y, int(i + t.size())};
                return true;
            }
        }
    }
    return false;
}

int Editor::replaceAll(const u32& t, const u32& r, bool cs, bool whole) {
    if (t.empty()) return 0;
    std::vector<u32> result = lines;
    int cnt = 0;
    for (auto& l : result) {
        u32 out;
        size_t i = 0;
        bool changed = false;
        while (i < l.size()) {
            if (matchAt(l, i, t, cs, whole)) {
                out += r;
                i += t.size();
                cnt++;
                changed = true;
            } else {
                out += l[i++];
            }
        }
        if (changed) l = std::move(out);
    }
    if (cnt) {  // without a match neither the undo nor the redo list changes
        checkpoint(0);
        lines = std::move(result);
        touch(0);
        modified = true;
    }
    anch = false;
    clampCursor();
    return cnt;
}

bool Editor::load(const std::string& path, std::string& err) {
    struct stat st;
    if (stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) { err = trs("That is a directory."); return false; }
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) { err = errnoText(errno); return false; }
    std::string data;
    char buf[65536];
    size_t r;
    while ((r = fread(buf, 1, sizeof buf, f)) > 0) data.append(buf, r);
    bool bad = ferror(f);
    fclose(f);
    if (bad) { err = trs("Read error."); return false; }

    Editor e;
    if (data.size() >= 3 && data.compare(0, 3, "\xEF\xBB\xBF") == 0) {
        e.bom = true;
        data.erase(0, 3);
    }
    u32 text;
    if (!fromUtf8(data, text)) {
        text.clear();
        for (unsigned char c : data) text += c;
        e.latin1 = true;
    }
    e.crlf = text.find(U"\r\n") != u32::npos;
    e.lines.clear();
    size_t s = 0;
    for (;;) {
        size_t q = text.find('\n', s);
        u32 l = text.substr(s, q == u32::npos ? u32::npos : q - s);
        if (e.crlf && !l.empty() && l.back() == '\r') l.pop_back();
        e.lines.push_back(std::move(l));
        if (q == u32::npos) break;
        s = q + 1;
    }
    e.filename = path;
    *this = std::move(e);
    return true;
}

// Writes data to path. An existing file is replaced by a temporary file written next to it,
// so that a failed write (a full disk) leaves the old file intact. Where replacing would change
// the file's identity it is written in place: for hard links and for files of another owner.
// Symbolic links are followed. The permissions of the file are kept.
static bool writeFile(const std::string& path, const std::string& data, std::string& err) {
    std::string target = path;
    char* real = realpath(path.c_str(), nullptr);  // follows symbolic links
    if (real) {
        target = real;
        free(real);
    }
    struct stat st;
    bool exists = stat(target.c_str(), &st) == 0;
    int fd = -1;
    std::string tmp;
    if (exists && S_ISREG(st.st_mode) && st.st_nlink == 1 && st.st_uid == geteuid()) {
        tmp = dirOf(target) + "/." + baseOf(target) + ".edith-XXXXXX";
        fd = mkstemp(&tmp[0]);
        // keep group and permissions; where that fails (or for a directory without write
        // permission) the file is written in place
        if (fd >= 0 && (fchown(fd, st.st_uid, st.st_gid) != 0 || fchmod(fd, st.st_mode & 07777) != 0)) {
            close(fd);
            unlink(tmp.c_str());
            fd = -1;
        }
        if (fd < 0) tmp.clear();
    }
    if (fd < 0) fd = open(target.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) { err = errnoText(errno); return false; }
    size_t off = 0;
    int e = 0;
    while (off < data.size()) {
        ssize_t n = write(fd, data.data() + off, data.size() - off);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { e = n < 0 ? errno : ENOSPC; break; }
        off += size_t(n);
    }
    if (!e && !tmp.empty() && fsync(fd) != 0) e = errno;
    if (close(fd) != 0 && !e) e = errno;
    if (!e && !tmp.empty() && rename(tmp.c_str(), target.c_str()) != 0) e = errno;
    if (e) {
        if (!tmp.empty()) unlink(tmp.c_str());
        err = e == EIO ? trs("Write error.") : errnoText(e);
        return false;
    }
    return true;
}

bool Editor::save(const std::string& path, std::string& err, bool& converted) {
    converted = false;
    if (latin1) {
        for (auto& l : lines)
            for (char32_t c : l)
                if (c > 0xFF) converted = true;
        if (converted) latin1 = false;
    }
    std::string out;
    if (bom) out = "\xEF\xBB\xBF";
    for (size_t i = 0; i < lines.size(); i++) {
        if (i) out += crlf ? "\r\n" : "\n";
        if (latin1) for (char32_t c : lines[i]) out += char(c);
        else for (char32_t c : lines[i]) putUtf8(out, c);
    }
    if (!writeFile(path, out, err)) return false;
    filename = path;
    modified = false;
    return true;
}
