#!/usr/bin/env python3
"""End-to-end tests: runs the real ./edith binary in a pseudo terminal, sends key
sequences and checks the screen (through a tiny VT emulator) and the written files.

    python3 tests/pty_test.py ./edith
"""

import fcntl
import os
import pty
import re
import select
import struct
import subprocess
import sys
import tempfile
import termios
import time

W, H = 80, 25
failures = 0
checks = 0


class Screen:
    """Just enough of a VT100 to follow Edith's output: cursor positioning and text."""

    def __init__(self):
        self.rows = [[" "] * W for _ in range(H)]
        self.x = self.y = 0
        self.pending = ""  # an escape sequence that is not complete yet

    def feed(self, data):
        # an escape sequence can be split between two reads (macOS delivers small pieces)
        data = self.pending + data
        self.pending = ""
        i = 0
        while i < len(data):
            c = data[i]
            if c == "\x1b":
                if not re.match(r"\x1b(\[[0-9;?<>=]*[ -/]*[@-~]|\][^\x07]*\x07|[^\[\]])", data[i:]):
                    self.pending = data[i:]
                    return
                m = re.match(r"\x1b\[([0-9;?<>=]*)([ -/]*)([@-~])", data[i:])
                if m:
                    params, final = m.group(1), m.group(3)
                    if final == "H":
                        p = [int(v) if v else 1 for v in params.split(";")] if params else [1, 1]
                        self.y, self.x = p[0] - 1, (p[1] - 1 if len(p) > 1 else 0)
                    elif final == "J" and params == "2":
                        self.rows = [[" "] * W for _ in range(H)]
                    i += m.end()
                    continue
                m = re.match(r"\x1b\][^\x07]*\x07", data[i:])
                if m:
                    i += m.end()
                    continue
                i += 2
                continue
            if c == "\r":
                self.x = 0
            elif c == "\n":
                self.y += 1
            elif ord(c) >= 32:
                if 0 <= self.y < H and 0 <= self.x < W:
                    self.rows[self.y][self.x] = c
                self.x += 1
                if 0x1100 <= ord(c) and (0x2E80 <= ord(c) <= 0xA4CF or 0xAC00 <= ord(c) <= 0xD7A3 or ord(c) >= 0x1F300):
                    self.x += 1
            i += 1

    def text(self):
        return "\n".join("".join(r).rstrip() for r in self.rows)


class Edit:
    def __init__(self, binary, args, env_lang="C.UTF-8", env_extra=None):
        """env_extra: more environment variables; the value None removes one."""
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            env = dict(os.environ)
            env.update({"TERM": "xterm-256color", "LANG": env_lang, "LC_ALL": "", "LANGUAGE": ""})
            env.pop("COLORTERM", None)
            # no clipboard programs (they would change the clipboard of the desktop): Edith does
            # not use them in SSH sessions
            env["SSH_TTY"] = "/dev/pts/0"
            for k, v in (env_extra or {}).items():
                if v is None:
                    env.pop(k, None)
                else:
                    env[k] = v
            os.execve(binary, [binary] + args, env)
        fcntl.ioctl(self.fd, termios.TIOCSWINSZ, struct.pack("HHHH", H, W, 0, 0))
        self.screen = Screen()
        self.raw = ""  # everything the program wrote
        self.dec = __import__("codecs").getincrementaldecoder("utf-8")("replace")
        self.settle()

    def settle(self, timeout=5.0, quiet=0.4):
        """Reads until the program has drawn its first screen and then wrote nothing for `quiet`
        seconds (a fixed wait is too short on a slow or cold machine)."""
        end = time.time() + timeout
        while time.time() < end:
            before = len(self.raw)
            self.read(quiet)
            if self.raw and len(self.raw) == before:
                break

    def read(self, timeout=0.3):
        end = time.time() + timeout
        while True:
            left = end - time.time()
            if left <= 0:
                break
            r, _, _ = select.select([self.fd], [], [], left)
            if not r:
                break
            try:
                data = os.read(self.fd, 65536)
            except OSError:
                break
            if not data:
                break
            text = self.dec.decode(data)
            self.raw += text
            self.screen.feed(text)

    def send(self, keys, wait=0.3):
        os.write(self.fd, keys.encode())
        self.read(wait)

    def esc(self):
        self.send("\x1b", 0.3)  # a lone Escape: wait so it is not read as Alt+key

    def alive(self):
        pid, _ = os.waitpid(self.pid, os.WNOHANG)
        return pid == 0

    def wait_exit(self, timeout=3.0):
        end = time.time() + timeout
        while time.time() < end:
            self.read(0.1)
            pid, status = os.waitpid(self.pid, os.WNOHANG)
            if pid != 0:
                return True
        return False

    def kill(self):
        try:
            os.kill(self.pid, 9)
            os.waitpid(self.pid, 0)
        except OSError:
            pass


