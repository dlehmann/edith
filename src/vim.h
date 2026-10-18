#pragma once
// Vim emulation for the text buffer (Options > Vim Mode).
//
// The engine works on an Editor and receives keys from the application. It implements
// Normal, Insert, Replace, Visual (character, line, block), Operator-pending and
// Command-line mode with Vim's semantics (":help" is the reference): motions, operators,
// text objects, counts, registers, marks, undo/redo, the dot command, macros, mappings,
// searching with Vim patterns and the common Ex commands.
//
// The implementation is split by topic:
//   vim.cpp         key codes, input queue, mappings, registers, insert and command-line mode
//   vim_normal.cpp  command parser, motions, operators, text objects, Visual mode
//   vim_ex.cpp      Ex commands (:s, :g, :w, :set, :map, ...)

#include <climits>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "common.h"
#include "editor.h"
#include "terminal.h"
#include "vimregex.h"

enum VimMode { VM_NORMAL, VM_INSERT, VM_REPLACE, VM_VISUAL, VM_VLINE, VM_VBLOCK, VM_CMDLINE };

// Keys inside the engine are code points: printable characters as themselves, control keys
// as ASCII control codes (Esc = 27, Enter = 13, Backspace = 8, Tab = 9) and all other keys
// (arrows, Home, Del, ...) in the private use plane.
namespace VK {
constexpr char32_t ESC = 27, CR = 13, NL = 10, BS = 8, TAB = 9;
constexpr char32_t SPECIAL = 0xF0000;
constexpr char32_t key(int code, int mods = 0) { return SPECIAL + char32_t(code) * 8 + char32_t(mods); }
constexpr char32_t NUL = SPECIAL + 1;  // Ctrl-@
constexpr char32_t UP = key(K_UP), DOWN = key(K_DOWN), LEFT = key(K_LEFT), RIGHT = key(K_RIGHT);
constexpr char32_t HOME = key(K_HOME), END = key(K_END), PGUP = key(K_PGUP), PGDN = key(K_PGDN);
constexpr char32_t INS = key(K_INS), DEL = key(K_DEL), STAB = key(K_TAB, M_SHIFT);
constexpr char32_t ctrl(char c) { return char32_t(c & 0x1f); }
inline bool special(char32_t c) { return c >= SPECIAL; }
}  // namespace VK

// Converts a key event to an engine key code; 0 = the engine does not use this key.
char32_t vimKeyCode(const Key& k);
// Parses key notation such as "dd<Esc>:w<CR>" or "<C-v>j" into key codes.
u32 vimParseKeys(const u32& notation);
// Key codes back to notation (for :map, :registers and the command display).
u32 vimKeyNames(const u32& codes);

enum { RT_CHAR, RT_LINE, RT_BLOCK };

struct VimReg {
    std::vector<u32> lines;
    int type = RT_CHAR;
    bool empty() const { return lines.empty(); }
    u32 text() const;  // joined with '\n' (linewise: with a trailing '\n')
};

// Callbacks into the application. The defaults do nothing, which is what the tests use.
struct VimHost {
    virtual ~VimHost() = default;
    virtual int viewHeight() { return 20; }
    virtual int viewWidth() { return 78; }
    virtual void clipboardSet(const u32&) {}
    virtual u32 clipboardGet() { return clip; }
    virtual void quit() { quitRequested = true; }
    virtual void bufferLoaded() {}
    virtual void showLines(const u32&, const std::vector<u32>& lines) { shown = lines; }
    // :s///c asks for each match: returns 'y', 'n', 'a', 'q', 'l' or 27
    virtual int confirm(const u32&) { return 'y'; }
    virtual void help() {}
    virtual void redraw() {}
    virtual bool setFiletype(const std::string&) { return false; }
    virtual void setSyntax(bool) {}
    // Windows and tab pages (":help windows", ":help tabpage"). cmd: split vsplit new vnew
    // close only quit qall next prev left down up right top bottom last equal height width
    // tonewtab tabnew tabclose tabonly tabnext tabprev tabfirst tablast tabmove tabs newbuf
    // wall. count 0 = not given. Returns false with an error message in err.
    virtual bool layout(const std::string& cmd, int count, const u32& arg, bool bang, u32& err);
    // Another window also shows the buffer of the current window.
    virtual bool bufferShownElsewhere() { return false; }
    // Indentation of the lines y1..y2 for "=" (-1 = keep); false = the engine's own rules
    virtual bool reindent(int, int, std::vector<int>&) { return false; }
    // Reformat Code (":Format")
    virtual void format() {}
    u32 clip;
    bool quitRequested = false;
    std::vector<u32> shown;
};

