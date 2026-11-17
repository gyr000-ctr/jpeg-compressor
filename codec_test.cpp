// codec_test.cpp - Round-trip tests for jpge (encoder) and jpgd (decoder).
//
// Usage:
//   codec_test            - basic encode/decode sanity check
//   codec_test --exif     - EXIF APP1 (Orientation) write/read test suite
//
// The --exif suite verifies:
//   * no EXIF requested: output is byte-for-byte identical to the previous
//     encoder (golden FNV-1a hashes computed from the pristine sources)
//   * Orientation 1-8: APP1 is present, its segment length is self-consistent,
//     the TIFF header is little-endian "II*\0" with IFD0 at offset 8, and the
//     single IFD entry is tag 0x0112 (SHORT, count 1) with the requested value
//   * raw EXIF payloads are written verbatim, in both II and MM byte order
//   * read_exif() recovers the Orientation and byte order, and reports an
//     absent Orientation tag as orientation == 0
//   * oversized EXIF, duplicate Exif APP1 segments, an illegal TIFF header,
//     a bad IFD offset, an out-of-range Orientation and conflicting options
//     each return a distinct exif_status (distinct names as well)
//   * jpgd still decodes EXIF-bearing JPEGs to the same pixels as the
//     EXIF-free encode

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

typedef unsigned char u8;

// Deterministic test image: gradient + LCG noise, so it's non-trivial to compress.
static void gen_image(std::vector<u8>& img, int w, int h, int channels, unsigned int seed)
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
				img[((size_t)y * w + x) * channels + c] = (u8)((grad + noise + y * 3 + c * 17) & 0xFF);
			}
		}
}

static bool encode_jpeg(const u8* pImg, int w, int h, int channels, const jpge::params& p, std::vector<u8>& out)
{
	int buf_size = w * h * channels + 131072;
	out.resize(buf_size);
	if (!jpge::compress_image_to_jpeg_file_in_memory(&out[0], buf_size, w, h, channels, pImg, p))
		return false;
	out.resize(buf_size);
	return true;
}

static bool decode_jpeg(const u8* pJpg, int jpg_size, int req_comps, std::vector<u8>& out, int& w, int& h)
{
	int actual_comps = 0;
	u8* p = jpgd::decompress_jpeg_image_from_memory(pJpg, jpg_size, &w, &h, &actual_comps, req_comps);
	if (!p)
		return false;
	out.assign(p, p + (size_t)w * h * req_comps);
	free(p);
	return true;
}

static unsigned long long fnv1a(const std::vector<u8>& v)
{
	unsigned long long h = 1469598103934665603ULL;
	for (size_t i = 0; i < v.size(); i++) { h ^= v[i]; h *= 1099511628211ULL; }
	return h;
}

struct jpeg_seg
{
	unsigned marker;
	size_t hdr_ofs;      // offset of the 0xFF marker byte
	size_t payload_ofs;  // first byte after the 2-byte length
	size_t payload_len;  // length field minus 2
};

// Walks the marker chain up to SOS/EOI. A desynchronized or overlong segment
// makes this return false, so a successful walk proves every length is
// self-consistent (each segment advances exactly 2 + len bytes).
static bool scan_segments(const std::vector<u8>& jpg, std::vector<jpeg_seg>& segs)
{
	segs.clear();
	if ((jpg.size() < 2) || (jpg[0] != 0xFF) || (jpg[1] != 0xD8))
		return false;

	size_t i = 2;
	while (i + 1 < jpg.size())
	{
		if (jpg[i] != 0xFF)
			return false;
		const unsigned m = jpg[i + 1];
		if (m == 0xFF) { i++; continue; }
		if ((m == 0xD9) || (m == 0xDA))
			return true; // EOI or SOS reached
		if ((m == 0x01) || ((m >= 0xD0) && (m <= 0xD7))) { i += 2; continue; }
		if (i + 3 >= jpg.size())
			return false;
		const size_t len = ((size_t)jpg[i + 2] << 8) | jpg[i + 3];
		if ((len < 2) || (len > (jpg.size() - i - 2)))
			return false;
		jpeg_seg s;
		s.marker = m;
		s.hdr_ofs = i;
		s.payload_ofs = i + 4;
		s.payload_len = len - 2;
		segs.push_back(s);
		i += 2 + len;
	}
	return false;
}

