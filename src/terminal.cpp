// Terminal setup and restore, and the decoding of keyboard, mouse and paste input.

#include "terminal.h"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <vector>

#include <poll.h>
#include <termios.h>
#include <unistd.h>

#include "paths.h"
#include "utf8.h"

static termios g_orig, g_raw;
static bool g_rawOn = false;
static volatile sig_atomic_t g_winch = 0;
static volatile sig_atomic_t g_cont = 0;  // continued after a stop: set the terminal up again

// Leaves the alternate screen and turns off everything termInit() turned on
static const char RESTORE_SEQ[] = "\x1b[<u\x1b[?2004l\x1b[?1006l\x1b[?1002l\x1b[?1000l\x1b[0m\x1b[0 q\x1b[?7h\x1b[?25h\x1b[?1049l";
// Alternate screen, kitty keyboard protocol, no autowrap, mouse (buttons + drag, SGR), bracketed paste.
// Terminals ignore the sequences they do not know.
static const char INIT_SEQ[] = "\x1b[?1049h\x1b[>31u\x1b[?7l\x1b[?1000h\x1b[?1002h\x1b[?1006h\x1b[?2004h\x1b[H\x1b[2J";

void writeAll(const std::string& s) {
    size_t off = 0;
    while (off < s.size()) {
        ssize_t n = write(1, s.data() + off, s.size() - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        off += size_t(n);
    }
}

// writeAll() without allocating: usable in signal handlers
static void writeSafe(const char* s) {
    size_t len = strlen(s), off = 0;
    while (off < len) {
        ssize_t n = write(1, s + off, len - off);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        off += size_t(n);
    }
}

void termRestore() {
    if (!g_rawOn) return;
    g_rawOn = false;
    writeSafe(RESTORE_SEQ);
    tcsetattr(0, TCSAFLUSH, &g_orig);
}

// Stopped by the shell's job control (kill -TSTP): give the terminal back, stop, and set it up
// again when continued. Only async-signal-safe calls (write, tcsetattr, signal, raise).
static void onStop(int) {
    int saved = errno;
    if (g_rawOn) {
        writeSafe(RESTORE_SEQ);
        tcsetattr(0, TCSANOW, &g_orig);
    }
    signal(SIGTSTP, SIG_DFL);
    sigset_t m;
    sigemptyset(&m);
    sigaddset(&m, SIGTSTP);
    sigprocmask(SIG_UNBLOCK, &m, nullptr);
    raise(SIGTSTP);  // stops here until SIGCONT (in an orphaned process group it does not stop)
    signal(SIGTSTP, onStop);
    if (g_rawOn) {
        tcsetattr(0, TCSANOW, &g_raw);
        writeSafe(INIT_SEQ);
    }
    g_winch = 1;  // redraw everything
    errno = saved;
}

static void onCont(int) { g_cont = 1; }

// Raw mode and the init sequences again, e.g. after the process was stopped and continued.
static void termReenter() {
    if (!g_rawOn) return;
    tcsetattr(0, TCSANOW, &g_raw);
    writeSafe(INIT_SEQ);
}

static void onFatalSignal(int sig) {
    termRestore();
    signal(sig, SIG_DFL);
    raise(sig);
}

static void onWinch(int) { g_winch = 1; }

bool termInit() {
    if (!isatty(0) || !isatty(1)) return false;
    if (tcgetattr(0, &g_orig) < 0) return false;
    termios t = g_orig;
    t.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    t.c_oflag &= ~OPOST;
    t.c_cflag |= CS8;
    t.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    if (tcsetattr(0, TCSAFLUSH, &t) < 0) return false;
    g_raw = t;
    g_rawOn = true;
    atexit(termRestore);

    struct sigaction sa {};
    sa.sa_handler = onWinch;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;  // no SA_RESTART: poll() must return with EINTR
    sigaction(SIGWINCH, &sa, nullptr);
    sa.sa_handler = onCont;
    sigaction(SIGCONT, &sa, nullptr);
    signal(SIGTSTP, onStop);
    signal(SIGTERM, onFatalSignal);
    signal(SIGHUP, onFatalSignal);
    signal(SIGSEGV, onFatalSignal);
    signal(SIGABRT, onFatalSignal);

    writeSafe(INIT_SEQ);
    return true;
}

// ---------------------------------------------------------------------------
// Keyboard and mouse input
// ---------------------------------------------------------------------------

MouseEv g_mouse;
u32 g_paste;
bool g_altPending = false;  // Alt is held down without another key so far
int g_keyTimeout = -1;
#ifdef __APPLE__
bool g_macKeys = true;
#else
bool g_macKeys = false;
#endif

// Milliseconds to wait after Esc for the rest of an escape sequence: $ESCDELAY as for ncurses
// programs (for slow connections), 30 by default.
static int escDelay() {
    static const int d = [] {
        const char* e = getenv("ESCDELAY");
        int v = e ? atoi(e) : 0;
        return v > 0 ? std::min(v, 2000) : 30;
    }();
    return d;
}
// Wait for the following bytes of a sequence that has started
static int seqDelay() { return std::max(50, escDelay()); }

static unsigned char g_ib[8192];
static int g_ibPos = 0, g_ibLen = 0;
static std::string g_back;  // bytes put back by termReadClipboard(), read before g_ib

// Reads one byte. -1 = timeout, -2 = interrupted (signal)
static int readByte(int timeoutMs) {
    if (!g_back.empty()) {
        int c = (unsigned char)g_back[0];
        g_back.erase(0, 1);
        return c;
    }
    if (g_ibPos < g_ibLen) return g_ib[g_ibPos++];
    pollfd p{0, POLLIN, 0};
    int r = poll(&p, 1, timeoutMs);
    if (r == 0) return -1;
    if (r < 0) return -2;
    ssize_t n = read(0, g_ib, sizeof g_ib);
    if (n == 0) exit(0);  // terminal was closed
    if (n < 0) return -2;
    g_ibLen = int(n);
    g_ibPos = 0;
    return g_ib[g_ibPos++];
}

// Is more input already waiting (key repeat, fast typing)?
bool inputPending() {
    if (!g_back.empty() || g_ibPos < g_ibLen) return true;
    pollfd p{0, POLLIN, 0};
    return poll(&p, 1, 0) > 0;
}

// Puts a byte back: readByte() returns it next
static void unreadByte(int c) { g_back.insert(g_back.begin(), char(c)); }

// The rest of an OSC sequence after "ESC ]", up to BEL or ESC \ (the terminator is not included).
// False when the sequence is incomplete.
static bool readOsc(std::string& s) {
    for (;;) {
        int c = readByte(seqDelay());
        if (c < 0) return false;
        if (c == 7) return true;
        if (c == 27) {
            int d = readByte(seqDelay());
            return d == '\\';
        }
        if (s.size() < 16 * 1024 * 1024) s += char(c);
    }
}

static std::string unbase64(const std::string& in) {
    std::string o;
    unsigned v = 0;
    int bits = 0;
    for (char ch : in) {
        int d = ch >= 'A' && ch <= 'Z' ? ch - 'A' : ch >= 'a' && ch <= 'z' ? ch - 'a' + 26
              : ch >= '0' && ch <= '9' ? ch - '0' + 52 : ch == '+' ? 62 : ch == '/' ? 63 : -1;
        if (d < 0) continue;
        v = (v << 6) | unsigned(d);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            o += char((v >> bits) & 255);
        }
    }
    return o;
}

