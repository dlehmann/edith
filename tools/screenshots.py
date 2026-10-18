#!/usr/bin/env python3
"""Takes the screenshots of docs/screenshots/: runs ./edith in a pseudo terminal, types the
keys of each scene and renders the screen as PNG in the CGA colors.

    python3 tools/screenshots.py [./edith]        (needs Pillow and the DejaVu Sans Mono font)

The scenes are defined in SCENES below. The terminal is emulated just far enough for Edith's
output: cursor positioning, clearing and the colors (Edith is run with true colors).
"""

import fcntl
import os
import pty
import re
import select
import struct
import sys
import termios
import time

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "docs", "screenshots")
SAMPLES = os.path.join(OUT, "samples")
W, H = 100, 30
FONT = "/usr/share/fonts/TTF/DejaVuSansMono.ttf"
FONT_BOLD = "/usr/share/fonts/TTF/DejaVuSansMono-Bold.ttf"
FONT_SIZE = 28  # rendered at twice the usual size, for sharp images on high-resolution screens

# The CGA palette (as Edith uses it for true color terminals)
CGA = [0x000000, 0xAA0000, 0x00AA00, 0xAA5500, 0x0000AA, 0xAA00AA, 0x00AAAA, 0xAAAAAA,
       0x555555, 0xFF5555, 0x55FF55, 0xFFFF55, 0x5555FF, 0xFF55FF, 0x55FFFF, 0xFFFFFF]


def rgb(v):
    return (v >> 16) & 255, (v >> 8) & 255, v & 255


class Terminal:
    """A character grid that follows Edith's escape sequences."""

    def __init__(self):
        self.pending = ""  # an escape sequence that is not complete yet
        self.cells = [[(" ", rgb(CGA[7]), rgb(CGA[0]), False)] * W for _ in range(H)]
        self.x = self.y = 0
        self.fg, self.bg, self.bold = rgb(CGA[7]), rgb(CGA[0]), False
        self.cursor = False

    def sgr(self, params):
        p = [int(v) if v else 0 for v in params.split(";")] if params else [0]
        i = 0
        while i < len(p):
            v = p[i]
            if v == 0:
                self.fg, self.bg, self.bold = rgb(CGA[7]), rgb(CGA[0]), False
            elif v == 1:
                self.bold = True
            elif 30 <= v <= 37:
                self.fg = rgb(CGA[v - 30 + (8 if self.bold else 0)])
            elif 90 <= v <= 97:
                self.fg = rgb(CGA[v - 90 + 8])
            elif 40 <= v <= 47:
                self.bg = rgb(CGA[v - 40])
            elif 100 <= v <= 107:
                self.bg = rgb(CGA[v - 100 + 8])
            elif v in (38, 48) and i + 4 < len(p) and p[i + 1] == 2:
                c = (p[i + 2], p[i + 3], p[i + 4])
                if v == 38:
                    self.fg = c
                else:
                    self.bg = c
                i += 4
            i += 1

    def feed(self, data):
        data = self.pending + data
        self.pending = ""
        i = 0
        while i < len(data):
            c = data[i]
            if c == "\x1b":
                if not re.match(r"\x1b(\[[0-9;?<>=]*[ -/]*[@-~]|\][^\x07]*\x07|[^\[\]])", data[i:]):
                    self.pending = data[i:]  # the rest arrives with the next read
                    return
                m = re.match(r"\x1b\[([0-9;?<>=]*)([ -/]*)([@-~])", data[i:])
                if m:
                    params, final = m.group(1), m.group(3)
                    if final == "H":
                        p = [int(v) if v else 1 for v in params.split(";")] if params else [1, 1]
                        self.y, self.x = p[0] - 1, (p[1] - 1 if len(p) > 1 else 0)
                    elif final == "J" and params == "2":
                        self.__init__()
                    elif final == "m" and not m.group(2):
                        self.sgr(params)
                    elif final in "hl" and params == "?25":
                        self.cursor = final == "h"
                    i += m.end()
                    continue
                m = re.match(r"\x1b\][^\x07]*\x07", data[i:])
                i += m.end() if m else 2
                continue
            if c == "\r":
                self.x = 0
            elif c == "\n":
                self.y += 1
            elif ord(c) >= 32 and 0 <= self.y < H and 0 <= self.x < W:
                self.cells[self.y][self.x] = (c, self.fg, self.bg, self.bold)
                self.x += 1
            i += 1


