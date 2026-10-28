#pragma once
// The application: owns the screen and the text buffer, draws the editor window,
// runs the main loop and implements menus, dialogs and mouse handling.
//
// The implementation is split by topic:
//   app.cpp          main loop, keyboard handling, menu actions
//   app_draw.cpp     drawing of menu bar, editor window and status line
//   app_menu.cpp     pull-down menus
//   app_dialogs.cpp  message boxes, file dialog, find/replace and other dialogs
//   app_mouse.cpp    mouse handling and scroll bars
//   app_windows.cpp  buffers, split windows and tabs
//   app_format.cpp   Reformat Code and re-indenting
//   app_hex.cpp      hex mode

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "common.h"
#include "editor.h"
#include "hex.h"
#include "screen.h"
#include "terminal.h"
#include "treesitter.h"
#include "vim.h"

struct App;

// Connects the Vim engine to the application (dialogs, clipboard, quitting ...).
struct AppVimHost : VimHost {
    App* app = nullptr;
    int viewHeight() override;
    int viewWidth() override;
    void clipboardSet(const u32& t) override;
    u32 clipboardGet() override;
    void quit() override;
    void bufferLoaded() override;
    void showLines(const u32& title, const std::vector<u32>& lines) override;
    int confirm(const u32& prompt) override;
    void help() override;
    void redraw() override;
    bool setFiletype(const std::string& name) override;
    void setSyntax(bool on) override;
    bool layout(const std::string& cmd, int count, const u32& arg, bool bang, u32& err) override;
    bool reindent(int y1, int y2, std::vector<int>& widths) override;
    void format() override;
    bool bufferShownElsewhere() override;
};

// A buffer: the text of a file with its syntax highlighting. The buffer of the active window
// is "checked out": its text lives in App::ed and store is empty; every other buffer keeps
// its text in store.
struct Doc {
    Editor store;
    int lang = -1;             // index into LANGS, -1 = plain text
    bool langManual = false;   // language was chosen by hand
    std::vector<int> hlStates{0};
    int hlValid = 0;           // hlStates[0..hlValid] are valid (fallback lexer)
    TsDoc tsd;                 // tree-sitter parser for this buffer
    std::vector<uint8_t> tsVis;
    VimBufState vim;           // marks etc. while the buffer is not active
    std::unique_ptr<HexBuf> hex;  // the bytes in hex mode, null in text mode
};

// What a window shows of its buffer
struct View {
    int cy = 0, cx = 0, top = 0, left = 0;
    bool anch = false;
    Pos anchor;
    int wantCol = -1, curswant = 0;
    size_t hexPos = 0, hexTop = 0;  // hex mode
    bool hexChars = false;
};

struct Win {
    Doc* doc = nullptr;
    View v;                    // saved while the window is not active
    VimWinState vim;           // jump list while the window is not active
    int x = 0, y = 0, w = 1, h = 1;  // text area including the line number column
    int sy = -1;               // row of the window's status line, -1 = none (only one window)
};

// The window layout of a tab page: a tree of rows and columns
struct Node {
    enum { LEAF, ROWS, COLS };
    Node* parent = nullptr;
    int kind = LEAF;
    double weight = 1;         // share of the space in the parent
    std::vector<std::unique_ptr<Node>> kids;
    std::unique_ptr<Win> win;  // LEAF
    int x = 0, y = 0, w = 0, h = 0;
};

struct Tab {
    std::unique_ptr<Node> root;
    Win* cur = nullptr;        // active window of this tab
    Win* prev = nullptr;       // previously active window (Ctrl-W p)
};

struct App {
    Screen scr;
    Editor ed;
    AppVimHost vimHost;
    Vim vim{ed, &vimHost};
    u32 clip;                  // internal clipboard
    bool quit = false;
    int dragging = 0;          // 1 = selecting text, 2 = vertical, 3 = horizontal scroll bar, 4 = window border
    Node* dragNode = nullptr;  // the layout node whose border is dragged
    int dragKid = 0;
    Clock::time_point lastClickT;
    Pos lastClickP{-1, -1};
    u32 findText, replText;

    App();
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;
    bool findCase = false, findWhole = false, findRegex = false;

    // Buffers, windows and tabs (app_windows.cpp)
    std::vector<std::unique_ptr<Doc>> docs;
    std::vector<std::unique_ptr<Tab>> tabs;
    int curTab = 0;
    Win* win = nullptr;        // the active window
    Doc& doc() const { return *win->doc; }
    Tab& tab() const { return *tabs[size_t(curTab)]; }
    // The text of a buffer (App::ed for the active one)
    Editor& textOf(Doc* d) { return d == win->doc ? ed : d->store; }
    void layoutAll();
    void layoutNode(Node* n, int x, int y, int w, int h, bool multi);
    std::vector<Win*> windows(int t = -1) const;  // windows of a tab (-1 = current) in screen order
    Node* nodeOf(Win* w) const;
    int tabOf(Win* w) const;
    bool shown(Doc* d, Win* except) const;        // is the buffer shown in another window?
    void saveView();
    void activate(Win* w);
    bool splitWin(bool vertical, Doc* d = nullptr, u32* err = nullptr);
    Doc* newDoc();
    void useNewDoc();                              // the current window gets an empty buffer
    bool confirmClose(Win* w);                     // asks to save a buffer that would be lost
    bool closeWin(Win* w, bool force);
    bool onlyWin(bool force);
    void removeWin(Win* w);
    void newTab(Doc* d);
    bool closeTab(int t, bool force);
    void gotoTab(int t);
    Win* neighbor(char32_t dir, int count);
    void resizeWin(bool vertical, int size, bool relative);
    void equalize(Node* n);
    void moveToNewTab();
    void openInNewTab();