// The answer to the OSC 52 query: 0 = not known yet, 1 = the terminal answers, -1 = it does not
static int g_osc52 = 0;

bool termCanReadClipboard() { return g_rawOn && g_osc52 >= 0; }

bool termReadClipboard(std::string& out) {
    if (!g_rawOn || g_osc52 < 0) return false;
    // The query, followed by a request for the device attributes (DA1), which every terminal
    // answers: when that answer comes first, the terminal probably cannot read the clipboard.
    // A terminal that asks the user for permission answers later, so it gets more time once
    // it has answered before.
    writeAll("\x1b]52;c;?\x07\x1b[c");
    std::string keep;  // other input that arrives meanwhile
    bool found = false;
    auto now = [] { return Clock::now(); };
    auto deadline = now() + std::chrono::milliseconds(g_osc52 > 0 ? 10000 : 1000);
    for (;;) {
        int left = int(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now()).count());
        if (left <= 0) break;
        int c = readByte(left);
        if (c == -2) continue;
        if (c < 0) break;
        if (c != 27) { keep += char(c); continue; }
        int d = readByte(seqDelay());
        if (d == ']') {
            std::string s;
            if (!readOsc(s)) continue;
            if (s.compare(0, 3, "52;") != 0) continue;
            out = unbase64(s.substr(s.rfind(';') + 1));
            found = true;
            break;
        }
        if (d == '[') {  // DA1 answer "ESC [ ? ... c" or another sequence (keep it)
            std::string s = "\x1b[";
            int e;
            while ((e = readByte(seqDelay())) >= 0) {
                s += char(e);
                if (e >= 0x40 && e <= 0x7e) break;
            }
            if (s.size() > 3 && s[2] == '?' && s.back() == 'c') {
                if (g_osc52 == 0) deadline = std::min(deadline, now() + std::chrono::milliseconds(150));
            } else {
                keep += s;
            }
            continue;
        }
        keep += char(27);
        if (d >= 0) keep += char(d);
    }
    g_osc52 = found ? 1 : g_osc52 > 0 ? 1 : -1;
    g_back = keep + g_back;
    return found;
}