static bool find_app1(const std::vector<u8>& jpg, jpeg_seg& app1)
{
	std::vector<jpeg_seg> segs;
	if (!scan_segments(jpg, segs))
		return false;
	for (size_t i = 0; i < segs.size(); i++)
		if (segs[i].marker == 0xE1) { app1 = segs[i]; return true; }
	return false;
}

// Builds a valid raw EXIF payload. Byte order is controlled by big_endian;
// orientation == 0 omits the Orientation entry. extra_bytes appends trailing
// data (e.g. a thumbnail or a second IFD) to prove the payload is verbatim.
static void build_exif_payload(std::vector<u8>& out, bool big_endian, int orientation, size_t extra_bytes = 0)
{
	out.clear();
	const char* prefix = "Exif\0\0";
	out.insert(out.end(), prefix, prefix + 6);
	out.push_back(big_endian ? 'M' : 'I');
	out.push_back(big_endian ? 'M' : 'I');
	if (big_endian)
	{
		const u8 hdr[] = { 0x00, 0x2A, 0x00, 0x00, 0x00, 0x08 };
		out.insert(out.end(), hdr, hdr + sizeof(hdr));
	}
	else
	{
		const u8 hdr[] = { 0x2A, 0x00, 0x08, 0x00, 0x00, 0x00 };
		out.insert(out.end(), hdr, hdr + sizeof(hdr));
	}

	const unsigned entries = orientation ? 1 : 0;
	if (big_endian) { out.push_back((u8)(entries >> 8)); out.push_back((u8)entries); }
	else            { out.push_back((u8)entries); out.push_back((u8)(entries >> 8)); }

	if (orientation)
	{
		if (big_endian)
		{
			const u8 e[] = { 0x01, 0x12, 0x00, 0x03, 0x00, 0x00, 0x00, 0x01,
			                 0x00, (u8)orientation, 0x00, 0x00 };
			out.insert(out.end(), e, e + sizeof(e));
		}
		else
		{
			const u8 e[] = { 0x12, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00,
			                 (u8)orientation, 0x00, 0x00, 0x00 };
			out.insert(out.end(), e, e + sizeof(e));
		}
	}

	// Next-IFD offset = 0.
	out.insert(out.end(), 4, 0);
	out.insert(out.end(), extra_bytes, 0x5A);
}

// ---------------------------------------------------------------------------
// Golden hashes from the pristine (pre-EXIF) encoder. Any EXIF-free encode of
// the same input with the same params must reproduce these exactly.
// ---------------------------------------------------------------------------

struct golden_case
{
	const char* name;
	int w, h, channels;
	unsigned int seed;
	int quality;
	jpge::subsampling_t subsampling;
	bool two_pass;
	bool std_tables;
	unsigned long long fnv;
};

static const golden_case s_golden[] =
{
	{ "default",     64, 64, 3, 0xABCDEFu, 90, jpge::H2V2,  false, false, 0x3762ebed5a1222d7ULL },
	{ "H1V1 2-pass", 96, 80, 3, 0x1234567u, 75, jpge::H1V1, true,  false, 0xe25a40254feaa5bbULL },
	{ "grayscale",   64, 48, 1, 0x2468ACu, 85, jpge::Y_ONLY, false, false, 0x75bcfbfd660b2ecaULL },
	{ "H2V1 std",   100, 76, 3, 0xDEADBEEu, 60, jpge::H2V1,  false, true,  0x0708775d30ddeb00ULL }
};

