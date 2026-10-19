// The Mac key bindings (Cmd shortcuts, Option and Cmd with the arrow keys, deleting words) and
// the system clipboard (clipboard.h).

#include <cstdio>
#include <fstream>
#include <sstream>

#include <unistd.h>

#include "apptest.h"
#include "clipboard.h"
#include "menus.h"
#include "process.h"
#include "terminal.h"
#include "test.h"
#include "widgets.h"

using namespace apptest;

namespace {
// Cmd+key in the kitty keyboard protocol (modifier 9 = 1 + Cmd, 10 = Shift, 11 = Alt)
std::string cmd(char c, int extra = 0) { return "\x1b[" + std::to_string(int(c)) + ";" + std::to_string(9 + extra) + "u"; }

// Turns the Mac key bindings on for one test
struct MacKeys {
    bool old = g_macKeys;
    MacKeys() { g_macKeys = true; }
    ~MacKeys() { g_macKeys = old; }
};

Key key(char32_t c, int mods) {
    Key k;
    k.code = K_CHAR;
    k.ch = c;
    k.mods = mods;
    return k;
}
}  // namespace

TEST(mac_cmd_key_decoded) {
    AppT t("");
    t.keys(cmd('s') + ESC);  // without a file name Save opens the Save As dialog; it must not type "s"
    CHECK_EQ(t.text(), "");
    sendKeys("\x1b[97;9u");  // Cmd+A
    Key k = getKey();
    CHECK_EQ(k.code, int(K_CHAR));
    CHECK_EQ(k.ch, char32_t('a'));
    CHECK_EQ(k.mods, int(M_CMD));
    sendKeys("\x1b[122;10u");  // Cmd+Shift+Z: the letter, not "Z" as typed text
    k = getKey();
    CHECK_EQ(k.ch, char32_t('z'));
    CHECK_EQ(k.mods, int(M_CMD | M_SHIFT));
    sendKeys("\x1b[1;9D");  // Cmd+Left without the Mac bindings: Left with Cmd
    k = getKey();
    CHECK_EQ(k.code, int(K_LEFT));
    CHECK_EQ(k.mods, int(M_CMD));
}

TEST(mac_cmd_shortcuts) {
    AppT t("one two");
    const App& a = t.a();
    CHECK_EQ(a.shortcut(key('s', M_CMD)), int(A_SAVE));
    CHECK_EQ(a.shortcut(key('z', M_CMD)), int(A_UNDO));
    CHECK_EQ(a.shortcut(key('z', M_CMD | M_SHIFT)), int(A_REDO));
    CHECK_EQ(a.shortcut(key('y', M_CMD)), int(A_REDO));
    CHECK_EQ(a.shortcut(key('f', M_CMD)), int(A_FIND));
    CHECK_EQ(a.shortcut(key('f', M_CMD | M_ALT)), int(A_REPLACE));
    CHECK_EQ(a.shortcut(key('g', M_CMD)), int(A_FINDNEXT));
    CHECK_EQ(a.shortcut(key('l', M_CMD)), int(A_GOTO));
    CHECK_EQ(a.shortcut(key('t', M_CMD)), int(A_NEWTAB));
    CHECK_EQ(a.shortcut(key('w', M_CMD)), int(A_CLOSEWIN));
    CHECK_EQ(a.shortcut(key(']', M_CMD | M_SHIFT)), int(A_NEXTTAB));
    CHECK_EQ(a.shortcut(key('}', M_CMD | M_SHIFT)), int(A_NEXTTAB));
    CHECK_EQ(a.shortcut(key('[', M_CMD | M_SHIFT)), int(A_PREVTAB));
    CHECK_EQ(a.shortcut(key('q', M_CMD)), int(A_EXIT));
    CHECK_EQ(a.shortcut(key('s', M_CMD | M_ALT)), int(A_NONE));
    CHECK_EQ(a.shortcut(key('g', M_CTRL)), int(A_GOTO));  // Ctrl stays as it was
    // through the decoder: select all, cut, undo, redo
    t.keys(cmd('a') + cmd('x'));
    CHECK_EQ(t.text(), "");
    CHECK_EQ(toUtf8(t.a().clip), "one two");
    t.keys(cmd('z'));
    CHECK_EQ(t.text(), "one two");
    t.keys(cmd('z', 1));  // Cmd+Shift+Z
    CHECK_EQ(t.text(), "");
    t.keys(cmd('v'));
    CHECK_EQ(t.text(), "one two");
}