static Key mk(int code, int mods = 0, char32_t ch = 0) {
    Key k;
    k.code = code;
    k.mods = mods;
    k.ch = ch;
    return k;
}

static Key keyFromByte(int c, int mods) {
    if (c == 13 || c == 10) return mk(K_ENTER, mods);
    if (c == 9) return mk(K_TAB, mods);
    if (c == 127 || c == 8) return mk(K_BKSP, mods);
    if (c == 0) return mk(K_CHAR, mods | M_CTRL, ' ');
    if (c < 32) return mk(K_CHAR, mods | M_CTRL, char32_t('a' + c - 1));
    char32_t ch = char32_t(c);
    int extra = 0;
    if (c >= 0xF0) { ch = c & 7; extra = 3; }
    else if (c >= 0xE0) { ch = c & 15; extra = 2; }
    else if (c >= 0xC0) { ch = c & 31; extra = 1; }
    else if (c >= 0x80) { ch = 0xFFFD; }
    for (int i = 0; i < extra; i++) {
        int b = readByte(seqDelay());
        if (b < 0 || (b & 0xC0) != 0x80) { ch = 0xFFFD; break; }
        ch = (ch << 6) | char32_t(b & 0x3F);
    }
    return mk(K_CHAR, mods, ch);
}

static Key readPaste() {
    std::string s;
    static const char endSeq[] = "\x1b[201~";
    for (;;) {
        int b = readByte(1000);
        if (b < 0) break;
        s += char(b);
        if (s.size() >= 6 && s.compare(s.size() - 6, 6, endSeq) == 0) {
            s.resize(s.size() - 6);
            break;
        }
    }
    u32 t = U(s), r;
    r.reserve(t.size());
    for (size_t i = 0; i < t.size(); i++) {
        if (t[i] == '\r') {
            r += '\n';
            if (i + 1 < t.size() && t[i + 1] == '\n') i++;
        } else {
            r += t[i];
        }
    }
    g_paste = r;
    return mk(K_PASTE);
}

static Key mouseKey(int b, int x, int y, bool release) {
    MouseEv m;
    m.x = x;
    m.y = y;
    m.mods = ((b & 4) ? M_SHIFT : 0) | ((b & 8) ? M_ALT : 0) | ((b & 16) ? M_CTRL : 0);
    if (b & 64) {
        int w = b & 3;
        if (w > 1) return mk(K_NONE);
        m.wheel = w == 0 ? -1 : 1;
    } else if (b & 32) {
        m.motion = true;
        m.btn = b & 3;
    } else if (release) {
        m.release = true;
        m.btn = b & 3;
    } else {
        m.press = true;
        m.btn = b & 3;
    }
    g_mouse = m;
    return mk(K_MOUSE);
}