static void test_no_exif_is_byte_identical()
{
	printf("case %-46s ", "no EXIF -> unchanged bitstream");
	for (size_t i = 0; i < sizeof(s_golden) / sizeof(s_golden[0]); i++)
	{
		const golden_case& c = s_golden[i];
		std::vector<u8> img, jpg;
		gen_image(img, c.w, c.h, c.channels, c.seed);
		jpge::params p;
		p.m_quality = c.quality;
		p.m_subsampling = c.subsampling;
		p.m_two_pass_flag = c.two_pass;
		p.m_use_std_tables = c.std_tables;
		CHECK(p.check_exif() == jpge::EXIF_OK);
		CHECK(encode_jpeg(&img[0], c.w, c.h, c.channels, p, jpg));
		CHECK(fnv1a(jpg) == c.fnv);
		jpeg_seg app1;
		CHECK(!find_app1(jpg, app1));
	}
	printf(g_failures ? "FAILED\n" : "ok\n");
}

static void test_generated_orientation()
{
	printf("case %-46s ", "generated Orientation 1..8");

	for (int orientation = 1; orientation <= 8; orientation++)
	{
		std::vector<u8> img, jpg_ref, jpg;
		gen_image(img, 64, 64, 3, 0xABCDEFu);

		jpge::params p_ref;
		p_ref.m_quality = 90;
		CHECK(encode_jpeg(&img[0], 64, 64, 3, p_ref, jpg_ref));

		jpge::params p;
		p.m_quality = 90;
		p.m_exif_orientation = orientation;
		CHECK(p.check_exif() == jpge::EXIF_OK);
		CHECK(encode_jpeg(&img[0], 64, 64, 3, p, jpg));

		// APP1 must be present and its length field self-consistent.
		jpeg_seg app1;
		CHECK(find_app1(jpg, app1));
		CHECK(app1.payload_len == 32);
		CHECK(app1.hdr_ofs >= 4);

		// Exact payload bytes: "Exif\0\0", II, 42, IFD0 @8, 1 entry,
		// tag 0x0112, type 3, count 1, value = orientation, next IFD 0.
		const u8 expected[32] =
		{
			'E', 'x', 'i', 'f', 0, 0,
			'I', 'I', 0x2A, 0x00, 0x08, 0x00, 0x00, 0x00,
			0x01, 0x00,
			0x12, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00,
			(u8)orientation, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00
		};
		CHECK(memcmp(&jpg[app1.payload_ofs], expected, sizeof(expected)) == 0);

		// read_exif recovers it, with a matching length field.
		jpge::exif_info info;
		CHECK(jpge::read_exif(&jpg[0], (jpge::uint)jpg.size(), info) == jpge::EXIF_OK);
		CHECK(info.orientation == orientation);
		CHECK(!info.big_endian);
		CHECK(info.segment_length == 34);
		CHECK(info.segment_length == app1.payload_len + 2);
		CHECK(info.payload_size == 32);

		// jpgd must still decode the stream to the same pixels (it skips APP1).
		std::vector<u8> dec_ref, dec;
		int rw = 0, rh = 0, w = 0, h = 0;
		CHECK(decode_jpeg(&jpg_ref[0], (int)jpg_ref.size(), 3, dec_ref, rw, rh));
		CHECK(decode_jpeg(&jpg[0], (int)jpg.size(), 3, dec, w, h));
		CHECK(rw == w && rh == h);
		CHECK(dec == dec_ref);
	}

	printf(g_failures ? "FAILED\n" : "ok\n");
}

