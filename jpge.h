// jpge.h - C++ class for JPEG compression.
// Public Domain or Apache 2.0, Richard Geldreich <richgel99@gmail.com>
// Alex Evans: Added RGBA support, linear memory allocator.
#ifndef JPEG_ENCODER_H
#define JPEG_ENCODER_H

namespace jpge
{
	typedef unsigned char  uint8;
	typedef signed short   int16;
	typedef signed int     int32;
	typedef unsigned short uint16;
	typedef unsigned int   uint32;
	typedef unsigned int   uint;

	// JPEG chroma subsampling factors. Y_ONLY (grayscale images) and H2V2 (color images) are the most common.
	enum subsampling_t { Y_ONLY = 0, H1V1 = 1, H2V1 = 2, H2V2 = 3 };

	// EXIF APP1 validation results. Zero means "OK" (or no EXIF requested).
	// The distinct errors let callers tell an oversized segment apart from a
	// malformed TIFF header before any JPEG bytes are produced.
	enum exif_status
	{
		EXIF_OK = 0,
		EXIF_ERR_NOT_FOUND,          // no Exif APP1 segment in the stream
		EXIF_ERR_BAD_SEGMENT_LENGTH, // APP1 length field < 2 or runs past end of data
		EXIF_ERR_TOO_LARGE,          // payload exceeds EXIF_MAX_APP1_PAYLOAD
		EXIF_ERR_DUPLICATE_APP1,     // more than one Exif APP1 segment
		EXIF_ERR_BAD_TIFF_HEADER,    // not "Exif\0\0" + II/MM, magic 42, sane IFD offset
		EXIF_ERR_BAD_IFD,            // IFD entry array or value offset outside the payload
		EXIF_ERR_ORIENTATION_RANGE,  // Orientation value is not in 1..8
		EXIF_ERR_CONFLICT            // raw payload and m_exif_orientation both supplied
	};

	// Human-readable name for an exif_status value (stable for logging/tests).
	const char* exif_status_name(exif_status status);

	// Maximum EXIF payload that fits in one APP1 segment: the 16-bit segment
	// length includes its own 2 bytes, so the payload tops out at 65533.
	static const uint EXIF_MAX_APP1_PAYLOAD = 65533;

	// Decoded EXIF orientation state.
	struct exif_info
	{
		inline exif_info() : orientation(0), big_endian(false), segment_length(0), payload_size(0) { }

		// Orientation tag value 1..8, or 0 if the EXIF block has no such tag.
		int orientation;
		// TIFF byte order: true = big endian ("MM"), false = little endian ("II").
		bool big_endian;
		// APP1 segment length field this Exif block came from (payload + 2).
		uint segment_length;
		// APP1 payload size, including the 6-byte "Exif\0\0" prefix.
		uint payload_size;
	};

	// Reads the first Exif APP1 segment of a JPEG stream and extracts Orientation.
	// Segments are located by walking the marker chain up to the first SOS or EOI.
	// Returns EXIF_OK when a well-formed EXIF block was found (whether or not it
	// carries an Orientation tag); otherwise a distinct exif_status value.
	exif_status read_exif(const void* pJpeg_data, uint jpeg_size, exif_info& info);

	// JPEG compression parameters structure.
	struct params
	{
		inline params() : m_quality(85), m_subsampling(H2V2), m_no_chroma_discrim_flag(false), m_two_pass_flag(false), m_use_std_tables(false),
			m_exif_orientation(0), m_pExif_data(0), m_exif_data_size(0) { }

		inline bool check() const
		{
			if ((m_quality < 1) || (m_quality > 100)) return false;
			if ((uint)m_subsampling > (uint)H2V2) return false;
			if (check_exif() != EXIF_OK) return false;
			return true;
		}

		// Validates the EXIF options without touching the JPEG stream. Returns one
		// of the exif_status values so oversized, malformed-TIFF and conflicting
		// requests can be reported separately.
		exif_status check_exif() const;

		// Quality: 1-100, higher is better. Typical values are around 50-95.
		int m_quality;

		// m_subsampling:
		// 0 = Y (grayscale) only
		// 1 = YCbCr, no subsampling (H1V1, YCbCr 1x1x1, 3 blocks per MCU)
		// 2 = YCbCr, H2V1 subsampling (YCbCr 2x1x1, 4 blocks per MCU)
		// 3 = YCbCr, H2V2 subsampling (YCbCr 4x1x1, 6 blocks per MCU-- very common)
		subsampling_t m_subsampling;

		// Disables CbCr discrimination - only intended for testing.
		// If true, the Y quantization table is also used for the CbCr channels.
		bool m_no_chroma_discrim_flag;

		bool m_two_pass_flag;

		// By default we use the same quantization tables as mozjpeg's default.
		// Set to true to use the traditional tables from JPEG Annex K.
		bool m_use_std_tables;

		// EXIF Orientation tag (1-8, see EXIF/TIFF Orientation). 0 (the default)
		// means "no orientation tag". Setting 1-8 makes the encoder synthesize a
		// minimal little-endian EXIF APP1 segment containing just this tag. The
		// output is byte-for-byte unchanged when both EXIF fields are left at their
		// defaults.
		int m_exif_orientation;

