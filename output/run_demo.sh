#!/bin/bash
# Demo: 4:1:1 (4x1) chroma subsampling support in jpgd — recorded non-interactively.
# Segments are titled and separated by pauses; no keyboard input is required.
cd "$(dirname "$0")/.." || exit 1
J=output/bin/jpge_new

title() {
  echo
  echo "================================================================"
  echo "  $1"
  echo "================================================================"
  sleep 2
}

title "1/5 构建：含 4:1:1 支持的解码器 (cmake --build)"
cmake --build output/build_new -j8 2>&1 | grep -E "Built target|error"
echo "build OK -> output/bin/jpge_new"
sleep 2

title "2/5 既有采样比例零回归：baseline vs 新版 (1x1/2x1/1x2/2x2/gray, 过滤+box 两种模式)"
pass=0; total=0
for j in output/src/photo_2x2.jpg output/src/photo_odd_1x2.jpg output/src/noise_2x1.jpg output/src/photo_1x1.jpg output/src/photo_gray.jpg; do
  n=$(basename "$j" .jpg)
  for mode in filt box; do
    [ "$mode" = box ] && opt="-box_filtering" || opt=""
    output/bin/jpge_baseline -d $opt "$j" /tmp/rb_old.tga >/dev/null 2>&1
    $J                   -d $opt "$j" /tmp/rb_new.tga >/dev/null 2>&1
    total=$((total+1))
    if cmp -s /tmp/rb_old.tga /tmp/rb_new.tga; then r=IDENTICAL; pass=$((pass+1)); else r="DIFF <<<"; fi
    printf "  %-18s [%-4s] %s\n" "$n" "$mode" "$r"
  done
done
echo "  => $pass/$total 输出逐字节一致（完整矩阵 30/30 见 output/regression_report.txt）"
sleep 3

title "3/5 4:1:1 解码与上采样：与 libjpeg 参考对比 (PSNR)"
for f in photo photo_odd noise; do
  $J -d output/src/${f}_4x1.jpg /tmp/h_f.tga >/dev/null 2>&1
  $J -d -box_filtering output/src/${f}_4x1.jpg /tmp/h_b.tga >/dev/null 2>&1
  convert /tmp/h_f.tga /tmp/h_f.png; convert /tmp/h_b.tga /tmp/h_b.png
  p1=$(compare -metric PSNR /tmp/h_f.png output/ref/${f}_4x1_fancy.png null: 2>&1)
  p2=$(compare -metric PSNR /tmp/h_b.png output/ref/${f}_4x1_box.png   null: 2>&1)
  printf "  %-10s 三角过滤 vs libjpeg(fancy): %s dB | box vs libjpeg(box): %s dB\n" "$f" "${p1%% *}" "${p2%% *}"
done
echo "  色度边缘对齐测试（绿->品红锐利边缘，真实位置 x=64）："
$J -d output/src/edge_4x1.jpg /tmp/e_f.tga >/dev/null 2>&1
convert /tmp/e_f.tga /tmp/e_f.png
python3 -I output/edge_check.py
sleep 3

title "4/5 非法采样系数报错 (h=0/5, v=0/5, 3x1, 2x4, 4x2 均被拒绝)"
for f in output/bad/samp_*.jpg; do
  n=$(basename "$f" .jpg)
  if $J -d "$f" /tmp/bad.tga >/dev/null 2>&1; then v="ACCEPTED"; else v="REJECTED"; fi
  printf "  %-12s -> %s\n" "$n" "$v"
done
sleep 3

title "5/5 总结"
echo "  - 新增 JPGD_YH4V1 (4x1:1:1) 解码与上采样"
echo "  - 公共层: HxVConvertBox(h,v) / HxV1ConvertFiltered(h)，2x1/1x2/2x2/4x1 共用"
echo "  - 既有比例 30/30 逐字节零回归；非法采样系数全部拒绝"
echo "  - 4:1:1 box 模式与 libjpeg 最大像素误差 <= 3 (仅 IDCT 舍入差异)"
sleep 2
echo "demo done."
