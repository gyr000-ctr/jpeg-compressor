// jpgd_arith.cpp - Arithmetic entropy decoder for jpgd.
//
// Implements the JPEG arithmetic coding procedures of ITU-T T.81:
//   - D.2  Arithmetic encoding principles / probability estimation
//   - D.3  The decoder model
//   - F.1  Sequential DCT, arithmetic coding (DC)
//   - F.2  Sequential DCT, arithmetic coding (AC)
//
// The state machine and statistics layout are a direct port of the
// well-audited libjpeg/libjpeg-turbo jdarith.c reference (IJG, Guido
// Vollbeding). Only the surrounding stream/object plumbing is jpgd's own:
// byte input is taken from jpeg_decoder::get_octet(), which already resolves
// 0xFF00 stuffing and stops at markers.
//
// Public Domain or Apache 2.0 (same terms as jpgd.cpp).
#include "jpgd_entropy.h"
#include <string.h>
#include <new>

namespace jpgd
{
	// Zigzag table defined in jpgd.cpp; shared by both entropy coders.
	extern const int g_ZAG[64];

	// Probability estimation tables (T.81 Table D.2), generated from
	// libjpeg-turbo's jpeg_aritab. The generator lives in
	// output/tools/gen/gen_arith_tables.py; this .inc is a checked-in artifact.
#include "jpgd_arith_tables.inc"

	enum
	{
		ARITH_FIXED_INDEX = 113  // non-adapting state, probability 0.5
	};

	arith_entropy_decoder::arith_entropy_decoder(jpeg_decoder* pD)
		: entropy_decoder(pD), m_c(0), m_a(0), m_ct(-16), m_eof_hit(false),
		  m_fixed_bin(ARITH_FIXED_INDEX)
	{
		memset(m_dc_context, 0, sizeof(m_dc_context));
		memset(m_dc_stats, 0, sizeof(m_dc_stats));
		memset(m_ac_stats, 0, sizeof(m_ac_stats));
	}

	// Reinitialize every conditioning context to its zero state (I=0, MPS=0).
	// T.81 requires all statistics areas to be reset at the start of a scan and
	// at every restart interval.
	void arith_entropy_decoder::init_stats()
	{
		memset(m_dc_stats, 0, sizeof(m_dc_stats));
		memset(m_ac_stats, 0, sizeof(m_ac_stats));

		// The fixed-probability context never adapts.
		m_fixed_bin = ARITH_FIXED_INDEX;

		for (int i = 0; i < JPGD_MAX_COMPONENTS; i++)
			m_dc_context[i] = 0;
	}

	// (Re)initialize the interval decoder. ct = -16 forces the two initial
	// bytes (Figure D.5) to be pulled before the first decision.
	void arith_entropy_decoder::init_engine()
	{
		m_c = 0;
		m_a = 0;
		m_ct = -16;
		m_eof_hit = false;
	}

	void arith_entropy_decoder::init()
	{
		init_stats();
		init_engine();
	}

	void arith_entropy_decoder::restart()
	{
		// T.81 Annex E: statistics areas and DC predictions are reset, then
		// the interval decoder is restarted from scratch.
		init_stats();
		memset(m_pD->m_last_dc_val, 0, m_pD->m_comps_in_frame * sizeof(uint));
		init_engine();
	}

	// Read one entropy-coded byte with the same semantics as the reference's
	// get_byte() plus its 0xFF handling (jdarith.c::arith_decode):
	//   0xFF 0x00            -> data byte 0xFF (byte stuffing)
	//   run of 0xFF bytes    -> swallowed
	//   0xFF 0xXX (XX != 0)  -> a marker; the marker is pushed back for the
	//                           outer marker machinery and zero data is fed
	// The padding-aware get_char() additionally distinguishes a genuine end of
	// stream (synthesized 0xFF 0xD9) from a real marker: hitting the former
	// while blocks still need data marks the stream truncated/corrupt.
	int arith_entropy_decoder::get_segment_byte(jpeg_decoder* pD)
	{
		bool pad = false;
		int data = pD->get_char(&pad);
		if (data != 0xFF)
			return data;

		int next;
		bool next_pad = false;
		do
		{
			next = pD->get_char(&next_pad);
			pad = pad || next_pad;
		} while (next == 0xFF);

		if (next == 0x00)
			return 0xFF;  // byte stuffing

		// A marker (RSTn/EOI). Re-stuff it so process_restart()/find_eoi()
		// consume it, and supply zero data per the reference convention.
		pD->stuff_char(static_cast<uint8>(next));
		pD->stuff_char(0xFF);

		// If either byte was synthesized past the physical end of stream, the
		// entropy-coded segment ended before all coefficients were produced.
		if (pad)
			m_eof_hit = true;

		return 0;
	}