TEST(mac_cmd_find_next_and_goto) {
    AppT t("foo\nbar\nfoo bar");
    t.keys(ctrl('f') + "bar\r");
    CHECK_EQ(t.ed().cy, 1);
    t.keys(cmd('g'));
    CHECK_EQ(t.ed().cy, 2);
    t.keys(cmd('l') + "1\r");
    CHECK_EQ(t.ed().cy, 0);
}

TEST(mac_cmd_in_vim_mode) {
    AppT t("abc");
    S.vim = true;
    t.a().vim.reset();
    t.a().clip = U"X";
    t.keys("i" + cmd('v'));  // the application pastes, nothing is typed by Vim
    CHECK_EQ(t.text(), "Xabc");
    t.keys(cmd('s', 2));  // Cmd+Alt+S: no shortcut, ignored
    CHECK_EQ(t.text(), "Xabc");
    S.vim = false;
}

TEST(mac_option_and_cmd_arrows) {
    MacKeys mac;
    AppT t("  one two\nthree");
    t.keys("\x1b[1;9C");  // Cmd+Right: end of the line
    CHECK_EQ(t.ed().cx, 9);
    t.keys("\x1b[1;3D");  // Option+Left: one word back
    CHECK_EQ(t.ed().cx, 6);
    t.keys(alt('b'));  // Terminal.app sends ESC b for Option+Left
    CHECK_EQ(t.ed().cx, 2);
    t.keys(alt('f'));  // and ESC f for Option+Right
    CHECK_EQ(t.ed().cx, 6);
    t.keys("\x1b[1;10D");  // Cmd+Shift+Left: selects to the start of the text (like Home)
    Pos a, b;
    CHECK(t.ed().sel(a, b));
    CHECK_EQ(a.x, 2);
    CHECK_EQ(b.x, 6);
    t.keys("\x1b[1;9B");  // Cmd+Down: end of the file
    CHECK_EQ(t.ed().cy, 1);
    CHECK_EQ(t.ed().cx, 5);
    CHECK(!t.ed().hasSel());
    t.keys("\x1b[1;9A");  // Cmd+Up: start of the file
    CHECK_EQ(t.ed().cy, 0);
    CHECK_EQ(t.ed().cx, 0);
    t.keys("\x1b[1;4C");  // Option+Shift+Right selects up to the next word like Ctrl+Shift+Right
    CHECK(t.ed().sel(a, b));
    CHECK_EQ(b.x, 2);
    // Alt+F with the kitty protocol still opens the File menu (Alt+N: New)
    t.keys("\x1b[102;3u\x1b[110;3u");
    CHECK_EQ(t.text(), "");
}

TEST(mac_keys_off_on_linux) {
    MacKeys mac;
    g_macKeys = false;
    AppT t("one two");
    t.keys("\x1b[F" + alt('b'));  // ESC b is Alt+B: no word movement
    CHECK_EQ(t.ed().cx, 7);
    t.keys(alt('f') + alt('n'));  // ESC f opens the File menu (New)
    CHECK_EQ(t.text(), "");
}

TEST(mac_delete_word_and_to_line_start) {
    AppT t("one two three");
    t.keys("\x1b[F\x1b\x7f");  // End, Option+Backspace (ESC DEL)
    CHECK_EQ(t.text(), "one two ");
    t.keys("\x1b[127;5u");  // Ctrl+Backspace
    CHECK_EQ(t.text(), "one ");
    t.keys("xy\x1b[127;9u");  // Cmd+Backspace: everything before the cursor
    CHECK_EQ(t.text(), "");
    t.keys(ctrl('z'));
    CHECK_EQ(t.text(), "one xy");
    t.ed().lines = {U"ab", U"cd"};
    t.ed().cy = 1;
    t.ed().cx = 0;
    t.keys("\x1b\x7f");  // at the start of a line: joins with the line above
    CHECK_EQ(t.text(), "abcd");
    t.ed().cy = 0;
    t.ed().cx = 0;
    t.keys("\x1b[127;9u");  // nothing before the cursor: nothing changes
    CHECK_EQ(t.text(), "abcd");
}