enum { MM_N = 1, MM_X = 2, MM_O = 4, MM_I = 8, MM_C = 16 };

// Buffer-local state that the application keeps for buffers in inactive windows
struct VimBufState {
    std::map<char32_t, Pos> marks;
    std::vector<Pos> changes;
    int changeIdx = 0;
    Pos lastVStart{-1, 0}, lastVEnd{-1, 0};
    int lastVMode = VM_VISUAL;
};

// Window-local state (the jump list)
struct VimWinState {
    std::vector<Pos> jumps;
    int jumpIdx = 0;
};

struct VimMap {
    u32 lhs, rhs;
    bool noremap = false;
    int modes = 0;
};

// A parsed Normal or Visual mode command
struct VimCmd {
    char32_t reg = 0;
    int count1 = 0, count2 = 0;  // 0 = not given
    u32 op;                      // operator ("d", "c", "g~", ...) or empty
    char32_t force = 0;          // v, V or Ctrl-V between operator and motion
    u32 key;                     // command or motion ("w", "gg", "iw", "f", ...)
    char32_t arg = 0;            // character argument (f, t, r, m, ...)
    u32 pat;                     // search pattern for the / and ? motions
    bool hasCount() const { return count1 || count2; }
    int count() const {
        long c = long(count1 ? count1 : 1) * (count2 ? count2 : 1);
        return int(std::min(c, 99999999L));
    }
};

struct VimOptions {
    int shiftwidth = 0;  // 0 = use 'tabstop'
    bool ignorecase = false, smartcase = false, wrapscan = true, hlsearch = false, incsearch = false;
    bool joinspaces = true, startofline = true, shiftround = false, tildeop = false, gdefault = false;
    bool timeout = true;
    bool write = true;       // 'write': files may be written
    int timeoutlen = 1000, textwidth = 0, scrolloff = 0, report = 2;
    std::string clipboard;  // "", "unnamed" or "unnamedplus"
    std::string nrformats = "bin,hex";
};

struct Vim {
    Editor& ed;
    VimHost* host;
    VimHost defaultHost;
    VimOptions opt;
    VimMode mode = VM_NORMAL;
    bool restricted = false;  // like rvim: no shell commands and filters

    explicit Vim(Editor& e, VimHost* h = nullptr) : ed(e), host(h ? h : &defaultHost) {}

    // ---- interface for the application ----
    void reset();                         // back to Normal mode, forget buffer positions (new file)
    void leaveWindow();                   // ends Insert/Visual/Command-line mode before a window switch
    void swapBufState(VimBufState& s);    // exchanges the buffer-local state (window switch)
    void swapWinState(VimWinState& s);    // exchanges the window-local state
    bool handleKey(const Key& k);         // false = the key is not used by the engine
    void feed(const u32& keys);           // typed keys (as key codes)
    void feedNotation(const char* keys);  // typed keys in key notation (tests)
    bool waiting() const;                 // an ambiguous mapping waits for more keys
    void timeout();                       // the mapping timeout expired
    void afterExternal();                 // the application moved the cursor or changed the text
    // Turning Vim mode on and off: the application's selection (anchor to cursor, the end exclusive)
    // becomes Visual mode, Visual mode becomes such a selection (a block from corner to corner).
    void visualFromSelection(Pos anchor, Pos cur);
    bool selectionFromVisual(Pos& anchor, Pos& cur) const;
    void pasteText(const u32& t);         // bracketed paste or menu Paste
    void menuCopy(bool cut);              // Edit > Copy / Cut in Visual mode
    bool visualActive() const { return mode == VM_VISUAL || mode == VM_VLINE || mode == VM_VBLOCK; }
    // Highlighted index range [s, e) of line y in Visual mode (e may be len+1 for the line break)
    bool visualSpan(int y, int& s, int& e) const;
    // Matches of the last search pattern in line y (for 'hlsearch')
    void searchSpans(int y, std::vector<std::pair<int, int>>& out) const { searchSpansIn(ed.lines, y, out); }
    void searchSpansIn(const std::vector<u32>& L, int y, std::vector<std::pair<int, int>>& out) const;
    u32 statusText() const;               // mode, pending command or message
    u32 showCmd() const;                  // the partly typed Normal mode command
    bool statusIsError() const { return msgErr && !msg.empty(); }
    bool cmdlineActive() const { return mode == VM_CMDLINE; }
    u32 cmdlineText() const;              // prompt and text, e.g. ":s/a/b"
    int cmdlineCursor() const;            // cursor column in cmdlineText()
    int cursorShape() const;              // DECSCUSR shape: block, bar or underline
    int shiftWidth() const { return opt.shiftwidth > 0 ? opt.shiftwidth : S.tabw; }

