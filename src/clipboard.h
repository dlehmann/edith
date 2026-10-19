#pragma once
// The system clipboard. Edith keeps its own clipboard (App::clip) and also exchanges text with
// the clipboard of the desktop: through a program (pbcopy / pbpaste on macOS, wl-copy /
// wl-paste, xclip or xsel on Linux) when Edith runs locally and one is installed, otherwise by
// asking the terminal (OSC 52, see termReadClipboard()). Copying also always sends OSC 52.

#include <string>

#include "common.h"

// Picks the clipboard programs (once at startup; not in SSH sessions, where they would use
// the clipboard of the wrong computer). Without it no program is used (as in the tests).
void clipboardDetect();
// Sets the shell commands for copying (text on stdin) and pasting (text on stdout); "" = none.
void clipboardSetCommands(const std::string& copy, const std::string& paste);
// Is there a way to read the system clipboard (a program or a terminal that answers)?
bool clipboardReadable();
// Puts text into the system clipboard.
void clipboardCopy(const u32& t);
// Reads the system clipboard; false when it cannot be read (then the own clipboard is used).
bool clipboardPaste(u32& t);
