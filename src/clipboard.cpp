// The system clipboard (see clipboard.h).

#include "clipboard.h"

#include <cstdlib>

#include <unistd.h>

#include "paths.h"
#include "process.h"
#include "terminal.h"
#include "utf8.h"

static std::string g_copyCmd, g_pasteCmd;

void clipboardSetCommands(const std::string& copy, const std::string& paste) {
    g_copyCmd = copy;
    g_pasteCmd = paste;
}

// Is the program in $PATH?
static bool inPath(const std::string& prog) {
    const char* p = getenv("PATH");
    std::string path = p ? p : "/usr/bin:/bin";
    size_t s = 0;
    for (;;) {
        size_t e = path.find(':', s);
        std::string dir = path.substr(s, e == std::string::npos ? std::string::npos : e - s);
        if (dir.empty()) dir = ".";
        if (access((dir + "/" + prog).c_str(), X_OK) == 0) return true;
        if (e == std::string::npos) return false;
        s = e + 1;
    }
}

static bool envSet(const char* name) {
    const char* v = getenv(name);
    return v && *v;
}

void clipboardDetect() {
    if (envSet("SSH_CONNECTION") || envSet("SSH_CLIENT") || envSet("SSH_TTY")) return;
    // The programs that stay in the background to serve the clipboard (wl-copy, xclip, xsel)
    // must not keep the pipes of runCommand() open.
#ifdef __APPLE__
    // pbcopy and pbpaste are part of macOS; they convert by the locale
    clipboardSetCommands("LC_ALL=en_US.UTF-8 pbcopy", "LC_ALL=en_US.UTF-8 pbpaste");
#else
    if (envSet("WAYLAND_DISPLAY") && inPath("wl-copy") && inPath("wl-paste"))
        clipboardSetCommands("wl-copy >/dev/null 2>&1", "wl-paste --no-newline --type text 2>/dev/null");
    else if (envSet("DISPLAY") && inPath("xclip"))
        clipboardSetCommands("xclip -selection clipboard -in >/dev/null 2>&1", "xclip -selection clipboard -out");
    else if (envSet("DISPLAY") && inPath("xsel"))
        clipboardSetCommands("xsel --clipboard --input >/dev/null 2>&1", "xsel --clipboard --output");
#endif
}

bool clipboardReadable() { return !g_pasteCmd.empty() || termCanReadClipboard(); }

void clipboardCopy(const u32& t) {
    std::string s = toUtf8(t);
    if (!g_copyCmd.empty()) {
        std::string out, err;
        int status;
        runCommand(g_copyCmd, "", s, 3000, out, err, status);
    }
    if (s.size() <= 100000) writeAll("\x1b]52;c;" + base64(s) + "\x07");
}

// Line ends as in the editor: CRLF and CR become LF
static u32 normalize(const std::string& s) {
    u32 t = U(s), r;
    r.reserve(t.size());
    for (size_t i = 0; i < t.size(); i++) {
        if (t[i] == '\r') {
            r += '\n';
            if (i + 1 < t.size() && t[i + 1] == '\n') i++;
        } else if (t[i] == '\n' || t[i] == '\t' || !isControl(t[i])) {
            r += t[i];
        }
    }
    return r;
}

bool clipboardPaste(u32& t) {
    std::string s;
    if (!g_pasteCmd.empty()) {
        std::string err;
        int status;
        // a failure (no display, an empty clipboard or one without text) leaves the own clipboard
        if (!runCommand(g_pasteCmd, "", "", 3000, s, err, status) || status != 0) return false;
    } else if (!termReadClipboard(s)) {
        return false;
    }
    t = normalize(s);
    return true;
}
