#!/usr/bin/env python3 -I
"""Verification driver for the H4V1 (4:1:1) feature.

Uses only the repository's own jpge entry point (tga2jpg) plus the bundled
stb_image reference decoder (via its -s switch). No third-party tools.

Three sections:
  A. Zero regression: baseline (pristine HEAD) and current binaries must emit
     byte-identical JPEGs for every pre-existing subsampling mode, and
     byte-identical decoded TGAs (box and linear, with/without SIMD).
  B. H4V1 chroma restoration: current jpgd output vs the bundled stb_image
     reference on the same H4V1 stream, plus per-stripe hue checks for
     chroma placement; baseline binary must reject the H4V1 stream.
  C. End-to-end exhaustive test of the legacy paths through the repo's -x mode.
"""
import os
import struct
import subprocess
import sys


def run(cmd):
    p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    return p.returncode, p.stdout


def load_tga(path):
    with open(path, "rb") as f:
        data = f.read()
    id_len, cmap_type, img_type = data[0], data[1], data[2]
    w, h = struct.unpack_from("<HH", data, 12)
    bpp, desc = data[16], data[17]
    assert cmap_type == 0 and img_type in (2, 10) and bpp in (24, 32), \
        f"unsupported TGA type={img_type} bpp={bpp}"
    nch = bpp // 8
    off = 18 + id_len
    row_bytes = w * nch
    raw = bytearray()
    if img_type == 2:
        raw = bytearray(data[off:off + h * row_bytes])
    else:  # type 10: RLE packets
        pos = off
        while len(raw) < h * row_bytes:
            hdr = data[pos]
            pos += 1
            count = (hdr & 0x7F) + 1
            if hdr & 0x80:
                chunk = data[pos:pos + nch] * count
                pos += nch
            else:
                chunk = data[pos:pos + count * nch]
                pos += count * nch
            raw.extend(chunk)
    px = bytearray(w * h * nch)
    top_left = bool(desc & 0x20)
    for y in range(h):
        src_y = y if top_left else (h - 1 - y)
        px[src_y * row_bytes:(src_y + 1) * row_bytes] = raw[y * row_bytes:(y + 1) * row_bytes]
    return w, h, nch, px


def rgb_at(px, nch, x, y, w):
    i = (y * w + x) * nch
    # TGA stores BGR(A)
    return px[i + 2], px[i + 1], px[i]


def diff_stats(a_path, b_path):
    wa, ha, na, pa = load_tga(a_path)
    wb, hb, nb, pb = load_tga(b_path)
    assert (wa, ha) == (wb, hb), "dimension mismatch"
    n = min(na, nb)
    max_err, sumsq, count = 0, 0.0, 0
    for i in range(0, wa * ha * n, n):
        for c in range(3):
            d = pa[i + c] - pb[i + c]
            max_err = max(max_err, abs(d))
            sumsq += d * d
            count += 1
    return max_err, (sumsq / count) ** 0.5


