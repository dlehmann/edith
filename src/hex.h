#pragma once
// Hex mode: the bytes of a file with cursor, selection and undo, the layout of a row and the
// detection of binary files. Knows nothing about the application (see app_hex.cpp).

#include <cstddef>
#include <string>
#include <vector>

#include "common.h"

// Does the content of a file look like binary data rather than text? (a NUL byte or many
// control characters near the start)
bool looksBinary(const std::string& data);

// The bytes written as hex digits in t ("4F 0a", "4f0a", "0x4F, 0x0A"); false when t contains
// anything else or an odd number of digits.
bool parseHex(const u32& t, std::string& out);
// Bytes as hex digits separated by spaces ("4F 0A")
u32 hexText(const std::string& bytes);

// The columns of a row: the offset, the bytes in hex (a gap after every eighth byte) and the
// bytes as characters. Bytes per row: 16, or fewer when the window is too narrow.
struct HexLayout {
    int digits = 8;  // hex digits of the offset
    int per = 16;    // bytes per row
    HexLayout() = default;
    HexLayout(int width, size_t size);
    int hexCol(int i) const { return digits + 2 + i * 3 + i / 8; }
    int charCol(int i) const { return digits + 2 + per * 3 + (per - 1) / 8 + 1 + i; }
    // The byte of a row at a column: its index (-1 = none) and whether it is the character area
    int at(int col, bool& chars) const;
};

struct HexBuf {
    std::string data;
    size_t pos = 0;          // the cursor: byte offset, size() = after the last byte
    int nib = 0;             // 0 = high, 1 = low digit of the byte at the cursor
    bool chars = false;      // the cursor is in the character area (Tab switches)
    bool anch = false;       // a selection exists: the bytes from anchor to the cursor
    size_t anchor = 0;
    bool insert = false;     // typing inserts bytes instead of overwriting them
    size_t top = 0;          // first visible row
    bool modified = false;   // changed since the application last looked (App::hexSync)
    bool stale = false;      // the buffer's text no longer matches the bytes
    bool fromText = false;   // hex mode was turned on for a text (switching back is one undo step)
    struct Change {
        size_t off;
        std::string old, neu;  // the bytes at off before and after the change
        size_t pos;            // the cursor before the change
    };
    std::vector<Change> undo, redo;
    int lastKind = 0;        // 1 = typing, 2 = Backspace, 3 = Delete (consecutive edits are merged)

    size_t size() const { return data.size(); }
    // The selection as a range [a, b); false when nothing is selected
    bool sel(size_t& a, size_t& b) const;
    bool hasSel() const;
    std::string selected() const;
    void setCursor(size_t p, bool select);
    // Replaces len bytes at off by s as an undo step (kind as for Editor::checkpoint)
    void replace(size_t off, size_t len, const std::string& s, int kind);
    bool deleteSel();
    void typeHex(int digit);     // a hex digit in the hex area
    void typeChar(char32_t c);   // a character in the character area (UTF-8)
    void backspace();
    void delFwd();
    void paste(const std::string& bytes);
    void selectAll();
    bool doUndo();
    bool doRedo();
    // Cursor keys with modifiers in rows of per bytes; rows = height of the view
    void moveKey(int code, int mods, int per, int rows);
    void scrollTo(int per, int rows, bool center = false);
    // Searches forward from "from", wrapping around; cs = case-sensitive for ASCII letters
    bool find(const std::string& pat, bool cs, size_t from, size_t& at) const;
};
