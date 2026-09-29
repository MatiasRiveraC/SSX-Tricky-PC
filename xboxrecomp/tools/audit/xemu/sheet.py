#!/usr/bin/env python3
"""sheet.py OUT.png [N] [COLS] -- contact sheet of the last N xemu screenshots."""
import glob, os, sys
from PIL import Image, ImageDraw
HERE = os.path.dirname(os.path.abspath(__file__))
out = sys.argv[1]
n = int(sys.argv[2]) if len(sys.argv) > 2 else 12
cols = int(sys.argv[3]) if len(sys.argv) > 3 else 4
fs = sorted(glob.glob(os.path.join(HERE, "shots", "*.png")))[-n:]
W, H = 400, 300
rows = (len(fs) + cols - 1) // cols
g = Image.new("RGB", (W * cols, H * rows))
d = ImageDraw.Draw(g)
for i, f in enumerate(fs):
    im = Image.open(f).convert("RGB")
    g.paste(im.resize((W, H)), ((i % cols) * W, (i // cols) * H))
    d.text(((i % cols) * W + 4, (i // cols) * H + 4), "%d %s %dx%d" % (i, os.path.basename(f)[16:24], im.width, im.height), fill=(255, 0, 255))
g.save(out)
print(out, len(fs))
