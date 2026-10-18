// UTF-8 conversion, character widths and case folding (see utf8.h).

#include "utf8.h"

#include <cstdio>

void putUtf8(std::string& o, char32_t c) {
    // surrogates and values above U+10FFFF (e.g. typed with Vim's Ctrl-V U) have no UTF-8 form;
    // their bytes would make the whole file invalid UTF-8, and it would be read back as Latin-1
    if (c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) c = 0xFFFD;
    if (c < 0x80) {
        o += char(c);
    } else if (c < 0x800) {
        o += char(0xC0 | (c >> 6));
        o += char(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        o += char(0xE0 | (c >> 12));
        o += char(0x80 | ((c >> 6) & 0x3F));
        o += char(0x80 | (c & 0x3F));
    } else {
        o += char(0xF0 | (c >> 18));
        o += char(0x80 | ((c >> 12) & 0x3F));
        o += char(0x80 | ((c >> 6) & 0x3F));
        o += char(0x80 | (c & 0x3F));
    }
}

std::string toUtf8(const u32& s) {
    std::string o;
    o.reserve(s.size());
    for (char32_t c : s) putUtf8(o, c);
    return o;
}

// Strict decoding; false for invalid UTF-8.
bool fromUtf8(const std::string& s, u32& out) {
    out.clear();
    out.reserve(s.size());
    size_t i = 0, n = s.size();
    while (i < n) {
        unsigned char c = s[i];
        if (c < 0x80) { out += c; i++; continue; }
        int len;
        char32_t cp;
        if ((c & 0xE0) == 0xC0) { len = 2; cp = c & 0x1F; }
        else if ((c & 0xF0) == 0xE0) { len = 3; cp = c & 0x0F; }
        else if ((c & 0xF8) == 0xF0) { len = 4; cp = c & 0x07; }
        else return false;
        if (i + len > n) return false;
        for (int k = 1; k < len; k++) {
            unsigned char b = s[i + k];
            if ((b & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (b & 0x3F);
        }
        if ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000) ||
            cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
            return false;
        out += cp;
        i += len;
    }
    return true;
}

// UTF-8 → u32, byte by byte (Latin-1) for invalid data.
u32 U(const std::string& s) {
    u32 r;
    if (!fromUtf8(s, r)) {
        r.clear();
        for (unsigned char c : s) r += c;
    }
    return r;
}

struct Range { char32_t lo, hi; };

static bool inRanges(char32_t c, const Range* r, size_t n) {
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (c < r[mid].lo) hi = mid;
        else if (c > r[mid].hi) lo = mid + 1;
        else return true;
    }
    return false;
}

// Display width of a character in the terminal (0, 1 or 2 columns).
int cw(char32_t c) {
    if (c < 0x300) return 1;
    static const Range zero[] = {
        {0x300, 0x36F}, {0x483, 0x489}, {0x591, 0x5BD}, {0x610, 0x61A}, {0x64B, 0x65F},
        {0x1AB0, 0x1AFF}, {0x1DC0, 0x1DFF}, {0x200B, 0x200F}, {0x202A, 0x202E},
        {0x2060, 0x2064}, {0x20D0, 0x20FF}, {0xFE00, 0xFE0F}, {0xFE20, 0xFE2F}, {0xFEFF, 0xFEFF},
    };
    static const Range wide[] = {
        {0x1100, 0x115F}, {0x231A, 0x231B}, {0x2329, 0x232A}, {0x23E9, 0x23EC}, {0x23F0, 0x23F0},
        {0x23F3, 0x23F3}, {0x25FD, 0x25FE}, {0x2614, 0x2615}, {0x2648, 0x2653}, {0x267F, 0x267F},
        {0x2693, 0x2693}, {0x26A1, 0x26A1}, {0x26AA, 0x26AB}, {0x26BD, 0x26BE}, {0x26C4, 0x26C5},
        {0x26CE, 0x26CE}, {0x26D4, 0x26D4}, {0x26EA, 0x26EA}, {0x26F2, 0x26F3}, {0x26F5, 0x26F5},
        {0x26FA, 0x26FA}, {0x26FD, 0x26FD}, {0x2705, 0x2705}, {0x270A, 0x270B}, {0x2728, 0x2728},
        {0x274C, 0x274C}, {0x274E, 0x274E}, {0x2753, 0x2755}, {0x2757, 0x2757}, {0x2795, 0x2797},
        {0x27B0, 0x27B0}, {0x27BF, 0x27BF}, {0x2B1B, 0x2B1C}, {0x2B50, 0x2B50}, {0x2B55, 0x2B55},
        {0x2E80, 0x303E}, {0x3041, 0x33FF}, {0x3400, 0x4DBF}, {0x4E00, 0x9FFF}, {0xA000, 0xA4CF},
        {0xA960, 0xA97F}, {0xAC00, 0xD7A3}, {0xF900, 0xFAFF}, {0xFE10, 0xFE19}, {0xFE30, 0xFE6F},
        {0xFF00, 0xFF60}, {0xFFE0, 0xFFE6}, {0x16FE0, 0x16FE4}, {0x17000, 0x18CFF},
        {0x1B000, 0x1B2FF}, {0x1F004, 0x1F004}, {0x1F0CF, 0x1F0CF}, {0x1F18E, 0x1F18E},
        {0x1F191, 0x1F19A}, {0x1F200, 0x1F251}, {0x1F300, 0x1F64F}, {0x1F680, 0x1F6FF},
        {0x1F7E0, 0x1F7EB}, {0x1F90C, 0x1F9FF}, {0x1FA70, 0x1FAFF}, {0x20000, 0x3FFFD},
    };
    if (inRanges(c, zero, sizeof zero / sizeof *zero)) return 0;
    if (inRanges(c, wide, sizeof wide / sizeof *wide)) return 2;
    return 1;
}

int strWidth(const u32& s) {
    int w = 0;
    for (char32_t c : s) w += cw(c);
    return w;
}

// Simple case folding (ASCII, Latin-1, Greek, Cyrillic).
char32_t fold(char32_t c) {
    if (c >= 'A' && c <= 'Z') return c + 32;
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 32;
    if (c >= 0x391 && c <= 0x3A9) return c + 32;
    if (c >= 0x410 && c <= 0x42F) return c + 32;
    if (c >= 0x400 && c <= 0x40F) return c + 80;
    return c;
}

bool isWord(char32_t c) {
    if (c < 0x80) return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    if (c == 0xD7 || c == 0xF7) return false;
    if (c < 0xC0) return c == 0xAA || c == 0xB5 || c == 0xBA;
    if (c >= 0x2000 && c <= 0x2BFF) return false;
    if (c >= 0x3000 && c <= 0x303F) return false;
    if (c >= 0xFF00 && c <= 0xFF0F) return false;
    return true;
}

u32 fmtU(const char* f, int a, int b) {
    char t[64];
    snprintf(t, sizeof t, f, a, b);
    return U(t);
}