    // ---- state ----
    struct QKey {
        char32_t c;
        bool noremap;
        bool typed = false;  // typed by the user (recorded by "q")
    };
    std::deque<QKey> q;          // keys waiting to be executed
    bool forceMapResolve = false;
    int mapDepth = 0;
    std::vector<VimMap> maps;
    u32 leader = U"\\";

    u32 cmdbuf;                  // Normal mode command being typed
    bool pendNeedArg = false;    // the pending command waits for a character argument (no mappings)
    bool pendOp = false;         // an operator is pending (Operator-pending mode)
    int beeps = 0;               // number of failed commands (for the tests)
    mutable VimRegex hlRx;       // compiled pattern for 'hlsearch'
    mutable u32 hlRxPat;
    mutable int hlRxIc = -1;
    mutable bool hlRxOk = false;
    u32 msg;
    bool msgErr = false;
    bool msgKeep = false;

    std::map<char32_t, VimReg> regs;
    std::map<char32_t, Pos> marks;
    std::vector<Pos> jumps;
    int jumpIdx = 0;
    std::vector<Pos> changes;
    int changeIdx = 0;

    // Visual mode (the start is kept in ed.anchor so that mouse dragging extends it)
    Pos vstart() const { return ed.anchor; }
    VimMode lastVMode = VM_VISUAL;
    Pos lastVStart{-1, 0}, lastVEnd{-1, 0};
    bool blockDollar = false;
    int curswant = 0;            // desired display column for j/k (INT_MAX = end of line)
    int scrollAmount = 0;        // 'scroll' for Ctrl-D / Ctrl-U (0 = half the window)
    int mWant = -1;              // set by motion(): -1 = column of the target, -2 = keep, else column

    // Insert mode
    int insCount = 1;
    char32_t insCmd = 'i';
    u32 insKeys;                 // keys typed in this insert (repeated for a count and by ".")
    u32 insTyped;                // inserted text (". register)
    u32 lastInserted;
    Pos insStart;
    int aiLine = -1;             // line with only the automatic indent so far
    std::vector<std::pair<Pos, int>> replStack;  // Replace mode: replaced characters (-1 = appended)
    bool insNeedUndo = false;
    bool insOneCmd = false, insOneEol = false;
    int insPending = 0;          // 1 = Ctrl-V, 2 = Ctrl-R, 3 = Ctrl-K first, 4 = Ctrl-K second, 5 = Ctrl-G
    u32 insPendBuf;
    struct BlockIns {
        bool on = false;
        int y1 = 0, y2 = 0, col = 0;
        bool dollar = false, append = false;
        u32 before;               // line y1 before the insert
    } blk;
    struct Compl {
        bool on = false;
        Pos start;
        u32 prefix;
        std::vector<u32> cands;
        int idx = -1;
    } cmpl;

    // Command-line mode
    char32_t cmdType = ':';
    u32 cmdText;
    int cmdPos = 0;
    int cmdPending = 0;          // 1 = Ctrl-V, 2 = Ctrl-R
    std::map<char32_t, std::vector<u32>> hist;
    int histIdx = -1;
    u32 histPrefix;
    bool cmdHasPendingOp = false;
    VimCmd cmdPendingOp;
    VimMode cmdFromMode = VM_NORMAL;
    Pos cmdStartCur;

