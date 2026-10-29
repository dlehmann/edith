// Menu definitions, the English help text and the status line hint.

#include "menus.h"

#include "i18n.h"
#include "terminal.h"
#include "utf8.h"

std::vector<Menu> MENUS;

void buildMenus() {
    const MItem SEP{nullptr, nullptr, 0, nullptr};
    MENUS = {
        {tr(U"&File"), {
            {tr(U"&New"), tr(U"Ctrl+N"), A_NEW, tr(U"Starts a new, empty file")},
            {tr(U"&Open..."), tr(U"Ctrl+O"), A_OPEN, tr(U"Loads an existing file")},
            {tr(U"Open in New Ta&b..."), U"", A_OPENTAB, tr(U"Loads a file in a new tab")},
            {tr(U"&Save"), tr(U"Ctrl+S"), A_SAVE, tr(U"Saves the current file")},
            {tr(U"Save &As..."), U"", A_SAVEAS, tr(U"Saves the file under a new name")},
            SEP,
            {tr(U"C&lose Tab"), U"F4", A_CLOSETAB, tr(U"Closes the current tab")},
            {tr(U"E&xit"), tr(U"Ctrl+Q"), A_EXIT, tr(U"Exits the editor")},
        }},
        {tr(U"&Edit"), {
            {tr(U"&Undo"), tr(U"Ctrl+Z"), A_UNDO, tr(U"Undoes the last change")},
            {tr(U"&Redo"), tr(U"Ctrl+Y"), A_REDO, tr(U"Restores the last undone change")},
            SEP,
            {tr(U"Cu&t"), tr(U"Ctrl+X"), A_CUT, tr(U"Moves the selected text to the clipboard")},
            {tr(U"&Copy"), tr(U"Ctrl+C"), A_COPY, tr(U"Copies the selected text to the clipboard")},
            {tr(U"&Paste"), tr(U"Ctrl+V"), A_PASTE, tr(U"Inserts the contents of the clipboard")},
            {tr(U"Cl&ear"), tr(U"Del"), A_CLEAR, tr(U"Deletes the selected text")},
            SEP,
            {tr(U"Select &All"), tr(U"Ctrl+A"), A_SELALL, tr(U"Selects the entire text")},
            SEP,
            {tr(U"Re&format Code"), U"F8", A_FORMAT, tr(U"Formats the file with the formatter of its language")},
        }},
        {tr(U"&Search"), {
            {tr(U"&Find..."), tr(U"Ctrl+F"), A_FIND, tr(U"Searches for text")},
            {tr(U"&Repeat Last Find"), U"F3", A_FINDNEXT, tr(U"Finds the next occurrence")},
            {tr(U"&Change..."), tr(U"Ctrl+R"), A_REPLACE, tr(U"Finds text and replaces it")},
            SEP,
            {tr(U"&Go to Line..."), tr(U"Ctrl+G"), A_GOTO,
             tr(U"Jumps to a line number (in hex mode: to an offset)")},
        }},
        {tr(U"&Options"), {
            {tr(U"&Overwrite Mode"), tr(U"Ins"), A_OVERWRITE, tr(U"Switches between insert and overwrite mode")},
            {tr(U"&Auto Indent"), U"", A_AUTOINDENT, tr(U"New lines keep the indentation of the previous line")},
            {tr(U"Tabs as &Spaces"), U"", A_EXPANDTAB, tr(U"The Tab key inserts spaces instead of tab characters")},
            {tr(U"&Tab Width..."), U"", A_TABWIDTH, tr(U"Sets the width of the tab stops")},
            SEP,
            {tr(U"Line &Numbers"), U"", A_LINENUM, tr(U"Shows or hides line numbers on the left")},
            {tr(U"&Relative Line Numbers"), U"", A_RELNUM, tr(U"Shows the distance of each line from the cursor line")},
            {tr(U"Show &Invisible Characters"), U"", A_INVISIBLES,
             tr(U"Shows tabs, spaces, line ends and other invisible characters")},
            SEP,
            {tr(U"Syntax &Highlighting"), U"", A_SYNTAX, tr(U"Turns syntax highlighting on or off")},
            {tr(U"&Language..."), U"", A_LANG, tr(U"Chooses the language for syntax highlighting")},
            SEP,
            {tr(U"He&x Mode"), U"", A_HEX, tr(U"Shows and edits the bytes of the file in hexadecimal")},
            {tr(U"&Vim Mode"), U"F12", A_VIM, tr(U"Turns the Vim key bindings on or off")},
        }},
        {tr(U"&Window"), {
            {tr(U"Split &Horizontally"), U"", A_SPLIT, tr(U"Opens a new window with an empty tab above")},
            {tr(U"Split &Vertically"), U"", A_VSPLIT, tr(U"Opens a new window with an empty tab on the left")},
            {tr(U"&Close Window"), tr(U"Ctrl+W"), A_CLOSEWIN, tr(U"Closes the current window and its tabs")},
            {tr(U"Close &Other Windows"), U"", A_ONLY, tr(U"Closes all other windows and their tabs")},
            {tr(U"&Next Window"), U"F6", A_NEXTWIN, tr(U"Moves to the next window")},
            {tr(U"&Previous Window"), tr(U"Shift+F6"), A_PREVWIN, tr(U"Moves to the previous window")},
            SEP,
            {tr(U"New &Tab"), tr(U"Ctrl+T"), A_NEWTAB, tr(U"Opens a new tab with an empty file")},
            {tr(U"Ne&xt Tab"), tr(U"Ctrl+PgDn"), A_NEXTTAB, tr(U"Moves to the next tab")},
            {tr(U"Previou&s Tab"), tr(U"Ctrl+PgUp"), A_PREVTAB, tr(U"Moves to the previous tab")},
        }},
        {tr(U"&Help"), {
            {tr(U"&Keyboard and Mouse..."), U"F1", A_HELP, tr(U"Shows the key bindings and mouse usage")},
            SEP,
            {tr(U"&About Edith..."), U"", A_ABOUT, tr(U"Information about this editor")},
        }},
    };
}

