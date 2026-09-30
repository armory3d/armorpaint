#include "iron_array.h"
#include "iron_gpu.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void     *gpu_create_texture_from_bytes(void *buffer, int width, int height, int format);
void      console_info(char *s);
buffer_t *iron_inflate(buffer_t *bytes, bool raw);

typedef struct {
	char name[256];
	int  pixel_type;
	bool linear;
} channel_t;

#ifdef WITH_COMPRESS
static void zip_undo(buffer_t *decomp, uint8_t *out) {
	uint8_t *t    = decomp->buffer + 1;
	uint8_t *stop = decomp->buffer + decomp->length;
	int      p    = decomp->buffer[0];
	while (t < stop) {
		int d    = *t;
		int orig = (d - 128 + p) & 0xFF;
		p        = orig;
		*t       = (uint8_t)orig;
		++t;
	}
	size_t half = (decomp->length + 1) / 2;
	for (size_t i = 0; i < half; i++) {
		out[i * 2] = decomp->buffer[i];
		if (i * 2 + 1 < decomp->length) {
			out[i * 2 + 1] = decomp->buffer[half + i];
		}
	}
}

typedef struct {
	uint8_t *p;
	uint64_t c;
	int      lc;
} bits_t;

static uint32_t get_bits(bits_t *b, int n) {
	while (b->lc < n) {
		b->c = (b->c << 8) | *b->p++;
		b->lc += 8;
	}
	b->lc -= n;
	return (uint32_t)(b->c >> b->lc) & ((1u << n) - 1);
}

// OpenEXR huffman
static void huf_decode(uint8_t *src, uint16_t *out, uint64_t out_count) {
	uint32_t im    = *(uint32_t *)(src);
	uint32_t iM    = *(uint32_t *)(src + 4);
	uint32_t nbits = *(uint32_t *)(src + 12);
	uint8_t *lens  = calloc(65537, 1);
	bits_t   b     = {src + 20, 0, 0};
	for (uint32_t i = im; i <= iM && i < 65537; i++) {
		int l = get_bits(&b, 6);
		if (l == 63) {
			i += get_bits(&b, 8) + 6 - 1; // Long zero run
		}
		else if (l >= 59) {
			i += l - 59 + 2 - 1; // Short zero run
		}
		else {
			lens[i] = l;
		}
	}

	// Canonical codes, longest codes start at 0
	uint64_t count[59] = {0};
	uint64_t first[59] = {0};
	uint32_t offset[59];
	for (uint32_t i = im; i <= iM && i < 65537; i++) {
		count[lens[i]]++;
	}
	uint64_t c = 0;
	for (int l = 58; l > 0; l--) {
		first[l] = c;
		c        = (c + count[l]) >> 1;
	}
	uint32_t n = 0;
	for (int l = 1; l <= 58; l++) {
		offset[l] = n;
		n += (uint32_t)count[l];
	}
	uint32_t *syms = malloc((n + 1) * sizeof(uint32_t));
	for (uint32_t i = im; i <= iM && i < 65537; i++) {
		if (lens[i] > 0) {
			syms[offset[lens[i]]++] = i;
		}
	}
	for (int l = 1; l <= 58; l++) {
		offset[l] -= (uint32_t)count[l];
	}

	bits_t   d    = {b.p, 0, 0}; // Data starts at the next byte
	uint64_t code = 0;
	int      len  = 0;
	uint64_t o    = 0;
	for (uint32_t k = 0; k < nbits && o < out_count && len < 58; k++) {
		code = (code << 1) | get_bits(&d, 1);
		len++;
		if (code - first[len] < count[len]) {
			uint32_t sym = syms[offset[len] + (code - first[len])];
			if (sym == iM) { // Run of the previous value
				int r = get_bits(&d, 8);
				k += 8;
				while (r-- > 0 && o > 0 && o < out_count) {
					out[o] = out[o - 1];
					o++;
				}
			}
			else {
				out[o++] = (uint16_t)sym;
			}
			code = 0;
			len  = 0;
		}
	}
	free(syms);
	free(lens);
}