    // Syntax highlighting
    std::vector<uint8_t> hlBuf;

    void setLang(int l);
    void autoLang();
    // Passes text changes on to the lexer state cache and to tree-sitter.
    void consumeDirty(Editor& e, Doc& d);
    void consumeDirty() { consumeDirty(ed, doc()); }
    bool hlOn(const Doc& d) const { return S.syntax && d.lang >= 0; }
    bool hlOn() const { return hlOn(doc()); }
    // Lexer state at the start of line y (computes missing lines).
    int hlStateAt(Editor& e, Doc& d, int y);

    // Geometry of the text area of the active window
    int ty() const { return win->y; }
    // Width of the line number column (digits + one space), 0 when hidden.
    int gutter() const { return hexMode() ? 0 : gutterFor(ed); }
    int gutterFor(const Editor& e) const;
    int tx() const { return win->x + gutter(); }
    int th() const { return std::max(1, win->h); }
    int tw() const { return std::max(1, win->w - gutter()); }
    bool tooSmall() const { return scr.w < 40 || scr.h < 10; }

    // Drawing (app_draw.cpp)
    std::vector<std::pair<int, int>> menuLayout() const;
    int menuAt(int x) const;
    void drawMenubar(int active);
    void drawLine(int x0, int left, int width, int y, const u32& l, int s, int e, const uint8_t* cls,
                  const std::vector<std::pair<int, int>>* hl = nullptr, int eol = 0);  // eol: 1 LF, 2 CRLF
    u32 titleOf(const Editor& e) const;  // file name and " *" when modified
    void drawEditor();
    void drawWindow(Win& w, bool active);
    void drawWinStatus(Win& w, bool active);
    void drawSeparators(Node* n);
    void drawTabLine();
    std::vector<std::pair<int, int>> tabLayout() const;  // x and width of each tab label
    int maxLineWidth() const;
    void drawStatus(const u32* status);
    void drawAll(const u32* status = nullptr, int activeMenu = -1);
    void placeCursor();
    void dropRect(int mi, int& x, int& y, int& w, int& h) const;
    void drawDropdown(int mi, int cur);

    // Menus (app_menu.cpp)
    bool enabled(int act) const;
    bool checked(int act) const;
    int menuLoop(int mi, bool dropped);
    int shortcut(const Key& k) const;

    // Dialogs and file operations (app_dialogs.cpp)
    int msgbox(const u32& title, const u32& text, std::vector<u32> btns = {}, int y = -1);
    void error(const u32& text);
    std::string fileDialog(const u32& title, const std::string& initName);
    bool confirmDiscard();     // the current buffer (when it is not shown in another window)
    bool confirmDiscardAll();  // every modified buffer (before exiting)
    bool writeTo(const std::string& path);
    bool doSave();
    bool doSaveAs();
    void openPath(const std::string& p);
    void openFiles(const std::vector<std::string>& paths, char how);  // how: 't' tabs, 'o'/'O' splits
    void doOpen();
    void findDialog();
    bool findNext();
    bool compileFind(VimRegex& rx);                       // the pattern of the Find dialog; a message on errors
    bool findMatch(const VimRegex& rx, Pos from, RxMatch& m);  // literal or regular expression
    void replaceDialog();
    int numberDialog(const u32& title, const u32& label, int val);
    void gotoDialog();
    void langDialog();
    void helpDialog();
    void linesDialog(const u32& title, const std::vector<u32>& lines);

    // Mouse (app_mouse.cpp)
    void vscrollTo(int y);
    void hscrollTo(int x);
    void handleMouse();

    // Reformat Code (app_format.cpp)
    u32 statusMsg;             // a message in the status line until the next key
    bool statusErr = false;
    void notify(const u32& m, bool err);
    bool reindentLines(int y1, int y2, std::vector<int>& widths);  // by the syntax tree or braces
    std::vector<u32> builtinFormat(u32& msg, bool& reindented);
    void reformatCode();

    // Hex mode (app_hex.cpp)
    bool hexStart = false;     // edith --hex: the files of the command line are opened in hex mode
    u32 hexFindText;
    bool hexFindHex = true;
    bool hexMode() const { return win->doc->hex != nullptr; }
    void setHex(bool on);
    void hexFromFile(std::string data);  // the current buffer shows these bytes of its file
    void hexIfBinary();                  // after loading a file: hex mode for a binary file
    HexLayout hexLayout() const;
    void hexSync();                      // a change of the bytes marks the buffer as modified
    void hexScroll(bool center = false);
    void drawHexWindow(Win& w, bool active);
    u32 hexStatus() const;
    bool hexCursor();
    void hexKey(const Key& k);
    void hexPaste(const u32& t);
    bool hexAct(int a);
    void hexMouseAt(int x, int y, bool select);
    void hexWheel(int d);
    void hexVscroll(int pos, int len);
    void hexFindDialog();
    bool hexFindNext();
    void hexGotoDialog();
    // Saves a buffer (its bytes in hex mode); conv as for Editor::save
    bool saveDoc(Doc* d, const std::string& path, std::string& err, bool& conv);

    // Main loop and actions (app.cpp)
    void copyToSystem(const u32& t);
    u32 pasteText();           // the system clipboard if it can be read, else App::clip
    void doAct(int a);
    void handleKey(const Key& k);
    void run();
};
