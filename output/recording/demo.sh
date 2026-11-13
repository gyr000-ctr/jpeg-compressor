#!/usr/bin/env bash
# Segmented, non-interactive demo of the arithmetic-JPEG work.
# Each section prints a title, pauses, runs its commands, then pauses again.
# Designed to be captured with asciinema; never waits on keyboard input.
set -u
ROOT=/home/gyr/feedback/uid5244
OUT=$ROOT/output
pause() { sleep "$1"; }
title() {
    echo
    echo "================================================================"
    echo ">>> $1"
    echo "================================================================"
    pause 2
}

title "Section 1/5: Build the modified decoder (CMake, repo's own entry point)"
mkdir -p "$OUT/build_demo"
cmake -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -S "$ROOT" -B "$OUT/build_demo" >/dev/null
make -C "$OUT/build_demo" -j4 2>&1 | tail -2
pause 2

title "Section 2/5: Baseline rejects arithmetic JPEG; new build decodes it"
echo "-- baseline binary (unmodified jpgd):"
"$OUT/tools/jpge_baseline" -d "$OUT/testdata/rgb420.arith.jpg" "$OUT/results/demo_base.tga" || echo "   -> baseline rejected it (expected)"
echo "-- new binary:"
"$ROOT/bin/jpge" -d "$OUT/testdata/rgb420.arith.jpg" "$OUT/results/demo_new.tga"
pause 2

title "Section 3/5: Arithmetic output equals coefficient-twin Huffman output"
"$OUT/tools/pixdiff" "$OUT/testdata/rgb420.arith.jpg" "$OUT/testdata/rgb420.huff.jpg"
"$OUT/tools/pixdiff" "$OUT/testdata/gray.arith.jpg"   "$OUT/testdata/gray.huff.jpg"
pause 2

title "Section 4/5: Corrupt and truncated streams are rejected with an error"
"$OUT/tools/dec_test" "$OUT/testdata" 2>&1 | sed -n '/\[2\]/,/\[3\]/p'
pause 2

title "Section 5/5: Full acceptance suite and Huffman zero-regression"
"$OUT/tools/dec_test" "$OUT/testdata" 2>&1 | tail -3
for b in gray rgb444 rgb422 rgb420; do
    "$OUT/tools/jpge_baseline" -d "$OUT/testdata/$b.huff.jpg" "/tmp/db_$b.tga" >/dev/null 2>&1
    "$ROOT/bin/jpge"          -d "$OUT/testdata/$b.huff.jpg" "/tmp/dn_$b.tga" >/dev/null 2>&1
    cmp -s "/tmp/db_$b.tga" "/tmp/dn_$b.tga" && echo "  PASS huffman TGA identical: $b"
done
echo
echo "Demo finished."