def check(name, cond, info=""):
    global failures, checks
    checks += 1
    if not cond:
        failures += 1
        print(f"  FAIL {name}" + (f"\n      {info}" if info else ""))


def test_plain_english(binary, tmp):
    f = os.path.join(tmp, "plain.txt")
    e = Edit(binary, ["--lang=en", f])
    try:
        s = e.screen.text()
        check("en: menu bar", "File" in s and "Options" in s, s[:200])
        check("en: status hint", "F1=Help" in s, s)
        e.send("hello world")
        check("en: typed text visible", "hello world" in e.screen.text())
        e.send("\x13")  # Ctrl+S
        check("en: saved", open(f).read() == "hello world")
        e.send("\x1bo", 0.4)  # Alt+O opens the Options menu
        check("en: options menu", "Vim Mode" in e.screen.text())
        e.esc()
        e.send("\x11")  # Ctrl+Q
        check("en: exit", e.wait_exit())
    finally:
        e.kill()


def test_plain_german(binary, tmp):
    f = os.path.join(tmp, "plain_de.txt")
    e = Edit(binary, [f], env_lang="de_DE.UTF-8")
    try:
        s = e.screen.text()
        check("de: menu bar from the locale", "Datei" in s and "Optionen" in s, s[:200])
        check("de: status hint", "F1=Hilfe" in s)
        e.send("\x1bo", 0.4)
        check("de: options menu", "Vim-Modus" in e.screen.text())
        e.esc()
        e.send("x\x11", 0.4)  # modified: Ctrl+Q asks
        check("de: save question", "Sollen die Änderungen gespeichert werden?" in e.screen.text())
        e.send("\x1b[C\r")  # "Nein"
        check("de: exit without saving", e.wait_exit())
        check("de: not saved", not os.path.exists(f))
    finally:
        e.kill()


def test_vim_english(binary, tmp):
    f = os.path.join(tmp, "vim.txt")
    e = Edit(binary, ["--vim", "--lang=en", f])
    try:
        e.send("i")
        check("vim en: insert mode shown", "-- INSERT --" in e.screen.text())
        e.send("first line\rsecond line")
        e.esc()
        check("vim en: insert mode left", "-- INSERT --" not in e.screen.text())
        e.send("ggdw")
        check("vim en: dw", "line" in e.screen.text() and "first line" not in e.screen.text())
        e.send("u")
        check("vim en: undo", "first line" in e.screen.text())
        e.send("Vj")
        check("vim en: visual line", "-- VISUAL LINE --" in e.screen.text())
        e.esc()
        e.send(":1s/first/1st/\r")
        check("vim en: substitute", "1st line" in e.screen.text())
        e.send(":wq\r")
        check("vim en: :wq exits", e.wait_exit())
        check("vim en: written", open(f).read() == "1st line\nsecond line")
    finally:
        e.kill()


def test_vim_german_and_menu_toggle(binary, tmp):
    e = Edit(binary, ["--lang=de"])
    try:
        e.send("\x1bo", 0.4)
        e.send("\x1bv", 0.4)  # Alt+V: Optionen > Vim-Modus
        e.send("i")
        check("vim de: insert mode shown", "-- EINFÜGEN --" in e.screen.text(), e.screen.text()[-200:])
        e.esc()
        e.send(":foo\r")
        check("vim de: error message", "E492: Kein Editor-Befehl: foo" in e.screen.text())
        e.send("qaihi\x1b", 0.4)
        e.send("q@a", 0.4)
        check("vim de: macro", "hhii" in e.screen.text())
        e.send(":q\r")
        check("vim de: E37 when modified", "E37" in e.screen.text() and e.alive())
        e.send(":q!\r")
        check("vim de: :q! exits", e.wait_exit())
    finally:
        e.kill()