// ---- the system clipboard ----

namespace {
std::string tmpFile() {
    char p[] = "/tmp/edith-clip-XXXXXX";
    int fd = mkstemp(p);
    if (fd >= 0) close(fd);
    return p;
}
std::string readAll(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}
void putFile(const std::string& p, const std::string& s) { std::ofstream(p, std::ios::binary) << s; }

// Clipboard commands that use a file instead of the desktop's clipboard
struct FileClipboard {
    std::string path = tmpFile();
    FileClipboard() { clipboardSetCommands("cat > " + path, "cat " + path); }
    ~FileClipboard() {
        clipboardSetCommands("", "");
        remove(path.c_str());
    }
};
}  // namespace

TEST(clipboard_copy_and_paste_through_programs) {
    FileClipboard fc;
    AppT t("one two");
    t.keys(ctrl('a') + ctrl('c'));
    CHECK_EQ(readAll(fc.path), "one two");
    putFile(fc.path, "from outside");
    t.keys("\x1b[F" + ctrl('v'));
    CHECK_EQ(t.text(), "one twofrom outside");
    putFile(fc.path, "a\r\nb\rc\x01");  // line ends as in the editor, no control characters
    t.keys(ctrl('a') + ctrl('v'));
    CHECK_EQ(t.text(), "a\nb\nc");
    // Edit > Paste is enabled although Edith's own clipboard is empty
    AppT t2("");
    CHECK(t2.a().clip.empty());
    CHECK(t2.a().enabled(A_PASTE));
}

TEST(clipboard_paste_falls_back_to_own_clipboard) {
    clipboardSetCommands("cat >/dev/null", "exit 1");  // e.g. no display or nothing copied
    AppT t("abc");
    t.keys(ctrl('a') + ctrl('c') + "\x1b[F" + ctrl('v'));
    CHECK_EQ(t.text(), "abcabc");
    clipboardSetCommands("", "");
    AppT t2("");
    CHECK(!t2.a().enabled(A_PASTE));  // no program, no terminal, nothing copied
}

TEST(clipboard_vim_registers) {
    FileClipboard fc;
    AppT t("hello world");
    S.vim = true;
    t.a().vim.reset();
    t.keys("\"+yiw");
    CHECK_EQ(readAll(fc.path), "hello");
    putFile(fc.path, "X");
    t.keys("$\"*p");
    CHECK_EQ(t.text(), "hello worldX");
    S.vim = false;
}

TEST(clipboard_paste_in_input_fields) {
    FileClipboard fc;
    AppT t("");
    putFile(fc.path, "first line\nsecond");
    Input in(0, 0, 20, U"");
    in.key(key('v', M_CTRL));
    CHECK_EQ(toUtf8(in.text), "first line");  // one line only
    in.key(key('v', M_CMD));
    CHECK_EQ(toUtf8(in.text), "first linefirst line");
    in.key(key('v', M_ALT));  // not a paste key
    CHECK_EQ(toUtf8(in.text), "first linefirst line");
    // in the Find dialog
    t.ed().lines = {U"xx first line"};
    t.keys(ctrl('f') + ctrl('v') + "\r");
    Pos a, b;
    CHECK(t.ed().sel(a, b));
    CHECK_EQ(a.x, 3);
}

TEST(clipboard_detect_skips_ssh) {
    const char* old = getenv("SSH_TTY");
    std::string keep = old ? old : "";
    setenv("SSH_TTY", "/dev/pts/0", 1);
    clipboardSetCommands("", "");
    clipboardDetect();
    CHECK(!clipboardReadable());  // over SSH only the terminal (no terminal in the tests)
    if (old) setenv("SSH_TTY", keep.c_str(), 1);
    else unsetenv("SSH_TTY");
    clipboardSetCommands("", "");
}

TEST(process_input_written_after_stdout_closed) {
    std::string p = tmpFile(), out, err;
    int st;
    std::string big(1 << 20, 'y');
    // like "wl-copy >/dev/null": the program closes stdout before it reads its input
    CHECK(runCommand("exec >/dev/null 2>&1; sleep 0.2; cat > " + p, "", big, 20000, out, err, st));
    CHECK_EQ(st, 0);
    CHECK_EQ(readAll(p).size(), big.size());
    remove(p.c_str());
}
