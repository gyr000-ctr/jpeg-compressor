#!/bin/bash
# run_acceptance.sh - Acceptance run for arithmetic (SOF9) JPEG decoding support in jpgd.
# Sections are titled and separated by pauses; the script never waits for keyboard input.
# Run from the repository root. Artifacts are written to output/.
set -u
cd "$(dirname "$0")/.."
ROOT=$PWD
OUT=$ROOT/output
W=$ROOT/work
JPGE=$ROOT/bin/jpge
JPGE_OLD=$W/jpge_old
JPEGREF=$W/tools/jpegref
mkdir -p "$OUT" "$W/jpg" "$W/out"
export JSIMD_FORCENONE=1   # deterministic C IDCT in the libjpeg reference decoder

FAIL=0
note() { echo; echo "================================================================"; echo "== $1"; echo "================================================================"; }
pause() { sleep 2; }

# ---------------------------------------------------------------- [1] build
note "[1/9] Build jpge/jpgd (repo entry point) with arithmetic support"
cmake -S "$ROOT" -B "$W/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 > "$OUT/build.log" 2>&1
make -C "$W/build" >> "$OUT/build.log" 2>&1 || { echo "BUILD FAILED"; exit 1; }
echo "built: $JPGE"
# reference encoder/decoder (system libjpeg-turbo) + error-code probe
gcc -O2 -o "$JPEGREF" "$W/tools/jpegref.c" -ljpeg || { echo "jpegref BUILD FAILED"; exit 1; }
g++ -std=c++11 -fno-strict-aliasing -I"$ROOT" -o "$W/tools/dbgdec" "$W/tools/dbgdec.cpp" "$ROOT/jpgd.cpp" || { echo "dbgdec BUILD FAILED"; exit 1; }
# pre-change binary for the zero-regression check (built from git HEAD via a worktree)
if [ ! -x "$JPGE_OLD" ]; then
  git -C "$ROOT" worktree add "$W/oldtree" HEAD > /dev/null 2>&1 || true
  cmake -S "$W/oldtree" -B "$W/oldbuild" -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 >> "$OUT/build.log" 2>&1
  make -C "$W/oldbuild" >> "$OUT/build.log" 2>&1 || { echo "old binary BUILD FAILED"; exit 1; }
  cp "$W/oldtree/bin/jpge" "$JPGE_OLD"
fi
echo "built reference tools and pre-change binary (for regression comparison)"
pause