def test_windows_and_tabs(binary, tmp):
    a, b = os.path.join(tmp, "win_a.txt"), os.path.join(tmp, "win_b.txt")
    open(a, "w").write("alpha text\n")
    open(b, "w").write("beta text\n")
    e = Edit(binary, ["--lang=en", "-o", a, b])
    try:
        s = e.screen.text()
        check("windows: both files shown", "alpha text" in s and "beta text" in s, s[:400])
        check("windows: tab lines", "win_a.txt" in s and "win_b.txt" in s)
        e.send("\x1b[17~")  # F6
        e.send("X")
        check("windows: F6 switches", "Xbeta text" in e.screen.text())
        e.send("\x14")  # Ctrl+T: new tab
        s = e.screen.text()
        check("tabs: tab line (file of the active window)", "1 win_b.txt *" in s and "2 Untitled" in s, s.splitlines()[1] if len(s.splitlines()) > 1 else s)
        e.send("\x1b[5;5~")  # Ctrl+PgUp
        check("tabs: back to the first tab", "alpha text" in e.screen.text())
        e.send("\x11", 0.4)  # Ctrl+Q: the modified buffer is asked for
        check("tabs: exit asks", "Do you want to save the changes?" in e.screen.text())
        e.send("\x1b[C\r")  # No
        check("tabs: exit", e.wait_exit())
        check("windows: not saved", open(b).read() == "beta text\n")
    finally:
        e.kill()
    e = Edit(binary, ["--vim", "--lang=de", a])
    try:
        e.send("\x17v", 0.4)  # Ctrl-W v
        e.send(":tabnew\r", 0.4)
        s = e.screen.text()
        check("vim tabs: tab line", "1 win_a.txt" in s and "2 Unbenannt" in s, s.splitlines()[1] if len(s.splitlines()) > 1 else s)
        e.send("gt")
        s = e.screen.text()
        check("vim windows: side by side", s.count("alpha text") == 2, s[:600])
        e.send(":qa\r")
        check("vim: :qa exits", e.wait_exit())
    finally:
        e.kill()
    e = Edit(binary, ["--lang=en", a])
    try:
        e.send("\x1bw", 0.3)
        e.send("\x1bv", 0.4)  # Window > Split Vertically: a new window with an empty tab
        e.send("new")
        s = e.screen.text()
        check("split: new empty tab", "Untitled *" in s and s.count("alpha text") == 1 and "new" in s, s[:600])
        e.send("\x17", 0.4)  # Ctrl+W closes the window and asks for its modified tab
        check("split: close asks", "Do you want to save the changes?" in e.screen.text())
        e.send("\x1b[C\r")  # No
        s = e.screen.text()
        check("split: window closed", "Untitled" not in s and "alpha text" in s, s[:600])
        e.send("\x11")
        check("split: exit", e.wait_exit())
    finally:
        e.kill()


def test_reformat(binary, tmp):
    f = os.path.join(tmp, "fmt.c")
    open(f, "w").write("int main(){\nreturn 0;\n}\n")
    e = Edit(binary, ["--lang=en", f])
    try:
        e.send("\x1b[19~", 1.5)  # F8: Reformat Code
        s = e.screen.text()
        check("reformat: message", "Formatted with" in s or "Re-indented" in s, s.splitlines()[-1])
        e.send("\x13")  # Ctrl+S
        lines = open(f).read().split("\n")
        # clang-format joins the short function (LLVM style), the fallback re-indents it
        formatted = lines[0] == "int main() { return 0; }"
        reindented = len(lines) > 1 and lines[1].strip() == "return 0;" and lines[1][:1] in "\t "
        check("reformat: formatted", formatted or reindented, repr(lines[:3]))
        e.send("\x11")
        check("reformat: exit", e.wait_exit())
    finally:
        e.kill()


