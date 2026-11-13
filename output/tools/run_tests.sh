#!/usr/bin/env bash
# End-to-end acceptance runner for arithmetic JPEG support.
# Uses the repository's own jpge entry point plus a libjpeg-turbo reference.
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/output"
TOOLS="$OUT/tools"
DATA="$OUT/testdata"
RES="$OUT/results"
mkdir -p "$DATA" "$RES"

CC="${CC:-gcc}"
CFLAGS="-O2 -std=c11 -Wall -I$ROOT"

echo "== build reference/harness tools =="
$CC $CFLAGS "$TOOLS/mkref.c"   -ljpeg -o "$TOOLS/mkref"   || exit 1
# dec_test links the exact jpgd code under test (static archive below).
$CC -O2 -std=c++11 -fno-strict-aliasing -I"$ROOT" -c "$ROOT/jpgd.cpp"       -o "$TOOLS/jpgd.o"        || exit 1
$CC -O2 -std=c++11 -fno-strict-aliasing -I"$ROOT" -c "$ROOT/jpgd_entropy.cpp" -o "$TOOLS/jpgd_entropy.o" || exit 1
$CC -O2 -std=c++11 -fno-strict-aliasing -I"$ROOT" -c "$ROOT/jpgd_arith.cpp" -o "$TOOLS/jpgd_arith.o"  || exit 1
ar rcs "$TOOLS/libjpgd_under_test.a" "$TOOLS/jpgd.o" "$TOOLS/jpgd_entropy.o" "$TOOLS/jpgd_arith.o"
$CC -O2 -std=c++11 -Wall -I"$ROOT" -x c++ "$TOOLS/dec_test.c" -x none "$TOOLS/libjpgd_under_test.a" -ljpeg -lstdc++ -o "$TOOLS/dec_test" || exit 1

echo "== generate source images =="
python3 -I "$TOOLS/gen/gen_images.py" "$DATA"

echo "== build arithmetic + coefficient-identical Huffman twins =="
# args: image prefix subsampling restart_rows
"$TOOLS/mkref" "$DATA/gray.ppm"    "$DATA/gray"    3 0
"$TOOLS/mkref" "$DATA/rgb444.ppm"  "$DATA/rgb444"  0 0
"$TOOLS/mkref" "$DATA/rgb422.ppm"  "$DATA/rgb422"  1 0
"$TOOLS/mkref" "$DATA/rgb420.ppm"  "$DATA/rgb420"  2 0
"$TOOLS/mkref" "$DATA/rgb_rst.ppm" "$DATA/rgb_rst" 2 8
"$TOOLS/mkref" "$DATA/small.ppm"   "$DATA/small"   2 0
"$TOOLS/mkref" "$DATA/tall.ppm"    "$DATA/tall"    2 0

echo "== construct malformed streams =="
python3 -I "$TOOLS/gen/make_bad.py" "$DATA"

echo "== run acceptance harness =="
"$TOOLS/dec_test" "$DATA" | tee "$RES/dec_test.log"
HARNESS_RC=${PIPESTATUS[0]}

echo
echo "== repository entry point (jpge -d) checks =="
NEWJPGE="$ROOT/bin/jpge"
BASEJPGE="$TOOLS/jpge_baseline"
# arithmetic: baseline must fail, new must succeed
"$BASEJPGE" -d "$DATA/rgb420.arith.jpg" "$RES/baseline_arith.tga" >"$RES/baseline_arith.log" 2>&1
echo "baseline on arithmetic jpg exit=$? (expect nonzero)"
"$NEWJPGE"  -d "$DATA/rgb420.arith.jpg" "$RES/new_arith.tga"      >"$RES/new_arith.log" 2>&1
echo "new build on arithmetic jpg exit=$? (expect zero)"

# Huffman zero regression: TGA output must be byte-identical between builds
for b in gray rgb444 rgb422 rgb420; do
    "$BASEJPGE" -d "$DATA/$b.huff.jpg" "$RES/base_$b.tga" >/dev/null 2>&1
    "$NEWJPGE"  -d "$DATA/$b.huff.jpg" "$RES/new_$b.tga"  >/dev/null 2>&1
    if cmp -s "$RES/base_$b.tga" "$RES/new_$b.tga"; then
        echo "  PASS huffman TGA identical: $b"
    else
        echo "  FAIL huffman TGA differs: $b"; HARNESS_RC=1
    fi
done

echo
if [ "$HARNESS_RC" -eq 0 ]; then echo "OVERALL: ACCEPTANCE PASSED"; else echo "OVERALL: ACCEPTANCE FAILED"; fi
exit "$HARNESS_RC"