static void test_raw_payloads()
{
	printf("case %-46s ", "raw EXIF payloads (II / MM, verbatim)");

	for (int endian = 0; endian < 2; endian++)
	{
		const bool big_endian = (endian != 0);
		std::vector<u8> img, payload, jpg;
		gen_image(img, 64, 64, 3, 0xABCDEFu);

		build_exif_payload(payload, big_endian, 6, 24);

		jpge::params p;
		p.m_quality = 90;
		p.m_pExif_data = &payload[0];
		p.m_exif_data_size = (jpge::uint)payload.size();
		CHECK(p.check_exif() == jpge::EXIF_OK);
		CHECK(encode_jpeg(&img[0], 64, 64, 3, p, jpg));

		jpeg_seg app1;
		CHECK(find_app1(jpg, app1));
		CHECK(app1.payload_len == payload.size());
		// Written verbatim, including the trailing bytes.
		CHECK(memcmp(&jpg[app1.payload_ofs], &payload[0], payload.size()) == 0);

		jpge::exif_info info;
		CHECK(jpge::read_exif(&jpg[0], (jpge::uint)jpg.size(), info) == jpge::EXIF_OK);
		CHECK(info.orientation == 6);
		CHECK(info.big_endian == big_endian);
		CHECK(info.segment_length == payload.size() + 2);
		CHECK(info.payload_size == payload.size());
	}

	// An EXIF block without an Orientation tag parses but reports 0.
	{
		std::vector<u8> img, payload, jpg;
		gen_image(img, 32, 32, 3, 0x13579u);
		build_exif_payload(payload, false, 0);
		jpge::params p;
		p.m_pExif_data = &payload[0];
		p.m_exif_data_size = (jpge::uint)payload.size();
		CHECK(p.check_exif() == jpge::EXIF_OK);
		CHECK(encode_jpeg(&img[0], 32, 32, 3, p, jpg));
		jpge::exif_info info;
		CHECK(jpge::read_exif(&jpg[0], (jpge::uint)jpg.size(), info) == jpge::EXIF_OK);
		CHECK(info.orientation == 0);
	}

	// No EXIF at all is reported distinctly.
	{
		std::vector<u8> img, jpg;
		gen_image(img, 32, 32, 3, 0x13579u);
		jpge::params p;
		CHECK(encode_jpeg(&img[0], 32, 32, 3, p, jpg));
		jpge::exif_info info;
		CHECK(jpge::read_exif(&jpg[0], (jpge::uint)jpg.size(), info) == jpge::EXIF_ERR_NOT_FOUND);
	}

	printf(g_failures ? "FAILED\n" : "ok\n");
}

// Inserts a copy of the first Exif APP1 segment immediately after it.
static std::vector<u8> duplicate_app1(std::vector<u8> jpg)
{
	jpeg_seg app1;
	if (!find_app1(jpg, app1))
		return jpg;
	const size_t begin = app1.hdr_ofs;
	const size_t end = app1.payload_ofs + app1.payload_len;
	jpg.insert(jpg.begin() + (long)end, jpg.begin() + (long)begin, jpg.begin() + (long)end);
	return jpg;
}

// Replaces the payload of the first Exif APP1 segment with a different block,
// fixing up the length field. Used to build malformed-but-walkable streams.
static std::vector<u8> replace_app1_payload(const std::vector<u8>& jpg, const std::vector<u8>& payload)
{
	jpeg_seg app1;
	if (!find_app1(jpg, app1))
		return jpg;
	std::vector<u8> out(jpg.begin(), jpg.begin() + (long)app1.hdr_ofs);
	const size_t new_len = payload.size() + 2;
	out.push_back(0xFF);
	out.push_back(0xE1);
	out.push_back((u8)(new_len >> 8));
	out.push_back((u8)(new_len & 0xFF));
	out.insert(out.end(), payload.begin(), payload.end());
	out.insert(out.end(), jpg.begin() + (long)(app1.payload_ofs + app1.payload_len), jpg.end());
	return out;
}