def main():
    repo, base_bin, cur_bin, tga_dir, out_dir = sys.argv[1:6]
    failures = []

    def check(cond, msg):
        print(("  [PASS] " if cond else "  [FAIL] ") + msg)
        if not cond:
            failures.append(msg)

    tgas = ["gradient.tga", "colorbars.tga", "oddsize.tga"]
    modes = [("-h1v1", "h1v1"), ("-h2v1", "h2v1"), ("-h2v2", "h2v2")]
    flag_sets = [([], "linear"), (["-box_filtering"], "box"), (["-no_simd"], "nosimd")]

    print("== A. Zero regression on pre-existing sampling modes ==")
    for tga in tgas:
        src = os.path.join(tga_dir, tga)
        for mode_flag, mode_name in modes:
            base_jpg = os.path.join(out_dir, f"base_{tga}_{mode_name}.jpg")
            cur_jpg = os.path.join(out_dir, f"cur_{tga}_{mode_name}.jpg")
            rc1, o1 = run([base_bin, mode_flag, src, base_jpg, "90"])
            rc2, o2 = run([cur_bin, mode_flag, src, cur_jpg, "90"])
            check(rc1 == 0 and rc2 == 0, f"{tga} {mode_name}: both encoders succeed")
            with open(base_jpg, "rb") as f:
                b1 = f.read()
            with open(cur_jpg, "rb") as f:
                b2 = f.read()
            check(b1 == b2, f"{tga} {mode_name}: encoded JPEG byte-identical ({len(b2)} bytes)")

            for flags, flabel in flag_sets:
                for tag, jpg, binp in (("base", base_jpg, base_bin), ("cur", cur_jpg, cur_bin)):
                    rc, _ = run([binp, "-d", *flags, jpg,
                                 os.path.join(out_dir, f"dec_{tag}_{tga}_{mode_name}_{flabel}.tga")])
                    check(rc == 0, f"{tga} {mode_name} {flabel}: {tag} decode succeeds")
                mx, rmse = diff_stats(
                    os.path.join(out_dir, f"dec_base_{tga}_{mode_name}_{flabel}.tga"),
                    os.path.join(out_dir, f"dec_cur_{tga}_{mode_name}_{flabel}.tga"))
                check(mx == 0, f"{tga} {mode_name} {flabel}: decoded pixels identical (max_err={mx})")

    print("== B. H4V1 (4:1:1) chroma restoration ==")
    # stb_image only provides nearest-neighbor upsampling for non-2x horizontal
    # factors (its generic resampler replicates each chroma sample hs times), so
    # it is an exact reference for BOX mode. LINEAR mode uses jpgd's triangular
    # filter (shared with the legacy H2V1 math) and is checked against the known
    # source colors directly.
    for tga in tgas:
        src = os.path.join(tga_dir, tga)
        h4 = os.path.join(out_dir, f"cur_{tga}_h4v1.jpg")
        rc, _ = run([cur_bin, "-h4v1", src, h4, "95"])
        check(rc == 0, f"{tga}: H4V1 encode succeeds")

        rc, _ = run([base_bin, "-d", h4, os.path.join(out_dir, "should_not_exist.tga")])
        check(rc != 0, f"{tga}: baseline binary rejects H4V1 stream")

        jpgd_box = os.path.join(out_dir, f"h4_{tga}_box_jpgd.tga")
        stb_box = os.path.join(out_dir, f"h4_{tga}_box_stb.tga")
        rc1, _ = run([cur_bin, "-d", "-box_filtering", h4, jpgd_box])
        rc2, _ = run([cur_bin, "-d", "-s", "-box_filtering", h4, stb_box])
        check(rc1 == 0 and rc2 == 0, f"{tga} box: jpgd and stb decode succeed")
        mx, rmse = diff_stats(jpgd_box, stb_box)
        print(f"  [info] {tga} box: jpgd vs stb max_err={mx} rmse={rmse:.2f}")
        check(rmse < 1.0 and mx <= 4, f"{tga} box: matches stb nearest-neighbor reference (rmse<1, max<=4)")

        jpgd_lin = os.path.join(out_dir, f"h4_{tga}_linear_jpgd.tga")
        rc, _ = run([cur_bin, "-d", h4, jpgd_lin])
        check(rc == 0, f"{tga} linear: jpgd triangular decode succeeds")

    # Linear-mode restoration against the source image's known stripe colors.
    src_bars = os.path.join(tga_dir, "colorbars.tga")
    h4 = os.path.join(out_dir, "cur_colorbars.tga_h4v1.jpg")
    run([cur_bin, "-h4v1", src_bars, h4, "95"])
    lin = os.path.join(out_dir, "h4_bars_linear.tga")
    run([cur_bin, "-d", h4, lin])
    w, hgt, nch, px = load_tga(lin)
    sw, sh, snch, spx = load_tga(src_bars)
    stripe = max(1, sw // 8)
    interior_ok = True
    for s in range(8):
        x0, x1 = s * stripe + 4, min((s + 1) * stripe - 4, sw - 1)
        for x in range(x0, max(x1, x0 + 1)):
            for y in (2, hgt // 2, hgt - 3):
                got = rgb_at(px, nch, x, y, w)
                exp = rgb_at(spx, snch, x, min(y, sh - 1), sw)
                if sum(abs(got[c] - exp[c]) for c in range(3)) // 3 > 10:
                    interior_ok = False
    check(interior_ok, "linear: interior stripe hues match source (correct chroma restoration)")

    bled = 0
    for s in range(1, 8):
        bx = s * stripe
        far = list(range(max(0, bx - 8), bx - 3)) + list(range(bx + 4, min(sw, bx + 9)))
        def col_err(x):
            es = []
            for y in (2, hgt // 2, hgt - 3):
                g = rgb_at(px, nch, x, y, w)
                e = rgb_at(spx, snch, x, min(y, sh - 1), sw)
                es.append(sum(abs(g[c] - e[c]) for c in range(3)) // 3)
            return sum(es) // len(es)
        if any(col_err(x) > 12 for x in far):
            bled += 1
    check(bled == 0, "linear: interpolation confined to chroma boundaries (no wide smearing)")

    print("== C. Repo exhaustive legacy test (-x) ==")
    rc, out = run([cur_bin, "-x", os.path.join(tga_dir, "gradient.tga")])
    tail = out.strip().splitlines()[-1] if out.strip() else ""
    check(rc == 0 and "Success" in tail, f"exhaustive compression test passes (last line: {tail})")

    print()
    if failures:
        print(f"VERIFICATION FAILED: {len(failures)} check(s)")
        for m in failures:
            print(" -", m)
        return 1
    print("ALL VERIFICATION CHECKS PASSED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
