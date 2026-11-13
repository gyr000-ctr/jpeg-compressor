# H4V1 (4:1:1) 色度采样支持 — 验证产物与结论

## 需求与实现对应

| 需求 | 实现 |
|---|---|
| 支持 4:1:1 解码与上采样 | `jpgd.cpp` 新增 `JPGD_YH4V1` 扫描类型、`H4V1Convert`（盒式）/`H4V1ConvertFiltered`（线性）|
| 与现有比例共用重采样逻辑 | 抽出公共层 `resample_chroma_horizontal_box()` / `resample_chroma_pair_horizontal_linear()` / `write_rgba_pixel()`；旧 H2V1 线性路径改为调用公共层，未复制代码 |
| 非法采样系数报错 | SOF 校验上限改为 `JPGD_MAX_SAMPLING_FACTOR=4`；0、3、5 等因子及色度非 1x1 一律 `JPGD_UNSUPPORTED_SAMP_FACTORS`(-231) 拒绝 |
| 既有采样比例输出零变化 | H2V1 线性路径在公共层中以 `h_samp==2` 分支逐位复刻旧实现；A/B 对比证明 H1V1/H2V1/H2V2 全部逐像素一致 |
| 编码侧对称支持（便于产生测试素材）| `jpge.cpp` 新增 `H4V1` 枚举、`load_block_32_8_8()` 4:1 下采样；`tga2jpg.cpp` 增加 `-h4v1` 入口 |

## 公共层设计要点（避免复制路径）

- 盒式上采样 `resample_chroma_horizontal_box()`：按 `x / h_samp` 定位色度样本，h_samp=2/4 同一实现。
- 线性上采样 `resample_chroma_pair_horizontal_linear()`：
  - `h_samp==2`：严格保留旧 H2V1 语义——算术右移（向下取整）、先取邻域再 clamp 的边界顺序、`(x&1)` 三角权重，逐位等价。
  - `h_samp==4`：4 亮度宽色度单元内，中央两列保持本单元样本（精确 hold，无舍入），外缘两列与相邻单元做 1/4..3/4 三角混合。
- 参考解码器说明：仓库自带 stb_image 对非 2x 水平因子只做最近邻（其 `resample_row_generic` 直接复制样本），因此它是**盒式**模式的精确参照；线性模式按 jpgd 既有三角滤波语义，对照源图已知颜色独立验证。

## 目录

- `logs/01_unit_harness.log` — C++ 单元测试（往返、SOF 系数、非法因子拒绝、参数范围）
- `logs/02_full_verification.log` — 端到端验证，**118 项检查全部 PASS，0 FAIL**
- `logs/03_ab_bitexact_legacy.log` — 新旧解码器 A/B 逐像素对比（H1V1/H2V1/H2V2 × 多尺寸 × 线性/盒式/禁SIMD）
- `logs/04_asan_edge_sizes.log` — ASan/UBSan 边缘尺寸（1..127，含非 32 倍数）内存安全
- `recordings/seg1..4.cast` — 四段 asciinema 录屏，各自带标题、段间停顿
- `recordings/full_demo.cast` — 合并版（含 2 秒段间停顿），`asciinema play full_demo.cast` 播放
- `testdata/`、`demo/` — 测试 TGA、H4V1 JPEG、解码输出等中间产物

## 复现实验命令

```bash
# 构建（现代 CMake 需兼容标志，仓库 CMakeLists 本身未改）
cmake -S . -B build-rel -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_BUILD_TYPE=Release
cmake --build build-rel -j

# 生成测试素材
python3 -I tests/gen_testdata.py output/testdata

# 单元 + 端到端 + 内存安全
./build-rel/test_h4v1 output/testdata
python3 -I tests/run_verification.py . /tmp/jpge-baseline/bin/jpge bin/jpge output/testdata output/testdata
./build-rel/asan_test

# 仓库自带入口直接体验
bin/jpge -h4v1 output/testdata/colorbars.tga out.jpg 95      # 编码 4:1:1
bin/jpge -d out.jpg out_lin.tga                              # 线性上采样解码
bin/jpge -d -box_filtering out.jpg out_box.tga               # 盒式上采样解码
```

## 已知边界

- 仓库内无 progressive 编码器，无法生成真实的 4:1:1 progressive 素材做端到端实测；progressive 的块编排/系数缓冲完全由采样因子表驱动（已支持因子 4，每 MCU 6 块 ≤ 上限 10），baseline sequential 路径与全部边缘尺寸已实测覆盖。
- CMake 3.27+ 需 `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` 才能配置旧 `cmake_minimum_required(3.0)`，属环境兼容问题，未修改仓库构建脚本。