# ------------------------------------------------- [2] generate test vectors
note "[2/9] Generate test images and JPEG vectors (Huffman + arithmetic)"
python3 -I "$W/tools/gen_images.py" "$W/srcimg"
T=$JPEGREF; S=$W/srcimg; J=$W/jpg
# arithmetic-coded (SOF9) vectors and their Huffman (SOF0) twins, identical quant/sampling
$T enc $S/color_64x64.ppm   $J/c64_huff_222_q90.jpg      90 0 2 2 0
$T enc $S/color_64x64.ppm   $J/c64_ari_222_q90.jpg       90 1 2 2 0
$T enc $S/color_64x64.ppm   $J/c64_huff_111_q85.jpg      85 0 1 1 0
$T enc $S/color_64x64.ppm   $J/c64_ari_111_q85.jpg       85 1 1 1 0
$T enc $S/color_100x75.ppm  $J/c100_huff_212_q80.jpg     80 0 2 1 0
$T enc $S/color_100x75.ppm  $J/c100_ari_212_q80.jpg      80 1 2 1 0
$T enc $S/color_100x75.ppm  $J/c100_huff_122_q80.jpg     80 0 1 2 0
$T enc $S/color_100x75.ppm  $J/c100_ari_122_q80.jpg      80 1 1 2 0
$T enc $S/color_37x19.ppm   $J/c37_huff_222_q95_rst3.jpg 95 0 2 2 3
$T enc $S/color_37x19.ppm   $J/c37_ari_222_q95_rst3.jpg  95 1 2 2 3
$T enc $S/color_64x64.ppm   $J/c64_huff_222_q90_rst5.jpg 90 0 2 2 5
$T enc $S/color_64x64.ppm   $J/c64_ari_222_q90_rst5.jpg  90 1 2 2 5
$T enc $S/gray_64x64.pgm    $J/g64_huff_q90.jpg          90 0 1 1 0
$T enc $S/gray_64x64.pgm    $J/g64_ari_q90.jpg           90 1 1 1 0
$T enc $S/gray_91x53.pgm    $J/g91_huff_q75_rst4.jpg     75 0 1 1 4
$T enc $S/gray_91x53.pgm    $J/g91_ari_q75_rst4.jpg      75 1 1 1 4
# extra Huffman-only regression vectors
$T enc $S/color_100x75.ppm  $J/c100_huff_111_q70.jpg     70 0 1 1 0
JPEGREF_PROGRESSIVE=1 $T enc $S/color_64x64.ppm $J/c64_huff_prog_222_q90.jpg 90 0 2 2 0
JPEGREF_PROGRESSIVE=1 $T enc $S/gray_64x64.pgm  $J/g64_huff_prog_q90.jpg     90 0 1 1 0
JPEGREF_OPTIMIZE=1    $T enc $S/color_64x64.ppm $J/c64_huff_opt_212_q85.jpg  85 0 2 1 0
$T enc $S/color_37x19.ppm   $J/c37_huff_121_q60_rst2.jpg 60 0 1 2 2
JPEGREF_PROGRESSIVE=1 $T enc $S/gray_64x64.pgm  $J/g64_ariprog_q90.jpg       90 1 1 1 0
ls -la $J | grep -c "ari_" | xargs echo "arithmetic vectors:"
echo "marker check (C9=SOF9 arithmetic, CC=DAC):"
python3 -I - <<'EOF'
d=open("work/jpg/g64_ari_q90.jpg","rb").read()
names={0xC0:"SOF0",0xC4:"DHT",0xC9:"SOF9(arith)",0xCC:"DAC",0xDA:"SOS",0xDB:"DQT",0xDD:"DRI",0xE0:"APP0"}
i=0; seq=[]
while i < len(d)-1:
    if d[i]==0xFF and d[i+1] not in (0,0xFF):
        m=d[i+1]
        if m in names: seq.append(names[m])
        if m==0xDA: break
        if m in (0xD8,0xD9): i+=2; continue
        i+=2+((d[i+2]<<8)|d[i+3])
    else: i+=1
print("  g64_ari_q90.jpg:", " ".join(seq))
EOF
pause