LANGS = {  # code: (menu bar words, locale)
    "en": (["File", "Edit", "Search", "Options", "Window", "Help"], "en_US.UTF-8"),
    "de": (["Datei", "Bearbeiten", "Suchen", "Optionen", "Fenster", "Hilfe"], "de_DE.UTF-8"),
    "fr": (["Fichier", "Édition", "Rechercher", "Options", "Fenêtre", "Aide"], "fr_FR.UTF-8"),
    "es": (["Archivo", "Editar", "Buscar", "Opciones", "Ventana", "Ayuda"], "es_ES.UTF-8"),
    "it": (["File", "Modifica", "Cerca", "Opzioni", "Finestra", "Aiuto"], "it_IT.UTF-8"),
    "hu": (["Fájl", "Szerkesztés", "Keresés", "Beállítások", "Ablak", "Súgó"], "hu_HU.UTF-8"),
    "pl": (["Plik", "Edycja", "Szukaj", "Opcje", "Okno", "Pomoc"], "pl_PL.UTF-8"),
    "tlh": (["ghItlh", "choH", "nej", "wIv", "Qorwagh", "QaH"], "tlh"),
}


def test_terminals(binary, tmp):
    """Edith does not use terminfo: it must work the same with every common TERM value."""
    for term in ("xterm", "xterm-256color", "screen-256color", "tmux-256color", "linux", "rxvt-unicode-256color",
                 "vt100", "alacritty", "xterm-kitty", None):
        f = os.path.join(tmp, f"term_{term}.txt")
        e = Edit(binary, ["--lang=en", f], env_extra={"TERM": term})
        try:
            check(f"term {term}: menu bar", "File" in e.screen.text(), e.screen.text()[:200])
            e.send("ab\x1b[Dc")  # Left between the characters
            e.send("\x13")
            check(f"term {term}: edit and save", open(f).read() == "acb")
            e.send("\x11")
            check(f"term {term}: exit", e.wait_exit())
            check(f"term {term}: screen restored", e.raw.endswith("\x1b[?1049l"), repr(e.raw[-60:]))
            if term == "linux":
                check("term linux: bright colors as bold", "\x1b[0;1;3" in e.raw and
                      not re.search(r"\x1b\[0;9\d;", e.raw))
        finally:
            e.kill()
    r = subprocess.run([binary], capture_output=True, text=True, env=dict(os.environ, TERM="dumb", LANG="C"))
    check("term dumb: refused", r.returncode == 1 and "TERM=dumb" in r.stderr, r.stderr)


def test_escdelay(binary, tmp):
    """ESCDELAY: an escape sequence that arrives in two parts (slow connection)."""
    f = os.path.join(tmp, "escdelay.txt")
    e = Edit(binary, ["--lang=en", f], env_extra={"ESCDELAY": "600"})
    try:
        e.send("ab")
        e.send("\x1b", 0.15)
        e.send("[D")  # the rest of the Left key
        e.send("X\x13")
        check("ESCDELAY: split sequence", open(f).read() == "aXb", open(f).read())
    finally:
        e.kill()


def test_job_control(binary, tmp):
    """Stopped and continued by the shell: the terminal is given back and set up again."""
    import signal
    f = os.path.join(tmp, "jobs.txt")
    e = Edit(binary, ["--lang=en", f])
    try:
        e.send("one")
        e.raw = ""
        os.kill(e.pid, signal.SIGSTOP)
        time.sleep(0.2)
        os.kill(e.pid, signal.SIGCONT)
        e.read(0.5)
        check("SIGCONT: terminal set up again", "\x1b[?1049h" in e.raw, repr(e.raw[:80]))
        check("SIGCONT: redrawn", "one" in e.screen.text() and "File" in e.screen.text())
        e.raw = ""
        os.kill(e.pid, signal.SIGTSTP)  # does not stop here (orphaned process group) but must recover
        e.read(0.5)
        check("SIGTSTP: terminal given back", "\x1b[?1049l" in e.raw, repr(e.raw[:80]))
        check("SIGTSTP: set up again", e.raw.rfind("\x1b[?1049h") > e.raw.find("\x1b[?1049l"))
        e.send(" two\x13")
        check("job control: still editing", open(f).read() == "one two")
    finally:
        e.kill()


