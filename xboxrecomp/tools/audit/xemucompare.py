#!/usr/bin/env python3
"""xemucompare.py OUT.png DIR... -- our closest frame beside each xemu reference.

For every reference screenshot taken on xemu (reference/xemu_frames/NN_*.png,
see xemu/launch.py), finds the frame in DIR... (walkcap.py dumps) that looks
most like it -- mean absolute difference of 160x120 grayscale thumbnails -- and
lays the two side by side, xemu on the left, with the score. A high score means
our walk never reached that screen (timing), not necessarily a rendering bug;
look at the pair.

    walkcap.py cap 90 --direct
    REFS=21-30 xemucompare.py race_pairs.png cap
"""
import glob
import os
import sys

import numpy as np
from PIL import Image, ImageDraw

import ssxpaths

REFS_DIR = os.path.join(ssxpaths.ROOT, "reference", "xemu_frames")
if not os.path.isdir(REFS_DIR):
    REFS_DIR = os.path.join(ssxpaths.ROOT, "docs", "reference", "xemu_frames")


def thumb(path):
    return np.asarray(Image.open(path).convert("L").resize((160, 120))).astype(float)


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    out, dirs = sys.argv[1], sys.argv[2:]
    refs = sorted(glob.glob(os.path.join(REFS_DIR, "*.png")))
    sel = os.environ.get("REFS")
    if sel:
        lo, hi = map(int, sel.split("-"))
        refs = [r for r in refs if lo <= int(os.path.basename(r)[:2]) <= hi]
    ours = [(f, thumb(f)) for d in dirs for f in sorted(glob.glob(os.path.join(d, "*.bmp")))]
    if not refs or not ours:
        sys.exit("no reference frames in %s or no frames in %s" % (REFS_DIR, dirs))
    w, h = 480, 360
    im = Image.new("RGB", (2 * w, h * len(refs)))
    draw = ImageDraw.Draw(im)
    for i, r in enumerate(refs):
        rt = thumb(r)
        best, bt = min(ours, key=lambda o: np.abs(o[1] - rt).mean())
        im.paste(Image.open(r).convert("RGB").resize((w, h)), (0, i * h))
        im.paste(Image.open(best).convert("RGB").resize((w, h)), (w, i * h))
        draw.text((4, i * h + 4), os.path.basename(r), fill=(255, 0, 255))
        draw.text((w + 4, i * h + 4), "%s  %.1f" % (os.path.basename(best), np.abs(bt - rt).mean()),
                  fill=(255, 0, 255))
    im.save(out)
    print(out)


if __name__ == "__main__":
    main()
