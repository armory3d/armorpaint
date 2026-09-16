#include "iron_array.h"
#include "iron_gpu.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *gpu_create_texture_from_bytes(void *buffer, int width, int height, int format);
void  console_info(char *s);
void  io_psd_import_layer(char *file_name, char *layer_name, void *tex);

static uint16_t psd_r16(uint8_t *buf, size_t pos) {
	return (uint16_t)(buf[pos] << 8 | buf[pos + 1]);
}

static uint32_t psd_r32(uint8_t *buf, size_t pos) {
	return (uint32_t)(buf[pos] << 24 | buf[pos + 1] << 16 | buf[pos + 2] << 8 | buf[pos + 3]);
}

static uint64_t psd_r64(uint8_t *buf, size_t pos) {
	return ((uint64_t)psd_r32(buf, pos) << 32) | psd_r32(buf, pos + 4);
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

// Assemble one pixel from planar channel buffers into RGBA8
// bufs[c] is the plane for channel c; r/g/b/a_c are channel indices (-1 = missing)
static void psd_pixel(uint8_t *dst, uint8_t **bufs, int r_c, int g_c, int b_c, int a_c, size_t src_i, uint16_t depth, uint16_t color_mode) {
	uint8_t r = 0, g = 0, b = 0, a = 255;
#define CHAN8(c)   ((c) >= 0 && bufs[c] ? bufs[c][src_i] : 0)
#define CHAN16(c)  ((c) >= 0 && bufs[c] ? (uint8_t)(psd_r16(bufs[c], src_i) / 257) : 0)
#define CHANF(c)   ((c) >= 0 && bufs[c] ? *(float *)(bufs[c] + src_i) : 0.0f)
#define CLAMP01(v) ((v) < 0.0f ? 0.0f : (v) > 1.0f ? 1.0f : (v))

	if (color_mode == 1) { // Grayscale
		uint8_t v = (depth == 16) ? (uint8_t)(psd_r16(bufs[0], src_i) / 257) : bufs[0][src_i];
		if (depth == 32) {
			float fv = *(float *)(bufs[0] + src_i);
			v        = (uint8_t)(CLAMP01(fv) * 255.0f);
		}
		r = g = b = v;
		if (a_c >= 0 && bufs[a_c]) {
			if (depth == 8)
				a = bufs[a_c][src_i];
			else if (depth == 16)
				a = (uint8_t)(psd_r16(bufs[a_c], src_i) / 257);
			else if (depth == 32)
				a = (uint8_t)(CLAMP01(*(float *)(bufs[a_c] + src_i)) * 255.0f);
		}
	}
	else {
		if (depth == 8) {
			r = CHAN8(r_c);
			g = CHAN8(g_c);
			b = CHAN8(b_c);
			a = (a_c >= 0 && bufs[a_c]) ? bufs[a_c][src_i] : 255;
		}
		else if (depth == 16) {
			r = CHAN16(r_c);
			g = CHAN16(g_c);
			b = CHAN16(b_c);
			a = (a_c >= 0 && bufs[a_c]) ? (uint8_t)(psd_r16(bufs[a_c], src_i) / 257) : 255;
		}
		else if (depth == 32) {
			r = (uint8_t)(CLAMP01(CHANF(r_c)) * 255.0f);
			g = (uint8_t)(CLAMP01(CHANF(g_c)) * 255.0f);
			b = (uint8_t)(CLAMP01(CHANF(b_c)) * 255.0f);
			a = (a_c >= 0 && bufs[a_c]) ? (uint8_t)(CLAMP01(*(float *)(bufs[a_c] + src_i)) * 255.0f) : 255;
		}
	}
	dst[0] = r;
	dst[1] = g;
	dst[2] = b;
	dst[3] = a;
#undef CHAN8
#undef CHAN16
#undef CHANF
#undef CLAMP01
}

// Decode all channels for one layer/strip and assemble into an RGBA8 buffer
// bufs/chan_ids/num_chans describe the decoded planar data
static uint8_t *psd_assemble_rgba(uint32_t w, uint32_t h, uint32_t depth_bytes, uint16_t depth, uint16_t color_mode, uint8_t **bufs, int16_t *chan_ids,
                                  uint16_t num_chans) {
	int r_c = -1, g_c = -1, b_c = -1, a_c = -1;
	for (uint16_t c = 0; c < num_chans; c++) {
		if (chan_ids[c] == 0)
			r_c = c;
		else if (chan_ids[c] == 1)
			g_c = c;
		else if (chan_ids[c] == 2)
			b_c = c;
		else if (chan_ids[c] == -1)
			a_c = c;
	}
	// Grayscale: treat channel 0 as luminance when no explicit R/G/B
	if (color_mode == 1 && r_c < 0 && num_chans > 0)
		r_c = 0;

	size_t   row_bytes = (size_t)w * depth_bytes;
	uint8_t *rgba      = (uint8_t *)malloc((size_t)w * h * 4);
	if (!rgba)
		return NULL;

	for (uint32_t y = 0; y < h; y++) {
		for (uint32_t x = 0; x < w; x++) {
			size_t src_i = (size_t)y * row_bytes + (size_t)x * depth_bytes;
			psd_pixel(rgba + ((size_t)y * w + x) * 4, bufs, r_c, g_c, b_c, a_c, src_i, depth, color_mode);
		}
	}
	return rgba;
}

// Decode one channel plane from the current file position
static uint8_t *psd_decode_channel(uint8_t *buf, size_t buf_size, size_t *pos, uint64_t chan_len, uint32_t rows, size_t row_bytes, uint16_t version) {
	if (*pos + chan_len > buf_size || chan_len < 2)
		return NULL;

	uint16_t comp       = psd_r16(buf, *pos);
	size_t   data_off   = *pos + 2;
	size_t   plane_size = rows * row_bytes;
	*pos += chan_len;

	uint8_t *plane = (uint8_t *)calloc(plane_size, 1);
	if (!plane)
		return NULL;

	if (comp == 0) {
		// Raw
		size_t copy_len = chan_len - 2;
		if (copy_len > plane_size)
			copy_len = plane_size;
		memcpy(plane, buf + data_off, copy_len);
	}
	else if (comp == 1) { // PackBits RLE
		size_t cnt_bytes = (version == 2) ? 4 : 2;
		size_t rdata     = data_off + (size_t)rows * cnt_bytes;
		for (uint32_t row = 0; row < rows; row++) {
			size_t   count_pos = data_off + (size_t)row * cnt_bytes;
			uint32_t rlen      = (cnt_bytes == 4) ? psd_r32(buf, count_pos) : psd_r16(buf, count_pos);
			if (rdata + rlen <= buf_size)
				packbits_decode(buf + rdata, rlen, plane + (size_t)row * row_bytes, row_bytes);
			rdata += rlen;
		}
	}
	else {
		free(plane);
		return NULL; // Unsupported per-channel compression
	}

	return plane;
}

#define PSD_MAX_CHAN 6

typedef struct {
	int32_t  top, left, bottom, right;
	int32_t  mask_top, mask_left, mask_bottom, mask_right;
	uint8_t  mask_default_color;
	int      has_mask;
	uint16_t num_channels;
	int16_t  chan_ids[PSD_MAX_CHAN];
	uint64_t chan_lengths[PSD_MAX_CHAN]; // Includes 2-byte compression header
	char     name[256];
} psd_layer_t;

void *io_psd_parse(uint8_t *buf, size_t buf_size, const char *file_name) {
	if (buf_size < 26)
		return NULL;
	if (buf[0] != '8' || buf[1] != 'B' || buf[2] != 'P' || buf[3] != 'S')
		return NULL;

	uint16_t version = psd_r16(buf, 4);
	if (version != 1 && version != 2)
		return NULL;

	uint16_t channels   = psd_r16(buf, 12);
	uint32_t height     = psd_r32(buf, 14);
	uint32_t width      = psd_r32(buf, 18);
	uint16_t depth      = psd_r16(buf, 22);
	uint16_t color_mode = psd_r16(buf, 24);

	if (width == 0 || height == 0)
		return NULL;

	size_t pos = 26;

	// Section 2: Color mode data
	if (pos + 4 > buf_size)
		return NULL;
	pos += 4 + psd_r32(buf, pos);

	// Section 3: Image resources
	if (pos + 4 > buf_size)
		return NULL;
	pos += 4 + psd_r32(buf, pos);

	// Section 4: Layer and mask information
	size_t   section4_start = pos;
	uint64_t section4_len   = 0;
	if (version == 2) {
		if (pos + 8 > buf_size)
			return NULL;
		section4_len = psd_r64(buf, pos);
		pos += 8;
	}
	else {
		if (pos + 4 > buf_size)
			return NULL;
		section4_len = psd_r32(buf, pos);
		pos += 4;
	}
	size_t section4_end = (version == 2) ? section4_start + 8 + section4_len : section4_start + 4 + section4_len;

	uint32_t depth_bytes = (depth + 7) / 8;

	// Parse layers
	void *result           = NULL;
	int   num_layers_found = 0;

	if (section4_len > 0) {
		// Layer info sub-section
		uint64_t layer_info_len = 0;
		if (version == 2) {
			if (pos + 8 > buf_size)
				goto fallback;
			layer_info_len = psd_r64(buf, pos);
			pos += 8;
		}
		else {
			if (pos + 4 > buf_size)
				goto fallback;
			layer_info_len = psd_r32(buf, pos);
			pos += 4;
		}

		if (layer_info_len == 0)
			goto fallback;

		size_t layer_info_end = pos + layer_info_len;

		// Layer count (signed: negative means first alpha = merged transparency)
		if (pos + 2 > buf_size)
			goto fallback;
		int16_t  layer_count_raw = (int16_t)psd_r16(buf, pos);
		uint16_t layer_count     = (uint16_t)(layer_count_raw < 0 ? -layer_count_raw : layer_count_raw);
		pos += 2;

		if (layer_count == 0)
			goto fallback;

		psd_layer_t *layers = (psd_layer_t *)calloc(layer_count, sizeof(psd_layer_t));
		if (!layers)
			goto fallback;

		// Pass 1: read layer records
		for (uint16_t i = 0; i < layer_count; i++) {
			if (pos + 18 > buf_size)
				break;
			layers[i].top = (int32_t)psd_r32(buf, pos);
			pos += 4;
			layers[i].left = (int32_t)psd_r32(buf, pos);
			pos += 4;
			layers[i].bottom = (int32_t)psd_r32(buf, pos);
			pos += 4;
			layers[i].right = (int32_t)psd_r32(buf, pos);
			pos += 4;
			layers[i].num_channels = psd_r16(buf, pos);
			pos += 2;
			if (layers[i].num_channels > PSD_MAX_CHAN)
				layers[i].num_channels = PSD_MAX_CHAN;

			for (uint16_t c = 0; c < layers[i].num_channels; c++) {
				layers[i].chan_ids[c] = (int16_t)psd_r16(buf, pos);
				pos += 2;
				if (version == 2) {
					layers[i].chan_lengths[c] = psd_r64(buf, pos);
					pos += 8;
				}
				else {
					layers[i].chan_lengths[c] = psd_r32(buf, pos);
					pos += 4;
				}
			}

			pos += 4; // blend mode signature "8BIM"
			pos += 4; // blend mode key
			pos += 1; // opacity
			pos += 1; // clipping
			pos += 1; // flags
			pos += 1; // filler

			uint32_t extra_len = psd_r32(buf, pos);
			pos += 4;
			size_t extra_end = pos + extra_len;

			// Layer mask data
			if (pos + 4 <= buf_size) {
				uint32_t mask_len = psd_r32(buf, pos);
				pos += 4;
				if (mask_len >= 17 && pos + 17 <= buf_size) {
					layers[i].mask_top           = (int32_t)psd_r32(buf, pos);
					layers[i].mask_left          = (int32_t)psd_r32(buf, pos + 4);
					layers[i].mask_bottom        = (int32_t)psd_r32(buf, pos + 8);
					layers[i].mask_right         = (int32_t)psd_r32(buf, pos + 12);
					layers[i].mask_default_color = buf[pos + 16];
					layers[i].has_mask           = 1;
				}
				pos += mask_len;
			}
			// Layer blending ranges
			if (pos + 4 <= buf_size) {
				uint32_t blend_len = psd_r32(buf, pos);
				pos += 4 + blend_len;
			}
			// Layer name (pascal string, padded to 4-byte boundary)
			if (pos < extra_end && pos < buf_size) {
				uint8_t name_len = buf[pos++];
				if (name_len > 255)
					name_len = 255;
				if (pos + name_len <= buf_size)
					memcpy(layers[i].name, buf + pos, name_len);
				layers[i].name[name_len] = 0;
				pos += name_len;
				// pad to 4-byte boundary counting from the length byte
				uint32_t padded = ((uint32_t)(name_len + 1) + 3) & ~3u;
				pos += padded - (name_len + 1);
			}

			pos = extra_end; // skip any remaining extra data
		}

		// Pass 2: read channel image data (immediately follows all records)
		for (uint16_t i = 0; i < layer_count; i++) {
			int32_t lw = layers[i].right - layers[i].left;
			int32_t lh = layers[i].bottom - layers[i].top;

			if (lw <= 0 || lh <= 0) {
				// Skip channel data for empty/invisible layers
				for (uint16_t c = 0; c < layers[i].num_channels; c++)
					pos += (size_t)layers[i].chan_lengths[c];
				continue;
			}

			size_t   row_bytes = (size_t)lw * depth_bytes;
			uint8_t *bufs[PSD_MAX_CHAN];
			memset(bufs, 0, sizeof(bufs));
			uint8_t *mask_buf = NULL;

			int32_t mw             = layers[i].has_mask ? (layers[i].mask_right - layers[i].mask_left) : lw;
			int32_t mh             = layers[i].has_mask ? (layers[i].mask_bottom - layers[i].mask_top) : lh;
			int32_t mox            = layers[i].has_mask ? layers[i].mask_left : layers[i].left;
			int32_t moy            = layers[i].has_mask ? layers[i].mask_top : layers[i].top;
			size_t  mask_row_bytes = (mw > 0 && mh > 0) ? (size_t)mw * depth_bytes : 0;

			for (uint16_t c = 0; c < layers[i].num_channels; c++) {
				if (layers[i].chan_ids[c] < -1) {
					// Decode first mask channel using mask bounds, skip additional ones
					if (mask_buf == NULL && mask_row_bytes > 0)
						mask_buf = psd_decode_channel(buf, buf_size, &pos, layers[i].chan_lengths[c], (uint32_t)mh, mask_row_bytes, version);
					else
						pos += (size_t)layers[i].chan_lengths[c];
					continue;
				}
				bufs[c] = psd_decode_channel(buf, buf_size, &pos, layers[i].chan_lengths[c], (uint32_t)lh, row_bytes, version);
			}

			uint8_t *layer_rgba =
			    psd_assemble_rgba((uint32_t)lw, (uint32_t)lh, depth_bytes, depth, color_mode, bufs, layers[i].chan_ids, layers[i].num_channels);

			for (uint16_t c = 0; c < layers[i].num_channels; c++)
				free(bufs[c]);

			if (!layer_rgba) {
				free(mask_buf);
				continue;
			}

			// Place the layer into a full-image canvas at its (left, top) offset
			uint8_t *rgba = (uint8_t *)calloc((size_t)width * height * 4, 1);
			if (!rgba) {
				free(layer_rgba);
				free(mask_buf);
				continue;
			}

			int32_t ox = layers[i].left;
			int32_t oy = layers[i].top;
			for (int32_t y = 0; y < lh; y++) {
				int32_t dy = oy + y;
				if (dy < 0 || dy >= (int32_t)height)
					continue;
				for (int32_t x = 0; x < lw; x++) {
					int32_t dx = ox + x;
					if (dx < 0 || dx >= (int32_t)width)
						continue;
					uint8_t *src = layer_rgba + ((size_t)y * lw + x) * 4;
					uint8_t *dst = rgba + ((size_t)dy * width + dx) * 4;
					dst[0]       = src[0];
					dst[1]       = src[1];
					dst[2]       = src[2];
					dst[3]       = src[3];
				}
			}
			free(layer_rgba);

			buffer_t *b = (buffer_t *)malloc(sizeof(buffer_t));
			b->buffer   = rgba;
			b->length = b->capacity = (uint32_t)((size_t)width * height * 4);
			void *tex               = gpu_create_texture_from_bytes(b, (int)width, (int)height, GPU_TEXTURE_FORMAT_RGBA32);

			char *layer_name = layers[i].name[0] != 0 ? layers[i].name : "layer";
			if (num_layers_found == 0) {
				result = tex;
			}
			else {
				io_psd_import_layer((char *)file_name, layer_name, tex);
			}
			num_layers_found++;

			// Import mask as a separate grayscale texture
			if (mask_buf != NULL) {
				uint8_t *mask_rgba = (uint8_t *)calloc((size_t)width * height * 4, 1);
				if (mask_rgba) {
					for (int32_t y = 0; y < mh; y++) {
						int32_t dy = moy + y;
						if (dy < 0 || dy >= (int32_t)height)
							continue;
						for (int32_t x = 0; x < mw; x++) {
							int32_t dx = mox + x;
							if (dx < 0 || dx >= (int32_t)width)
								continue;
							size_t  src_i = ((size_t)y * mw + x) * depth_bytes;
							uint8_t v;
							if (depth == 8)
								v = mask_buf[src_i];
							else if (depth == 16)
								v = (uint8_t)(psd_r16(mask_buf, src_i) / 257);
							else {
								float fv = *(float *)(mask_buf + src_i);
								v        = (uint8_t)((fv < 0.0f ? 0.0f : fv > 1.0f ? 1.0f : fv) * 255.0f);
							}
							uint8_t *dst = mask_rgba + ((size_t)dy * width + dx) * 4;
							if (layers[i].mask_default_color == 255) {
								// White background, dark stroke: invert so stroke is opaque, bg transparent
								dst[0] = dst[1] = dst[2] = 0;
								dst[3]                   = (uint8_t)(255 - v);
							}
							else {
								// Black background, light stroke
								dst[0] = dst[1] = dst[2] = 255;
								dst[3]                   = v;
							}
						}
					}
					buffer_t *mb = (buffer_t *)malloc(sizeof(buffer_t));
					mb->buffer   = mask_rgba;
					mb->length = mb->capacity = (uint32_t)((size_t)width * height * 4);
					void *mask_tex            = gpu_create_texture_from_bytes(mb, (int)width, (int)height, GPU_TEXTURE_FORMAT_RGBA32);

					char mask_name[256 + 5];
					snprintf(mask_name, sizeof(mask_name), "%s_mask", layer_name);
					io_psd_import_layer((char *)file_name, mask_name, mask_tex);
				}
				free(mask_buf);
			}
		}

		free(layers);

		if (result != NULL)
			return result;
	}

fallback: // No layers
	pos = section4_end;
	if (pos + 2 > buf_size)
		return NULL;

	uint16_t compression = psd_r16(buf, pos);
	pos += 2;

	if (compression != 0 && compression != 1) {
		console_info("Error: This psd compression type is not yet implemented");
		return NULL;
	}

	size_t   row_bytes  = (size_t)width * depth_bytes;
	size_t   plane_size = (size_t)height * row_bytes;
	uint16_t read_chans = channels > 4 ? 4 : channels;

	uint8_t *planes = (uint8_t *)calloc((size_t)read_chans * plane_size, 1);
	if (!planes)
		return NULL;

	// Build synthetic chan_ids for the composite (RGB order: 0, 1, 2; alpha: -1)
	int16_t comp_chan_ids[4] = {0, 1, 2, -1};

	if (compression == 0) {
		for (uint16_t c = 0; c < channels; c++) {
			if (pos + plane_size > buf_size) {
				free(planes);
				return NULL;
			}
			if (c < read_chans)
				memcpy(planes + (size_t)c * plane_size, buf + pos, plane_size);
			pos += plane_size;
		}
	}
	else {
		size_t cnt_bytes      = (version == 2) ? 4 : 2;
		size_t rle_counts_pos = pos;
		size_t rle_data_pos   = pos + (size_t)channels * height * cnt_bytes;

		for (uint16_t c = 0; c < channels; c++) {
			for (uint32_t row = 0; row < height; row++) {
				size_t   count_pos = rle_counts_pos + ((size_t)c * height + row) * cnt_bytes;
				uint32_t rlen      = (cnt_bytes == 4) ? psd_r32(buf, count_pos) : psd_r16(buf, count_pos);
				if (c < read_chans && rle_data_pos + rlen <= buf_size)
					packbits_decode(buf + rle_data_pos, rlen, planes + (size_t)c * plane_size + (size_t)row * row_bytes, row_bytes);
				rle_data_pos += rlen;
			}
		}
	}

	uint8_t *planes_ptrs[4] = {NULL, NULL, NULL, NULL};
	for (uint16_t c = 0; c < read_chans; c++)
		planes_ptrs[c] = planes + (size_t)c * plane_size;

	uint8_t *rgba = psd_assemble_rgba(width, height, depth_bytes, depth, color_mode, planes_ptrs, comp_chan_ids, read_chans);
	free(planes);

	if (!rgba)
		return NULL;

	buffer_t *b = (buffer_t *)malloc(sizeof(buffer_t));
	b->buffer   = rgba;
	b->length = b->capacity = (uint32_t)((size_t)width * height * 4);
	return gpu_create_texture_from_bytes(b, (int)width, (int)height, GPU_TEXTURE_FORMAT_RGBA32);
}
