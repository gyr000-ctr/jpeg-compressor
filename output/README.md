# 4:1:1 (4x1) 色度子采样解码支持 — 验证产物

## 改动摘要（jpgd.cpp / jpgd.h，净 -67 行）

1. **新扫描类型** `JPGD_YH4V1`（4x1:1:1，6 blocks/MCU，MCU 32x8）。
2. **公共重采样层**（既有的 H2V1Convert/H1V2Convert/H2V2Convert/H2V1ConvertFiltered 已删除，改为共享实现）：
   - `HxVConvertBox(h_samp, v_samp)` — box（最近邻）色度上采样，2x1 / 1x2 / 2x2 / 4x1 共用；
   - `HxV1ConvertFiltered(h_samp)` — 三角滤波水平色度上采样，2x1 / 4x1 共用。
3. **非法采样系数报错**：SOF 阶段拒绝 0 或 >4 的系数；`init_frame` 拒绝合法但不支持的组合（3x1、2x4、4x2 等），均返回 `JPGD_UNSUPPORTED_SAMP_FACTORS`。
4. **扫描线缓冲尺寸修正**：按 `max(mcu_x_size, 16)` 对齐（原代码固定 16）。4:1:1 的 MCU 宽 32 像素，宽度非 32 倍数时原公式会导致转换器越界写 64 字节、踩坏后续 MCU 行的系数缓冲（首块系数损坏）。既有比例（MCU ≤16）缓冲尺寸逐字节不变。
5. **4:1:1 过滤模式右边缘**：色度行宽按 `ceil(width/4)` 截断（h=2 保留原有截断行为以保证零回归）。

## 验证（仓库自带入口 `jpge -d`，二进制在 output/bin/）

- **零回归**：`regression_report.txt` — 既有 5 种比例 × 过滤/box × 3 测试图（含奇数尺寸、灰度）共 30 项，baseline 与新版输出 **逐字节一致**。
- **4:1:1 色度还原**：`h411_report.txt` — box 模式与 libjpeg(box) 最大像素误差 ≤3（仅 IDCT 舍入）；过滤模式与 libjpeg(fancy) PSNR 38–43dB（照片）。锐利色边（绿→品红）边缘位置：本解码器 x=64.49，libjpeg x=64.5，真实位置 64 —— 无颜色错位。含奇数宽度（129x67）与渐进式 JPEG。
- **非法输入**：`illegal_report.txt` — h=0/5、v=0/5、3x1、2x4、4x2 全部被拒（退出码非 0、不写输出），未修改的对照样本正常解码。
- 仓库自带 `-x` 全量压缩/解压回归通过（"Success."）。

## 录屏

`demo.typescript` + `demo.timing`（分段标题、段间停顿、无键盘交互）：
`scriptreplay -t output/demo.timing output/demo.typescript` 回放；`output/run_demo.sh` 为被录脚本。

## 复现

```
cmake -S . -B output/build_new -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build output/build_new -j8
bin/jpge -d output/src/photo_4x1.jpg out.tga            # 4:1:1 三角过滤上采样
bin/jpge -d -box_filtering output/src/photo_4x1.jpg out.tga   # box 上采样
```
