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
// M_CMD: the Command key on macOS (Super elsewhere), only reported with the kitty keyboard protocol
enum { M_SHIFT = 1, M_ALT = 2, M_CTRL = 4, M_CMD = 8 };

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
// Mac key bindings in macKey(): true on macOS
extern bool g_macKeys;

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
// With g_macKeys: the text navigation keys of macOS become the keys Edith knows. Option+Left /
// Option+Right move by word like Ctrl+Left / Ctrl+Right, Cmd+Left / Cmd+Right become Home / End
// and Cmd+Up / Cmd+Down Ctrl+Home / Ctrl+End. Other keys are returned unchanged. (readKeyRaw()
// already reads ESC b / ESC f, which Terminal.app sends for Option+Left / Right, as Ctrl+Left / Right.)
Key macKey(Key k);
// Asks the terminal for the system clipboard (OSC 52) and waits for the answer. False when
// stdin is no terminal, the terminal does not answer or an earlier query failed. Keys typed
// meanwhile are kept for readKeyRaw().
bool termReadClipboard(std::string& out);
// Can termReadClipboard() succeed (a terminal that has not failed to answer)?
bool termCanReadClipboard();
