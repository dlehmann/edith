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
};
extern Settings S;  // the global settings

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
    void moveKey(int code, int mods, int th);
    // Scrolls a view of th lines and tw columns so that the cursor is visible.
    void scrollTo(int th, int tw, bool center = false);
    // Does t occur at index i of l? cs = case-sensitive, whole = only whole words.
    static bool matchAt(const u32& l, size_t i, const u32& t, bool cs, bool whole);

    // Searches forward from "from", wrapping around to the start of the file
    bool find(const u32& t, bool cs, bool whole, Pos from, Pos& rs, Pos& re) const;
    // Replaces every occurrence (one undo step); returns their number.
    int replaceAll(const u32& t, const u32& r, bool cs, bool whole);
    // Loads a file (UTF-8, or Latin-1 when it is not valid UTF-8); err = message on failure.
    bool load(const std::string& path, std::string& err);
    // Saves in the file's format. converted = a Latin-1 file had to be saved as UTF-8.
    // An existing file is replaced only after the new text was written completely.
    bool save(const std::string& path, std::string& err, bool& converted);
};