static void wdec(bool w14, uint16_t l, uint16_t h, uint16_t *a, uint16_t *b) {
	if (w14) {
		int hi = (int16_t)h;
		int ai = (int16_t)l + (hi & 1) + (hi >> 1);
		*a     = (uint16_t)ai;
		*b     = (uint16_t)(ai - hi);
	}
	else {
		int bb = (l - (h >> 1)) & 0xffff;
		*b     = (uint16_t)bb;
		*a     = (uint16_t)((h + bb - 0x8000) & 0xffff);
	}
}

// Inverse 2d haar wavelet
static void wav2_decode(uint16_t *in, int nx, int ox, int ny, int oy, uint16_t mx) {
	bool w14 = mx < (1 << 14);
	int  n   = nx > ny ? ny : nx;
	int  p   = 1;
	while (p <= n) {
		p <<= 1;
	}
	p >>= 1;
	int p2 = p;
	p >>= 1;
	while (p >= 1) {
		int oy1 = oy * p;
		int oy2 = oy * p2;
		int ox1 = ox * p;
		int ox2 = ox * p2;
		int ey  = oy * (ny - p2);
		int py  = 0;
		for (; py <= ey; py += oy2) {
			int px = py;
			int ex = py + ox * (nx - p2);
			for (; px <= ex; px += ox2) {
				uint16_t *p00 = in + px;
				uint16_t *p01 = p00 + ox1;
				uint16_t *p10 = p00 + oy1;
				uint16_t *p11 = p10 + ox1;
				uint16_t  i00, i01, i10, i11;
				wdec(w14, *p00, *p10, &i00, &i10);
				wdec(w14, *p01, *p11, &i01, &i11);
				wdec(w14, i00, i01, p00, p01);
				wdec(w14, i10, i11, p10, p11);
			}
			if (nx & p) { // Odd column
				uint16_t i00;
				wdec(w14, in[px], in[px + oy1], &i00, &in[px + oy1]);
				in[px] = i00;
			}
		}
		if (ny & p) { // Odd line
			int px = py;
			int ex = py + ox * (nx - p2);
			for (; px <= ex; px += ox2) {
				uint16_t i00;
				wdec(w14, in[px], in[px + ox1], &i00, &in[px + ox1]);
				in[px] = i00;
			}
		}
		p2 = p;
		p >>= 1;
	}
}

static void piz_decode(uint8_t *src, uint8_t *dst, int width, int lines, channel_t *channels, int num_channels) {
	uint16_t min_non_zero = *(uint16_t *)src;
	uint16_t max_non_zero = *(uint16_t *)(src + 2);
	uint8_t *p            = src + 4;
	uint8_t  bitmap[8192] = {0};
	if (max_non_zero >= 8192) {
		return;
	}
	if (min_non_zero <= max_non_zero) {
		memcpy(bitmap + min_non_zero, p, max_non_zero - min_non_zero + 1);
		p += max_non_zero - min_non_zero + 1;
	}
	uint16_t *lut = calloc(65536, 2);
	int       k   = 0;
	for (int i = 0; i < 65536; i++) {
		if (i == 0 || (bitmap[i >> 3] & (1 << (i & 7)))) {
			lut[k++] = (uint16_t)i;
		}
	}
	p += 4; // Huffman length

	// Channels are stored one after another, float as two shorts
	size_t total = 0;
	for (int c = 0; c < num_channels; c++) {
		total += (size_t)width * lines * (channels[c].pixel_type == 1 ? 1 : 2);
	}
	uint16_t *tmp = calloc(total, 2);
	huf_decode(p, tmp, total);
	uint16_t *start = tmp;
	for (int c = 0; c < num_channels; c++) {
		int size = channels[c].pixel_type == 1 ? 1 : 2;
		for (int j = 0; j < size; j++) {
			wav2_decode(start + j, width, size, lines, width * size, (uint16_t)(k - 1));
		}
		start += (size_t)width * lines * size;
	}
	for (size_t i = 0; i < total; i++) {
		tmp[i] = lut[tmp[i]];
	}

	uint16_t *out = (uint16_t *)dst;
	for (int y = 0; y < lines; y++) {
		start = tmp;
		for (int c = 0; c < num_channels; c++) {
			int size = channels[c].pixel_type == 1 ? 1 : 2;
			memcpy(out, start + (size_t)y * width * size, width * size * 2);
			out += width * size;
			start += (size_t)width * lines * size;
		}
	}
	free(tmp);
	free(lut);
}

