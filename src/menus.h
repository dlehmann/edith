#pragma once
// Menu bar contents, the help text and the status line hint, in the UI language.

#include <vector>

#include "common.h"

enum Act {
    A_NONE,
    A_NEW,
    A_OPEN,
    A_SAVE,
    A_SAVEAS,
    A_EXIT,
    A_UNDO,
    A_REDO,
    A_CUT,
    A_COPY,
    A_PASTE,
    A_CLEAR,
    A_SELALL,
    A_FIND,
    A_FINDNEXT,
    A_REPLACE,
    A_GOTO,
    A_OVERWRITE,
    A_AUTOINDENT,
    A_EXPANDTAB,
    A_TABWIDTH,
    A_HELP,
    A_ABOUT,
    A_REDRAW,
    A_SYNTAX,
    A_LANG,
    A_LINENUM,
    A_RELNUM,
    A_INVISIBLES,
    A_VIM,
    A_SPLIT,
    A_VSPLIT,
    A_CLOSEWIN,
    A_ONLY,
    A_NEXTWIN,
    A_PREVWIN,
    A_NEWTAB,
    A_OPENTAB,
    A_CLOSETAB,
    A_NEXTTAB,
    A_PREVTAB,
    A_FORMAT
};

// One menu entry. '&' marks the hotkey letter; label == nullptr is a separator.
struct MItem {
    const char32_t* label;
    const char32_t* sc;    // shortcut shown on the right
    int act;
    const char32_t* help;  // description shown in the status line
};

struct Menu {
    const char32_t* title;
    std::vector<MItem> items;
};

extern std::vector<Menu> MENUS;

// Fills MENUS in the current UI language (call after the language is known).
void buildMenus();
// Hotkey letter of a label (the character after '&'), case-folded.
char32_t hotkeyOf(const char32_t* s);
// Label without the '&' marker.
u32 stripAmp(const char32_t* s);
// Text of the help dialog.
const std::vector<u32>& helpText();
// Default status line text.
u32 statusHint();
