#!/usr/bin/env python3
# Unpack libjpeg-turbo's jpeg_aritab (T.81 Table D.2) V() encoding into four
# plain C arrays: Qe, Next_Index_LPS, Next_Index_MPS, Switch_MPS.
#
# V(i, a, b, c, d) = (a<<16) | (c<<8) | (d<<7) | b produces a 32-bit word
# laid out as  [ NMPS : 8 ][ SW : 1 ][ NLPS : 7 ][ Qe : 16 ], matching the
# unpacking in jdarith.c::arith_decode(). Qe's top bit is always zero so it
# shares bit 23 with the switch flag.
import re, sys

src = open('/tmp/jpeg-ref/jaricom.c').read()
entries = re.findall(r'V\(\s*(\d+),\s*0x([0-9a-fA-F]+),\s*(\d+),\s*(\d+),\s*(\d+)\s*\)', src)
assert len(entries) == 114, len(entries)

qe, nlps, nmps, sw = [], [], [], []
for idx, a, b, c, d in entries:
    assert int(idx) == len(qe)
    a, b, c, d = int(a, 16), int(b), int(c), int(d)
    assert a < 0x8000  # Qe top bit unused
    packed = (a << 16) | (c << 8) | (d << 7) | b
    # mirror jdarith.c's two-step unpacking
    nl = packed & 0xFF
    nm = (packed >> 8) & 0xFF
    assert nl == (b | (d << 7)) and nm == c
    qe.append(a); nlps.append(b); nmps.append(c); sw.append(d)

def emit(name, vals, hexv=False):
    out = [f"\tstatic const uint {name}[JPGD_ARITH_NUM_STATES + 1] =", "\t{"]
    line = "\t"
    for v in vals:
        t = (f"0x{v:04x}" if hexv else str(v)).rjust(7 if hexv else 4)
        if len(line) + len(t) + 1 > 100:
            out.append(line.rstrip()); line = "\t"
        line += t + ","
    out.append(line.rstrip()); out.append("\t};")
    return "\n".join(out)

body = "\n\n".join([
    emit("s_arith_qe", qe, True),
    emit("s_arith_nlps", nlps),
    emit("s_arith_nmps", nmps),
    emit("s_arith_switch", sw),
])
open(sys.argv[1], 'w').write(body + "\n")
print(f"wrote {len(qe)} entries")
