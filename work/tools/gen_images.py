#!/usr/bin/env python3
# gen_images.py - generate deterministic test PNM images (no external deps).
import sys, os, random

def gen_color(w, h, path):
    rnd = random.Random(1234)
    rows = []
    for y in range(h):
        row = bytearray()
        for x in range(w):
            r = (x * 255 // max(w - 1, 1) + y) & 0xFF
            g = (y * 255 // max(h - 1, 1) + x) & 0xFF
            b = ((x ^ y) * 3) & 0xFF
            # add deterministic speckle so AC coefficients are nontrivial
            n = rnd.randint(-16, 16)
            row += bytes(((r + n) & 0xFF, (g + n) & 0xFF, (b + n) & 0xFF))
        rows.append(bytes(row))
    with open(path, "wb") as f:
        f.write(b"P6\n%d %d\n255\n" % (w, h))
        for row in rows:
            f.write(row)

def gen_gray(w, h, path):
    rnd = random.Random(5678)
    rows = []
    for y in range(h):
        row = bytearray()
        for x in range(w):
            v = ((x * x + y * 7) // 4 + rnd.randint(0, 32)) & 0xFF
            row.append(v)
        rows.append(bytes(row))
    with open(path, "wb") as f:
        f.write(b"P5\n%d %d\n255\n" % (w, h))
        for row in rows:
            f.write(row)

if __name__ == "__main__":
    outdir = sys.argv[1]
    os.makedirs(outdir, exist_ok=True)
    gen_color(64, 64, os.path.join(outdir, "color_64x64.ppm"))
    gen_color(100, 75, os.path.join(outdir, "color_100x75.ppm"))   # odd MCU edges
    gen_color(37, 19, os.path.join(outdir, "color_37x19.ppm"))
    gen_gray(64, 64, os.path.join(outdir, "gray_64x64.pgm"))
    gen_gray(91, 53, os.path.join(outdir, "gray_91x53.pgm"))
    print("generated")
