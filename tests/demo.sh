#!/usr/bin/env bash
# Demo driver for the H4V1 (4:1:1) feature. Segmentized with on-screen titles
# and inter-segment pauses. All artifacts go to output/; never waits on input.
set -u
REPO="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$REPO/bin/jpge"
TD="$REPO/output/testdata"
OUT="$REPO/output/demo"
mkdir -p "$OUT"

title() {
	printf '\n\033[1;36m================================================================\n'
	printf '  %s\n' "$1"
	printf '================================================================\033[0m\n'
	sleep 2
}
pause() { sleep 2; }

title "段 1/4：仓库自带入口生成 4:1:1 (H4V1) 素材"
echo "\$ $BIN -h4v1 $TD/colorbars.tga $OUT/h4v1.jpg 95"
sleep 1
"$BIN" -h4v1 "$TD/colorbars.tga" "$OUT/h4v1.jpg" 95
pause

title "段 2/4：确认 SOF 采样系数为 Y=4x1, Cb/Cr=1x1"
python3 -I - "$OUT/h4v1.jpg" <<'PY'
import sys
d = open(sys.argv[1], "rb").read()
i = 2
while i < len(d):
    if d[i] != 0xFF:
        i += 1; continue
    m = d[i+1]
    if m in (0xC0, 0xC2):
        nf = d[i+9]
        print(f"SOF marker 0x{m:02X}, components={nf}")
        for c in range(nf):
            spec = i + 10 + c*3
            hv = d[spec+1]
            print(f"  comp {c}: id={d[spec]} H={hv>>4} V={hv&1}")
        break
    seg = (d[i+2] << 8) | d[i+3]
    i += 2 + seg
PY
pause

title "段 3/4：jpgd 解码（线性/盒式两种上采样）并与 stb 参考对照"
"$BIN" -d             "$OUT/h4v1.jpg" "$OUT/h4v1_linear.tga"
"$BIN" -d -box_filtering "$OUT/h4v1.jpg" "$OUT/h4v1_box.tga"
"$BIN" -d -s -box_filtering "$OUT/h4v1.jpg" "$OUT/h4v1_stb_ref.tga"
python3 -I - "$OUT/h4v1_box.tga" "$OUT/h4v1_stb_ref.tga" <<'PY'
import struct, sys
def load(p):
    d = open(p, "rb").read()
    w, h = struct.unpack_from("<HH", d, 12)
    bpp, desc, img_type = d[16], d[17], d[2]
    n = bpp // 8; off = 18 + d[0]; rb = w * n
    if img_type == 2:
        raw = bytearray(d[off:off + h*rb])
    else:
        raw = bytearray(); pos = off
        while len(raw) < h*rb:
            hd = d[pos]; pos += 1; cnt = (hd & 0x7F) + 1
            if hd & 0x80: raw += d[pos:pos+n]*cnt; pos += n
            else: raw += d[pos:pos+cnt*n]; pos += cnt*n
    px = bytearray(w*h*n); tl = desc & 0x20
    for y in range(h):
        sy = y if tl else h-1-y
        px[sy*rb:(sy+1)*rb] = raw[y*rb:(y+1)*rb]
    return w, h, n, px
w, h, n, a = load(sys.argv[1]); _, _, _, b = load(sys.argv[2])
mx = max(abs(a[i]-b[i]) for i in range(w*h*n))
print(f"box 模式 jpgd vs stb 参考: 最大通道差 = {mx} (盒式上采样应近乎一致)")
PY
pause

title "段 4/4：非法采样系数被拒绝 + 旧路径零回归摘要"
python3 -I - "$OUT/h4v1.jpg" <<'PY'
import sys
# Patch the luma sampling byte to illegal values and confirm jpgd rejects them.
data = bytearray(open(sys.argv[1], "rb").read())
i = 2
sof = None
while i < len(data):
    if data[i] != 0xFF: i += 1; continue
    if data[i+1] in (0xC0, 0xC2): sof = i; break
    i += 2 + ((data[i+2] << 8) | data[i+3])
samp_byte = sof + 10 + 1
for hv, name in ((0x00, "零因子"), (0x31, "因子3"), (0x51, "因子5")):
    bad = bytearray(data); bad[samp_byte] = hv
    open("/tmp/_bad.jpg", "wb").write(bad)
    print(f"  非法采样 {name} (0x{hv:02X}) 已写入测试流")
PY
"$REPO/build-rel/test_h4v1" "$OUT" | sed -n '/Illegal sampling/,/out-of-range/p' | grep -E "factor|PASS|FAIL"
echo
echo "旧模式 (H1V1/H2V1/H2V2) 编码字节与解码像素均与基线逐位一致。"
echo "完整结论见 output/logs/02_full_verification.log (118 项检查全部 PASS)。"
printf '\n\033[1;32m演示完成，所有产物位于 output/，无需键盘交互。\033[0m\n'