		// Optional raw EXIF APP1 payload, starting with "Exif\0\0" followed by a
		// TIFF header (II*\0 or MM\0*, big or little endian). The payload is
		// written verbatim, so extra IFDs and thumbnail data survive. The encoder
		// validates the TIFF header and the APP1 length limit. Its size must not
		// exceed EXIF_MAX_APP1_PAYLOAD. May not be combined with
		// m_exif_orientation (use the payload's own Orientation tag instead).
		const uint8* m_pExif_data;
		uint m_exif_data_size;
	};

	// Writes JPEG image to a file. 
	// num_channels must be 1 (Y) or 3 (RGB), image pitch must be width*num_channels.
	bool compress_image_to_jpeg_file(const char* pFilename, int width, int height, int num_channels, const uint8* pImage_data, const params& comp_params = params());

	// Writes JPEG image to memory buffer. 
	// On entry, buf_size is the size of the output buffer pointed at by pBuf, which should be at least ~1024 bytes. 
	// If return value is true, buf_size will be set to the size of the compressed data.
	bool compress_image_to_jpeg_file_in_memory(void* pBuf, int& buf_size, int width, int height, int num_channels, const uint8* pImage_data, const params& comp_params = params());

	// Output stream abstract class - used by the jpeg_encoder class to write to the output stream. 
	// put_buf() is generally called with len==JPGE_OUT_BUF_SIZE bytes, but for headers it'll be called with smaller amounts.
	class output_stream
	{
	public:
		virtual ~output_stream() { };
		virtual bool put_buf(const void* Pbuf, int len) = 0;
		template<class T> inline bool put_obj(const T& obj) { return put_buf(&obj, sizeof(T)); }
	};

	// Lower level jpeg_encoder class - useful if more control is needed than the above helper functions.
	class jpeg_encoder
	{
	public:
		jpeg_encoder();
		~jpeg_encoder();

		// Initializes the compressor.
		// pStream: The stream object to use for writing compressed data.
		// params - Compression parameters structure, defined above.
		// width, height  - Image dimensions.
		// channels - May be 1, or 3. 1 indicates grayscale, 3 indicates RGB source data.
		// Returns false on out of memory or if a stream write fails.
		bool init(output_stream* pStream, int width, int height, int src_channels, const params& comp_params = params());

		const params& get_params() const { return m_params; }

		// Deinitializes the compressor, freeing any allocated memory. May be called at any time.
		void deinit();

		uint get_total_passes() const { return m_params.m_two_pass_flag ? 2 : 1; }
		inline uint get_cur_pass() { return m_pass_num; }

		// Call this method with each source scanline.
		// width * src_channels bytes per scanline is expected (RGB or Y format).
		// You must call with NULL after all scanlines are processed to finish compression.
		// Returns false on out of memory or if a stream write fails.
		bool process_scanline(const void* pScanline);

	private:
		jpeg_encoder(const jpeg_encoder&);
		jpeg_encoder& operator =(const jpeg_encoder&);

		typedef int32 sample_array_t;

		output_stream* m_pStream;
		params m_params;
		uint8 m_num_components;
		uint8 m_comp_h_samp[3], m_comp_v_samp[3];
		int m_image_x, m_image_y, m_image_bpp, m_image_bpl;
		int m_image_x_mcu, m_image_y_mcu;
		int m_image_bpl_xlt, m_image_bpl_mcu;
		int m_mcus_per_row;
		int m_mcu_x, m_mcu_y;
		uint8* m_mcu_lines[16];
		uint8 m_mcu_y_ofs;
		sample_array_t m_sample_array[64];
		int16 m_coefficient_array[64];
		int32 m_quantization_tables[2][64];
		uint m_huff_codes[4][256];
		uint8 m_huff_code_sizes[4][256];
		uint8 m_huff_bits[4][17];
		uint8 m_huff_val[4][256];
		uint32 m_huff_count[4][256];
		int m_last_dc_val[3];
		enum { JPGE_OUT_BUF_SIZE = 2048 };
		uint8 m_out_buf[JPGE_OUT_BUF_SIZE];
		uint8* m_pOut_buf;
		uint m_out_buf_left;
		uint32 m_bit_buffer;
		uint m_bits_in;
		uint8 m_pass_num;
		bool m_all_stream_writes_succeeded;

		void optimize_huffman_table(int table_num, int table_len);
		void emit_byte(uint8 i);
		void emit_word(uint i);
		void emit_marker(int marker);
		void emit_jfif_app0();
		void emit_exif_app1();
		void emit_dqt();
		void emit_sof();
		void emit_dht(uint8* bits, uint8* val, int index, bool ac_flag);
		void emit_dhts();
		void emit_sos();
		void emit_markers();
		void compute_huffman_table(uint* codes, uint8* code_sizes, uint8* bits, uint8* val);
		void compute_quant_table(int32* dst, int16* src);
		void adjust_quant_table(int32* dst, int32* src);
		void first_pass_init();
		bool second_pass_init();
		bool jpg_open(int p_x_res, int p_y_res, int src_channels);
		void load_block_8_8_grey(int x);
		void load_block_8_8(int x, int y, int c);
		void load_block_16_8(int x, int c);
		void load_block_16_8_8(int x, int c);
		void load_quantized_coefficients(int component_num);
		void flush_output_buffer();
		void put_bits(uint bits, uint len);
		void code_coefficients_pass_one(int component_num);
		void code_coefficients_pass_two(int component_num);
		void code_block(int component_num);
		void process_mcu_row();
		bool terminate_pass_one();
		bool terminate_pass_two();
		bool process_end_of_image();
		void load_mcu(const void* src);
		void clear();
		void init();
	};

} // namespace jpge

#endif // JPEG_ENCODER