static void test_error_classes()
{
	printf("case %-46s ", "distinct EXIF error classes");

	std::vector<u8> img, base_jpg, carrier_jpg;
	gen_image(img, 48, 48, 3, 0x0BADF00Du);

	jpge::params base_p;
	CHECK(encode_jpeg(&img[0], 48, 48, 3, base_p, base_jpg));

	// Carrier stream with one valid Exif APP1 (Orientation 6), used as the
	// starting point for malformed-segment cases.
	{
		jpge::params p;
		p.m_exif_orientation = 6;
		CHECK(encode_jpeg(&img[0], 48, 48, 3, p, carrier_jpg));
	}

	jpge::exif_info info;
	std::vector<u8> payload;

	// (a) Oversized requested EXIF: rejected before any bytes are written.
	{
		build_exif_payload(payload, false, 3);
		payload.resize(jpge::EXIF_MAX_APP1_PAYLOAD + 1, 0);
		jpge::params p;
		p.m_pExif_data = &payload[0];
		p.m_exif_data_size = (jpge::uint)payload.size();
		CHECK(p.check_exif() == jpge::EXIF_ERR_TOO_LARGE);
		CHECK(!p.check());
		std::vector<u8> jpg;
		CHECK(!encode_jpeg(&img[0], 48, 48, 3, p, jpg));
	}

	// Largest legal payload (segment length 65535) is accepted, and the
	// emitted length field stays within the 16-bit limit.
	{
		build_exif_payload(payload, false, 4);
		payload.resize(jpge::EXIF_MAX_APP1_PAYLOAD, 0);
		jpge::params p;
		p.m_pExif_data = &payload[0];
		p.m_exif_data_size = (jpge::uint)payload.size();
		CHECK(p.check_exif() == jpge::EXIF_OK);
		std::vector<u8> jpg;
		CHECK(encode_jpeg(&img[0], 48, 48, 3, p, jpg));
		CHECK(jpge::read_exif(&jpg[0], (jpge::uint)jpg.size(), info) == jpge::EXIF_OK);
		CHECK(info.orientation == 4);
		CHECK(info.segment_length == 65535);
		CHECK(info.payload_size == jpge::EXIF_MAX_APP1_PAYLOAD);
	}

	// (b) Duplicate Exif APP1 segments.
	{
		const std::vector<u8> dup = duplicate_app1(carrier_jpg);
		CHECK(jpge::read_exif(&dup[0], (jpge::uint)dup.size(), info) == jpge::EXIF_ERR_DUPLICATE_APP1);
	}

	// (c) Illegal TIFF header: wrong magic, wrong byte-order tag, bad IFD offset.
	{
		std::vector<u8> bad(32, 0);
		memcpy(&bad[0], "Exif\0\0", 6);
		memcpy(&bad[6], "II", 2);
		bad[8] = 0x2B; // magic 43, must be 42
		jpge::params p;
		p.m_pExif_data = &bad[0];
		p.m_exif_data_size = (jpge::uint)bad.size();
		CHECK(p.check_exif() == jpge::EXIF_ERR_BAD_TIFF_HEADER);

		const std::vector<u8> jpg_bad_magic = replace_app1_payload(carrier_jpg, bad);
		CHECK(jpge::read_exif(&jpg_bad_magic[0], (jpge::uint)jpg_bad_magic.size(), info) == jpge::EXIF_ERR_BAD_TIFF_HEADER);

		// Wrong byte-order tag.
		std::vector<u8> bad_order = bad;
		bad_order[6] = 'X'; bad_order[7] = 'X';
		bad_order[8] = 0x00; bad_order[9] = 0x2A;
		p.m_pExif_data = &bad_order[0];
		CHECK(p.check_exif() == jpge::EXIF_ERR_BAD_TIFF_HEADER);

		// Valid header, but IFD0 offset points outside the payload.
		build_exif_payload(payload, false, 1);
		payload[10] = 0xFF; payload[11] = 0x00; payload[12] = 0x00; payload[13] = 0x00; // offset 255
		p.m_pExif_data = &payload[0];
		p.m_exif_data_size = (jpge::uint)payload.size();
		CHECK(p.check_exif() == jpge::EXIF_ERR_BAD_IFD);
		const std::vector<u8> jpg_bad_ifd = replace_app1_payload(carrier_jpg, payload);
		CHECK(jpge::read_exif(&jpg_bad_ifd[0], (jpge::uint)jpg_bad_ifd.size(), info) == jpge::EXIF_ERR_BAD_IFD);
	}

	// (d) Orientation out of range: encoder option and a raw tag value of 0.
	{
		jpge::params p;
		p.m_exif_orientation = 9;
		CHECK(p.check_exif() == jpge::EXIF_ERR_ORIENTATION_RANGE);
		CHECK(!p.check());

		build_exif_payload(payload, false, 1);
		payload[24] = 0; // Orientation value field -> 0
		const std::vector<u8> jpg = replace_app1_payload(carrier_jpg, payload);
		CHECK(jpge::read_exif(&jpg[0], (jpge::uint)jpg.size(), info) == jpge::EXIF_ERR_ORIENTATION_RANGE);
	}

	// (e) Conflicting options: generated orientation + raw payload.
	{
		build_exif_payload(payload, false, 3);
		jpge::params p;
		p.m_exif_orientation = 3;
		p.m_pExif_data = &payload[0];
		p.m_exif_data_size = (jpge::uint)payload.size();
		CHECK(p.check_exif() == jpge::EXIF_ERR_CONFLICT);
	}

	// (f) A malformed 16-bit length field in the stream.
	{
		std::vector<u8> jpg = base_jpg;
		const u8 app1[] = { 0xFF, 0xE1, 0xFF, 0xFF, 'E', 'x', 'i', 'f', 0, 0 };
		jpg.insert(jpg.begin() + 2, app1, app1 + sizeof(app1));
		CHECK(jpge::read_exif(&jpg[0], (jpge::uint)jpg.size(), info) == jpge::EXIF_ERR_BAD_SEGMENT_LENGTH);
	}

	// All status names must be unique, so logs can tell the classes apart.
	{
		const jpge::exif_status all[] =
		{
			jpge::EXIF_OK, jpge::EXIF_ERR_NOT_FOUND, jpge::EXIF_ERR_BAD_SEGMENT_LENGTH,
			jpge::EXIF_ERR_TOO_LARGE, jpge::EXIF_ERR_DUPLICATE_APP1, jpge::EXIF_ERR_BAD_TIFF_HEADER,
			jpge::EXIF_ERR_BAD_IFD, jpge::EXIF_ERR_ORIENTATION_RANGE, jpge::EXIF_ERR_CONFLICT
		};
		const size_t n = sizeof(all) / sizeof(all[0]);
		for (size_t i = 0; i < n; i++)
			for (size_t j = i + 1; j < n; j++)
			{
				CHECK(all[i] != all[j]);
				CHECK(strcmp(jpge::exif_status_name(all[i]), jpge::exif_status_name(all[j])) != 0);
			}
	}

	printf(g_failures ? "FAILED\n" : "ok\n");
}

