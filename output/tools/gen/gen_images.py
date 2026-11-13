#!/usr/bin/env python3
"""Generate deterministic PPM/PGM test images (stdlib only, no third-party libs)."""
import os, struct, sys

OUT = sys.argv[1]
os.makedirs(OUT, exist_ok=True)

def lcg(seed):
    s = seed
    while True:
        s = (s * 1103515245 + 12345) & 0x7FFFFFFF
        yield s

def write_pnm(path, w, h, comps, rows):
    with open(path, "wb") as f:
        f.write(b"P6\n" if comps == 3 else b"P5\n")
        f.write(f"{w} {h}\n255\n".encode())
        for r in rows:
            f.write(bytes(r))

def rgb_image(w, h, seed, style):
    rnd = lcg(seed)
    rows = []
    for y in range(h):
        row = []
        for x in range(w):
            if style == "gradient":
                r, g, b = (x * 255 // w), (y * 255 // h), ((x + y) * 255 // (w + h))
            elif style == "noise":
                r, g, b = next(rnd) & 255, next(rnd) & 255, next(rnd) & 255
            else:  # blocks: sharp edges exercise long runs and large magnitudes
                r = 255 if (x // 16) % 2 else 0
                g = 255 if (y // 16) % 2 else 40
                b = 128 if ((x + y) // 32) % 2 else 20
            row += [r, g, b]
        rows.append(row)
    return rows

def gray_image(w, h, seed):
    rnd = lcg(seed)
    rows = []
    for y in range(h):
        row = []
        for x in range(w):
            row.append(((x * 7 + y * 13) ^ next(rnd)) & 255)
        rows.append(row)
    return rows

# RGB set: one image per subsampling/feature, varied dimensions (non-MCU-aligned too)
write_pnm(f"{OUT}/rgb444.ppm", 63, 47, 3, rgb_image(63, 47, 1, "gradient"))
write_pnm(f"{OUT}/rgb422.ppm", 80, 50, 3, rgb_image(80, 50, 2, "blocks"))
write_pnm(f"{OUT}/rgb420.ppm", 96, 80, 3, rgb_image(96, 80, 3, "noise"))
write_pnm(f"{OUT}/rgb_rst.ppm", 128, 96, 3, rgb_image(128, 96, 4, "noise"))
write_pnm(f"{OUT}/small.ppm", 8, 8, 3, rgb_image(8, 8, 5, "blocks"))
write_pnm(f"{OUT}/tall.ppm", 24, 130, 3, rgb_image(24, 130, 6, "gradient"))
write_pnm(f"{OUT}/gray.ppm", 70, 55, 1, gray_image(70, 55, 7))
print("wrote 7 PNM images to", OUT)
