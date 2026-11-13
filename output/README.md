# Arithmetic (SOF9) JPEG decoding support — acceptance artifacts

## What changed

`jpgd` now decodes arithmetic-coded sequential JPEG files (SOF9), e.g. scans
produced by cameras/scanners that use arithmetic entropy coding. The
implementation follows ITU-T T.81 Annex D (QM decoder) and F.2.4 / F.1.4.4
(sequential DC/AC decoding with conditioning contexts), and plugs into the
existing sequential decode path — the MCU loop, coefficient dequantization,
IDCT, chroma upsampling and color conversion are shared with the Huffman
path. No separate decode pipeline was added.

Changes (multiple small edits + a shared entropy-symbol layer):

- `jpgd.h`
  - new error codes `JPGD_BAD_DAC_MARKER`, `JPGD_BAD_ARITH_CODE`
  - arithmetic decoder state (QM registers, statistics areas, conditioning
    tables) and method declarations
- `jpgd.cpp`
  - QE probability estimation table (T.81 Table D.2)
  - `arith_get_byte()` / `arith_decode_bit()` — QM decoder core with 0xFF00
    unstuffing and marker detection (markers are pushed back; zero data is
    supplied past a marker, per T.81)
  - `arith_decode_dc()` / `arith_decode_ac_block()` — sequential DC/AC
    coefficient decoding (T.81 F.19–F.24)
  - `read_dac_marker()` — DAC segment parsing with validation
  - `arith_init_scan()` / `arith_restart()` — per-scan and per-restart
    statistics/register resets
  - `process_markers()` accepts DAC; `locate_sof_marker()` accepts SOF9
    (SOF10/11/13/14/15 still return `JPGD_NO_ARITHMITIC_SUPPORT`)
  - `decode_next_row()` — branches only the entropy-symbol source; restart
    handling and `fix_in_buffer()` skip Huffman bit-buffer priming for
    arithmetic scans

## Verification (see acceptance.typescript)

- **[3/9]** All 8 arithmetic test vectors decode via the repo entry point
  (`bin/jpge -d`).
- **[4/9]** Arithmetic vs Huffman twins (same image, same quant/sampling)
  decode **bit-identically** through jpgd — proves the shared
  coefficient/color path.
- **[5/9]** Cross-check against the reference decoder (libjpeg-turbo, C
  ISLOW IDCT, `JSIMD_FORCENONE=1`): max |diff| ≤ 3 (IDCT/fixed-point
  rounding) once chroma upsampling filters are matched.
- **[6/9]** Corrupt/unsupported streams are rejected with error codes:
  bad DAC (`JPGD_BAD_DAC_MARKER`), invalid SOS table selector
  (`JPGD_BAD_ARITH_CODE`), garbage (`JPGD_NOT_JPEG`), progressive
  arithmetic (`JPGD_NO_ARITHMITIC_SUPPORT`). Truncated streams are
  tolerated by design (jpgd pads EOI — same as the Huffman path).
- **[7/9]** 480 randomized corruption variants under ASan: no crashes, no
  sanitizer reports. (The arithmetic path is also UBSan-clean; the
  progressive Huffman path has a pre-existing UBSan report that exists
  identically in the unmodified upstream code.)
- **[8/9]** Zero regression: 13 existing Huffman files (baseline,
  progressive, optimized Huffman, restarts, grayscale, 4:4:4/4:2:2/4:2:0)
  plus `box_filtering` and `no_simd` modes decode **byte-identically**
  with the pre-change binary.

## Files

- `acceptance.typescript`, `acceptance.timing` — terminal recording of the
  acceptance run (titled sections with pauses, no keyboard input).
  Replay: `scriptreplay acceptance.timing acceptance.typescript`
- `summary.txt` — overall result
- `ref_compare.log` — jpgd vs libjpeg-turbo comparison numbers
- `build.log` — build log
- `test_vectors/` — generated JPEG vectors (`*_ari_*` = SOF9 arithmetic,
  `*_huff_*` = Huffman twins/regression vectors)
- `decoded/` — jpgd decode output (TGA) for the arithmetic vectors

Reproduce: `bash work/run_acceptance.sh` (builds everything, regenerates
vectors, exits non-zero on failure).