static int run_exif_tests()
{
	printf("EXIF APP1 (Orientation) test suite\n");
	test_no_exif_is_byte_identical();
	test_generated_orientation();
	test_raw_payloads();
	test_error_classes();
	printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}

// ---------------------------------------------------------------------------
// Basic sanity check (default mode).
// ---------------------------------------------------------------------------

static int run_basic_tests()
{
	printf("basic round-trip sanity\n");

	std::vector<u8> img, jpg, dec;
	gen_image(img, 64, 64, 3, 0xABCDEFu);
	CHECK(encode_jpeg(&img[0], 64, 64, 3, jpge::params(), jpg));

	jpge::exif_info info;
	CHECK(jpge::read_exif(&jpg[0], (jpge::uint)jpg.size(), info) == jpge::EXIF_ERR_NOT_FOUND);

	int w = 0, h = 0;
	CHECK(decode_jpeg(&jpg[0], (int)jpg.size(), 3, dec, w, h));
	CHECK(w == 64 && h == 64);
	CHECK(dec.size() == 64 * 64 * 3);

	printf("%d checks, %d failures\n", g_checks, g_failures);
	printf("(run \"codec_test --exif\" for the EXIF APP1 suite)\n");
	return g_failures ? 1 : 0;
}

int main(int argc, char** argv)
{
	if ((argc > 1) && !strcmp(argv[1], "--exif"))
		return run_exif_tests();
	return run_basic_tests();
}