    // Searching
    u32 lastPat;                 // last search pattern
    bool lastDirFwd = true;
    u32 lastOffset;
    bool hlOn = false;           // highlight is shown (cleared by :nohlsearch)
    char32_t lastFind = 0, lastFindCmd = 0;
    u32 lastSubPat, lastSubRep, lastSubFlags;
    bool lastSubSet = false;
    u32 lastExCmd;
    u32 lastBangCmd;
    bool searchIsRepeat = false; // doSearch() repeats the last search (n, N)
    bool inGlobal = false;
    std::vector<u32> printBuf;   // lines printed by :p inside :g
    bool numberAlso = false;     // 'number' together with 'relativenumber'

    // Macros and repeat
    char32_t recReg = 0;
    u32 recBuf;
    char32_t lastExecReg = 0;
    int execDepth = 0;
    struct Dot {
        bool valid = false;
        VimCmd cmd;
        u32 ins;                  // keys typed in Insert mode, ending with <Esc>
        bool visual = false;
        VimMode vmode = VM_VISUAL;
        int vlines = 0, vcols = 0;
        bool vdollar = false;
    } dot, dotNew;
    bool dotRecording = false;   // the current command is repeatable and enters Insert mode
    bool replaying = false;

    // Undo grouping
    int changeDepth = 0;
    std::vector<Editor::Snap> savedRedo;
    bool savedModified = false;
    bool undoLocked = false;     // :g and :normal group their changes into one undo step

    std::vector<int>* trackLines = nullptr;  // lines followed through changes (:g)

    // ---- input (vim.cpp) ----
    void process();
    void exec(char32_t c);
    int mapMode() const;
    int tryMap();
    void runKeys(const u32& keys, bool remap);
    void beep();
    void error(const u32& m);
    void message(const u32& m);
    void abortPending();

    // ---- buffer primitives (vim.cpp) ----
    int n() const { return ed.n(); }
    const u32& line(int y) const { return ed.lines[y]; }
    int len(int y) const { return int(ed.lines[y].size()); }
    void setLine(int y, const u32& s);
    void insLines(int y, const std::vector<u32>& ls);
    void delLines(int y1, int y2);
    void replaceLines(int y1, int y2, const std::vector<u32>& ls);
    void insText(Pos p, const u32& t);
    void delText(Pos a, Pos b);
    void adjustMarks(int y, int delta, int delFrom = -1, int delTo = -1);
    void beginChange();
    void endChange();
    void setCursor(int y, int x);
    void clampNormal();
    int firstNonBlank(int y) const;
    void toFirstNonBlank(int y);
    int vcol(int y, int x) const { return ed.colOf(ed.lines[y], x); }
    int idxAtCol(int y, int col) const;
    u32 indentOf(int y) const;
    int indentWidth(int y) const;
    u32 makeIndent(int width) const;
    void setIndent(int y, int width);
    void setMark(char32_t m, Pos p);
    bool getMark(char32_t m, Pos& p);
    void pushJump();
    void recordChange(Pos p);

    // ---- registers (vim.cpp) ----
    bool validReg(char32_t r, bool write) const;
    VimReg getReg(char32_t r);
    void setReg(char32_t r, const VimReg& v, bool yank, bool smallDelete);
    VimReg textReg(Pos a, Pos b, int type) const;
    void put(char32_t reg, int count, bool after, bool gp, bool adjustIndent);

    // ---- insert mode (vim.cpp) ----
    void startInsert(char32_t cmd, int count, bool replace = false);
    void insertKey(char32_t c);
    void insChar(char32_t c);
    void insNewline();
    void insBackspace();
    void insEsc(bool interrupt);
    void insReplay(const u32& keys);
    void insComplete(bool fwd);
    void insDigraph(char32_t a, char32_t b);

    // ---- command-line mode (vim.cpp) ----
    void startCmdline(char32_t type, const u32& init = u32());
    void cmdKey(char32_t c);
    void cmdExecute();

