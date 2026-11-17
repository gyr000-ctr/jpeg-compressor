// codec_test.cpp - Round-trip tests for jpge (encoder) and jpgd (decoder).
//
// Usage:
//   codec_test            - basic encode/decode sanity check
//   codec_test --restart  - restart marker (DRI/RSTn) test suite
//
// The --restart suite verifies:
//   * interval 0 (default): no DRI/RSTn in the stream, bitstream identical to
//     explicitly setting m_restart_interval = 0
//   * a normal interval: DRI present, expected number of RSTn markers, RST0-7
//     cycling, decoded pixels bit-identical to the no-restart encode
//   * interval 1 (restart before every MCU)
//   * a huge interval (65535): DRI present but zero RSTn markers
//   * intervals that don't divide the MCU count, and image dimensions that
//     aren't multiples of the MCU size
//   * grayscale, H1V1, H2V1, H2V2 subsampling, and two-pass mode
//   * params::check() rejects out-of-range intervals

#include "jpge.h"
#include "jpgd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond) do { \
		g_checks++; \
		if (!(cond)) { g_failures++; printf("  FAIL: %s (line %d)\n", #cond, __LINE__); } \
	} while (0)

// Deterministic test image: gradient + LCG noise, so it's non-trivial to compress.
static void gen_image(std::vector<unsigned char>& img, int w, int h, int channels, unsigned int seed)
{
	img.resize((size_t)w * h * channels);
	unsigned int s = seed;
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
		{
			s = s * 1664525u + 1013904223u;
			for (int c = 0; c < channels; c++)
			{
				int grad = (x * 255) / (w > 1 ? w - 1 : 1);
				int noise = (int)((s >> (8 + c * 5)) & 0x3F);
				img[((size_t)y * w + x) * channels + c] = (unsigned char)((grad + noise + y * 3 + c * 17) & 0xFF);
			}
		}
}

static bool encode_jpeg(const unsigned char* pImg, int w, int h, int channels, const jpge::params& p, std::vector<unsigned char>& out)
{
	int buf_size = w * h * channels + 65536;
	out.resize(buf_size);
	if (!jpge::compress_image_to_jpeg_file_in_memory(&out[0], buf_size, w, h, channels, pImg, p))
		return false;
	out.resize(buf_size);
	return true;
}

static bool decode_jpeg(const unsigned char* pJpg, int jpg_size, int req_comps, std::vector<unsigned char>& out, int& w, int& h)
{
	int actual_comps = 0;
	unsigned char* p = jpgd::decompress_jpeg_image_from_memory(pJpg, jpg_size, &w, &h, &actual_comps, req_comps);
	if (!p)
		return false;
	out.assign(p, p + (size_t)w * h * req_comps);
	free(p);
	return true;
}

struct marker_info
{
	bool has_dri;
	int dri_interval;
	int rst_count;
	bool rst_sequence_ok; // RST0..RST7 cycling, starting at RST0

	marker_info() : has_dri(false), dri_interval(0), rst_count(0), rst_sequence_ok(true) { }
};

// Scans the JPEG stream for DRI and RSTn markers, skipping stuffed 0xFF00 bytes.
static marker_info scan_markers(const std::vector<unsigned char>& jpg)
{
	marker_info mi;
	int expected_rst = 0;
	size_t i = 0;
	const size_t n = jpg.size();
	while (i + 1 < n)
	{
		if (jpg[i] != 0xFF) { i++; continue; }          // entropy-coded data byte
		const unsigned char m = jpg[i + 1];
		if (m == 0x00) { i += 2; continue; }            // stuffed 0xFF
		if (m == 0xFF) { i++; continue; }               // fill byte
		if ((m >= 0xD0) && (m <= 0xD7))                 // RSTn
		{
			mi.rst_count++;
			if (m != (unsigned char)(0xD0 + expected_rst))
				mi.rst_sequence_ok = false;
			expected_rst = (expected_rst + 1) & 7;
			i += 2;
			continue;
		}
		if (m == 0xD8) { i += 2; continue; }            // SOI
		if (m == 0x01) { i += 2; continue; }            // TEM
		if (m == 0xD9) break;                           // EOI
		// All remaining markers carry a length field.
		if (i + 3 >= n) break;
		const int len = (jpg[i + 2] << 8) | jpg[i + 3];
		if (len < 2) break;
		if ((m == 0xDD) && (len == 4) && (i + 5 < n))   // DRI
		{
			mi.has_dri = true;
			mi.dri_interval = (jpg[i + 4] << 8) | jpg[i + 5];
		}
		i += 2 + len;
		if (m == 0xDA)                                  // SOS: entropy-coded data follows
		{
			// Continue scanning; the byte-wise loop above handles the entropy data.
		}
	}
	return mi;
}

static void mcu_dims(jpge::subsampling_t ss, int& mcu_w, int& mcu_h)
{
	switch (ss)
	{
	case jpge::Y_ONLY:
	case jpge::H1V1: mcu_w = 8; mcu_h = 8; break;
	case jpge::H2V1: mcu_w = 16; mcu_h = 8; break;
	default:         mcu_w = 16; mcu_h = 16; break;
	}
}

static int total_mcus(int w, int h, jpge::subsampling_t ss)
{
	int mcu_w, mcu_h;
	mcu_dims(ss, mcu_w, mcu_h);
	return ((w + mcu_w - 1) / mcu_w) * ((h + mcu_h - 1) / mcu_h);
}

static int expected_rst_count(int w, int h, jpge::subsampling_t ss, int interval)
{
	if (!interval) return 0;
	const int total = total_mcus(w, h, ss);
	return (total - 1) / interval;
}

// Encodes img with the given params, then checks the stream against expectations
// and verifies jpgd decodes it to pixels identical to the interval-0 encode.
static void test_restart_case(const char* name, int w, int h, int channels, jpge::subsampling_t ss, bool two_pass, int interval)
{
	printf("case %-40s ", name);

	std::vector<unsigned char> img;
	gen_image(img, w, h, channels, 0x1234567u + (unsigned)interval);

	jpge::params base;
	base.m_quality = 90;
	base.m_subsampling = ss;
	base.m_two_pass_flag = two_pass;

	jpge::params p = base;
	p.m_restart_interval = interval;

	std::vector<unsigned char> jpg_base, jpg_rst;
	CHECK(encode_jpeg(&img[0], w, h, channels, base, jpg_base));
	CHECK(encode_jpeg(&img[0], w, h, channels, p, jpg_rst));

	const marker_info mi = scan_markers(jpg_rst);
	if (interval == 0)
	{
		CHECK(!mi.has_dri);
		CHECK(mi.rst_count == 0);
		CHECK(jpg_rst == jpg_base); // disabled: bitstream must be unchanged
	}
	else
	{
		CHECK(mi.has_dri);
		CHECK(mi.dri_interval == interval);
		CHECK(mi.rst_count == expected_rst_count(w, h, ss, interval));
		CHECK(mi.rst_sequence_ok);
	}

	// Both streams must decode, and to identical pixels: restart markers only
	// change how the same coefficients are coded.
	std::vector<unsigned char> dec_base, dec_rst;
	int bw = 0, bh = 0, rw = 0, rh = 0;
	CHECK(decode_jpeg(&jpg_base[0], (int)jpg_base.size(), channels, dec_base, bw, bh));
	CHECK(decode_jpeg(&jpg_rst[0], (int)jpg_rst.size(), channels, dec_rst, rw, rh));
	CHECK(bw == w && bh == h && rw == w && rh == h);
	CHECK(dec_base == dec_rst);

	printf(g_failures ? "FAILED\n" : "ok (jpg %zu bytes, %d RSTn)\n", jpg_rst.size(), mi.rst_count);
}

static int run_restart_tests()
{
	printf("restart marker test suite\n");

	// Interval 0 (default) vs. explicit 0: no DRI/RSTn, identical bitstreams.
	test_restart_case("interval=0 (disabled)", 96, 80, 3, jpge::H2V2, false, 0);

	// Normal intervals, all subsampling factors.
	test_restart_case("H2V2 interval=4", 96, 80, 3, jpge::H2V2, false, 4);
	test_restart_case("H2V2 interval=1 (every MCU)", 96, 80, 3, jpge::H2V2, false, 1);
	test_restart_case("H1V1 interval=5", 48, 48, 3, jpge::H1V1, false, 5);
	test_restart_case("H2V1 interval=4", 64, 32, 3, jpge::H2V1, false, 4);
	test_restart_case("grayscale interval=3", 64, 48, 1, jpge::Y_ONLY, false, 3);

	// Huge interval: DRI is written, but no RSTn ever appears.
	test_restart_case("interval=65535 (huge)", 96, 80, 3, jpge::H2V2, false, 65535);

	// Interval doesn't divide the MCU count; image not a multiple of MCU size.
	test_restart_case("interval=7, 30 MCUs (uneven)", 96, 80, 3, jpge::H2V2, false, 7);
	test_restart_case("interval=6, 100x76 (odd size)", 100, 76, 3, jpge::H2V2, false, 6);

	// Two-pass (optimized Huffman) mode.
	test_restart_case("two-pass interval=4", 96, 80, 3, jpge::H2V2, true, 4);
	test_restart_case("two-pass interval=0", 96, 80, 3, jpge::H2V2, true, 0);

	// Out-of-range intervals must be rejected.
	{
		printf("case %-40s ", "interval range validation");
		jpge::params p;
		p.m_restart_interval = -1;
		CHECK(!p.check());
		p.m_restart_interval = 65536;
		CHECK(!p.check());
		p.m_restart_interval = 65535;
		CHECK(p.check());
		p.m_restart_interval = 0;
		CHECK(p.check());
		printf(g_failures ? "FAILED\n" : "ok\n");
	}

	printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}

static int run_basic_tests()
{
	printf("basic round-trip sanity\n");

	std::vector<unsigned char> img;
	gen_image(img, 64, 64, 3, 0xABCDEFu);

	std::vector<unsigned char> jpg;
	CHECK(encode_jpeg(&img[0], 64, 64, 3, jpge::params(), jpg));

	const marker_info mi = scan_markers(jpg);
	CHECK(!mi.has_dri);
	CHECK(mi.rst_count == 0);

	std::vector<unsigned char> dec;
	int w = 0, h = 0;
	CHECK(decode_jpeg(&jpg[0], (int)jpg.size(), 3, dec, w, h));
	CHECK(w == 64 && h == 64);
	CHECK(dec.size() == 64 * 64 * 3);

	printf("%d checks, %d failures\n", g_checks, g_failures);
	printf("(run \"codec_test --restart\" for the restart marker suite)\n");
	return g_failures ? 1 : 0;
}

int main(int argc, char** argv)
{
	if ((argc > 1) && !strcmp(argv[1], "--restart"))
		return run_restart_tests();
	return run_basic_tests();
}
