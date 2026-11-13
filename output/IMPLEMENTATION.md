# Arithmetic JPEG decoding — implementation and acceptance

## Goal

The bundled `jpgd` decoder rejected arithmetic-coded JPEGs
(`SOF9` / `DAC`) with `JPGD_NO_ARITHMITIC_SUPPORT`. This change adds full
sequential DCT arithmetic decoding (ITU-T T.81 Annex D / F.1 / F.2) while
reusing the exact coefficient, IDCT and color pipeline of the Huffman path.

## Design: one shared pipeline, two entropy back-ends

No second decode pipeline was introduced. A small abstract entropy layer was
extracted instead.

```
decode_next_row()  — MCU scheduling, restart handling, transform_mcu()  [shared, unchanged in shape]
        |
        v
entropy_decoder::decode_block(mcu_block, component_id)   [new common interface]
        |--- huffman_entropy_decoder   (original Huffman block logic, moved verbatim)
        |--- arith_entropy_decoder     (new T.81 Annex D engine)
        |
        v
same dequantized coefficient buffer -> same idct() -> same color conversion
```

Both back-ends write into the same `m_pMCU_coefficients` and set the same
`m_mcu_block_max_zag` hint, so IDCT, upsampling and YCbCr→RGB are byte-for-byte
shared.

## Files

| File | Change |
|---|---|
| `jpgd_entropy.h` | New. `entropy_decoder` interface + both concrete decoder declarations |
| `jpgd_entropy.cpp` | New. Huffman block decoder (moved out of `decode_next_row`) + shared helpers |
| `jpgd_arith.cpp` | New. Arithmetic interval engine, DC/AC statistical models (port of libjpeg `jdarith.c`) |
| `jpgd_arith_tables.inc` | New. Generated T.81 Table D.2 Qe/NLPS/NMPS/SWITCH arrays |
| `jpgd.h` / `jpgd.cpp` | Small additive hooks: mode flag, DAC parse, marker dispatch, shared buffer primitives, restart/EOI handling |
| `CMakeLists.txt` | Adds the two new translation units |

### Marker handling
- `SOF9` (extended sequential, arithmetic) -> enabled.
- `SOF10/11` (progressive arithmetic) -> explicitly rejected with
  `JPGD_UNSUPPORTED_MARKER` (the existing progressive engine is Huffman-only).
- `DAC` (0xCC) parsed per T.81 B.2.4.4; index byte is `(Tc<<4)|Tb`, DC tables
  carry L/U nibbles, AC tables carry K. Malformed values (L>U, K out of 1..63,
  bad class/table) return `JPGD_BAD_DAC_MARKER`.
- Default conditioning matches libjpeg: DC L=0/U=1, AC K=5.

### Corrupt streams
The arithmetic engine distinguishes a genuine in-stream marker from the
synthesized 0xFFD9 end-of-buffer padding via the padding-aware byte reader.
Exhausting the physical stream before the expected coefficients are produced
raises `JPGD_ARITH_DECODE_ERROR` instead of silently decoding the reference
zero fill. Truncated and corrupted streams are therefore rejected.

## Why pixel output is validated against a coefficient twin, not raw libjpeg

`mkref` re-encodes each arithmetic file with libjpeg-turbo's
`jpeg_read_coefficients`/`jpeg_write_coefficients`, producing a Huffman file
with **bit-identical DCT coefficients** — only the entropy coding differs.
Decoding both with the *same* jpgd build must then yield byte-identical pixels.
This isolates entropy-decoder correctness from IDCT implementation differences.

jpgd and libjpeg-turbo use different (both standards-valid) IDCT/color
implementations, so jpgd pixels are not expected to equal libjpeg pixels
exactly. The acceptance check instead proves the arithmetic and Huffman paths
have an **identical per-pixel residual to libjpeg** — verified true on every
case. The same residual difference already exists on the unmodified Huffman
baseline, confirming it is unrelated to this change.

## Verification (repo entry point `jpge`, in-memory harness, libjpeg-turbo reference)

Run: `output/tools/run_tests.sh`

- **Correctness** — 7 fixed images (gray, 444/422/420, restart-interval,
  undersized, tall) + 24 randomized combinations of size/subsampling/restart:
  arithmetic output byte-identical to coefficient-twin Huffman output.
- **Reference** — every arithmetic stream accepted by libjpeg-turbo; residuals
  match the Huffman path exactly.
- **Error rejection** — 4 truncation points, corruption+truncation, invalid
  DAC, SOF10, non-JPEG input all rejected.
- **Zero regression** — Huffman TGAs from the baseline and new binary are
  byte-identical; progressive (SOF2) and baseline (SOF0) Huffman unchanged;
  scalar and SSE2 IDCT modes each match across entropy codings; clean build
  with `-Wall -Wextra` (no warnings).

Result: `ALL CHECKS PASSED`, `OVERALL: ACCEPTANCE PASSED` (67/67 assertions).
