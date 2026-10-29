#pragma once
// Off-screen character buffer that is written to the terminal with minimal updates.

#include <climits>
#include <cstdint>
#include <string>
#include <vector>

#include "common.h"
#include "terminal.h"

// Foreground and background color (ANSI indexes 0-15).
struct Attr { uint8_t fg, bg; };

// Color indexes in ANSI order (0 black, 4 blue, 6 cyan, 7 light gray, 8 dark gray, 15 white)
namespace Col {
const Attr TEXT{7, 4}, SEL{4, 7}, FRAME{7, 4}, TITLE{4, 7};
const Attr TABCUR{15, 4};  // the shown tab of a window that is not active
const Attr MENU{0, 7}, MHOT{15, 7}, MSEL{7, 0}, MSELHOT{15, 0}, MDIS{8, 7};
const Attr STATUS{15, 6}, SHADOW{8, 0};
const Attr DIALOG{0, 7}, INPUT{15, 4}, INPUTSEL{0, 6}, BUTTONF{15, 0};
const Attr LISTSEL{15, 0}, LISTUNF{0, 6}, SCROLL{0, 7};
// Colors after the 16 ANSI colors exist only with true color; other terminals get a substitute (Screen::sgr).
constexpr uint8_t DIMBLACK = 16;  // black at 30 % over the blue background; else black
const Attr GUTTER{7, DIMBLACK}, GUTTERCUR{15, DIMBLACK};  // the line numbers, the cursor line bright
const Attr SEARCH{0, 6}, ERRMSG{15, 1};
const Attr INVIS{8, 4}, ZEROW{15, 5};  // invisible characters shown (Options > Show Invisible Characters)
}  // namespace Col

// One character cell. The right half of a wide character has ch == 0.
struct Cell {
    char32_t ch;
    uint8_t fg, bg;
    bool operator==(const Cell& o) const { return ch == o.ch && fg == o.fg && bg == o.bg; }
};

// The screen: everything is drawn into buf, flush() sends the cells that differ from the
// previous frame (old) to the terminal.
struct Screen {
    int w = 80, h = 25;
    std::vector<Cell> buf, old;
    bool force = true;          // redraw everything with the next flush()
    bool truecolor = false;     // use the CGA palette as 24-bit colors
    bool boldBright = false;    // bright foreground colors as bold instead of SGR 90-97 (16-color mode)
    bool cvis = false;          // show the cursor at cx, cy with shape cshape (DECSCUSR)
    int cx = 0, cy = 0, cshape = 3;
    // Takes the size of the terminal and clears the buffers.
    void resize();

    void clear() { std::fill(buf.begin(), buf.end(), Cell{' ', 7, 0}); }

    // Drawing; everything is clipped to the screen.
    void put(int x, int y, char32_t c, Attr a);
    // Draws text of at most maxw columns; returns the columns used.
    int text(int x, int y, const u32& s, Attr a, int maxw = INT_MAX);
    void fill(int x, int y, int n, Attr a, char32_t ch = ' ');

    // Text with a "&" marker: the following character is highlighted
    void hot(int x, int y, const u32& s, Attr a, Attr ha);
    // Changes the colors of a cell and keeps its character.
    void recolor(int x, int y, Attr a);
    // A frame with a centered title, filled with blanks unless fillIn is false.
    void box(int x, int y, int bw, int bh, Attr a, const u32& title = u32(), bool fillIn = true);
    // The shadow to the right of and below a box.
    void shadow(int x, int y, int bw, int bh);
    // Appends the escape sequence for the colors fg/bg to o.
    void sgr(std::string& o, int fg, int bg);
    // Writes the changes since the last frame to the terminal.
    void flush();
};

extern Screen* g_scr;  // screen that is resized when the terminal size changes

// Reads the next key (resizes the screen on K_RESIZE).
Key getKey();
