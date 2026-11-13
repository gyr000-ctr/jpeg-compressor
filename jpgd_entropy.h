// jpgd_entropy.h - Common entropy decoding layer for jpgd.
//
// Both the Huffman and arithmetic entropy coders are exposed through the same
// entropy_decoder interface. The JPEG decoder's MCU scheduling, dequantization,
// IDCT and color conversion code is shared and never branches on the entropy
// coding method.
//
// Arithmetic coding support follows ITU-T T.81 (JPEG), Annex D.
//
// Public Domain or Apache 2.0 (same terms as jpgd.cpp).
#ifndef JPGD_ENTROPY_H
#define JPGD_ENTROPY_H

#include "jpgd.h"

namespace jpgd
{
	// Number of probability estimate states in the JPEG arithmetic coder
	// (Annex D.2). State index 0 is the special non-adapting state.
	#define JPGD_ARITH_NUM_STATES 113

	// Per-context adaptive statistics, stored in the compact T.81/libjpeg
	// representation: bit 7 is MPS(Cx), bits 0..6 are the probability
	// estimate table index I(Cx).
	typedef uint8 arith_stat_bin;

	// Abstract entropy decoder.
	// A concrete implementation decodes one 8x8 block at a time into the
	// decoder's MCU coefficient buffer, taking care of every entropy-coding
	// detail (symbols, extra bits, EOB runs, ...). Everything downstream is
	// identical for both coding methods.
	class entropy_decoder
	{
	public:
		explicit entropy_decoder(jpeg_decoder* pD) : m_pD(pD) { }
		virtual ~entropy_decoder() { }

		// Called once, right before the entropy-coded segment begins.
		// (After the SOS marker and the decoder's bit/byte buffer has been
		// positioned at the start of the segment.)
		virtual void init() = 0;

		// Called at every restart interval boundary. Re-initializes all
		// prediction values and coder state, as required by T.81 E.
		virtual void restart() = 0;

		// Entropy-decode a single 8x8 block of a sequential scan.
		// mcu_block is the block's position within the current MCU,
		// component_id is the owning frame component. The block is written
		// into the decoder's MCU coefficient buffer, already dequantized,
		// exactly like the original Huffman path did.
		virtual void decode_block(int mcu_block, int component_id) = 0;

	protected:
		jpeg_decoder* m_pD;
	};

	// Huffman entropy decoder (baseline/extended sequential).
	class huffman_entropy_decoder : public entropy_decoder
	{
	public:
		explicit huffman_entropy_decoder(jpeg_decoder* pD) : entropy_decoder(pD) { }

		virtual void init() { }
		virtual void restart();
		virtual void decode_block(int mcu_block, int component_id);
	};

	// Arithmetic entropy decoder (SOF9, extended sequential DCT).
	class arith_entropy_decoder : public entropy_decoder
	{
	public:
		explicit arith_entropy_decoder(jpeg_decoder* pD);

		virtual void init();
		virtual void restart();
		virtual void decode_block(int mcu_block, int component_id);

	private:
		void init_stats();          // (re)initialize all conditioning contexts
		void init_engine();         // (re)initialize the interval decoder
		int  get_segment_byte(jpeg_decoder* pD);  // marker-safe byte input
		int  arith_decode(arith_stat_bin& st);                // T.81 D.2
		int  decode_dc_diff(int component_id);                // T.81 F.1
		void decode_ac_block(jpgd_block_coeff_t* p, int component_id, int ac_table); // T.81 F.2

		// Interval decoder registers (T.81 D.3).
		uint m_c;
		uint m_a;
		int  m_ct;
		// Set once the physical input stream is exhausted while the decoder
		// still needs entropy bytes; any further decision is a corrupt/truncated
		// stream rather than the legal post-marker zero convention.
		bool m_eof_hit;

		// Per-frame-component DC conditioning category (T.81 F.1.4.4.1.2).
		int m_dc_context[JPGD_MAX_COMPONENTS];

		// Fixed probability 0.5 context (T.851, state index 113), used for
		// the sign decision of nonzero AC values.
		arith_stat_bin m_fixed_bin;

		// DC contexts: up to 64 bins per DC table slot (only 0..33 are used).
		arith_stat_bin m_dc_stats[(JPGD_MAX_HUFF_TABLES >> 1) * 64];
		// AC contexts: 256 bins per AC table slot
		// (3*63 zigzag contexts + magnitude/EOB contexts 189..255).
		arith_stat_bin m_ac_stats[(JPGD_MAX_HUFF_TABLES >> 1) * 256];
	};

	// Factories. Storage comes from the decoder's arena allocator, so it never
	// needs to be freed explicitly.
	entropy_decoder* create_huffman_entropy_decoder(jpeg_decoder* pD);
	entropy_decoder* create_arith_entropy_decoder(jpeg_decoder* pD);

} // namespace jpgd

#endif // JPGD_ENTROPY_H
