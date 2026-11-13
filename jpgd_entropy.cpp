// jpgd_entropy.cpp - Common entropy decoding layer for jpgd.
//
// Contains the concrete Huffman and arithmetic (T.81 Annex D) entropy decoders
// behind the shared entropy_decoder interface. The surrounding JPEG decoder
// (MCU scheduling, dequantization, IDCT, color conversion) is unchanged.
//
// Public Domain or Apache 2.0 (same terms as jpgd.cpp).
#include "jpgd_entropy.h"
#include <string.h>
#include <new>

namespace jpgd
{
	// Zigzag table defined in jpgd.cpp; shared by both entropy coders.
	extern const int g_ZAG[64];

	// Tables and macro used to fully decode the DPCM differences.
	static const int s_extend_test[16] = { 0, 0x0001, 0x0002, 0x0004, 0x0008, 0x0010, 0x0020, 0x0040, 0x0080, 0x0100, 0x0200, 0x0400, 0x0800, 0x1000, 0x2000, 0x4000 };
	static const int s_extend_offset[16] = { 0, -1, -3, -7, -15, -31, -63, -127, -255, -511, -1023, -2047, -4095, -8191, -16383, -32767 };

	// Sign-extend a magnitude/s bit pair (T.81 F.1.5.1, same math as the
	// Huffman path's JPGD_HUFF_EXTEND).
	int jpeg_decoder::extend_value(int bits, int s)
	{
		return (bits < s_extend_test[s & 15]) ? (bits + s_extend_offset[s & 15]) : bits;
	}

	static inline int min_int(int a, int b) { return (a < b) ? a : b; }

	//==========================================================================
	// Huffman entropy decoder
	//
	// This is the exact per-block decoding logic that originally lived in
	// jpeg_decoder::decode_next_row(). Only its placement changed: it now sits
	// behind the entropy_decoder interface so the arithmetic path reuses the
	// exact same scheduling, dequantization and transform machinery.
	//==========================================================================

	void huffman_entropy_decoder::restart()
	{
		// Nothing coder-specific to do: jpeg_decoder::process_restart() already
		// resets predictions and re-primes the Huffman bit buffer.
	}

	void huffman_entropy_decoder::decode_block(int mcu_block, int component_id)
	{
		jpeg_decoder* pD = m_pD;

		if (pD->m_comp_quant[component_id] >= JPGD_MAX_QUANT_TABLES)
			pD->stop_decoding(JPGD_DECODE_ERROR);

		jpgd_quant_t* q = pD->m_quant[pD->m_comp_quant[component_id]];
		jpgd_block_coeff_t* p = pD->m_pMCU_coefficients + 64 * mcu_block;

		int r, s;
		s = pD->huff_decode(pD->m_pHuff_tabs[pD->m_comp_dc_tab[component_id]], r);
		if (s >= 16)
			pD->stop_decoding(JPGD_DECODE_ERROR);

		s = jpeg_decoder::extend_value(r, s);

		pD->m_last_dc_val[component_id] = (s += pD->m_last_dc_val[component_id]);

		p[0] = static_cast<jpgd_block_coeff_t>(s * q[0]);

		int prev_num_set = pD->m_mcu_block_max_zag[mcu_block];

		jpeg_decoder::huff_tables* pH = pD->m_pHuff_tabs[pD->m_comp_ac_tab[component_id]];

		int k;
		for (k = 1; k < 64; k++)
		{
			int extra_bits;
			s = pD->huff_decode(pH, extra_bits);

			r = s >> 4;
			s &= 15;

			if (s)
			{
				if (r)
				{
					if ((k + r) > 63)
						pD->stop_decoding(JPGD_DECODE_ERROR);

					if (k < prev_num_set)
					{
						int n = min_int(r, prev_num_set - k);
						int kt = k;
						while (n--)
							p[g_ZAG[kt++]] = 0;
					}

					k += r;
				}

				s = jpeg_decoder::extend_value(extra_bits, s);

				if (k >= 64)
					pD->stop_decoding(JPGD_DECODE_ERROR);

				p[g_ZAG[k]] = static_cast<jpgd_block_coeff_t>(s * q[k]);
			}
			else
			{
				if (r == 15)
				{
					if ((k + 16) > 64)
						pD->stop_decoding(JPGD_DECODE_ERROR);

					if (k < prev_num_set)
					{
						int n = min_int(16, prev_num_set - k);
						int kt = k;
						while (n--)
						{
							if (kt > 63)
								pD->stop_decoding(JPGD_DECODE_ERROR);
							p[g_ZAG[kt++]] = 0;
						}
					}

					k += 16 - 1; // - 1 because the loop counter is k

					if (p[g_ZAG[k & 63]] != 0)
						pD->stop_decoding(JPGD_DECODE_ERROR);
				}
				else
					break;
			}
		}

		if (k < prev_num_set)
		{
			int kt = k;
			while (kt < prev_num_set)
				p[g_ZAG[kt++]] = 0;
		}

		pD->m_mcu_block_max_zag[mcu_block] = k;
	}

	entropy_decoder* create_huffman_entropy_decoder(jpeg_decoder* pD)
	{
		void* p = pD->alloc(sizeof(huffman_entropy_decoder), true);
		return new (p) huffman_entropy_decoder(pD);
	}

} // namespace jpgd
