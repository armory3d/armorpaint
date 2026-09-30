/*

LZ4X - An optimized LZ4 compressor

Written and placed in the public domain by Ilya Muravyov

*/

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define _CRT_DISABLE_PERFCRIT_LOCKS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef unsigned char  U8;
typedef unsigned short U16;
typedef unsigned int   U32;

#define LZ4_MAGIC        0x184C2102
#define BLOCK_SIZE       (8 << 20) // 8 MB
#define PADDING_LITERALS 5

#define WINDOW_BITS 16
#define WINDOW_SIZE (1 << WINDOW_BITS)
#define WINDOW_MASK (WINDOW_SIZE - 1)

#define MIN_MATCH 4

#define EXCESS (16 + (BLOCK_SIZE / 255))

static U8 g_buf[BLOCK_SIZE + BLOCK_SIZE + EXCESS];

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

#define LOAD_16(p)     (*(const U16 *)(&g_buf[p]))
#define LOAD_32(p)     (*(const U32 *)(&g_buf[p]))
#define STORE_16(p, x) (*(U16 *)(&g_buf[p]) = (x))
#define COPY_32(d, s)  (*(U32 *)(&g_buf[d]) = LOAD_32(s))

#define HASH_BITS 18
#define HASH_SIZE (1 << HASH_BITS)
#define NIL       (-1)

#define HASH_32(p) ((LOAD_32(p) * 0x9E3779B9) >> (32 - HASH_BITS))

static size_t kread(void *dst, size_t size, const char *src, size_t *offset, size_t compressedSize) {
	size_t realSize = MIN(size, compressedSize - *offset);
	memcpy(dst, &src[*offset], realSize);
	*offset += realSize;
	return realSize;
}

static size_t kwrite(void *src, size_t size, char *dst, size_t *offset, int maxOutputSize) {
	size_t realSize = MIN(size, maxOutputSize - *offset);
	memcpy(&dst[*offset], src, realSize);
	*offset += realSize;
	return realSize;
}

static inline void wild_copy(int d, int s, int n) {
	COPY_32(d, s);
	COPY_32(d + 4, s + 4);

	for (int i = 8; i < n; i += 8) {
		COPY_32(d + i, s + i);
		COPY_32(d + 4 + i, s + 4 + i);
	}
}