    // ---- normal / visual mode (vim_normal.cpp) ----
    void normalKey(char32_t c);
    int parse(const u32& b, VimCmd& c, bool& needArg) const;
    void execute(VimCmd c);
    bool simpleCommand(VimCmd& c);
    bool visualCommand(VimCmd& c);
    void afterCommand();
    bool motion(const VimCmd& c, Pos& to, int& mtype, bool& jump, bool forOp);
    bool textObject(const VimCmd& c, Pos& a, Pos& b, int& mtype, bool& incl);
    void applyOperator(const VimCmd& c, Pos a, Pos b, int mtype, bool block, int bcol1, int bcol2, bool dollar);
    void opDelete(const VimCmd& c, Pos a, Pos b, int mtype, bool change);
    void opYank(char32_t reg, Pos a, Pos b, int mtype, bool moveCursor);
    void opCase(const u32& op, Pos a, Pos b, int mtype);
    void opShift(int y1, int y2, int dir, int amount);
    void opReindent(int y1, int y2);
    void opFormat(int y1, int y2, bool keepCursor);
    void joinLines(int y, int count, bool spaces);
    void enterVisual(VimMode m);
    void exitVisual();
    void visualBounds(Pos& a, Pos& b, int& c1, int& c2) const;
    void blockOp(const VimCmd& c, int y1, int y2, int c1, int c2, bool dollar);
    void blockSpan(int y, int c1, int c2, int& s, int& e) const;
    void expandTabsIn(int y, int c1, int c2);
    bool increment(int delta, int y, int x1, int x2, bool progressive, int step);
    void repeatDot(int count);
    void scrollCmd(const u32& k, int count);

    // motions (vim_normal.cpp)
    int cls(Pos p, bool big) const;
    int inc(Pos& p) const;
    int dec(Pos& p) const;
    bool fwdWord(Pos& p, int count, bool big, bool eol) const;
    bool bckWord(Pos& p, int count, bool big, bool stop) const;
    bool endWord(Pos& p, int count, bool big, bool stop, bool empty) const;
    bool bckEndWord(Pos& p, int count, bool big, bool eol) const;
    bool findChar(Pos& p, char32_t cmd, char32_t ch, int count, bool repeat) const;
    bool paragraph(Pos& p, int dir, int count, bool forOp, bool& incl) const;
    bool sentence(Pos& p, int dir, int count) const;
    bool matchPair(Pos& p) const;
    bool unmatched(Pos& p, char32_t open, char32_t close, bool fwd, int count) const;
    bool section(Pos& p, char32_t brace, bool fwd, int count) const;

    // searching (vim_ex.cpp)
    bool compilePat(const u32& pat, VimRegex& rx, bool useSmartcase = true);
    bool searchPat(const u32& pat, bool fwd, Pos from, int count, Pos& res, Pos* end, bool quiet, bool smart = true);
    bool doSearch(const u32& spec, bool fwd, int count, Pos& to, int& mtype, bool& incl);
    bool starSearch(bool fwd, bool whole, int count, Pos& to);

    // ---- Ex commands (vim_ex.cpp) ----
    void exCommand(u32 cmd);
    bool parseRange(const u32& s, size_t& i, int& l1, int& l2, int& naddr);
    bool parseAddr(const u32& s, size_t& i, int& ln, bool& got);
    void exSubstitute(int l1, int l2, const u32& args, char32_t name);
    void exGlobal(int l1, int l2, const u32& args, bool invert);
    void exSet(const u32& args);
    void exMap(const u32& name, const u32& args, bool bang);
    void exNormal(int l1, int l2, bool hasRange, const u32& args, bool bang);
    void exSort(int l1, int l2, const u32& args, bool bang);
    bool exWrite(const std::string& path, int l1, int l2, bool all, bool force, bool append);
    bool exEdit(const std::string& path, bool force);
    void exFilter(int l1, int l2, const std::string& cmd);
    void exRead(int after, const u32& arg);
    bool layoutCmd(const std::string& cmd, int count, const u32& arg = u32(), bool bang = false);
    bool winCmd(char32_t c, int count);   // Ctrl-W {c} and :wincmd {c}
    bool shellAllowed();
    // Runs a shell command and shows its output (and a failing exit status) under the title.
    void showShellOutput(const u32& title, const std::string& cmd, const std::string& input);
    std::vector<u32> listRegisters(const u32& which);
    u32 expandReplacement(const u32& rep, const RxMatch& m, const std::vector<u32>& L) const;
};
