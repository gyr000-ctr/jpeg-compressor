#!/usr/bin/env python3
"""Extended fuzz-style regression: many random images, all subsampling modes,
with and without restart intervals. Verifies arithmetic output is byte-identical
to the coefficient-twin Huffman decode through the jpgd library. Stdlib only."""
import os, subprocess, random, struct, sys, tempfile

ROOT = "/home/gyr/feedback/uid5244"
TOOLS = f"{ROOT}/output/tools"
DATA = f"{ROOT}/output/testdata/fuzz"
os.makedirs(DATA, exist_ok=True)
random.seed(20261114)

def write_pnm(path, w, h, comps, data):
    with open(path, "wb") as f:
        f.write(b"P6\n" if comps == 3 else b"P5\n")
        f.write(f"{w} {h}\n255\n".encode())
        f.write(bytes(data))

fails = 0
cases = 0
for it in range(24):
    w = random.choice([8, 16, 17, 31, 33, 48, 64, 80, 100, 128])
    h = random.choice([8, 16, 24, 33, 64, 72, 96, 120])
    comps = random.choice([1, 3])
    n = w * h * comps
    # mix of smooth gradients and noise so both low- and high-frequency content
    data = []
    for i in range(n):
        if it % 2:
            data.append(random.randint(0, 255))
        else:
            data.append((i * 37 + (i // max(1, w)) * 11) & 255)
    ppm = f"{DATA}/f{it}.ppm"
    write_pnm(ppm, w, h, comps, data)
    subsamp = 3 if comps == 1 else random.choice([0, 1, 2])
    restart = random.choice([0, 4, 8, 16])
    pref = f"{DATA}/f{it}"
    r = subprocess.run([f"{TOOLS}/mkref", ppm, pref, str(subsamp), str(restart)],
                       capture_output=True)
    if r.returncode != 0:
        print("mkref fail", it, r.stderr.decode()[:200]); fails += 1; continue
    cases += 1
    # pixel-level twin comparison via the dedicated tool
    rc = subprocess.run([f"{TOOLS}/pixdiff", f"{pref}.arith.jpg", f"{pref}.huff.jpg"],
                        capture_output=True, text=True)
    line = rc.stdout.strip()
    ok = "diff pixels=0/" in line
    print(f"case {it:2d} {w}x{h} c={comps} ss={subsamp} rst={restart}: {line}")
    if not ok:
        fails += 1

print(f"\nfuzz regression: {cases} cases, {fails} failure(s)")
sys.exit(1 if fails else 0)
