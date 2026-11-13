// H4V1 (4:1:1) decoder/encoder test harness.
// Build: g++ -I<repo> tests/test_h4v1.cpp jpgd.cpp jpge.cpp -o test_h4v1
//
// Checks:
//   1. H4V1 encode -> decode round trip through both libraries, correct
//      frame sampling factors appear in the emitted SOF marker.
//   2. Illegal sampling factors in a SOF marker are rejected with
//      JPGD_UNSUPPORTED_SAMP_FACTORS (zero factor / factor 3 / factor 5).
//   3. params::check() rejects out-of-range encoder subsampling values.
#include "jpge.h"
#include "jpgd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <vector>

static int g_failures = 0;

#define CHECK(cond, msg) do { \
	if (cond) { printf("  [PASS] %s\n", msg); } \
	else { printf("  [FAIL] %s\n", msg); g_failures++; } \
} while (0)

// Locate the SOF0/SOF2 segment and return a pointer to the first component spec
// (after the 8-byte SOF header: length(2)+precision(1)+h(2)+w(2)+Nf(1)).
static unsigned char* find_sof_components(std::vector<unsigned char>& buf)
{
	for (size_t i = 2; i + 9 < buf.size();)
	{
		if (buf[i] != 0xFF) { i++; continue; }
		unsigned char marker = buf[i + 1];
		if (marker == 0xC0 || marker == 0xC2)
			return &buf[i + 2 + 8];
		unsigned short seg_len = (buf[i + 2] << 8) | buf[i + 3];
		i += 2 + seg_len;
	}
	return NULL;
}

// Patch component `comp` (0..2) sampling byte: high nibble = H factor, low = V factor.
static bool patch_sampling(std::vector<unsigned char> buf_in, int comp, unsigned char hv, std::vector<unsigned char>& out)
{
	out = buf_in;
	unsigned char* specs = find_sof_components(out);
	if (!specs) return false;
	specs[comp * 3 + 1] = hv; // component spec: id(1), sampling(1), quant(1)
	return true;
}

static int try_decode(const std::vector<unsigned char>& jpeg)
{
	int w = 0, h = 0, actual = 0;
	unsigned char* img = jpgd::decompress_jpeg_image_from_memory(
		&jpeg[0], (int)jpeg.size(), &w, &h, &actual, 3);
	free(img);
	if (!img)
	{
		// Re-instantiate the low-level decoder to read back the precise error code.
		jpgd::jpeg_decoder_mem_stream stream(&jpeg[0], (jpgd::uint)jpeg.size());
		jpgd::jpeg_decoder dec(&stream);
		dec.begin_decoding();
		return (int)dec.get_error_code();
	}
	return jpgd::JPGD_SUCCESS;
}

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		printf("usage: %s <output_dir>\n", argv[0]);
		return 2;
	}
	char path[512];

	const int W = 96, H = 64;
	std::vector<unsigned char> rgb(W * H * 3);
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++)
		{
			unsigned char* p = &rgb[(y * W + x) * 3];
			p[0] = (unsigned char)(x * 255 / (W - 1));
			p[1] = (unsigned char)(y * 255 / (H - 1));
			p[2] = (unsigned char)((x + y) * 255 / (W + H - 2));
		}

	printf("== 1. H4V1 encode/decode round trip ==\n");
	std::vector<unsigned char> jpeg(W * H * 3 + 1024);
	int jpeg_size = (int)jpeg.size();
	jpge::params params;
	params.m_quality = 95;
	params.m_subsampling = jpge::H4V1;
	CHECK(params.check(), "jpge::params accepts H4V1");
	bool enc_ok = jpge::compress_image_to_jpeg_file_in_memory(&jpeg[0], jpeg_size, W, H, 3, &rgb[0], params);
	CHECK(enc_ok, "H4V1 compression succeeds");
	jpeg.resize(jpeg_size);

	snprintf(path, sizeof(path), "%s/h4v1_gradient.jpg", argv[1]);
	FILE* f = fopen(path, "wb");
	fwrite(&jpeg[0], 1, jpeg.size(), f);
	fclose(f);

	{
		unsigned char* specs = find_sof_components(jpeg);
		CHECK(specs != NULL, "SOF marker found in H4V1 stream");
		if (specs)
		{
			CHECK(specs[0 * 3 + 1] == 0x41, "Y component sampling is H4V1 (0x41)");
			CHECK(specs[1 * 3 + 1] == 0x11, "Cb component sampling is H1V1 (0x11)");
			CHECK(specs[2 * 3 + 1] == 0x11, "Cr component sampling is H1V1 (0x11)");
		}
	}

	int dw = 0, dh = 0, actual = 0;
	unsigned char* img = jpgd::decompress_jpeg_image_from_memory(&jpeg[0], (int)jpeg.size(), &dw, &dh, &actual, 3);
	CHECK(img != NULL, "jpgd decodes H4V1 stream");
	CHECK(dw == W && dh == H, "decoded dimensions match");
	if (img)
	{
		// High quality 4:1:1 must stay close to the source (luma is full rate).
		double sumsq = 0;
		for (int i = 0; i < W * H * 3; i++)
		{
			double d = (double)img[i] - rgb[i];
			sumsq += d * d;
		}
		double rmse = sqrt(sumsq / (W * H * 3));
		printf("  [info] H4V1 round-trip RMSE: %.3f\n", rmse);
		CHECK(rmse < 6.0, "H4V1 round-trip RMSE within tolerance (<6)");
		free(img);
	}

	printf("== 2. Illegal sampling factors are rejected ==\n");
	struct IllegalCase { int comp; unsigned char hv; const char* desc; };
	IllegalCase cases[] =
	{
		{ 0, 0x00, "zero sampling factors (0x00)" },
		{ 0, 0x31, "factor 3 horizontal (0x31)" },
		{ 0, 0x51, "factor 5 horizontal (0x51)" },
		{ 1, 0x22, "chroma not 1x1 (Cb 0x22)" },
	};
	for (int i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); i++)
	{
		std::vector<unsigned char> bad;
		bool patched = patch_sampling(jpeg, cases[i].comp, cases[i].hv, bad);
		CHECK(patched, "stream patch succeeds");
		if (patched)
		{
			int code = try_decode(bad);
			printf("  [info] '%s' -> error code %d\n", cases[i].desc, code);
			CHECK(code == jpgd::JPGD_UNSUPPORTED_SAMP_FACTORS, cases[i].desc);
		}
	}

	printf("== 3. Encoder params range checks ==\n");
	jpge::params bad_params;
	bad_params.m_subsampling = (jpge::subsampling_t)99;
	CHECK(!bad_params.check(), "out-of-range subsampling rejected by params::check()");

	printf("\n%s: %d failure(s)\n", g_failures ? "FAILED" : "ALL TESTS PASSED", g_failures);
	return g_failures ? 1 : 0;
}
