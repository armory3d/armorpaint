#include "iron_array.h"
#include "iron_gpu.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void     *gpu_create_texture_from_bytes(void *buffer, int width, int height, int format);
void      console_info(char *s);
buffer_t *iron_inflate(buffer_t *bytes, bool raw);

// TIFF tags
#define TAG_IMAGE_WIDTH          256
#define TAG_IMAGE_LENGTH         257
#define TAG_BITS_PER_SAMPLE      258
#define TAG_COMPRESSION          259
#define TAG_PHOTOMETRIC          262
#define TAG_STRIP_OFFSETS        273
#define TAG_SAMPLES_PER_PIXEL    277
#define TAG_ROWS_PER_STRIP       278
#define TAG_STRIP_BYTE_COUNTS    279
#define TAG_PLANAR_CONFIGURATION 284
#define TAG_PREDICTOR            317
#define TAG_COLOR_MAP            320
#define TAG_TILE_WIDTH           322
#define TAG_TILE_LENGTH          323
#define TAG_TILE_OFFSETS         324
#define TAG_TILE_BYTE_COUNTS     325
#define TAG_SAMPLE_FORMAT        339

// Compression types
#define COMP_NONE     1
#define COMP_LZW      5
#define COMP_DEFLATE  8
#define COMP_PACKBITS 32773
#define COMP_DEFLATE2 32946

static bool     tiff_le;
static uint8_t *tiff_buf;
static size_t   tiff_size;

static uint16_t r16(size_t pos) {
	uint8_t *p = tiff_buf + pos;
	return tiff_le ? (uint16_t)(p[0] | p[1] << 8) : (uint16_t)(p[0] << 8 | p[1]);
}

static uint32_t r32(size_t pos) {
	uint8_t *p = tiff_buf + pos;
	return tiff_le ? (uint32_t)(p[0] | p[1] << 8 | p[2] << 16 | p[3] << 24) : (uint32_t)(p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]);
}

static uint32_t ifd_val(size_t vpos, uint16_t type, uint32_t idx) {
	if (type == 3 || type == 8)
		return r16(vpos + idx * 2); // SHORT / SSHORT
	if (type == 4 || type == 9)
		return r32(vpos + idx * 4); // LONG / SLONG
	return tiff_buf[vpos + idx];    // BYTE and others
}

static void packbits_decode(uint8_t *in, size_t in_len, uint8_t *out, size_t out_len) {
	size_t ip = 0, op = 0;
	while (ip < in_len && op < out_len) {
		int8_t n = (int8_t)in[ip++];
		if (n >= 0) {
			size_t cnt = (size_t)(n + 1);
			if (op + cnt > out_len)
				cnt = out_len - op;
			memcpy(out + op, in + ip, cnt);
			ip += (size_t)(n + 1);
			op += cnt;
		}
		else if (n != -128) {
			size_t cnt = (size_t)(-n + 1);
			if (op + cnt > out_len)
				cnt = out_len - op;
			memset(out + op, in[ip++], cnt);
			op += cnt;
		}
	}
}

static void lzw_decode(uint8_t *in, size_t in_len, uint8_t *out, size_t out_len) {
	uint16_t prefix[4096];
	uint8_t  suffix[4096];
	uint8_t  first[4096];
	uint8_t  stack[4096];

	for (int i = 0; i < 256; i++) {
		prefix[i] = 0xFFFF;
		suffix[i] = first[i] = (uint8_t)i;
	}

	int    bit_len  = 9;
	int    nxt      = 258;
	size_t ipos     = 0;
	size_t opos     = 0;
	int    bit_pos  = 0;
	int    old_code = -1;

	while (opos < out_len) {
		// Read bit_len bits, MSB first
		int code = 0;
		for (int b = 0; b < bit_len; b++) {
			if (ipos >= in_len)
				return;
			code = (code << 1) | ((in[ipos] >> (7 - bit_pos)) & 1);
			if (++bit_pos == 8) {
				bit_pos = 0;
				ipos++;
			}
		}

		if (code == 256) {
			bit_len  = 9;
			nxt      = 258;
			old_code = -1;
			continue;
		}
		if (code == 257)
			break;

		int stack_top = 0;
		int cur       = code;

		if (code >= nxt) {
			// Special case: new code equals next table entry
			stack[stack_top++] = first[old_code];
			cur                = old_code;
		}

		while (prefix[cur] != 0xFFFF && stack_top < 4096) {
			stack[stack_top++] = suffix[cur];
			cur                = prefix[cur];
		}
		stack[stack_top++] = suffix[cur]; // root / first char

		if (old_code >= 0 && nxt < 4096) {
			prefix[nxt] = (uint16_t)old_code;
			suffix[nxt] = stack[stack_top - 1]; // first char of current string
			first[nxt]  = first[old_code];
			nxt++;
			if (nxt == (1 << bit_len) && bit_len < 12)
				bit_len++;
		}

		for (int s = stack_top - 1; s >= 0 && opos < out_len; s--)
			out[opos++] = stack[s];

		old_code = code;
	}
}