	// Core binary arithmetic decision (T.81 D.2.4/D.2.5, port of
	// jdarith.c::arith_decode). Returns 0 or 1 (the decoded binary symbol).
	int arith_entropy_decoder::arith_decode(arith_stat_bin& st)
	{
		// A truncated/corrupt stream exhausts the physical input before the
		// expected number of decisions can be made. Report it rather than
		// silently decoding the reference's zero fill.
		if (m_eof_hit)
			m_pD->stop_decoding(JPGD_ARITH_DECODE_ERROR);

		// Renormalization & data input per D.2.6.
		while (m_a < 0x8000)
		{
			if (--m_ct < 0)
			{
				const int data = get_segment_byte(m_pD);
				m_c = (m_c << 8) | static_cast<uint>(data);  // insert data
				if ((m_ct += 8) < 0)
				{
					// Need both initial bytes before decisions can begin.
					if (++m_ct == 0)
						m_a = 0x8000;  // becomes 0x10000 after the loop shift
				}
			}
			m_a <<= 1;
		}

		unsigned int sv = st;
		const uint qe_val = s_arith_qe[sv & 0x7F];
		// Mirror jdarith.c's two-step unpacking exactly:
		//   nl carries Next_Index_LPS plus Switch_MPS in bit 7
		//   nm is the plain Next_Index_MPS (bit 7 stays zero)
		const unsigned int nl = (s_arith_nlps[sv & 0x7F]) | (s_arith_switch[sv & 0x7F] << 7);
		const unsigned int nm = s_arith_nmps[sv & 0x7F];

		uint temp = m_a - qe_val;
		m_a = temp;
		temp <<= m_ct;

		if (m_c >= temp)
		{
			// Conditional LPS (less probable symbol) exchange.
			m_c -= temp;
			if (m_a < qe_val)
			{
				m_a = qe_val;
				st = static_cast<arith_stat_bin>((sv & 0x80) ^ nm);  // Estimate_after_MPS
			}
			else
			{
				m_a = qe_val;
				st = static_cast<arith_stat_bin>((sv & 0x80) ^ nl);  // Estimate_after_LPS
				sv ^= 0x80;                                          // Exchange LPS/MPS
			}
		}
		else if (m_a < 0x8000)
		{
			// Conditional MPS (more probable symbol) exchange.
			if (m_a < qe_val)
			{
				st = static_cast<arith_stat_bin>((sv & 0x80) ^ nl);  // Estimate_after_LPS
				sv ^= 0x80;                                          // Exchange LPS/MPS
			}
			else
				st = static_cast<arith_stat_bin>((sv & 0x80) ^ nm);  // Estimate_after_MPS
		}

		return sv >> 7;
	}

	// Decode one DC difference (T.81 F.1.4.4.1, Figures F.19/F.21-F.24).
	// Returns the signed difference to add to the predictor.
	int arith_entropy_decoder::decode_dc_diff(int component_id)
	{
		const int dc_table = m_pD->m_comp_dc_tab[component_id];
		arith_stat_bin* const table_stats = m_dc_stats + dc_table * 64;
		arith_stat_bin* st = table_stats + m_dc_context[component_id];

		// Figure F.19: Decode_DC_DIFF
		if (arith_decode(*st) == 0)
		{
			m_dc_context[component_id] = 0;
			return 0;
		}

		// Figure F.22: sign of v
		const int sign = arith_decode(*(st + 1));
		st += 2;
		st += sign;

		// Figure F.23: magnitude category
		int m = arith_decode(*st);
		if (m != 0)
		{
			st = table_stats + 20;  // Table F.4: X1 = 20
			while (arith_decode(*st))
			{
				if ((m <<= 1) == 0x8000)
					m_pD->stop_decoding(JPGD_ARITH_DECODE_ERROR);  // magnitude overflow
				st += 1;
			}
		}

		// F.1.4.4.1.2: establish the next DC conditioning category.
		const int L = m_pD->m_arith_dc_L[dc_table];
		const int U = m_pD->m_arith_dc_U[dc_table];
		if (m < ((1 << L) >> 1))
			m_dc_context[component_id] = 0;
		else if (m > ((1 << U) >> 1))
			m_dc_context[component_id] = 12 + (sign * 4);
		else
			m_dc_context[component_id] = 4 + (sign * 4);

		// Figure F.24: magnitude bit pattern.
		int v = m;
		st += 14;
		while (m >>= 1)
			if (arith_decode(*st))
				v |= m;

		v += 1;
		if (sign)
			v = -v;
		return v;
	}

