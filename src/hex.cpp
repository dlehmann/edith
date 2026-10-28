// Hex mode: editing bytes, undo, searching and the detection of binary files.

#include "hex.h"

#include <algorithm>

#include "terminal.h"
#include "utf8.h"

bool looksBinary(const std::string& data) {
    size_t n = std::min<size_t>(data.size(), 8000), ctl = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)data[i];
        if (c == 0) return true;
        // tabs, line ends, form feeds, Backspace and Escape (colors in logs) occur in text
        if ((c < 32 && c != '\t' && c != '\n' && c != '\r' && c != '\f' && c != '\v' && c != '\b' && c != 27) ||
            c == 127)
            ctl++;
    }
    return ctl * 10 > n;
}

static int hexDigit(char32_t c) {
    if (c >= '0' && c <= '9') return int(c - '0');
    if (c >= 'a' && c <= 'f') return int(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return int(c - 'A' + 10);
    return -1;
}

bool parseHex(const u32& t, std::string& out) {
    out.clear();
    int hi = -1;
    for (size_t i = 0; i < t.size(); i++) {
        char32_t c = t[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ',') {
            if (hi >= 0) return false;  // a byte needs both digits
            continue;
        }
        if (hi < 0 && c == '0' && i + 1 < t.size() && (t[i + 1] == 'x' || t[i + 1] == 'X')) {
            i++;
            continue;
        }
        int d = hexDigit(c);
        if (d < 0) return false;
        if (hi < 0) hi = d;
        else { out += char(hi << 4 | d); hi = -1; }
    }
    return hi < 0 && !out.empty();
}

u32 hexText(const std::string& bytes) {
    static const char* const H = "0123456789ABCDEF";
    u32 r;
    for (size_t i = 0; i < bytes.size(); i++) {
        unsigned char c = (unsigned char)bytes[i];
        if (i) r += ' ';
        r += char32_t(H[c >> 4]);
        r += char32_t(H[c & 15]);
    }
    return r;
}

HexLayout::HexLayout(int width, size_t size) {
    for (size_t s = size >> 32; s; s >>= 4) digits++;
    while (per > 1 && charCol(per) > width) per /= 2;
}

int HexLayout::at(int col, bool& chars) const {
    chars = col >= charCol(0) - 1;
    if (chars) return std::clamp(col - charCol(0), 0, per - 1);
    if (col < hexCol(0)) return -1;
    for (int i = per - 1; i >= 0; i--)
        if (col >= hexCol(i)) return i;
    return 0;
}

bool HexBuf::sel(size_t& a, size_t& b) const {
    if (!anch || anchor == pos) return false;
    a = std::min(anchor, pos);
    b = std::max(anchor, pos);
    return true;
}

bool HexBuf::hasSel() const {
    size_t a, b;
    return sel(a, b);
}

std::string HexBuf::selected() const {
    size_t a, b;
    return sel(a, b) ? data.substr(a, b - a) : std::string();
}

void HexBuf::setCursor(size_t p, bool select) {
    if (select) {
        if (!anch) { anch = true; anchor = pos; }
    } else {
        anch = false;
    }
    pos = std::min(p, size());
    nib = 0;
    lastKind = 0;
}

void HexBuf::replace(size_t off, size_t len, const std::string& s, int kind) {
    off = std::min(off, size());
    len = std::min(len, size() - off);
    if (len == 0 && s.empty()) return;
    bool merged = false;
    if (kind && kind == lastKind && !undo.empty()) {
        // the change touches the region of the last one: both become one undo step
        Change& c = undo.back();
        size_t end = c.off + c.neu.size();
        if (off <= end && off + len >= c.off) {
            size_t start = std::min(c.off, off);
            std::string pre = data.substr(start, c.off - start);
            std::string post = off + len > end ? data.substr(end, off + len - end) : std::string();
            size_t region = pre.size() + c.neu.size() + post.size();
            c.old = pre + c.old + post;
            c.off = start;
            data.replace(off, len, s);
            c.neu = data.substr(start, region - len + s.size());
            merged = true;
        }
    }
    if (!merged) {
        undo.push_back({off, data.substr(off, len), s, pos});
        if (undo.size() > 10000) undo.erase(undo.begin());
        data.replace(off, len, s);
    }
    redo.clear();
    lastKind = kind;
    modified = stale = true;
}

bool HexBuf::deleteSel() {
    size_t a, b;
    if (!sel(a, b)) { anch = false; return false; }
    replace(a, b - a, std::string(), 0);
    pos = a;
    anch = false;
    nib = 0;
    return true;
}

// Typing over a selection: insert mode deletes it, overwrite mode starts at its beginning
static void typeOverSel(HexBuf& h) {
    size_t a, b;
    if (!h.sel(a, b)) { h.anch = false; return; }
    if (h.insert) h.deleteSel();
    h.anch = false;
    h.pos = a;
    h.nib = 0;
    h.lastKind = 0;
}

void HexBuf::typeHex(int d) {
    typeOverSel(*this);
    if (insert && nib == 0) {
        replace(pos, 0, std::string(1, char(d << 4)), 1);
        nib = 1;
        return;
    }
    unsigned char old = pos < size() ? (unsigned char)data[pos] : 0;
    unsigned char v = nib == 0 ? (unsigned char)(d << 4 | (old & 15)) : (unsigned char)((old & 0xF0) | d);
    replace(pos, pos < size() ? 1 : 0, std::string(1, char(v)), 1);
    if (nib == 0) nib = 1;
    else { nib = 0; pos++; }
}

void HexBuf::typeChar(char32_t c) {
    typeOverSel(*this);
    std::string b;
    putUtf8(b, c);
    replace(pos, insert ? 0 : b.size(), b, 1);
    pos += b.size();
    nib = 0;
}

void HexBuf::backspace() {
    if (deleteSel()) return;
    nib = 0;
    if (pos == 0) return;
    replace(pos - 1, 1, std::string(), 2);
    pos--;
}

void HexBuf::delFwd() {
    if (deleteSel()) return;
    nib = 0;
    if (pos < size()) replace(pos, 1, std::string(), 3);
}

void HexBuf::paste(const std::string& bytes) {
    if (bytes.empty()) return;
    size_t a, b;
    if (sel(a, b)) {
        replace(a, b - a, bytes, 0);
        pos = a;
    } else {
        replace(pos, insert ? 0 : bytes.size(), bytes, 0);
    }
    pos = std::min(pos + bytes.size(), size());
    anch = false;
    nib = 0;
    lastKind = 0;
}

void HexBuf::selectAll() {
    anch = true;
    anchor = 0;
    pos = size();
    nib = 0;
}

bool HexBuf::doUndo() {
    if (undo.empty()) return false;
    Change c = std::move(undo.back());
    undo.pop_back();
    data.replace(c.off, c.neu.size(), c.old);
    pos = std::min(c.pos, size());
    redo.push_back(std::move(c));
    anch = false;
    nib = 0;
    lastKind = 0;
    modified = stale = true;
    return true;
}

bool HexBuf::doRedo() {
    if (redo.empty()) return false;
    Change c = std::move(redo.back());
    redo.pop_back();
    data.replace(c.off, c.old.size(), c.neu);
    pos = std::min(c.off + c.neu.size(), size());
    undo.push_back(std::move(c));
    anch = false;
    nib = 0;
    lastKind = 0;
    modified = stale = true;
    return true;
}

void HexBuf::moveKey(int code, int mods, int per, int rows) {
    size_t p = pos, n = size(), w = size_t(std::max(1, per));
    size_t lastRow = n / w, page = w * size_t(std::max(1, rows - 1));
    bool c = mods & M_CTRL;
    switch (code) {
        case K_LEFT: if (p) p--; break;
        case K_RIGHT: if (p < n) p++; break;
        case K_UP: case K_DOWN:
            if (c) {  // scroll the view, the cursor stays in it
                if (code == K_UP) top = top ? top - 1 : 0;
                else top = std::min(top + 1, lastRow);
                size_t row = p / w, last = top + size_t(std::max(1, rows)) - 1;
                if (row < top) p = std::min(top * w + p % w, n);
                else if (row > last) p = std::min(last * w + p % w, n);
                else return;
            } else if (code == K_UP) {
                if (p >= w) p -= w;
            } else {
                if (p + w <= n) p += w;
                else if (p / w < lastRow) p = n;
            }
            break;
        case K_PGUP:
            p = p >= page ? p - page : p % w;
            top = top * w >= page ? top - page / w : 0;
            break;
        case K_PGDN:
            p = std::min(p + page, n);
            top = std::min(top + page / w, lastRow);
            break;
        case K_HOME: p = c ? 0 : p - p % w; break;
        case K_END: p = c ? n : std::min(p - p % w + w - 1, n); break;
    }
    setCursor(p, mods & M_SHIFT);
}

void HexBuf::scrollTo(int per, int rows, bool center) {
    size_t w = size_t(std::max(1, per)), h = size_t(std::max(1, rows));
    pos = std::min(pos, size());
    if (anch) anchor = std::min(anchor, size());
    size_t row = pos / w;
    top = std::min(top, size() / w);
    if (center && (row < top || row >= top + h)) top = row > h / 2 ? row - h / 2 : 0;
    if (row < top) top = row;
    else if (row >= top + h) top = row - h + 1;
}

bool HexBuf::find(const std::string& pat, bool cs, size_t from, size_t& at) const {
    size_t n = size(), m = pat.size();
    if (m == 0 || m > n) return false;
    auto low = [](unsigned char c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; };
    for (size_t k = 0; k < n; k++) {
        size_t i = (from + k) % n;
        if (i + m > n) continue;
        size_t j = 0;
        for (; j < m; j++) {
            unsigned char a = (unsigned char)data[i + j], b = (unsigned char)pat[j];
            if (cs ? a != b : low(a) != low(b)) break;
        }
        if (j == m) { at = i; return true; }
    }
    return false;
}
