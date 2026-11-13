#!/usr/bin/env python3
"""Construct malformed JPEG inputs for rejection tests (stdlib only)."""
import os, sys

DATA = sys.argv[1]

def find_soi(d):
    assert d[:2] == b"\xff\xd8"
    return 2

src = os.path.join(DATA, "rgb420.arith.jpg")
d = open(src, "rb").read()
p = find_soi(d)

# 1. Invalid DAC marker: length 4, one parameter with DC L > U (0x10 means L=0? ->
#    craft val=0x81: low nibble L=1, high nibble U=8 -> L<=U valid; instead use
#    val=0x18 => L=8, U=1, L>U => must be rejected).
dac = b"\xff\xcc\x00\x04\x00\x18"
bad_dac = d[:p] + dac + d[p:]
open(os.path.join(DATA, "bad_dac.jpg"), "wb").write(bad_dac)

# 2. SOF9 (0xFFC9) -> SOF10 (0xFFCA): progressive arithmetic, explicitly unsupported.
i = d.find(b"\xff\xc9")
assert i >= 0, "no SOF9 marker found"
bad_sof = d[:i] + b"\xff\xca" + d[i + 2:]
open(os.path.join(DATA, "bad_sof10.jpg"), "wb").write(bad_sof)

# 3. Not a JPEG at all.
open(os.path.join(DATA, "notjpeg.bin"), "wb").write(b"this is definitely not a jpeg file\n" * 4)

# 4. DAC with an out-of-range table index (index 8, only 0..7 allowed).
dac_badidx = b"\xff\xcc\x00\x04\x08\x05"
open(os.path.join(DATA, "bad_dac_index.jpg"), "wb").write(d[:p] + dac_badidx + d[p:])

print("wrote malformed streams to", DATA)
