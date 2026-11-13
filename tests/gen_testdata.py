#!/usr/bin/env python3
"""Generate 24-bit uncompressed TGA test images (stdlib only).

Images exercise the 4:1:1 horizontal chroma path:
  gradient.tga : smooth full-frame RGB gradient (interpolation accuracy)
  colorbars.tga: sharp vertical color stripes (chroma placement/alignment)
  oddsize.tga  : dimensions that are not multiples of 32 (MCU edge padding)
"""
import os
import struct
import sys


def write_tga(path, width, height, pixel_fn):
    header = bytearray(18)
    header[2] = 2                      # uncompressed true-color
    header[12:14] = struct.pack("<H", width)
    header[14:16] = struct.pack("<H", height)
    header[16] = 24                    # bpp
    header[17] = 0x20                  # top-left origin
    with open(path, "wb") as f:
        f.write(header)
        row = bytearray(width * 3)
        for y in range(height):
            for x in range(width):
                r, g, b = pixel_fn(x, y, width, height)
                i = x * 3
                row[i] = b
                row[i + 1] = g
                row[i + 2] = r
            f.write(row)


def gradient(x, y, w, h):
    return (x * 255 // max(w - 1, 1),
            y * 255 // max(h - 1, 1),
            (x + y) * 255 // max(w + h - 2, 1))


COLORS = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 0),
          (255, 0, 255), (0, 255, 255), (255, 255, 255), (0, 0, 0)]


def colorbars(x, y, w, h):
    # 8 stripes, each 16 px wide where possible
    stripe = min(16, max(1, w // 8))
    return COLORS[min(x // stripe, 7)]


def oddsize(x, y, w, h):
    # Checker-ish pattern with sharp transitions every 4 px
    return COLORS[((x // 4) + (y // 4)) & 7]


def main(out_dir):
    os.makedirs(out_dir, exist_ok=True)
    write_tga(os.path.join(out_dir, "gradient.tga"), 96, 64, gradient)
    write_tga(os.path.join(out_dir, "colorbars.tga"), 128, 32, colorbars)
    write_tga(os.path.join(out_dir, "oddsize.tga"), 37, 17, oddsize)
    print("wrote gradient.tga (96x64), colorbars.tga (128x32), oddsize.tga (37x17) to", out_dir)


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else ".")