def test_languages(binary):
    for code, (words, locale) in LANGS.items():
        for args, env_lang in ((["--lang=" + code], "C.UTF-8"), ([], locale)):
            e = Edit(binary, args, env_lang=env_lang)
            try:
                top = e.screen.text().splitlines()[0]
                how = "--lang" if args else "LANG=" + locale
                check(f"{code} ({how}): menu bar", all(w in top for w in words), top)
                e.send("\x06", 0.4)  # Ctrl+F: the find dialog
                e.esc()
                e.send("\x11")
                check(f"{code} ({how}): exit", e.wait_exit())
            finally:
                e.kill()


def test_cli(binary):
    env = {k: v for k, v in os.environ.items() if k not in ("LANG", "LC_ALL", "LC_MESSAGES", "LANGUAGE")}
    env["LANG"] = "C.UTF-8"
    out = subprocess.run([binary, "--help", "--lang=en"], capture_output=True, text=True, env=env).stdout
    check("cli: help en", "--vim" in out and "--hex" in out and "Usage:" in out)
    out = subprocess.run([binary, "--lang=de", "--help"], capture_output=True, text=True).stdout
    check("cli: help de", "--vim" in out and "Aufruf:" in out)
    r = subprocess.run([binary, "--lang=xx"], capture_output=True, text=True, env=env)
    check("cli: unknown language", r.returncode == 2 and "unknown language" in r.stderr, r.stderr)
    r = subprocess.run([binary, "--lang=xx"], capture_output=True, text=True, env=dict(env, LANG="de_DE.UTF-8"))
    check("cli: unknown language (German)", r.returncode == 2 and "unbekannte Sprache" in r.stderr, r.stderr)
    check("cli: help lists the languages", "en de fr es it hu pl tlh" in out, out)
    r = subprocess.run([binary], capture_output=True, text=True, stdin=subprocess.DEVNULL)
    check("cli: needs a terminal", r.returncode == 1)
    r = subprocess.run([binary, "--version"], capture_output=True, text=True)
    check("cli: --version", r.returncode == 0 and re.fullmatch(r"edith \d+\.\d+\.\d+\n", r.stdout), r.stdout)
    r = subprocess.run([binary, "-x"], capture_output=True, text=True, env=env)
    check("cli: unknown option", r.returncode == 2 and "unknown option \"-x\"" in r.stderr, r.stderr)
    # after "--" everything is a file name (so this only fails for lack of a terminal)
    r = subprocess.run([binary, "--", "-x", "--version"], capture_output=True, text=True, env=env,
                       stdin=subprocess.DEVNULL)
    check("cli: -- ends the options", r.returncode == 1 and r.stdout == "", r.stdout + r.stderr)
    out = subprocess.run([binary, "--ts-check"], capture_output=True, text=True).stdout
    bad = [l for l in out.splitlines() if "MISSING" in l or re.search(r"\b[1-9]\d* dropped", l)]
    check("cli: --ts-check without dropped patterns", out.strip() != "" and not bad, "\n".join(bad))


def test_clipboard_osc52(binary, tmp):
    """Paste asks the terminal for the clipboard (OSC 52) when there is no clipboard program."""
    import base64
    f = os.path.join(tmp, "clip.txt")
    e = Edit(binary, ["--lang=en", f])
    try:
        e.send("x\x16", 0.3)  # Ctrl+V
        check("osc52: query sent", "\x1b]52;c;?\x07" in e.raw, repr(e.raw[-200:]))
        # keys typed before the answer come after the pasted text
        answer = "\x1b]52;c;" + base64.b64encode("from terminal\r\nline 2".encode()).decode() + "\x07"
        e.send("ab" + answer + "\x1b[?62;22c", 0.5)
        check("osc52: pasted", "xfrom terminal" in e.screen.text() and "line 2ab" in e.screen.text(),
              e.screen.text()[:300])
        # a late answer after Edith gave up is not typed into the text
        e.send(answer, 0.3)
        check("osc52: late answer ignored", "52;c" not in e.screen.text())
    finally:
        e.kill()
    e = Edit(binary, ["--lang=en", f])
    try:
        t0 = time.time()
        e.send("\x16", 0.1)
        e.send("\x1b[?62;22c", 0.6)  # only the device attributes: no OSC 52
        check("osc52: no answer ends the wait early", time.time() - t0 < 1.0)
        e.send("\x16", 0.3)
        check("osc52: not asked again", e.raw.count("\x1b]52;c;?") == 1)
        e.send("ok")
        check("osc52: typing works afterwards", "ok" in e.screen.text())
    finally:
        e.kill()