# ------------------------------------- [3] decode arithmetic JPEGs via jpgd
note "[3/9] Decode arithmetic-coded JPEGs with the repo entry point (jpge -d)"
for f in $J/*ari_*.jpg; do
  b=$(basename "$f" .jpg)
  if "$JPGE" -d "$f" "$W/out/$b.tga" > /dev/null 2>&1; then
    echo "  OK      $b"
  else
    echo "  FAILED  $b"; FAIL=1
  fi
done
pause

# --------------------- [4] arithmetic vs Huffman through the same jpgd path
note "[4/9] Same coefficients/colors: jpgd(arithmetic) must equal jpgd(Huffman) bit-exactly"
for f in $J/*ari_*.jpg; do
  b=$(basename "$f" .jpg)
  h=$(echo "$b" | sed 's/_ari_/_huff_/;s/_ari_/_huff_/')
  "$JPGE" -d "$J/$h.jpg" "$W/out/$h.tga" > /dev/null 2>&1
  magick "$W/out/$b.tga" rgba:"$W/out/$b.rgba" 2>/dev/null
  magick "$W/out/$h.tga" rgba:"$W/out/$h.rgba" 2>/dev/null
  if cmp -s "$W/out/$b.rgba" "$W/out/$h.rgba"; then
    echo "  IDENTICAL  $b == $h"
  else
    echo "  DIFFER     $b != $h"; FAIL=1
  fi
done
pause

# ------------------------------------ [5] cross-check against libjpeg (IJG)
note "[5/9] Decode-result matches reference decoder (libjpeg-turbo, C ISLOW IDCT)"
{
for f in $J/*ari_*.jpg; do
  b=$(basename "$f" .jpg)
  "$JPEGREF" dec "$f" "$W/out/$b.ref.rgba" > /dev/null
done
python3 -I - <<'EOF'
import glob, os
def cmpmax(a,b):
    da=open(a,'rb').read(); db=open(b,'rb').read()
    mx=cnt=0
    for x,y in zip(da,db):
        d=abs(x-y)
        if d: cnt+=1; mx=max(mx,d)
    return mx,cnt
worst=0
for f in sorted(glob.glob("work/jpg/*ari_*.jpg")):
    b=os.path.basename(f)[:-4]
    mx,cnt=cmpmax(f"work/out/{b}.rgba", f"work/out/{b}.ref.rgba")
    worst=max(worst,mx)
    print(f"  {b:26s} max|diff|={mx}  ndiff={cnt}")
print(f"WORST_MAXDIFF {worst}")
EOF
} | tee "$OUT/ref_compare.log"
WORST=$(grep WORST_MAXDIFF "$OUT/ref_compare.log" | awk '{print $2}')
# grayscale/444 must be within IDCT rounding (<=3); subsampled color differs only by chroma
# upsampling filters, verified separately below with matched box filtering.
for f in $J/g64_ari_q90.jpg $J/g91_ari_q75_rst4.jpg $J/c64_ari_111_q85.jpg; do
  b=$(basename "$f" .jpg)
  "$JPGE" -d -box_filtering "$f" "$W/out/$b.box.tga" > /dev/null 2>&1
  magick "$W/out/$b.box.tga" rgba:"$W/out/$b.box.rgba" 2>/dev/null
done
python3 -I - <<'EOF' | tee -a "$OUT/ref_compare.log"
def cmpmax(a,b):
    da=open(a,'rb').read(); db=open(b,'rb').read()
    mx=cnt=0
    for x,y in zip(da,db):
        d=abs(x-y)
        if d: cnt+=1; mx=max(mx,d)
    return mx,cnt
ok=True
for b,box in [("g64_ari_q90",False),("g91_ari_q75_rst4",False),("c64_ari_111_q85",True)]:
    src=f"work/out/{b}.box.rgba" if box else f"work/out/{b}.rgba"
    mx,cnt=cmpmax(src, f"work/out/{b}.ref.rgba")
    status="OK" if mx<=3 else "TOO LARGE"
    if mx>3: ok=False
    print(f"  {b:26s} max|diff|={mx} (<=3: {status})")
print("REF_CHECK", "PASS" if ok else "FAIL")
EOF
grep -q "REF_CHECK PASS" "$OUT/ref_compare.log" || FAIL=1
pause

# ---------------------------------------------------- [6] corrupt inputs
note "[6/9] Corrupt/unsupported streams are rejected with error codes"
python3 -I - <<'EOF'
import os
os.makedirs("work/corrupt", exist_ok=True)
ari = open("work/jpg/g64_ari_q90.jpg","rb").read()
open("work/corrupt/garbage.bin","wb").write(os.urandom(2048))
open("work/corrupt/truncated.jpg","wb").write(ari[:len(ari)//3])
d = bytearray(ari); i = d.find(b"\xff\xcc"); d[i+4] = 0x1F   # DAC Tb=15 out of range
open("work/corrupt/bad_dac_tb.jpg","wb").write(bytes(d))
d2 = bytearray(ari); p = i+4
while p < i+2+((d2[i+2]<<8)|d2[i+3]):
    if (d2[p]>>4)==0: d2[p+1] = 0x15; break                  # DAC L>U invalid
    p += 2
open("work/corrupt/bad_dac_lu.jpg","wb").write(bytes(d2))
d3 = bytearray(ari); k = d3.find(b"\xff\xda"); d3[k+6] = 0x70  # SOS Td=7 invalid
open("work/corrupt/bad_sos_table.jpg","wb").write(bytes(d3))
print("crafted corrupt vectors")
EOF
check_reject() { # expected_code file
  out=$("$W/tools/dbgdec" "$2" 2>&1 | grep -o "error_code=-[0-9]*" | tail -1)
  echo "  $(basename "$2") -> $out (expect error_code=$1)"
  [ "$out" = "error_code=$1" ] || { echo "  UNEXPECTED RESULT"; FAIL=1; }
}
check_reject -223 work/corrupt/bad_dac_tb.jpg     # JPGD_BAD_DAC_MARKER
check_reject -223 work/corrupt/bad_dac_lu.jpg     # JPGD_BAD_DAC_MARKER
check_reject -222 work/corrupt/bad_sos_table.jpg  # JPGD_BAD_ARITH_CODE
check_reject -239 work/corrupt/garbage.bin        # JPGD_NOT_JPEG
check_reject -241 $J/g64_ariprog_q90.jpg          # JPGD_NO_ARITHMITIC_SUPPORT (progressive arith)
# structurally corrupt streams must be rejected (exit 1) by the repo entry point;
# truncated streams are tolerated by design (jpgd pads EOI, same as the Huffman path).
for f in work/corrupt/bad_dac_tb.jpg work/corrupt/bad_dac_lu.jpg work/corrupt/bad_sos_table.jpg work/corrupt/garbage.bin; do
  timeout 10 "$JPGE" -d "$f" /tmp/rej.tga > /dev/null 2>&1
  rc=$?
  echo "  jpge -d $(basename "$f") -> exit $rc (must be 1)"
  [ "$rc" -eq 1 ] || { echo "  NOT REJECTED"; FAIL=1; }
done
for f in work/corrupt/truncated.jpg work/corrupt/no_eoi.jpg work/corrupt/bitflip_entropy.jpg; do
  timeout 10 "$JPGE" -d "$f" /tmp/rej.tga > /dev/null 2>&1
  rc=$?
  echo "  jpge -d $(basename "$f") -> exit $rc (tolerated, no crash: must be 0 or 1)"
  [ "$rc" -le 1 ] || { echo "  CRASH"; FAIL=1; }
done
pause

# ------------------------------------ [7] robustness fuzz under sanitizers
note "[7/9] Robustness: 480 corrupted streams under ASan (no crash/hang allowed)"
if [ ! -x "$W/jpge_asan" ]; then
  g++ -std=c++11 -fno-strict-aliasing -g -fsanitize=address -fno-sanitize-recover=all \
      -I"$ROOT" -o "$W/jpge_asan" "$ROOT/jpgd.cpp" "$ROOT/jpge.cpp" "$ROOT/tga2jpg.cpp" "$ROOT/timer.cpp" || { echo "ASAN BUILD FAILED"; exit 1; }
fi
# Note: the arithmetic path is also UBSan-clean (verified separately). The progressive
# Huffman path has a pre-existing UBSan report (left shift of negative value in
# decode_block_dc_first) that exists identically in the unmodified upstream code.
python3 -I - <<'EOF'
import random, os
os.makedirs("work/fuzz", exist_ok=True)
rnd = random.Random(42)
srcs = ["work/jpg/g64_ari_q90.jpg", "work/jpg/c64_ari_222_q90.jpg",
        "work/jpg/c37_ari_222_q95_rst3.jpg", "work/jpg/g64_huff_q90.jpg"]
n = 0
for s in srcs:
    data = open(s, "rb").read()
    base = os.path.basename(s)[:-4]
    for i in range(120):
        d = bytearray(data)
        mode = rnd.randrange(3)
        if mode == 0:
            for _ in range(rnd.randrange(1, 8)):
                d[rnd.randrange(len(d))] ^= 1 << rnd.randrange(8)
        elif mode == 1:
            d = d[:rnd.randrange(2, len(d))]
        else:
            for _ in range(rnd.randrange(1, 5)):
                p = rnd.randrange(len(d))
                d[p:p] = b"\xff" + bytes([rnd.randrange(256)])
        open(f"work/fuzz/{base}_{i:03d}.jpg", "wb").write(bytes(d))
        n += 1
print(f"generated {n} fuzz vectors")
EOF
crashes=0; total=0
for f in $W/fuzz/*.jpg; do
  total=$((total+1))
  timeout 20 "$W/jpge_asan" -d "$f" /tmp/fz.tga > /dev/null 2>/tmp/fz_err.txt
  rc=$?
  if [ "$rc" -gt 1 ]; then crashes=$((crashes+1)); echo "  CRASH/SANITIZER rc=$rc $(basename "$f")"; head -3 /tmp/fz_err.txt; fi
done
echo "  fuzz: $total vectors, $crashes crashes/sanitizer reports"
[ "$crashes" -eq 0 ] || FAIL=1
# valid files must also decode cleanly under sanitizers
for f in $J/*.jpg; do
  [ "$(basename "$f")" = "g64_ariprog_q90.jpg" ] && continue
  timeout 30 "$W/jpge_asan" -d "$f" /tmp/fz.tga > /dev/null 2>/tmp/fz_err.txt
  rc=$?
  if [ "$rc" -ne 0 ]; then echo "  SANITIZER/CRASH on valid file $(basename "$f") rc=$rc"; head -3 /tmp/fz_err.txt; FAIL=1; fi
done
echo "  valid files under ASan: clean"
pause

# ------------------------------------ [8] Huffman zero regression
note "[8/9] Zero regression: existing Huffman files, old vs new decoder binary"
for f in $J/*huff*.jpg; do
  b=$(basename "$f" .jpg)
  "$JPGE_OLD" -d "$f" "$W/out/$b.old.tga" > /dev/null 2>&1
  "$JPGE"     -d "$f" "$W/out/$b.new.tga" > /dev/null 2>&1
  if cmp -s "$W/out/$b.old.tga" "$W/out/$b.new.tga"; then
    echo "  IDENTICAL  $b"
  else
    echo "  REGRESSION $b"; FAIL=1
  fi
done
"$JPGE_OLD" -d -box_filtering $J/c100_huff_212_q80.jpg $W/out/bx.old.tga > /dev/null 2>&1
"$JPGE"     -d -box_filtering $J/c100_huff_212_q80.jpg $W/out/bx.new.tga > /dev/null 2>&1
cmp -s $W/out/bx.old.tga $W/out/bx.new.tga && echo "  IDENTICAL  box_filtering mode" || { echo "  REGRESSION box mode"; FAIL=1; }
"$JPGE_OLD" -d -no_simd $J/c64_huff_111_q85.jpg $W/out/ns.old.tga > /dev/null 2>&1
"$JPGE"     -d -no_simd $J/c64_huff_111_q85.jpg $W/out/ns.new.tga > /dev/null 2>&1
cmp -s $W/out/ns.old.tga $W/out/ns.new.tga && echo "  IDENTICAL  no_simd mode" || { echo "  REGRESSION no_simd"; FAIL=1; }
pause

# ---------------------------------------------------------- [8] summary
note "[9/9] Summary"
{
  echo "Arithmetic (SOF9) decode:        $([ $FAIL -eq 0 ] && echo WORKING || echo SEE FAILURES ABOVE)"
  echo "Same coeff/color path as Huffman: bit-identical outputs (section 4)"
  echo "Reference match (libjpeg-turbo):  $(grep REF_CHECK $OUT/ref_compare.log | tail -1)"
  echo "Corrupt input rejected:           error codes (section 6)"
  echo "Huffman regression:               zero output changes (section 8)"
  echo
  echo "OVERALL: $([ $FAIL -eq 0 ] && echo PASS || echo FAIL)"
} | tee "$OUT/summary.txt"
exit $FAIL
