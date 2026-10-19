#!/usr/bin/env python3
"""Makes the short demo video of the website (docs/screenshots/demo.mp4 and demo.webm): runs
./edith in a pseudo terminal like tools/screenshots.py, plays the scene in SCENE below, renders
every step as an image in the CGA colors and joins the images to a looping video with ffmpeg.

    python3 tools/demo.py [./edith]        (needs Pillow, the DejaVu Sans Mono font and ffmpeg)
"""

import os
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from screenshots import OUT, ROOT, SAMPLES, Edith, render  # noqa: E402

ESC, ENTER = "\x1b[27u", "\r"
F3 = "\x1bOR"
DOWN, RIGHT, END = "\x1b[B", "\x1b[C", "\x1b[F"
CTRL_F, CTRL_G, CTRL_HOME = "\x06", "\x07", "\x1b[1;5H"


def alt(c):
    return "\x1b" + c


# The scene: (keys, seconds the result stays on screen). "type" entries are typed one character
# at a time.
SCENE = [
    ("", 1.6),
    # through the menus
    (alt("f"), 0.9), (RIGHT, 0.9), (RIGHT, 0.9), (RIGHT, 0.7), (DOWN, 0.35), (DOWN, 0.35), (DOWN, 0.35),
    (DOWN, 0.35), (DOWN, 0.9), (ESC, 0.5),
    # go to a line and add code: the highlighting follows while typing
    (CTRL_G, 0.6), ("type", "35", 0.08), (ENTER, 0.5), (END, 0.5), (ENTER, 0.3),
    ("type", 'inv.add(Item("mouse", 19.90, 5, ["input"]))', 0.055), ("", 1.2),
    # find
    (CTRL_F, 0.6), ("type", "price", 0.1), (ENTER, 1.0), (F3, 0.8), (F3, 1.0),
    # undo through the Edit menu: back to the start, so that the loop does not jump
    (alt("e"), 1.2), (alt("u"), 0.9), (alt("e"), 0.8), (alt("u"), 0.8), (CTRL_HOME, 1.4),
]


def snapshot(term):
    return [row[:] for row in term.cells], term.x, term.y, term.cursor


class Snap:
    """A terminal state for render()"""

    def __init__(self, s):
        self.cells, self.x, self.y, self.cursor = s


def main():
    binary = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "edith"))
    if not shutil.which("ffmpeg"):
        sys.exit("demo: ffmpeg is needed")
    frames = []  # (state, seconds)
    e = Edith(binary, ["-n", "inventory.py"], SAMPLES)
    try:
        for step in SCENE:
            if step[0] == "type":
                for c in step[1]:
                    os.write(e.fd, c.encode())
                    e.read(0.12)
                    frames.append((snapshot(e.term), step[2]))
                continue
            keys, hold = step
            if keys:
                os.write(e.fd, keys.encode())
            e.read(0.4)
            frames.append((snapshot(e.term), hold))
    finally:
        e.close()

    with tempfile.TemporaryDirectory() as tmp:
        lines = []
        for i, (state, hold) in enumerate(frames):
            path = os.path.join(tmp, f"{i:04d}.png")
            render(Snap(state), path)
            lines += [f"file '{path}'", f"duration {hold:.3f}"]
        lines.append(f"file '{path}'")  # the concat demuxer needs the last file twice
        listing = os.path.join(tmp, "frames.txt")
        with open(listing, "w") as f:
            f.write("\n".join(lines) + "\n")
        common = ["ffmpeg", "-y", "-loglevel", "error", "-f", "concat", "-safe", "0", "-i", listing,
                  "-vf", "fps=25,format=yuv420p", "-an"]
        mp4 = os.path.join(OUT, "demo.mp4")
        webm = os.path.join(OUT, "demo.webm")
        subprocess.run(common + ["-c:v", "libx264", "-preset", "veryslow", "-tune", "animation", "-crf", "20",
                                 "-movflags", "+faststart", mp4], check=True)
        subprocess.run(common + ["-c:v", "libvpx-vp9", "-crf", "30", "-b:v", "0", "-row-mt", "1", webm],
                       check=True)
        print(mp4)
        print(webm)


if __name__ == "__main__":
    main()