char32_t hotkeyOf(const char32_t* s) {
    for (; *s; s++)
        if (*s == '&' && s[1]) return fold(s[1]);
    return 0;
}

u32 stripAmp(const char32_t* s) {
    u32 r;
    for (; *s; s++)
        if (*s != '&') r += *s;
    return r;
}

static const std::vector<u32> HELP_EN = {
    U"MENUS",
    U"  Alt or F10               Activate the menu bar (Alt alone works only",
    U"                           with the kitty keyboard protocol)",
    U"  Alt+letter               Open a menu directly (e.g. Alt+F for File)",
    U"  Arrow keys / Enter       Move / choose in a menu",
    U"  h j k l                  Like the arrow keys in menus and dialogs",
    U"                           (not in input fields; lists: only j k)",
    U"  Alt+letter in a menu     Choose the entry with that letter",
    U"  Esc                      Close a menu or dialog",
    U"",
    U"FILE",
    U"  Ctrl+N  New              Ctrl+O  Open",
    U"  Ctrl+S  Save             Ctrl+Q  Exit",
    U"",
    U"EDIT",
    U"  Ctrl+Z  Undo             Ctrl+Y  Redo",
    U"  Ctrl+X  Cut              (also Shift+Del)",
    U"  Ctrl+C  Copy             (also Ctrl+Ins)",
    U"  Ctrl+V  Paste            (also Shift+Ins)",
    U"  Ctrl+A  Select all",
    U"  Ins     Toggle insert / overwrite mode",
    U"  Tab / Shift+Tab   Indent / unindent the selected lines",
    U"",
    U"CURSOR AND SELECTION",
    U"  Arrows, Home, End, PgUp, PgDn   Move the cursor",
    U"  Ctrl+← / Ctrl+→          Move by word",
    U"  Alt+Backspace            Delete the word before the cursor",
    U"  Ctrl+Home / Ctrl+End     Start / end of the file",
    U"  Ctrl+↑ / Ctrl+↓          Scroll the view by one line",
    U"  Shift + movement         Select text",
    U"",
    U"LINE NUMBERS",
    U"  Options > Line Numbers shows them, Options > Relative",
    U"  Line Numbers shows the distance from the cursor line.",
    U"  Clicking the number column selects the whole line.",
    U"  At startup: edith -n (absolute) or edith -r (relative).",
    U"",
    U"INVISIBLE CHARACTERS",
    U"  Options > Show Invisible Characters (in Vim mode :set list)",
    U"  shows tabs as →, spaces as ·, line ends as ¶ (↵ with CRLF),",
    U"  no-break spaces as °, control characters as ␀ to ␟ and",
    U"  marks zero-width characters with a colored cell.",
    U"",
    U"SEARCH",
    U"  Ctrl+F  Find             F3      Repeat last find",
    U"  Ctrl+R  Change           Ctrl+G  Go to line",
    U"  Regular Expression: ( ) | + ? {n,m} [ ] ^ $ \\d \\w \\s,",
    U"  < > word start/end; in the replacement \\1 to \\9 insert groups.",
    U"",
    U"MOUSE",
    U"  Click to place the cursor, drag to select text,",
    U"  double-click to select a word, Shift+click to extend",
    U"  the selection. The wheel scrolls. Menus, dialogs and",
    U"  scroll bars can be clicked.",
    U"",
    U"SYNTAX HIGHLIGHTING",
    U"  The language is detected from the file extension, the",
    U"  file name or the #! line and is shown at the bottom right.",
    U"  The Options menu turns it off or lets you choose it.",
    U"  More than 50 languages are fully parsed with tree-sitter,",
    U"  including embedded languages (PHP/HTML/JS/CSS, code",
    U"  blocks in Markdown, heredocs, regular expressions ...).",
    U"",
    U"REFORMAT CODE",
    U"  Edit > Reformat Code (F8, in Vim mode also :Format) runs",
    U"  the standard formatter of the language if it is installed:",
    U"  clang-format (C, C++, C#, Java), prettier (JavaScript,",
    U"  TypeScript, CSS, HTML, JSON, Markdown, YAML), ruff or black",
    U"  (Python), gofmt, rustfmt, shfmt, stylua, zig fmt, xmllint",
    U"  and others. Project settings such as .clang-format or",
    U"  pyproject.toml apply. Without a formatter JSON is formatted",
    U"  by Edith and other languages are re-indented by their syntax",
    U"  (Python, YAML, Makefile and Markdown keep their indentation).",
    U"  Other commands: ~/.config/edith/formatters, one line per",
    U"  language, e.g.  Python = black -q -  or  C = none",
    U"",
    U"WINDOWS AND TABS",
    U"  Every window has its own tabs in its top line.",
    U"  Window > Split Horizontally / Vertically opens a new window",
    U"  with an empty tab; closing a window closes its tabs (it asks",
    U"  to save modified files).",
    U"  F6 / Shift+F6            Next / previous window",
    U"  Ctrl+W                   Close the window and its tabs",
    U"  Ctrl+T                   New tab in the window",
    U"  F4                       Close the tab (the last one closes",
    U"                           the window)",
    U"  Ctrl+PgDn / Ctrl+PgUp    Next / previous tab of the window",
    U"  Click a window to activate it, drag its tab line or the",
    U"  border between windows to resize it, click a tab to switch",
    U"  to it; a click on the × of a tab closes it. edith a b opens",
    U"  tabs, edith -o a b / edith -O a b split windows.",
    U"  In Vim mode: Ctrl+W s v w h j k l c o q + - < > = _ | T,",
    U"  gt gT and :split :vsplit :new :close :only :resize",
    U"  :tabnew :tabclose :tabnext :tabprevious :tabmove :tabs",
    U"  (the tabs of the current window). :split shows the same",
    U"  file in a new window, :new an empty one.",
    U"",
    U"HEX MODE",
    U"  Options > Hex Mode (or edith --hex) shows the bytes of the",
    U"  file in hexadecimal and as characters; binary files open in",
    U"  hex mode automatically. Hex digits change the bytes, Tab",
    U"  switches to the characters. Ins switches between overwrite",
    U"  and insert, Del and Backspace delete bytes, Ctrl+F finds text",
    U"  or hex bytes, Ctrl+G jumps to an offset (hexadecimal).",
    U"",
    U"VIM MODE",
    U"  Options > Vim Mode (or edith --vim) turns on the Vim key",
    U"  bindings: Normal, Insert, Replace, Visual (v, V, Ctrl+V)",
    U"  and Command-line mode with motions, operators, text",
    U"  objects, counts, registers, marks, macros (q, @), the",
    U"  dot command, undo (u, Ctrl+R), searching with / ? n N * #",
    U"  and Ex commands such as :w :q :e :s :g :normal :set :map.",
    U"  The menus stay available with Alt and F10, Ctrl+S saves",
    U"  and Ctrl+Q exits in every mode. F12 turns Vim mode on and off.",
    U"",
    U"CLIPBOARD",
    U"  Cut, Copy and Paste use the system clipboard: on macOS",
    U"  through pbcopy / pbpaste, on Linux through wl-clipboard,",
    U"  xclip or xsel if one of them is installed. Otherwise, and",
    U"  over SSH, Edith asks the terminal (OSC 52); if it does not",
    U"  answer, Paste uses Edith's own clipboard. Copied text is",
    U"  also sent to the terminal via OSC 52. The terminal's paste",
    U"  function (usually Ctrl+Shift+V) works as well. In Vim mode",
    U"  the registers + and * are the system clipboard.",
};