def test_binary_file(binary, tmp):
    """A binary file is shown without sending control characters to the terminal: the C1
    controls U+0080..U+009F (U+009B is CSI, U+009D is OSC) would garble the screen."""
    f = os.path.join(tmp, "data.bin")
    with open(f, "wb") as out:
        out.write((b"\x00\x01\x1b" + bytes(range(0x7F, 0xA1)) + b"\xff\n") * 2000)
    e = Edit(binary, ["--lang=en", f])
    try:
        e.send("\x1b[6~\x1b[6~")  # PgDn
        bad = sorted({hex(ord(c)) for c in e.raw if 0x80 <= ord(c) < 0xA0 or (ord(c) < 32 and c not in "\x1b\r\n\x07")})
        check("binary: no control characters written", not bad, bad)
        check("binary: screen intact", "File" in e.screen.text() and "data.bin" in e.screen.text(), e.screen.text())
        check("binary: opened in hex mode", "00 01 1B 7F 80" in e.screen.text() and "│ Hex │" in e.screen.text(),
              e.screen.text())
    finally:
        e.kill()


def test_hex_option(binary, tmp):
    """edith --hex shows a text file as bytes; Options > Hex Mode switches back to the text."""
    f = os.path.join(tmp, "hex.txt")
    with open(f, "w") as out:
        out.write("Hallo\n")
    e = Edit(binary, ["--hex", "--lang=de", f])
    try:
        check("hex: bytes shown", "00000000  48 61 6C 6C 6F 0A" in e.screen.text(), e.screen.text())
        check("hex: status line", "│ Hex │ ÜB" in e.screen.text(), e.screen.text())
        e.send("4a")
        e.send("\x13")  # Ctrl+S
        with open(f, "rb") as inp:
            check("hex: saved", inp.read() == b"Jallo\n")
        e.send("\x1bo")
        e.send("\x1bh")  # Optionen > Hex-Modus
        check("hex: back to the text", "Jallo" in e.screen.text() and "│ Hex │" not in e.screen.text(),
              e.screen.text())
    finally:
        e.kill()


def test_third_party_licenses():
    """THIRD_PARTY_LICENSES (made for the binary packages) contains the license text of every
    component in third_party/, and every packaging script ships it."""
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    tp = os.path.join(root, "third_party")
    r = subprocess.run([os.path.join(root, "tools/third-party-licenses.sh"), tp], capture_output=True, text=True)
    check("licenses: script succeeds", r.returncode == 0 and r.stderr == "", r.stderr)
    out = r.stdout
    grammars = sorted(os.listdir(os.path.join(tp, "grammars")))
    comps = [("tree-sitter", os.path.join(tp, "tree-sitter")), ("nvim-treesitter queries", os.path.join(tp, "queries"))]
    comps += [("tree-sitter grammar: " + g, os.path.join(tp, "grammars", g)) for g in grammars]
    missing = [n for n, d in comps
               if not re.search(r"^" + re.escape(n), out, re.M) or open(os.path.join(d, "LICENSE")).read() not in out]
    check("licenses: every component and its license text", len(grammars) > 50 and not missing, missing)
    check("licenses: glibc of the static binary", "GNU C Library" in out and "GNU Lesser" in out)
    for f in ("packaging/build-release.sh", "packaging/debian/build-deb.sh", "packaging/appimage/build-appimage.sh",
              "packaging/arch/PKGBUILD"):
        with open(os.path.join(root, f)) as fh:
            check(f"licenses: shipped by {f}", re.search(r"third-party-licenses\.sh\"? > \S*THIRD_PARTY_LICENSES", fh.read()))


def main():
    binary = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "./edith")
    with tempfile.TemporaryDirectory() as tmp:
        for t in (test_plain_english, test_plain_german, test_vim_english, test_vim_german_and_menu_toggle,
                  test_windows_and_tabs, test_reformat, test_terminals, test_escdelay, test_job_control,
                  test_binary_file, test_hex_option, test_clipboard_osc52):
            t(binary, tmp)
        test_languages(binary)
    test_cli(binary)
    test_third_party_licenses()
    print(f"pty tests: {checks} checks, {failures} failed")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