// Undo horizontal differencing predictor (predictor == 2)
static void undo_predictor(uint8_t *data, uint32_t w, uint32_t h, uint16_t spp, uint16_t bps) {
	uint32_t row_bytes = w * spp * ((bps + 7) / 8);
	for (uint32_t y = 0; y < h; y++) {
		uint8_t *row = data + (size_t)y * row_bytes;
		if (bps == 16) {
			uint16_t *p = (uint16_t *)row;
			for (uint32_t x = 1; x < w; x++)
				for (uint16_t c = 0; c < spp; c++)
					p[x * spp + c] += p[(x - 1) * spp + c];
		}
		else { // 8-bit
			for (uint32_t x = 1; x < w; x++)
				for (uint16_t c = 0; c < spp; c++)
					row[x * spp + c] += row[(x - 1) * spp + c];
		}
	}
}

void *io_tiff_parse(uint8_t *buf, size_t buf_size) {
	tiff_buf  = buf;
	tiff_size = buf_size;

	if (buf_size < 8)
		return NULL;
	if (buf[0] == 'I' && buf[1] == 'I')
		tiff_le = true;
	else if (buf[0] == 'M' && buf[1] == 'M')
		tiff_le = false;
	else
		return NULL;
	if (r16(2) != 42)
		return NULL;

	uint32_t ifd_offset = r32(4);

	uint32_t width = 0, height = 0;
	uint16_t bps            = 8;
	uint16_t bps_arr[4]     = {8, 8, 8, 8};
	uint16_t compression    = COMP_NONE;
	uint16_t photometric    = 2;
	uint16_t spp            = 3;
	uint16_t predictor      = 1;
	uint32_t rows_per_strip = 0xFFFFFFFF;
	uint32_t tile_w = 0, tile_h = 0;
	uint16_t sample_fmt = 1;

	uint32_t *strip_offsets = NULL;
	uint32_t *strip_counts  = NULL;
	uint32_t  num_strips    = 0;
	uint32_t *tile_offsets  = NULL;
	uint32_t *tile_counts   = NULL;
	uint32_t  num_tiles     = 0;
	uint16_t *color_map     = NULL;
	uint32_t  color_map_len = 0;

	static const size_t type_sizes[] = {0, 1, 1, 2, 4, 8, 1, 1, 2, 4, 8, 4, 8};

	size_t   pos         = ifd_offset;
	uint16_t num_entries = r16(pos);
	pos += 2;

	for (uint16_t i = 0; i < num_entries; i++, pos += 12) {
		uint16_t tag   = r16(pos);
		uint16_t type  = r16(pos + 2);
		uint32_t count = r32(pos + 4);
		uint32_t vraw  = r32(pos + 8);

		size_t tsz  = (type < 13) ? type_sizes[type] : 1;
		size_t vpos = (tsz * count <= 4) ? (pos + 8) : vraw;

		switch (tag) {
		case TAG_IMAGE_WIDTH:
			width = ifd_val(vpos, type, 0);
			break;
		case TAG_IMAGE_LENGTH:
			height = ifd_val(vpos, type, 0);
			break;
		case TAG_COMPRESSION:
			compression = (uint16_t)ifd_val(vpos, type, 0);
			break;
		case TAG_PHOTOMETRIC:
			photometric = (uint16_t)ifd_val(vpos, type, 0);
			break;
		case TAG_SAMPLES_PER_PIXEL:
			spp = (uint16_t)ifd_val(vpos, type, 0);
			break;
		case TAG_PREDICTOR:
			predictor = (uint16_t)ifd_val(vpos, type, 0);
			break;
		case TAG_SAMPLE_FORMAT:
			sample_fmt = (uint16_t)ifd_val(vpos, type, 0);
			break;
		case TAG_ROWS_PER_STRIP:
			rows_per_strip = ifd_val(vpos, type, 0);
			break;
		case TAG_TILE_WIDTH:
			tile_w = ifd_val(vpos, type, 0);
			break;
		case TAG_TILE_LENGTH:
			tile_h = ifd_val(vpos, type, 0);
			break;
		case TAG_BITS_PER_SAMPLE:
			bps = (uint16_t)ifd_val(vpos, type, 0);
			for (uint32_t c = 0; c < count && c < 4; c++)
				bps_arr[c] = (uint16_t)ifd_val(vpos, type, c);
			break;
		case TAG_STRIP_OFFSETS:
			num_strips    = count;
			strip_offsets = (uint32_t *)malloc(count * sizeof(uint32_t));
			for (uint32_t s = 0; s < count; s++)
				strip_offsets[s] = ifd_val(vpos, type, s);
			break;
		case TAG_STRIP_BYTE_COUNTS:
			strip_counts = (uint32_t *)malloc(count * sizeof(uint32_t));
			for (uint32_t s = 0; s < count; s++)
				strip_counts[s] = ifd_val(vpos, type, s);
			break;
		case TAG_TILE_OFFSETS:
			num_tiles    = count;
			tile_offsets = (uint32_t *)malloc(count * sizeof(uint32_t));
			for (uint32_t t = 0; t < count; t++)
				tile_offsets[t] = ifd_val(vpos, type, t);
			break;
		case TAG_TILE_BYTE_COUNTS:
			tile_counts = (uint32_t *)malloc(count * sizeof(uint32_t));
			for (uint32_t t = 0; t < count; t++)
				tile_counts[t] = ifd_val(vpos, type, t);
			break;
		case TAG_COLOR_MAP:
			color_map_len = count;
			color_map     = (uint16_t *)malloc(count * sizeof(uint16_t));
			for (uint32_t c = 0; c < count; c++)
				color_map[c] = (uint16_t)ifd_val(vpos, 3, c);
			break;
		}
	}

	if (width == 0 || height == 0)
		return NULL;

	uint8_t *rgba      = (uint8_t *)calloc((size_t)width * height * 4, 1);
	uint32_t bps_bytes = (bps + 7) / 8;

	bool     tiled   = (tile_w > 0 && tile_h > 0 && tile_offsets != NULL);
	uint32_t tiles_x = tiled ? (width + tile_w - 1) / tile_w : 1;
	uint32_t blk_cnt = tiled ? num_tiles : num_strips;

	for (uint32_t blk = 0; blk < blk_cnt; blk++) {
		uint32_t off  = tiled ? tile_offsets[blk] : strip_offsets[blk];
		uint32_t blen = tiled ? tile_counts[blk] : strip_counts[blk];
		if (off + blen > buf_size)
			break;

		uint32_t blk_x, blk_y, blk_w, blk_h;
		if (tiled) {
			blk_x = (blk % tiles_x) * tile_w;
			blk_y = (blk / tiles_x) * tile_h;
			blk_w = tile_w;
			blk_h = tile_h;
		}
		else {
			blk_x = 0;
			blk_y = blk * rows_per_strip;
			blk_w = width;
			blk_h = rows_per_strip;
			if (blk_y + blk_h > height)
				blk_h = height - blk_y;
		}

		size_t   row_stride  = (size_t)blk_w * spp * bps_bytes;
		size_t   expected_sz = (size_t)blk_h * row_stride;
		uint8_t *raw         = NULL;
		bool     raw_free    = false;

		if (compression == COMP_NONE) {
			raw = buf + off;
		}
		else if (compression == COMP_PACKBITS) {
			raw      = (uint8_t *)malloc(expected_sz);
			raw_free = true;
			packbits_decode(buf + off, blen, raw, expected_sz);
		}
		else if (compression == COMP_LZW) {
			raw      = (uint8_t *)malloc(expected_sz);
			raw_free = true;
			lzw_decode(buf + off, blen, raw, expected_sz);
		}
#ifdef WITH_COMPRESS
		else if (compression == COMP_DEFLATE || compression == COMP_DEFLATE2) {
			buffer_t compressed;
			compressed.buffer = buf + off;
			compressed.length = compressed.capacity = blen;
			buffer_t *decomp                        = iron_inflate(&compressed, false);
			raw                                     = decomp->buffer;
			raw_free                                = false;
		}
#endif
		else {
			console_info("Error: TIFF compression type not supported");
			continue;
		}

		if (!raw)
			continue;

		if (predictor == 2 && compression != COMP_NONE)
			undo_predictor(raw, blk_w, blk_h, spp, bps);

		for (uint32_t row = 0; row < blk_h; row++) {
			uint32_t img_y = blk_y + row;
			if (img_y >= height)
				break;
			uint8_t *src = raw + row * row_stride;

			for (uint32_t col = 0; col < blk_w; col++) {
				uint32_t img_x = blk_x + col;
				if (img_x >= width)
					break;
				uint8_t *dst = rgba + ((size_t)img_y * width + img_x) * 4;

				uint8_t r = 0, g = 0, b = 0, a = 255;

				if (photometric == 3) {
					// Palette / indexed color
					uint32_t idx = 0;
					if (bps == 8)
						idx = src[col];
					else if (bps == 4)
						idx = (col & 1) ? (src[col / 2] & 0x0F) : (src[col / 2] >> 4);
					else if (bps == 1)
						idx = (src[col / 8] >> (7 - col % 8)) & 1;
					uint32_t nc = color_map_len / 3;
					if (color_map && idx < nc) {
						r = color_map[idx] >> 8;
						g = color_map[nc + idx] >> 8;
						b = color_map[nc * 2 + idx] >> 8;
					}
				}
				else if (bps == 8) {
					uint8_t *p = src + col * spp;
					if (photometric <= 1) {
						uint8_t v = photometric == 0 ? 255 - p[0] : p[0];
						r = g = b = v;
						a         = spp >= 2 ? p[1] : 255;
					}
					else {
						r = p[0];
						g = p[1];
						b = p[2];
						a = spp >= 4 ? p[3] : 255;
					}
				}
				else if (bps == 16) {
					uint16_t *p = (uint16_t *)(src + col * spp * 2);
					if (photometric <= 1) {
						uint8_t v = photometric == 0 ? 255 - p[0] / 257 : p[0] / 257;
						r = g = b = v;
						a         = spp >= 2 ? p[1] / 257 : 255;
					}
					else {
						r = p[0] / 257;
						g = p[1] / 257;
						b = p[2] / 257;
						a = spp >= 4 ? p[3] / 257 : 255;
					}
				}
				else if (bps == 32 && sample_fmt == 3) {
					// 32-bit float
					float *p = (float *)(src + col * spp * 4);
					if (photometric <= 1) {
						float v = photometric == 0 ? 1.0f - p[0] : p[0];
						v       = v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
						r = g = b = (uint8_t)(v * 255.0f);
						a         = spp >= 2 ? (uint8_t)((p[1] < 0.0f ? 0.0f : p[1] > 1.0f ? 1.0f : p[1]) * 255.0f) : 255;
					}
					else {
						float fr = p[0] < 0.0f ? 0.0f : p[0] > 1.0f ? 1.0f : p[0];
						float fg = p[1] < 0.0f ? 0.0f : p[1] > 1.0f ? 1.0f : p[1];
						float fb = p[2] < 0.0f ? 0.0f : p[2] > 1.0f ? 1.0f : p[2];
						r        = (uint8_t)(fr * 255.0f);
						g        = (uint8_t)(fg * 255.0f);
						b        = (uint8_t)(fb * 255.0f);
						a        = spp >= 4 ? (uint8_t)((p[3] < 0.0f ? 0.0f : p[3] > 1.0f ? 1.0f : p[3]) * 255.0f) : 255;
					}
				}

				dst[0] = r;
				dst[1] = g;
				dst[2] = b;
				dst[3] = a;
			}
		}

		if (raw_free)
			free(raw);
	}

	free(strip_offsets);
	free(strip_counts);
	free(tile_offsets);
	free(tile_counts);
	free(color_map);

	buffer_t *b = (buffer_t *)malloc(sizeof(buffer_t));
	b->buffer   = rgba;
	b->length = b->capacity = (uint32_t)((size_t)width * height * 4);
	return gpu_create_texture_from_bytes(b, (int)width, (int)height, GPU_TEXTURE_FORMAT_RGBA32);
}