// rxvt's arrow keys with modifiers end in a lower-case letter: a b c d = up down right left
static int rxvtArrow(int c) {
    static const int K[] = {K_UP, K_DOWN, K_RIGHT, K_LEFT};
    return K[c - 'a'];
}

// A code point from a key sequence that may become text (no control character, no surrogate)
static bool printable(int c) { return c >= 32 && c <= 0x10FFFF && !(c >= 0xD800 && c <= 0xDFFF) && !isControl(char32_t(c)); }

static Key parseCSI() {
    int c = readByte(seqDelay());
    if (c < 0) return mk(K_CHAR, M_ALT, '[');
    if (c == '<') {  // SGR mouse: ESC [ < b ; x ; y M/m
        std::string s;
        while ((c = readByte(seqDelay())) >= 0 && c != 'M' && c != 'm' && s.size() < 32) s += char(c);
        int b = 0, x = 0, y = 0;
        if (sscanf(s.c_str(), "%d;%d;%d", &b, &x, &y) != 3 || b < 0 || x < 1 || y < 1) return mk(K_NONE);
        return mouseKey(b, x - 1, y - 1, c == 'm');
    }
    if (c == 'M') {  // classic X10 mouse
        int b = readByte(seqDelay()) - 32, x = readByte(seqDelay()) - 33, y = readByte(seqDelay()) - 33;
        if (b < 0 || x < 0 || y < 0) return mk(K_NONE);  // incomplete sequence
        if ((b & 3) == 3 && !(b & 64)) return mouseKey(b & ~3, x, y, true);
        return mouseKey(b, x, y, false);
    }
    if (c == '[') {  // Linux console: F1..F5
        c = readByte(seqDelay());
        if (c >= 'A' && c <= 'E') return mk(K_F1 + (c - 'A'));
        return mk(K_NONE);
    }
    std::string p;
    while (c >= 0x20 && c < 0x40 && c != '$') {  // '$' ends rxvt's Shift+key sequences
        p += char(c);
        c = readByte(seqDelay());
    }
    if (c < 0) return mk(K_NONE);
    if (p.empty() && c >= 'a' && c <= 'd') return mk(rxvtArrow(c), M_SHIFT);  // rxvt: Shift+arrows
    // Parameters: fields separated by ';', each with ':' subfields
    std::vector<std::vector<int>> f(1, std::vector<int>(1, 0));
    for (char ch : p) {
        // (capped: a long run of digits must not overflow)
        if (ch >= '0' && ch <= '9') f.back().back() = std::min(f.back().back() * 10 + (ch - '0'), 9999999);
        else if (ch == ':') f.back().push_back(0);
        else if (ch == ';') f.push_back(std::vector<int>(1, 0));
    }
    auto F = [&](size_t i, size_t j) { return i < f.size() && j < f[i].size() ? f[i][j] : 0; };
    int a0 = F(0, 0);
    int mods = F(1, 0) > 1 ? ((F(1, 0) - 1) & 15) : 0;  // ignore Hyper, Meta, Caps Lock, Num Lock
    int ev = F(1, 1) ? F(1, 1) : 1;                     // 1 = pressed, 2 = repeated, 3 = released
    if (c == 'u') {  // kitty keyboard protocol
        if (a0 == 57443 || a0 == 57449) {  // left / right Alt key
            if (ev == 1) { g_altPending = true; return mk(K_NONE); }
            if (ev == 3 && g_altPending) { g_altPending = false; return mk(K_ALTMENU); }
            return mk(K_NONE);
        }
        if (ev == 3) return mk(K_NONE);
        u32 text;
        for (size_t j = 0; j < (f.size() > 2 ? f[2].size() : 0); j++)
            if (printable(f[2][j])) text += char32_t(f[2][j]);
        if (a0 >= 57344 && a0 <= 63743) {  // functional keys from the private use area (keypad, modifiers ...)
            switch (a0) {
                case 57414: return mk(K_ENTER, mods);
                case 57417: return mk(K_LEFT, mods);
                case 57418: return mk(K_RIGHT, mods);
                case 57419: return mk(K_UP, mods);
                case 57420: return mk(K_DOWN, mods);
                case 57421: return mk(K_PGUP, mods);
                case 57422: return mk(K_PGDN, mods);
                case 57423: return mk(K_HOME, mods);
                case 57424: return mk(K_END, mods);
                case 57425: return mk(K_INS, mods);
                case 57426: return mk(K_DEL, mods);
            }
            if (!text.empty()) return mk(K_CHAR, 0, text[0]);
            g_altPending = false;  // another modifier key was pressed
            return mk(K_NONE);
        }
        if (a0 == 13) return mk(K_ENTER, mods);
        if (a0 == 9) return mk(K_TAB, mods);
        if (a0 == 27) return mk(K_ESC, mods);
        if (a0 == 127 || a0 == 8) return mk(K_BKSP, mods);
        int shifted = printable(F(0, 1)) ? F(0, 1) : 0;
        if (!text.empty() && !(mods & (M_CTRL | M_CMD))) {
            // Some terminals also send the text "d" for Alt+D. Same letter as the key
            // = Alt combination; a different character (AltGr+Q = @) = typed character.
            bool altCombo = (mods & M_ALT) &&
                            (fold(text[0]) == fold(char32_t(a0)) || (shifted && text[0] == char32_t(shifted)));
            if (!altCombo) return mk(K_CHAR, 0, text[0]);
        }
        if (!(mods & (M_CTRL | M_ALT | M_CMD)) && (mods & M_SHIFT) && shifted) return mk(K_CHAR, mods, char32_t(shifted));
        if (printable(a0))
            return mk(K_CHAR, mods, (mods & (M_CTRL | M_ALT | M_CMD)) ? fold(char32_t(a0)) : char32_t(a0));
        return mk(K_NONE);
    }
    if (ev == 3) return mk(K_NONE);
    switch (c) {
        case 'A': return mk(K_UP, mods);
        case 'B': return mk(K_DOWN, mods);
        case 'C': return mk(K_RIGHT, mods);
        case 'D': return mk(K_LEFT, mods);
        case 'H': return mk(K_HOME, mods);
        case 'F': return mk(K_END, mods);
        case 'E': return mk(K_NONE);
        case 'Z': return mk(K_TAB, M_SHIFT);
        // rxvt: "~" with Shift, Ctrl or both as the final byte
        case '$': c = '~'; mods |= M_SHIFT; break;
        case '^': c = '~'; mods |= M_CTRL; break;
        case '@': c = '~'; mods |= M_CTRL | M_SHIFT; break;
    }
    if (c == '~' && a0 == 27 && F(2, 0)) {  // xterm modifyOtherKeys: CSI 27 ; mods ; key ~
        int key = F(2, 0);
        if (key == 13) return mk(K_ENTER, mods);
        if (key == 9) return mk(K_TAB, mods);
        if (key == 27) return mk(K_ESC, mods);
        if (key == 127 || key == 8) return mk(K_BKSP, mods);
        if (!printable(key)) return mk(K_NONE);
        if (!(mods & (M_CTRL | M_ALT | M_CMD))) return mk(K_CHAR, 0, char32_t(key));
        return mk(K_CHAR, mods & ~M_SHIFT, fold(char32_t(key)));
    }
    switch (c) {
        case 'P': return mk(K_F1, mods);
        case 'Q': return mk(K_F1 + 1, mods);
        case 'R': return mk(K_F1 + 2, mods);
        case 'S': return mk(K_F1 + 3, mods);
        case '~':
            switch (a0) {
                case 1: case 7: return mk(K_HOME, mods);
                case 2: return mk(K_INS, mods);
                case 3: return mk(K_DEL, mods);
                case 4: case 8: return mk(K_END, mods);
                case 5: return mk(K_PGUP, mods);
                case 6: return mk(K_PGDN, mods);
                case 11: case 12: case 13: case 14: case 15: return mk(K_F1 + a0 - 11, mods);
                case 17: case 18: case 19: case 20: case 21: return mk(K_F1 + 5 + a0 - 17, mods);
                case 23: case 24: return mk(K_F1 + 10 + a0 - 23, mods);
                case 200: return readPaste();
            }
            return mk(K_NONE);
    }
    return mk(K_NONE);
}