size_t LZ4_compress(const char *source, int sourceSize, char *dest, const int max_chain, int maxDestSize) {
	static int head[HASH_SIZE];
	static int tail[WINDOW_SIZE];

	size_t read_offset  = 0;
	size_t write_offset = 0;
	int    n;

	while ((n = kread(g_buf, BLOCK_SIZE, source, &read_offset, sourceSize)) > 0) {
		for (int i = 0; i < HASH_SIZE; ++i)
			head[i] = NIL;

		int op = BLOCK_SIZE;
		int pp = 0;

		int p = 0;
		while (p < n) {
			int best_len = 0;
			int dist     = 0;

			const int max_match = (n - PADDING_LITERALS) - p;
			if (max_match >= MAX(12 - PADDING_LITERALS, MIN_MATCH)) {
				const int limit     = MAX(p - WINDOW_SIZE, NIL);
				int       chain_len = max_chain;

				int s = head[HASH_32(p)];
				while (s > limit) {
					if (g_buf[s + best_len] == g_buf[p + best_len] && LOAD_32(s) == LOAD_32(p)) {
						int len = MIN_MATCH;
						while (len < max_match && g_buf[s + len] == g_buf[p + len])
							++len;

						if (len > best_len) {
							best_len = len;
							dist     = p - s;

							if (len == max_match)
								break;
						}
					}

					if (--chain_len == 0)
						break;

					s = tail[s & WINDOW_MASK];
				}
			}

			if (best_len >= MIN_MATCH) {
				int       len = best_len - MIN_MATCH;
				const int nib = MIN(len, 15);

				if (pp != p) {
					const int run = p - pp;
					if (run >= 15) {
						g_buf[op++] = (15 << 4) + nib;

						int j = run - 15;
						for (; j >= 255; j -= 255)
							g_buf[op++] = 255;
						g_buf[op++] = j;
					}
					else
						g_buf[op++] = (run << 4) + nib;

					wild_copy(op, pp, run);
					op += run;
				}
				else
					g_buf[op++] = nib;

				STORE_16(op, dist);
				op += 2;

				if (len >= 15) {
					len -= 15;
					for (; len >= 255; len -= 255)
						g_buf[op++] = 255;
					g_buf[op++] = len;
				}

				pp = p + best_len;

				while (p < pp) {
					const U32 h           = HASH_32(p);
					tail[p & WINDOW_MASK] = head[h];
					// size_t read_offset    = 0;
					// size_t write_offset   = 0;
					head[h] = p++;
				}
			}
			else {
				const U32 h           = HASH_32(p);
				tail[p & WINDOW_MASK] = head[h];
				head[h]               = p++;
			}
		}

		if (pp != p) {
			const int run = p - pp;
			if (run >= 15) {
				g_buf[op++] = 15 << 4;

				int j = run - 15;
				for (; j >= 255; j -= 255)
					g_buf[op++] = 255;
				g_buf[op++] = j;
			}
			else
				g_buf[op++] = run << 4;

			wild_copy(op, pp, run);
			op += run;
		}

		int comp_len = op - BLOCK_SIZE;

		kwrite(&comp_len, sizeof(comp_len), dest, &write_offset, maxDestSize);
		kwrite(&g_buf[BLOCK_SIZE], comp_len, dest, &write_offset, maxDestSize);
	}

	return write_offset;
}

int LZ4_compress_bound(int inputSize) {
	return inputSize + (inputSize / 255) + 16;
}

size_t LZ4_compress_default(const char *source, char *dest, int sourceSize, int maxDestSize) {
	return LZ4_compress(source, sourceSize, dest, WINDOW_SIZE, maxDestSize);
}

int LZ4_decompress_safe(const char *source, char *buf, int compressedSize, int maxOutputSize) {
	size_t read_offset  = 0;
	size_t write_offset = 0;
	int    comp_len;
	while (kread(&comp_len, sizeof(comp_len), source, &read_offset, compressedSize) > 0) {
		if (comp_len < 2 || comp_len > (BLOCK_SIZE + EXCESS) || kread(&g_buf[BLOCK_SIZE], comp_len, source, &read_offset, compressedSize) != comp_len)
			return -1;

		int p = 0;

		int       ip     = BLOCK_SIZE;
		const int ip_end = ip + comp_len;

		for (;;) {
			const int token = g_buf[ip++];
			if (token >= 16) {
				int run = token >> 4;
				if (run == 15) {
					for (;;) {
						const int c = g_buf[ip++];
						run += c;
						if (c != 255)
							break;
					}
				}
				if ((p + run) > BLOCK_SIZE)
					return -1;

				wild_copy(p, ip, run);
				p += run;
				ip += run;
				if (ip >= ip_end)
					break;
			}

			int s = p - LOAD_16(ip);
			ip += 2;
			if (s < 0)
				return -1;

			int len = (token & 15) + MIN_MATCH;
			if (len == (15 + MIN_MATCH)) {
				for (;;) {
					const int c = g_buf[ip++];
					len += c;
					if (c != 255)
						break;
				}
			}
			if ((p + len) > BLOCK_SIZE)
				return -1;

			if ((p - s) >= 4) {
				wild_copy(p, s, len);
				p += len;
			}
			else {
				while (len-- != 0)
					g_buf[p++] = g_buf[s++];
			}
		}

		if (kwrite(g_buf, p, buf, &write_offset, maxOutputSize) != p) {
			return -1;
		}
	}

	return (int)write_offset;
}
