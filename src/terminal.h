#pragma once
// Terminal setup (raw mode, alternate screen, mouse, bracketed paste) and
// decoding of keyboard and mouse input from escape sequences.

#include <string>

#include "common.h"

enum KeyCode {
    K_NONE, K_CHAR, K_UP, K_DOWN, K_LEFT, K_RIGHT, K_HOME, K_END, K_PGUP, K_PGDN,
    K_INS, K_DEL, K_ENTER, K_TAB, K_BKSP, K_ESC, K_MOUSE, K_PASTE, K_RESIZE,
    K_ALTMENU,  // Alt pressed and released alone (only with the kitty keyboard protocol)
    K_TIMEOUT,  // no key within g_keyTimeout milliseconds
    K_F1 = 100  // F1..F12 = 100..111
};
enum { M_SHIFT = 1, M_ALT = 2, M_CTRL = 4 };

// A key press: code K_CHAR with the character ch, or a special key; mods = M_* bits.
struct Key {
    int code = K_NONE;
    char32_t ch = 0;
    int mods = 0;
};

struct MouseEv {
    int x = 0, y = 0, btn = 0, mods = 0, wheel = 0;
    bool press = false, release = false, motion = false;
};

extern MouseEv g_mouse;     // last mouse event (valid after K_MOUSE)
extern u32 g_paste;         // pasted text (valid after K_PASTE)
extern bool g_altPending;   // Alt is held down without another key so far
extern int g_keyTimeout;    // readKeyRaw() returns K_TIMEOUT after this many ms (-1 = wait forever)

// Writes s to stdout completely.
void writeAll(const std::string& s);
// Switches the terminal to raw mode and the alternate screen; false when stdin/stdout is no
// terminal. The terminal is restored at exit and on fatal signals.
bool termInit();
void termRestore();
// Is more input already waiting (key repeat, fast typing)?
bool inputPending();
// Waits for the next key, mouse event, paste or resize (see g_keyTimeout).
Key readKeyRaw();
