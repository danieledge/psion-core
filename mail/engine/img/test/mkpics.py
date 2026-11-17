#!/usr/bin/env python3
"""mkpics.py DIR - test pictures for pmimgtest (Pillow): a colour photo-like
picture as baseline JPEG, progressive JPEG, PNG (and interlaced, and with
transparency) and GIF, and its mean colour in mean.txt."""
import os
import sys
from PIL import Image

out = sys.argv[1]
os.makedirs(out, exist_ok=True)
w, h = 600, 400
im = Image.new("RGB", (w, h))
px = im.load()
for y in range(h):
    for x in range(w):
        px[x, y] = (40 + 200 * x // w, 60 + 150 * y // h, 200 - 160 * x // w)
for y in range(150, 250):
    for x in range(250, 350):
        px[x, y] = (230, 30, 30)
im.save(os.path.join(out, "c.jpg"), quality=90)
im.save(os.path.join(out, "cp.jpg"), quality=90, progressive=True)
im.save(os.path.join(out, "c.png"))
im.save(os.path.join(out, "ci.png"), interlace=1)
a = im.convert("RGBA")
a.putalpha(128)
a.save(os.path.join(out, "ca.png"))
im.quantize(256).save(os.path.join(out, "c.gif"))
m = [sum(c[i] * n for n, c in im.getcolors(w * h)) // (w * h) for i in range(3)]
with open(os.path.join(out, "mean.txt"), "w") as f:
    f.write("%d %d %d\n" % tuple(m))