// Appended to the help on macOS
static const std::vector<u32> HELP_MAC_EN = {
    U"",
    U"MACOS",
    U"  ⌥← / ⌥→                  Move by word (Ctrl+← / Ctrl+→ switch",
    U"                           Spaces in macOS)",
    U"  ⌘← / ⌘→                  Start / end of the line",
    U"  ⌘↑ / ⌘↓                  Start / end of the file",
    U"  ⌥⌫ / ⌘⌫                  Delete the word / up to the line start",
    U"  ⌘⇧Z  Redo                ⌘G  Repeat last find",
    U"  ⌘⌥F  Change              ⌘L  Go to line",
    U"  ⌘⇧] / ⌘⇧[                Next / previous tab",
    U"  ⌘ works like Ctrl (⌘S, ⌘Z, ⌘F, ...) in terminals that pass",
    U"  it on: kitty, Ghostty, WezTerm, iTerm2. Keys the terminal",
    U"  uses itself (often ⌘C, ⌘W, ⌘T, ⌘Q) do not reach Edith.",
    U"",
    U"TERMINAL.APP",
    U"  Terminal.app keeps ⌘ for itself: use the Ctrl shortcuts",
    U"  (⌘V still pastes text). Function keys need fn: fn+F1 shows",
    U"  this help, fn+F10 opens the menu.",
};


const std::vector<u32>& helpText() {
    static std::vector<u32> help;
    const std::vector<u32>* h = translatedHelp();
    help = h ? *h : HELP_EN;
    if (g_macKeys) {
        const std::vector<u32>* m = translatedHelpMac();
        help.insert(help.end(), (m ? *m : HELP_MAC_EN).begin(), (m ? *m : HELP_MAC_EN).end());
    }
    return help;
}

u32 statusHint() { return tr(U"<F1=Help>  Alt or F10 activates the menu"); }