	// Decode the 63 AC coefficients of one block (T.81 F.1.4.4.2, Figures
	// F.20/F.21-F.24). p[] is in natural (de-zigzagged) storage and starts
	// zeroed; coefficients are written raw like in the reference decoder.
	void arith_entropy_decoder::decode_ac_block(jpgd_block_coeff_t* p, int component_id, int ac_table)
	{
		(void)component_id;
		arith_stat_bin* const table_stats = m_ac_stats + ac_table * 256;

		for (int k = 1; k <= 63; k++)
		{
			arith_stat_bin* st = table_stats + 3 * (k - 1);

			// Figure F.20: EOB flag.
			if (arith_decode(*st))
				break;

			// Skip zero runs.
			while (arith_decode(*(st + 1)) == 0)
			{
				st += 3;
				k++;
				if (k > 63)
					m_pD->stop_decoding(JPGD_ARITH_DECODE_ERROR);  // spectral overflow
			}

			// Figure F.22: sign uses fixed probability 0.5.
			const int sign = arith_decode(m_fixed_bin);
			st += 2;

			// Figure F.23: magnitude category.
			int m = arith_decode(*st);
			if (m != 0)
			{
				if (arith_decode(*st))
				{
					m <<= 1;
					st = table_stats + (k <= m_pD->m_arith_ac_K[ac_table] ? 189 : 217);
					while (arith_decode(*st))
					{
						if ((m <<= 1) == 0x8000)
							m_pD->stop_decoding(JPGD_ARITH_DECODE_ERROR);  // magnitude overflow
						st += 1;
					}
				}
			}

			// Figure F.24: magnitude bit pattern.
			int v = m;
			st += 14;
			while (m >>= 1)
				if (arith_decode(*st))
					v |= m;

			v += 1;
			if (sign)
				v = -v;

			p[g_ZAG[k]] = static_cast<jpgd_block_coeff_t>(v);
		}
	}

	void arith_entropy_decoder::decode_block(int mcu_block, int component_id)
	{
		jpeg_decoder* pD = m_pD;

		if (pD->m_comp_quant[component_id] >= JPGD_MAX_QUANT_TABLES)
			pD->stop_decoding(JPGD_DECODE_ERROR);

		jpgd_quant_t* q = pD->m_quant[pD->m_comp_quant[component_id]];
		jpgd_block_coeff_t* p = pD->m_pMCU_coefficients + 64 * mcu_block;

		// Arithmetic blocks rely on being initially zero (T.81 F.2).
		memset(p, 0, 64 * sizeof(jpgd_block_coeff_t));

		// DC: difference coding against the per-component predictor.
		const int diff = decode_dc_diff(component_id);
		pD->m_last_dc_val[component_id] =
			static_cast<uint>((int)pD->m_last_dc_val[component_id] + diff) & 0xFFFF;
		p[0] = static_cast<jpgd_block_coeff_t>(pD->m_last_dc_val[component_id]);

		// m_comp_ac_tab stores the AC slot plus a fixed +4 offset (it shares
		// storage with the Huffman AC table indices); the arithmetic stats use
		// a per-slot 0..3 layout, so normalize here.
		const int ac_table = pD->m_comp_ac_tab[component_id] - (JPGD_MAX_HUFF_TABLES >> 1);
		decode_ac_block(p, component_id, ac_table);

		// Dequantize. This is exactly the Huffman path's multiplication, just
		// applied uniformly because arithmetic coding emits raw coefficients.
		for (int zag = 1; zag < 64; zag++)
		{
			jpgd_block_coeff_t v = p[g_ZAG[zag]];
			if (v)
				p[g_ZAG[zag]] = static_cast<jpgd_block_coeff_t>(v * q[zag]);
		}
		if (p[0])
			p[0] = static_cast<jpgd_block_coeff_t>(p[0] * q[0]);

		// Supply the same fast-path hint the Huffman path produces: the index
		// just past the last nonzero coefficient in zigzag order (its loop
		// counter k). An all-zero block yields 1 (DC-only fast path).
		int last_nonz = 0;
		for (int zag = 1; zag < 64; zag++)
			if (p[g_ZAG[zag]])
				last_nonz = zag;

		pD->m_mcu_block_max_zag[mcu_block] = last_nonz + 1;
	}

	entropy_decoder* create_arith_entropy_decoder(jpeg_decoder* pD)
	{
		void* p = pD->alloc(sizeof(arith_entropy_decoder), true);
		return new (p) arith_entropy_decoder(pD);
	}

} // namespace jpgd
