
#include "iron_lz4.h"

#include "iron_alloc.h"
#include "iron_system.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

uint32_t lz4_encode_bound(uint32_t size) {
	uint32_t i = size / 255;
	return size > 0x7e000000 ? 0 : size + (i | 0) + 16;
}

buffer_t *lz4_encode(buffer_t *b) {
	const uint8_t *src  = b->buffer;
	uint32_t       ilen = b->length;
	if (ilen >= 0x7e000000) {
		iron_log("LZ4 range error");
		return NULL;
	}

	uint32_t olen       = lz4_encode_bound(ilen);
	uint8_t *dst        = (uint8_t *)malloc(olen);
	uint32_t opos       = 0;
	uint32_t anchor_pos = 0;

	if (ilen >= 13) {
		uint32_t last_match_pos   = ilen - 12;
		uint32_t last_literal_pos = ilen - 5;
		int32_t *hash_table       = (int32_t *)malloc(65536 * sizeof(int32_t));
		for (uint32_t i = 0; i < 65536; ++i) {
			hash_table[i] = -65536;
		}

		uint32_t ipos = 0;
		while (true) {
			uint32_t ref_pos         = 0;
			uint32_t moffset         = 0;
			uint32_t step            = 1;
			uint32_t search_match_nb = 64;

			while (ipos <= last_match_pos) {
				uint32_t seq;
				memcpy(&seq, src + ipos, 4);
				uint32_t hash    = (seq * 2654435761u) >> 16;
				int32_t  ref     = hash_table[hash];
				hash_table[hash] = (int32_t)ipos;
				if (ref >= 0) {
					moffset = ipos - (uint32_t)ref;
					if (moffset > 0 && moffset < 65536) {
						uint32_t ref_seq;
						memcpy(&ref_seq, src + ref, 4);
						if (seq == ref_seq) {
							ref_pos = (uint32_t)ref;
							break;
						}
					}
				}
				ipos += step;
				step = search_match_nb++ >> 6;
			}

			if (ipos > last_match_pos) {
				break;
			}

			uint32_t llen   = ipos - anchor_pos;
			uint32_t mstart = ipos;
			ipos += 4;
			ref_pos += 4;
			while (ipos + 8 <= last_literal_pos) {
				uint64_t a;
				uint64_t c;
				memcpy(&a, src + ipos, 8);
				memcpy(&c, src + ref_pos, 8);
				if (a != c) {
					break;
				}
				ipos += 8;
				ref_pos += 8;
			}
			while (ipos < last_literal_pos && src[ipos] == src[ref_pos]) {
				ipos += 1;
				ref_pos += 1;
			}
			uint32_t mlen  = ipos - mstart;
			uint32_t token = mlen < 19 ? mlen - 4 : 15;

			if (llen >= 15) {
				dst[opos++] = 0xf0 | token;
				uint32_t l  = llen - 15;
				while (l >= 255) {
					dst[opos++] = 255;
					l -= 255;
				}
				dst[opos++] = l;
			}
			else {
				dst[opos++] = (llen << 4) | token;
			}

			if (llen > 0) {
				memcpy(dst + opos, src + anchor_pos, llen);
				opos += llen;
				anchor_pos += llen;
			}

			dst[opos + 0] = (uint8_t)moffset;
			dst[opos + 1] = (uint8_t)(moffset >> 8);
			opos += 2;

			if (mlen >= 19) {
				uint32_t l = mlen - 19;
				while (l >= 255) {
					dst[opos++] = 255;
					l -= 255;
				}
				dst[opos++] = l;
			}

			anchor_pos = ipos;
		}

		free(hash_table);
	}

	// Last sequence is literals only
	uint32_t llen = ilen - anchor_pos;
	if (llen >= 15) {
		dst[opos++] = 0xf0;
		uint32_t l  = llen - 15;
		while (l >= 255) {
			dst[opos++] = 255;
			l -= 255;
		}
		dst[opos++] = l;
	}
	else {
		dst[opos++] = llen << 4;
	}
	if (llen > 0) {
		memcpy(dst + opos, src + anchor_pos, llen);
		opos += llen;
	}

	uint8_t    *trimmed = (uint8_t *)realloc(dst, opos > 0 ? opos : 1);
	u8_array_t *result  = (u8_array_t *)malloc(sizeof(u8_array_t));
	result->buffer      = trimmed != NULL ? trimmed : dst;
	result->length      = opos;
	result->capacity    = opos > 0 ? opos : 1;
	return result;
}

bool lz4_decode_to(buffer_t *b, uint8_t *dst, uint32_t olen) {
	(void)olen;
	const uint8_t *src  = b->buffer;
	uint32_t       ilen = b->length;
	uint32_t       ipos = 0;
	uint32_t       opos = 0;

	while (ipos < ilen) {
		uint32_t token = src[ipos++];

		// Literals
		uint32_t clen = token >> 4;
		if (clen != 0) {
			if (clen == 15) {
				uint32_t l;
				do {
					l = src[ipos++];
					clen += l;
				} while (l == 255);
			}
			memcpy(dst + opos, src + ipos, clen);
			opos += clen;
			ipos += clen;
			if (ipos == ilen) {
				break;
			}
		}

		// Match
		uint32_t moffset = (uint32_t)src[ipos] | ((uint32_t)src[ipos + 1] << 8);
		if (moffset == 0 || moffset > opos) {
			return false;
		}
		ipos += 2;

		clen = (token & 0x0f) + 4;
		if (clen == 19) {
			uint32_t l;
			do {
				l = src[ipos++];
				clen += l;
			} while (l == 255);
		}

		uint32_t mpos = opos - moffset;
		if (moffset >= clen) {
			memcpy(dst + opos, dst + mpos, clen);
		}
		else if (moffset == 1) {
			memset(dst + opos, dst[mpos], clen);
		}
		else {
			uint32_t copied = moffset;
			memcpy(dst + opos, dst + mpos, copied);
			while (copied < clen) {
				uint32_t chunk = clen - copied;
				if (chunk > copied) {
					chunk = copied;
				}
				memcpy(dst + opos + copied, dst + opos, chunk);
				copied += chunk;
			}
		}
		opos += clen;
	}

	return true;
}

buffer_t *lz4_decode(buffer_t *b, uint32_t olen) {
	u8_array_t *obuf = (u8_array_t *)malloc(sizeof(u8_array_t));
	obuf->buffer     = (uint8_t *)malloc(olen > 0 ? olen : 1);
	obuf->length     = olen;
	obuf->capacity   = olen > 0 ? olen : 1;
	if (!lz4_decode_to(b, obuf->buffer, olen)) {
		array_free(obuf);
		free(obuf);
		return NULL;
	}
	return obuf;
}