Key readKeyRaw() {
    for (;;) {
        if (g_cont) {  // continued after a stop: the terminal may have been reset meanwhile
            g_cont = 0;
            termReenter();
            g_winch = 1;
        }
        if (g_winch) {
            g_winch = 0;
            return mk(K_RESIZE);
        }
        int c = readByte(g_keyTimeout);
        if (c == -1 && g_keyTimeout >= 0) return mk(K_TIMEOUT);
        if (c < 0) continue;
        if (c == 27) {
            int c2 = readByte(escDelay());
            if (c2 < 0) return mk(K_ESC);
            if (c2 == '[') return parseCSI();
            if (c2 == 'O') {
                int c3 = readByte(escDelay());
                if (c3 < 0) return mk(K_CHAR, M_ALT, 'o');
                int mods = 0;
                if (c3 >= '2' && c3 <= '9') {  // "ESC O 5 A": with modifiers (some terminals)
                    mods = (c3 - '1') & 15;
                    c3 = readByte(seqDelay());
                }
                switch (c3) {
                    case 'A': return mk(K_UP, mods);
                    case 'B': return mk(K_DOWN, mods);
                    case 'C': return mk(K_RIGHT, mods);
                    case 'D': return mk(K_LEFT, mods);
                    case 'H': return mk(K_HOME, mods);
                    case 'F': return mk(K_END, mods);
                    case 'M': return mk(K_ENTER, mods);
                    case 'P': case 'Q': case 'R': case 'S': return mk(K_F1 + (c3 - 'P'), mods);
                    case 'a': case 'b': case 'c': case 'd': return mk(rxvtArrow(c3), M_CTRL);  // rxvt
                }
                return mk(K_NONE);
            }
            if (c2 == 27) {
                unreadByte(c2);
                return mk(K_ESC);
            }
            if (c2 == ']') {  // a late answer to termReadClipboard(): "ESC ] 52 ; ..."
                int a = readByte(seqDelay()), b = a == '5' ? readByte(seqDelay()) : -1;
                if (a == '5' && b == '2') {
                    std::string s;
                    readOsc(s);
                    g_osc52 = 1;
                    continue;
                }
                if (b >= 0) unreadByte(b);
                if (a >= 0) unreadByte(a);
            }
            // Terminal.app and iTerm2 send ESC b / ESC f for Option+Left / Option+Right (with the
            // kitty protocol Alt+B and Alt+F arrive as CSI u and still open the menus)
            if (g_macKeys && (c2 == 'b' || c2 == 'f')) return mk(c2 == 'b' ? K_LEFT : K_RIGHT, M_CTRL);
            Key k = keyFromByte(c2, M_ALT);
            if (k.code == K_CHAR && !(k.mods & M_CTRL)) k.ch = fold(k.ch);
            return k;
        }
        return keyFromByte(c, 0);
    }
}

Key macKey(Key k) {
    if (!g_macKeys) return k;
    int shift = k.mods & M_SHIFT;
    if ((k.code == K_LEFT || k.code == K_RIGHT) && (k.mods & M_CMD)) return mk(k.code == K_LEFT ? K_HOME : K_END, shift);
    if ((k.code == K_UP || k.code == K_DOWN) && (k.mods & M_CMD))
        return mk(k.code == K_UP ? K_HOME : K_END, shift | M_CTRL);
    if ((k.code == K_LEFT || k.code == K_RIGHT) && (k.mods & M_ALT)) return mk(k.code, shift | M_CTRL);
    return k;
}