static float half_to_float(uint16_t h) {
	uint32_t e = (h >> 10) & 0x1f;
	uint32_t m = h & 0x3ff;
	float    f = e == 0 ? m * (1.0f / 16777216.0f) : ldexpf((float)(m | 0x400), (int)e - 25);
	return (h & 0x8000) ? -f : f;
}

static const uint8_t zigzag[64] = {0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,  12, 19, 26, 33, 40, 48,
                                   41, 34, 27, 20, 13, 6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23,
                                   30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

// DWAA / DWAB, lossy dct channels only
static void dwa_decode(uint8_t *src, uint8_t *dst, int width, int lines, channel_t *channels, int num_channels) {
	uint64_t h[11]; // version, unknown uncompressed / compressed, ac, dc, rle compressed, rle uncompressed, rle raw, ac count, dc count, ac compression
	memcpy(h, src, sizeof(h));
	uint8_t *p = src + sizeof(h);
	if (h[0] >= 2) {
		p += *(uint16_t *)p; // Channel rules
	}
	p += h[2]; // Unknown data

	uint16_t *ac = calloc(h[8] + 1, 2);
	if (h[10] == 0) { // Static huffman
		huf_decode(p, ac, h[8]);
	}
	else { // Deflate
		buffer_t  compressed = {.buffer = p, .length = (uint32_t)h[3], .capacity = (uint32_t)h[3]};
		buffer_t *decomp     = iron_inflate(&compressed, false);
		memcpy(ac, decomp->buffer, decomp->length < h[8] * 2 ? decomp->length : h[8] * 2);
		free(decomp->buffer);
	}
	p += h[3];

	uint16_t *dc         = calloc(h[9] + 1, 2);
	buffer_t  compressed = {.buffer = p, .length = (uint32_t)h[4], .capacity = (uint32_t)h[4]};
	buffer_t *decomp     = iron_inflate(&compressed, false);
	if (decomp->length <= h[9] * 2) {
		zip_undo(decomp, (uint8_t *)dc);
	}
	free(decomp->buffer);

	float cosines[8][8];
	for (int x = 0; x < 8; x++) {
		for (int u = 0; u < 8; u++) {
			cosines[x][u] = (u == 0 ? sqrtf(0.125f) : 0.5f) * cosf((2 * x + 1) * u * 3.14159265f / 16.0f);
		}
	}

	// Rgb channels are decoded together from ycbcr, followed by the rest
	int csc[3] = {-1, -1, -1};
	for (int c = 0; c < num_channels; c++) {
		char *s = strchr("RGBrgb", channels[c].name[0]);
		if (s != NULL && channels[c].name[1] == 0) {
			csc[(s - "RGBrgb") % 3] = c;
		}
	}
	bool has_csc = csc[0] >= 0 && csc[1] >= 0 && csc[2] >= 0;
	int  groups[4][3];
	int  group_size[4];
	int  num_groups = 0;
	if (has_csc) {
		memcpy(groups[num_groups], csc, sizeof(csc));
		group_size[num_groups++] = 3;
	}
	for (int c = 0; c < num_channels; c++) {
		if (!has_csc || (c != csc[0] && c != csc[1] && c != csc[2])) {
			groups[num_groups][0]    = c;
			group_size[num_groups++] = 1;
		}
	}

	int       blocks_x = (width + 7) / 8;
	int       blocks_y = (lines + 7) / 8;
	uint16_t *acp      = ac;
	uint16_t *ac_end   = ac + h[8];
	uint16_t *dcp      = dc;
	for (int g = 0; g < num_groups; g++) {
		int n = group_size[g];
		for (int by = 0; by < blocks_y; by++) {
			for (int bx = 0; bx < blocks_x; bx++) {
				float block[3][64];
				for (int c = 0; c < n; c++) {
					uint16_t zz[64] = {0};
					zz[0]           = dcp[c * blocks_x * blocks_y + by * blocks_x + bx];
					for (int i = 1; i < 64 && acp < ac_end;) {
						uint16_t v = *acp++;
						if (v == 0xff00) { // End of block
							break;
						}
						else if ((v >> 8) == 0xff) { // Zero run
							i += v & 0xff;
						}
						else {
							zz[i++] = v;
						}
					}
					float coef[64];
					for (int i = 0; i < 64; i++) {
						coef[zigzag[i]] = half_to_float(zz[i]);
					}
					// Inverse dct
					float tmp[64];
					for (int y = 0; y < 8; y++) {
						for (int x = 0; x < 8; x++) {
							float s = 0.0f;
							for (int u = 0; u < 8; u++) {
								s += cosines[x][u] * coef[y * 8 + u];
							}
							tmp[y * 8 + x] = s;
						}
					}
					for (int y = 0; y < 8; y++) {
						for (int x = 0; x < 8; x++) {
							float s = 0.0f;
							for (int v = 0; v < 8; v++) {
								s += cosines[y][v] * tmp[v * 8 + x];
							}
							block[c][y * 8 + x] = s;
						}
					}
				}
				if (n == 3) { // Rec709 ycbcr to rgb
					for (int i = 0; i < 64; i++) {
						float y     = block[0][i];
						float cb    = block[1][i];
						float cr    = block[2][i];
						block[0][i] = y + 1.5747f * cr;
						block[1][i] = y - 0.1873f * cb - 0.4682f * cr;
						block[2][i] = y + 1.8556f * cb;
					}
				}
				for (int c = 0; c < n; c++) {
					int ch = groups[g][c];
					for (int y = 0; y < 8 && by * 8 + y < lines; y++) {
						uint16_t *row = (uint16_t *)dst + ((size_t)(by * 8 + y) * num_channels + ch) * width;
						for (int x = 0; x < 8 && bx * 8 + x < width; x++) {
							float f = block[c][y * 8 + x];
							if (!channels[ch].linear) { // Nonlinear to linear
								float a = fabsf(f);
								a       = a <= 1.0f ? powf(a, 2.2f) : expf(2.2f * (a - 1.0f));
								f       = f < 0 ? -a : a;
							}
							row[bx * 8 + x] = float_to_half_fast(f);
						}
					}
				}
			}
		}
		dcp += n * blocks_x * blocks_y;
	}
	free(ac);
	free(dc);
}
#endif

void *io_exr_parse(uint8_t *buf, size_t buf_size) {
	if (buf[0] != 0x76 || buf[1] != 0x2f || buf[2] != 0x31 || buf[3] != 0x01) {
		return NULL;
	}
	size_t pos = 0;
	pos += 4;
	pos += 4; // version

	int       width      = 0;
	int       height     = 0;
	int       bits       = 16;
	int       pixel_type = 0;
	channel_t channels[4];
	int       num_channels = 0;
	int       compression  = 0;

	while (1) {
		char name[256];
		int  i = 0;
		while (buf[pos] != 0) {
			name[i++] = (char)buf[pos];
			pos++;
		}
		name[i] = 0;
		pos++; // null

		if (strlen(name) == 0) {
			break; // end of header
		}

		char attr_type[256];
		i = 0;
		while (buf[pos] != 0) {
			attr_type[i++] = (char)buf[pos];
			pos++;
		}
		attr_type[i] = 0;
		pos++; // null

		uint32_t attr_size = *(uint32_t *)(buf + pos);
		pos += 4;

		if (strcmp(name, "channels") == 0 && strcmp(attr_type, "chlist") == 0) {
			size_t chpos = pos;
			while (1) {
				char chname[256];
				i = 0;
				while (buf[chpos] != 0) {
					chname[i++] = (char)buf[chpos];
					chpos++;
				}
				chname[i] = 0;
				chpos++; // null
				if (strlen(chname) == 0) {
					break;
				}

				int32_t chpixel_type = *(int32_t *)(buf + chpos);
				chpos += 4;
				uint8_t p_linear = buf[chpos];
				chpos += 1; // 1 byte
				chpos += 3; // Skip reserved (3 bytes)
				// int32_t xSampling = *(int32_t *)(buf + chpos);
				chpos += 4;
				// int32_t ySampling = *(int32_t *)(buf + chpos);
				chpos += 4;

				strcpy(channels[num_channels].name, chname);
				channels[num_channels].pixel_type = chpixel_type;
				channels[num_channels].linear     = p_linear != 0;
				num_channels++;
			}
		}
		else if (strcmp(name, "dataWindow") == 0 || strcmp(name, "displayWindow") == 0) {
			int32_t xMin = *(int32_t *)(buf + pos);
			int32_t yMin = *(int32_t *)(buf + pos + 4);
			int32_t xMax = *(int32_t *)(buf + pos + 8);
			int32_t yMax = *(int32_t *)(buf + pos + 12);
			if (strcmp(name, "dataWindow") == 0) {
				width  = xMax - xMin + 1;
				height = yMax - yMin + 1;
			}
		}
		else if (strcmp(name, "compression") == 0) {
			compression = buf[pos];
			if (compression > 4 && compression != 8 && compression != 9) {
				console_info("Error: This exr compression type is not yet implemented");
				return NULL;
			}
		}
		pos += attr_size;
	}

	pixel_type = channels[0].pixel_type;
	bits       = (pixel_type == 1) ? 16 : 32;

	int lines_per_chunk = compression == 3 ? 16 : compression == 4 || compression == 8 ? 32 : compression == 9 ? 256 : 1;
	int num_chunks      = (height + lines_per_chunk - 1) / lines_per_chunk;
	if (compression == 8 || compression == 9) {
		for (int c = 0; c < num_channels; c++) {
			if (channels[c].pixel_type != 1 || channels[c].name[1] != 0 || strchr("RGBYrgby", channels[c].name[0]) == NULL) {
				console_info("Error: This exr compression type is not yet implemented for this channel");
				return NULL;
			}
		}
	}

	uint32_t *line_offset_table = (uint32_t *)malloc(num_chunks * sizeof(uint32_t));
	for (int y = 0; y < num_chunks; y++) {
		uint32_t lo          = *(uint32_t *)(buf + pos);
		line_offset_table[y] = lo;
		pos += 8;
	}

	int r_idx = -1;
	int g_idx = -1;
	int b_idx = -1;
	int a_idx = -1;
	for (int c = 0; c < num_channels; c++) {
		if (strcmp(channels[c].name, "R") == 0)
			r_idx = c;
		else if (strcmp(channels[c].name, "G") == 0)
			g_idx = c;
		else if (strcmp(channels[c].name, "B") == 0)
			b_idx = c;
		else if (strcmp(channels[c].name, "A") == 0)
			a_idx = c;
	}

	bool     is_16bit      = bits == 16;
	int      channel_bytes = is_16bit ? 2 : 4;
	size_t   image_size    = (size_t)width * height * 4 * channel_bytes;
	uint8_t *pixels        = (uint8_t *)malloc(image_size);
	uint8_t *reordered     = NULL;
	size_t   chunk_line    = (size_t)width * num_channels * channel_bytes;
	uint8_t *chunk_data    = lines_per_chunk > 1 ? (uint8_t *)malloc(chunk_line * lines_per_chunk) : NULL;

	for (int y = 0; y < height; y++) {
		uint32_t  scan_line_pos  = line_offset_table[y / lines_per_chunk];
		uint32_t  compressed_len = *(uint32_t *)(buf + scan_line_pos + 4);
		uint32_t  off            = scan_line_pos + 8;
		uint8_t  *line_data      = NULL;
		buffer_t *decomp;

		if (compression == 0) { // None
			line_data = buf + off;
		}

#ifdef WITH_COMPRESS
		else if (compression == 1) { // RLE
			size_t size = chunk_line;
			if (compressed_len >= size) { // Stored uncompressed
				line_data = buf + off;
			}
			else {
				if (reordered == NULL) {
					reordered = malloc(size * 2);
				}
				buffer_t unrle = {.buffer = reordered + size, .length = 0, .capacity = (uint32_t)size};
				int8_t  *in    = (int8_t *)(buf + off);
				int8_t  *end   = in + compressed_len;
				while (in < end) {
					int count = *in++;
					if (count < 0) { // Literal bytes
						count = -count;
						if (unrle.length + count > size || in + count > end) {
							break;
						}
						memcpy(unrle.buffer + unrle.length, in, count);
						in += count;
					}
					else { // Repeated byte
						count += 1;
						if (unrle.length + count > size || in >= end) {
							break;
						}
						memset(unrle.buffer + unrle.length, *(uint8_t *)in++, count);
					}
					unrle.length += count;
				}
				zip_undo(&unrle, reordered);
				line_data = reordered;
			}
		}
		else if (compression == 2) { // ZIPS
			buffer_t compressed;
			compressed.buffer = buf + off;
			compressed.length = compressed.capacity = compressed_len;
			decomp                                  = iron_inflate(&compressed, false);

			if (reordered == NULL) {
				reordered = malloc(decomp->length);
			}
			zip_undo(decomp, reordered);
			line_data = reordered;
		}
		else if (compression == 3 || compression == 4) { // ZIP, PIZ
			if (y % lines_per_chunk == 0) {
				int    lines = height - y < lines_per_chunk ? height - y : lines_per_chunk;
				size_t size  = lines * chunk_line;
				if (compressed_len >= size) { // Stored uncompressed
					memcpy(chunk_data, buf + off, size);
				}
				else if (compression == 4) {
					piz_decode(buf + off, chunk_data, width, lines, channels, num_channels);
				}
				else {
					buffer_t compressed;
					compressed.buffer = buf + off;
					compressed.length = compressed.capacity = compressed_len;
					decomp                                  = iron_inflate(&compressed, false);
					if (decomp->length <= size) {
						zip_undo(decomp, chunk_data);
					}
					free(decomp->buffer);
				}
			}
			line_data = chunk_data + (y % lines_per_chunk) * chunk_line;
		}
		else if (compression == 8 || compression == 9) { // DWAA, DWAB
			if (y % lines_per_chunk == 0) {
				int lines = height - y < lines_per_chunk ? height - y : lines_per_chunk;
				dwa_decode(buf + off, chunk_data, width, lines, channels, num_channels);
			}
			line_data = chunk_data + (y % lines_per_chunk) * chunk_line;
		}
#endif

		if (line_data) {
			uint8_t *plane_starts[4];
			size_t   plane_size = (size_t)width * channel_bytes;
			for (int c = 0; c < num_channels; c++) {
				plane_starts[c] = line_data + (size_t)c * plane_size;
			}

			for (int x = 0; x < width; x++) {
				size_t outi = ((size_t)y * width + x) * 4 * channel_bytes;

				if (is_16bit) {
					uint16_t vals[4] = {0};
					for (int c = 0; c < num_channels; c++) {
						vals[c] = *(uint16_t *)(plane_starts[c] + (size_t)x * channel_bytes);
					}
					uint16_t r_h = (r_idx >= 0 ? vals[r_idx] : (num_channels > 0 ? vals[0] : 0));
					uint16_t g_h = (g_idx >= 0 ? vals[g_idx] : r_h);
					uint16_t b_h = (b_idx >= 0 ? vals[b_idx] : r_h);
					uint16_t a_h = (a_idx >= 0 ? vals[a_idx] : 0x3c00);

					*(uint16_t *)(pixels + outi + 0 * channel_bytes) = r_h;
					*(uint16_t *)(pixels + outi + 1 * channel_bytes) = g_h;
					*(uint16_t *)(pixels + outi + 2 * channel_bytes) = b_h;
					*(uint16_t *)(pixels + outi + 3 * channel_bytes) = a_h;
				}
				else {
					float vals_f[4] = {0.0f};
					for (int c = 0; c < num_channels; c++) {
						vals_f[c] = *(float *)(plane_starts[c] + (size_t)x * channel_bytes);
					}
					float r = (r_idx >= 0 ? vals_f[r_idx] : (num_channels > 0 ? vals_f[0] : 0.0f));
					float g = (g_idx >= 0 ? vals_f[g_idx] : r);
					float b = (b_idx >= 0 ? vals_f[b_idx] : r);
					float a = (a_idx >= 0 ? vals_f[a_idx] : 1.0f);

					*(float *)(pixels + outi + 0)  = r;
					*(float *)(pixels + outi + 4)  = g;
					*(float *)(pixels + outi + 8)  = b;
					*(float *)(pixels + outi + 12) = a;
				}
			}
		}
	}

	free(line_offset_table);
	free(chunk_data);

	buffer_t *b = (buffer_t *)malloc(sizeof(buffer_t));
	b->buffer   = pixels;
	b->length = b->capacity = (uint32_t)image_size;
	int format              = is_16bit ? GPU_TEXTURE_FORMAT_RGBA64 : GPU_TEXTURE_FORMAT_RGBA128;
	return gpu_create_texture_from_bytes(b, width, height, format);
}