class Edith:
    def __init__(self, binary, args, cwd):
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            os.chdir(cwd)
            env = dict(os.environ, TERM="xterm-256color", COLORTERM="truecolor", LANG="en_US.UTF-8",
                       LC_ALL="", LANGUAGE="", XDG_CONFIG_HOME="/nonexistent")
            os.execve(binary, [binary] + args, env)
        fcntl.ioctl(self.fd, termios.TIOCSWINSZ, struct.pack("HHHH", H, W, 0, 0))
        self.term = Terminal()
        self.dec = __import__("codecs").getincrementaldecoder("utf-8")("replace")
        self.read(0.8)

    def read(self, timeout):
        end = time.time() + timeout
        while (left := end - time.time()) > 0:
            r, _, _ = select.select([self.fd], [], [], left)
            if not r:
                break
            try:
                data = os.read(self.fd, 65536)
            except OSError:
                break
            if not data:
                break
            self.term.feed(self.dec.decode(data))

    def keys(self, *seq):
        for k in seq:
            if isinstance(k, (int, float)):
                self.read(k)
            else:
                os.write(self.fd, k.encode())
                self.read(0.25)

    def close(self):
        try:
            os.kill(self.pid, 9)
            os.waitpid(self.pid, 0)
        except OSError:
            pass


def render(term, path):
    font = ImageFont.truetype(FONT, FONT_SIZE)
    bold = ImageFont.truetype(FONT_BOLD, FONT_SIZE)
    asc, desc = font.getmetrics()
    cw, ch = round(font.getlength("M")), asc + desc
    img = Image.new("RGB", (W * cw, H * ch))
    d = ImageDraw.Draw(img)
    for y, row in enumerate(term.cells):
        for x, (c, fg, bg, b) in enumerate(row):
            d.rectangle([x * cw, y * ch, (x + 1) * cw - 1, (y + 1) * ch - 1], fill=bg)
            if c != " ":
                d.text((x * cw, y * ch), c, font=bold if b else font, fill=fg)
    if term.cursor and 0 <= term.y < H and 0 <= term.x < W:  # the cursor as an underline
        d.rectangle([term.x * cw, (term.y + 1) * ch - 5, (term.x + 1) * cw - 1, (term.y + 1) * ch - 1],
                    fill=rgb(CGA[15]))
    img.save(path, optimize=True)


ESC, ENTER = "\x1b", "\r"
CTRL_O, CTRL_T, CTRL_PGUP, F6, ALT_W = "\x0f", "\x14", "\x1b[5;5~", "\x1b[17~", "\x1bw"
DOWN, SHIFT_DOWN, SHIFT_END = "\x1b[B", "\x1b[1;2B", "\x1b[1;2F"


def down(n):
    return DOWN * n


# name: (arguments, keys)
SCENES = {
    "main": (["-n", "inventory.py"], [down(21), "\x1b[C" * 8]),
    "menu": (["-n", "server.c"], [down(12), "\x1bo", 0.4]),
    # three windows in the first tab (split side by side, then the right one above each other), two more tabs
    "windows": (["-O", "server.c", "inventory.py"],
                [CTRL_T, CTRL_O, 0.3, "notes.md\r", 0.3, CTRL_T, CTRL_O, 0.3, "inventory.py\r", 0.3,
                 CTRL_PGUP, CTRL_PGUP, 0.3, F6, ALT_W, 0.3, "\x1bh", 0.3, CTRL_O, 0.3, "notes.md\r", 0.3,
                 down(4), F6, down(9), F6, down(17)]),
    "replace": (["-n", "server.c"], ["\x12", "client", "\t", "conn", 0.4]),
    "vim": (["--vim", "-n", "inventory.py"], [down(20), "V", "jjj", 0.4]),
    "markdown": (["notes.md"], [down(10)]),
}


def main():
    binary = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "edith"))
    names = sys.argv[2:] or list(SCENES)
    for name in names:
        args, keys = SCENES[name]
        e = Edith(binary, args, SAMPLES)
        try:
            e.keys(*keys)
            e.read(0.5)
            path = os.path.join(OUT, f"{name}.png")
            render(e.term, path)
            print(path)
        finally:
            e.close()


if __name__ == "__main__":
    main()
