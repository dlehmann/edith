#pragma once
// The text buffer: lines, cursor, selection, undo/redo, search and file I/O.

#include <algorithm>
#include <string>
#include <vector>

#include "common.h"

// Editor settings shared by all buffers (Options menu, command line, Vim's ":set").
struct Settings {
    int tabw = 4;
    bool autoindent = true, expandtab = false, overwrite = false, syntax = true;
    int lineNums = 0;  // 0 = off, 1 = absolute, 2 = relative to the cursor line
    bool vim = false;  // Vim key bindings (Options > Vim Mode)
    bool invisibles = false;  // show tabs, spaces, line ends and control characters (Vim's 'list')
    bool wrap = false;        // soft wrap: long lines continue on the next screen row (Vim's 'wrap')
    bool linebreak = true;    // ... and break at blanks instead of in the middle of a word (Vim's 'linebreak')
    bool showMargin = false;  // the right margin: a vertical line after column margin (Vim's 'colorcolumn')
    int margin = 80;
};
extern Settings S;  // the global settings

// Reads a whole file; err = message on failure.
bool readFile(const std::string& path, std::string& data, std::string& err);
// Writes a whole file safely (see editor.cpp); err = message on failure.
bool writeFile(const std::string& path, const std::string& data, std::string& err);

// A position in the text: line y, index x into the line (code points, not columns).
struct Pos {
    int y = 0, x = 0;
};
inline bool operator<(Pos a, Pos b) { return a.y < b.y || (a.y == b.y && a.x < b.x); }
inline bool operator==(Pos a, Pos b) { return a.y == b.y && a.x == b.x; }

// A text buffer with cursor and selection. The text is a list of lines without line breaks
// (always at least one). Undo keeps whole snapshots of the text (at most 300).
struct Editor {
    std::vector<u32> lines{u32()};
    int cy = 0, cx = 0;         // cursor (line, index)
    int top = 0, left = 0;      // first visible line and column
    int topRow = 0;             // with soft wrap: the first visible screen row of line top
    bool anch = false;          // a selection exists: from anchor to the cursor
    Pos anchor;
    std::string filename;       // absolute path, "" for a new file
    bool modified = false;
    bool crlf = false, latin1 = false, bom = false;  // file format, kept when saving
    struct Snap {
        std::vector<u32> lines;
        int cy, cx;
    };
    std::vector<Snap> undo, redo;
    int lastEdit = 0;  // 1 = typing, 2 = Backspace, 3 = Delete (consecutive edits are merged)
    int wantCol = -1;  // column kept by vertical movement (-1 = the cursor's column)
    int dirty = 0;     // first changed line since the last redraw (INT_MAX = none)

    void touch(int y) { dirty = std::min(dirty, y); }

    int n() const { return int(lines.size()); }

    // Screen column of index idx in line l (tabs and wide characters) and back.
    int colOf(const u32& l, int idx) const;
    int idxOf(const u32& l, int col) const;
    // The selection as an ordered range [a, b); false when nothing is selected.
    bool sel(Pos& a, Pos& b) const;
    bool hasSel() const;
    void clampCursor();
    // Moves the cursor; select = extend the selection (Shift+movement).
    void setCursor(int y, int x, bool select);
    // Saves an undo step before a change. Consecutive changes of the same kind (1 typing,
    // 2 Backspace, 3 Delete) share one step; kind 0 always starts a new one.
    void checkpoint(int kind);
    bool doUndo();
    bool doRedo();
    // Inserts text (may contain '\n') at the cursor; the cursor ends after it.
    void insert(const u32& t);
    void deleteRange(Pos a, Pos b);
    u32 getText(Pos a, Pos b) const;
    bool deleteSel();

    // Editing commands of the keyboard (with undo steps and the editor settings)
    void typeChar(char32_t c);
    void backspace();
    // Ctrl+Backspace / Option+Backspace: deletes the word before the cursor; toLineStart
    // (Cmd+Backspace): everything before the cursor in its line.
    void backspaceWord(bool toLineStart);
    void delFwd();
    void newline();
    void indent(bool out);
    void tab(bool shift);
    void paste(const u32& t);
    void selectAll();
    void selectWord(int y, int x);
    void wordLeft(int& y, int& x) const;
    void wordRight(int& y, int& x) const;
    // Cursor keys with modifiers; th = height of the view (for PgUp/PgDn).
    // tw = width of the view (soft wrap: Up/Down move by screen rows).
    void moveKey(int code, int mods, int th, int tw = 0);
    // Scrolls a view of th lines and tw columns so that the cursor is visible.
    void scrollTo(int th, int tw, bool center = false);

    // Soft wrap (Options > Word Wrap). A line is shown in screen rows of at most w columns; with
    // S.linebreak a row ends after a blank where possible. A line whose last row is full gets an
    // empty row more for the cursor at its end. w = 0 means no wrapping (one row per line).
    std::vector<int> wrapRows(const u32& l, int w) const;  // the index where each row starts
    int rowCount(int y, int w) const;
    // The row of index x in line y and its column within the row
    void rowCol(int y, int x, int w, int& r, int& c) const;
    // The index shown at column c of row r of line y (in a row that is not the last one at most
    // the index before the next row)
    int idxAtRow(int y, int r, int c, int w) const;
    // Moves the screen row (y, r) by d rows; returns the number of rows moved (with sign)
    int stepRows(int& y, int& r, int d, int w) const;
    // The screen row of (y, r) counted from (top, topRow): -1 before the view, at most limit
    int rowFromTop(int y, int r, int w, int limit) const;
    // Scrolls a view of th rows by d rows (the last page stays full)
    void scrollRows(int d, int th, int w);
    // The width for wrapRows() in a view of tw columns: 0 without soft wrap
    static int wrapWidth(int tw) { return S.wrap ? std::max(1, tw) : 0; }
    // Does t occur at index i of l? cs = case-sensitive, whole = only whole words.
    static bool matchAt(const u32& l, size_t i, const u32& t, bool cs, bool whole);

    // Searches forward from "from", wrapping around to the start of the file
    bool find(const u32& t, bool cs, bool whole, Pos from, Pos& rs, Pos& re) const;
    // Replaces every occurrence (one undo step); returns their number.
    int replaceAll(const u32& t, const u32& r, bool cs, bool whole);
    // Loads a file (UTF-8, or Latin-1 when it is not valid UTF-8); err = message on failure.
    bool load(const std::string& path, std::string& err);
    // Sets the text and the file format from the content of a file (no undo step)
    void setData(std::string data);
    // The content of the file as save() writes it. converted = a Latin-1 text needs UTF-8.
    std::string encode(bool& converted) const;
    // The offset of a position in encode()'s bytes, and the position of a byte offset
    size_t byteOffset(Pos p) const;
    Pos posOfByte(size_t off) const;
    // Saves in the file's format. converted = a Latin-1 file had to be saved as UTF-8.
    // An existing file is replaced only after the new text was written completely.
    bool save(const std::string& path, std::string& err, bool& converted);
};
