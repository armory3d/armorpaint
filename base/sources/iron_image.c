
#include "iron_image.h"

#ifdef WITH_IMAGE_WRITE

#ifdef WITH_COMPRESS
unsigned char *iron_deflate_raw(unsigned char *data, int data_len, int *out_len, int quality);
#define STBIW_ZLIB_COMPRESS iron_deflate_raw
#endif
#define STBI_WINDOWS_UTF8
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

void _write_image(char *path, buffer_t *bytes, i32 w, i32 h, i32 format, int image_format, int quality) {
	int            comp   = 0;
	unsigned char *pixels = NULL;
	unsigned char *rgba   = (unsigned char *)bytes->buffer;
	if (format == 0) { // RGBA
		comp = 4;
#ifdef IRON_BGRA
		pixels = (unsigned char *)malloc(w * h * comp);
		for (int i = 0; i < w * h; ++i) {
			pixels[i * 4]     = rgba[i * 4 + 2];
			pixels[i * 4 + 1] = rgba[i * 4 + 1];
			pixels[i * 4 + 2] = rgba[i * 4];
			pixels[i * 4 + 3] = rgba[i * 4 + 3];
		}
#else
		pixels = rgba;
#endif
	}
	else if (format == 1) { // R
		comp   = 1;
		pixels = rgba;
	}
	else if (format == 2) { // RGB1
		comp   = 3;
		pixels = (unsigned char *)malloc(w * h * comp);
		for (int i = 0; i < w * h; ++i) {
#ifdef IRON_BGRA
			pixels[i * 3]     = rgba[i * 4 + 2];
			pixels[i * 3 + 1] = rgba[i * 4 + 1];
			pixels[i * 3 + 2] = rgba[i * 4];
#else
			pixels[i * 3]     = rgba[i * 4];
			pixels[i * 3 + 1] = rgba[i * 4 + 1];
			pixels[i * 3 + 2] = rgba[i * 4 + 2];
#endif
		}
	}
	else if (format > 2) { // RRR1, GGG1, BBB1, AAA1
		comp    = 1;
		pixels  = (unsigned char *)malloc(w * h * comp);
		int off = format - 3;
#ifdef IRON_BGRA
		off = off == 0 ? 2 : off == 2 ? 0 : off; // Alpha stays in place
#endif
		for (int i = 0; i < w * h; ++i) {
			pixels[i] = rgba[i * 4 + off];
		}
	}

	image_format == 0 ? stbi_write_jpg(path, w, h, comp, pixels, quality) : stbi_write_png(path, w, h, comp, pixels, w * comp);

	if (pixels != rgba) {
		free(pixels);
	}
}

void iron_write_jpg(char *path, buffer_t *bytes, i32 w, i32 h, i32 format, i32 quality) {
	// RGBA, R, RGB1, RRR1, GGG1, BBB1, AAA1
	_write_image(path, bytes, w, h, format, 0, quality);
}

void iron_write_png(char *path, buffer_t *bytes, i32 w, i32 h, i32 format) {
	_write_image(path, bytes, w, h, format, 1, 100);
}

unsigned char *_encode_data;
int            _encode_size;
void           _encode_image_func(void *context, void *data, int size) {
    memcpy(_encode_data + _encode_size, data, size);
    _encode_size += size;
}

buffer_t *_encode_image(buffer_t *bytes, i32 w, i32 h, i32 format, i32 quality) {
#ifdef IRON_BGRA
	unsigned char *pixels = bytes->buffer;
	for (int i = 0; i < w * h; ++i) {
		unsigned char c   = pixels[i * 4];
		pixels[i * 4]     = pixels[i * 4 + 2];
		pixels[i * 4 + 2] = c;
	}
#endif
	_encode_data = (unsigned char *)malloc(w * h * 4);
	_encode_size = 0;
	format == 0 ? stbi_write_jpg_to_func(&_encode_image_func, NULL, w, h, 4, bytes->buffer, quality)
	            : stbi_write_png_to_func(&_encode_image_func, NULL, w, h, 4, bytes->buffer, w * 4);
	buffer_t *buffer = malloc(sizeof(buffer_t));
	buffer->buffer   = _encode_data;
	buffer->length   = _encode_size;
	return buffer;
}

buffer_t *iron_encode_jpg(buffer_t *bytes, i32 w, i32 h, i32 format, i32 quality) {
	return _encode_image(bytes, w, h, 0, quality);
}

buffer_t *iron_encode_png(buffer_t *bytes, i32 w, i32 h, i32 format) {
	return _encode_image(bytes, w, h, 1, 100);
}
#endif

#ifdef WITH_VIDEO_WRITE

// Lossless H.264 in a minimal MP4 container. Changed macroblocks are stored as I_PCM,
// unchanged ones are skipped in P frames.

#define IRON_MP4_KEY_INTERVAL 60

double iron_time(void);

static FILE     *iron_mp4_fp = NULL;
static int       iron_mp4_stride; // Source width
static int       iron_mp4_w;      // Even-sized output
static int       iron_mp4_h;
static int       iron_mp4_mbw; // Size in 16x16 macroblocks
static int       iron_mp4_mbh;
static uint8_t  *iron_mp4_rbsp;
static uint8_t  *iron_mp4_nal;
static uint8_t  *iron_mp4_prev; // Previous frame pixels
static int       iron_mp4_prev_size;
static uint8_t   iron_mp4_sps[64];
static int       iron_mp4_sps_len;
static uint8_t   iron_mp4_pps[16];
static int       iron_mp4_pps_len;
static uint64_t  iron_mp4_mdat_size;
static uint32_t *iron_mp4_sizes;
static double   *iron_mp4_times;
static int       iron_mp4_count;
static int       iron_mp4_cap;

// Bit writer for nal payloads, big-endian for mp4 boxes
static uint8_t *iron_mp4_p;
static int      iron_mp4_pos;
static int      iron_mp4_bit;

static void iron_mp4_u(uint32_t v, int n) {
	while (n-- > 0) {
		if (iron_mp4_bit == 0) {
			iron_mp4_p[iron_mp4_pos] = 0;
		}
		iron_mp4_p[iron_mp4_pos] |= ((v >> n) & 1) << (7 - iron_mp4_bit);
		if (++iron_mp4_bit == 8) {
			iron_mp4_bit = 0;
			iron_mp4_pos++;
		}
	}
}

static void iron_mp4_ue(uint32_t v) {
	int n = 0;
	while (((v + 1) >> n) > 1) {
		n++;
	}
	iron_mp4_u(0, n);
	iron_mp4_u(v + 1, n + 1);
}

static void iron_mp4_align(bool stop_bit) {
	if (stop_bit) {
		iron_mp4_u(1, 1);
	}
	while (iron_mp4_bit != 0) {
		iron_mp4_u(0, 1);
	}
}

static void iron_mp4_w8(uint32_t v) {
	iron_mp4_p[iron_mp4_pos++] = v;
}

static void iron_mp4_w32(uint32_t v) {
	iron_mp4_u(v, 32);
}

static void iron_mp4_zero(int n) {
	while (n-- > 0) {
		iron_mp4_w8(0);
	}
}

static int iron_mp4_box(const char *type) {
	int at = iron_mp4_pos;
	iron_mp4_w32(0);
	iron_mp4_w32(type[0] << 24 | type[1] << 16 | type[2] << 8 | type[3]);
	return at;
}

static int iron_mp4_fullbox(const char *type, uint32_t version_flags) {
	int at = iron_mp4_box(type);
	iron_mp4_w32(version_flags);
	return at;
}

static void iron_mp4_box_end(int at) {
	int end      = iron_mp4_pos;
	iron_mp4_pos = at;
	iron_mp4_w32(end - at);
	iron_mp4_pos = end;
}

static void iron_mp4_matrix() {
	uint32_t m[9] = {0x10000, 0, 0, 0, 0x10000, 0, 0, 0, 0x40000000};
	for (int i = 0; i < 9; ++i) {
		iron_mp4_w32(m[i]);
	}
}

static int iron_mp4_escape(uint8_t *dst, uint8_t *src, int len) {
	// Insert emulation prevention bytes
	int n     = 0;
	int zeros = 0;
	for (int i = 0; i < len; ++i) {
		if (zeros == 2 && src[i] <= 3) {
			dst[n++] = 3;
			zeros    = 0;
		}
		dst[n++] = src[i];
		zeros    = src[i] == 0 ? zeros + 1 : 0;
	}
	return n;
}

static void iron_mp4_rbsp_begin() {
	iron_mp4_p   = iron_mp4_rbsp;
	iron_mp4_pos = 0;
	iron_mp4_bit = 0;
}

static int iron_mp4_rbsp_end(uint8_t *dst, uint8_t nal_header) {
	iron_mp4_align(true);
	dst[0] = nal_header;
	return iron_mp4_escape(dst + 1, iron_mp4_rbsp, iron_mp4_pos) + 1;
}

void iron_mp4_begin(char *path, i32 w, i32 h) {
	iron_mp4_fp = fopen(path, "wb");
	if (iron_mp4_fp == NULL) {
		return;
	}
	iron_mp4_stride    = w;
	iron_mp4_w         = w & ~1;
	iron_mp4_h         = h & ~1;
	iron_mp4_mbw       = (iron_mp4_w + 15) / 16;
	iron_mp4_mbh       = (iron_mp4_h + 15) / 16;
	int rbsp_size      = iron_mp4_mbw * iron_mp4_mbh * (384 + 8) + 64;
	iron_mp4_rbsp      = malloc(rbsp_size);
	iron_mp4_nal       = malloc(rbsp_size * 3 / 2 + 8);
	iron_mp4_count     = 0;
	iron_mp4_cap       = 1024;
	iron_mp4_sizes     = malloc(iron_mp4_cap * sizeof(uint32_t));
	iron_mp4_times     = malloc(iron_mp4_cap * sizeof(double));
	iron_mp4_prev_size = w * h * 4;
	iron_mp4_prev      = malloc(iron_mp4_prev_size);
	iron_mp4_mdat_size = 16;

	// Sequence parameter set: baseline, 4:2:0, bt.709 limited range
	iron_mp4_rbsp_begin();
	iron_mp4_u(66, 8);                                             // profile_idc
	iron_mp4_u(0xc0, 8);                                           // constraint_set0/1
	iron_mp4_u(iron_mp4_mbw * iron_mp4_mbh <= 36864 ? 52 : 62, 8); // level_idc
	iron_mp4_ue(0);                                                // seq_parameter_set_id
	iron_mp4_ue(0);                                                // log2_max_frame_num_minus4
	iron_mp4_ue(2);                                                // pic_order_cnt_type
	iron_mp4_ue(1);                                                // max_num_ref_frames
	iron_mp4_u(0, 1);                                              // gaps_in_frame_num_allowed
	iron_mp4_ue(iron_mp4_mbw - 1);
	iron_mp4_ue(iron_mp4_mbh - 1);
	iron_mp4_u(1, 1); // frame_mbs_only
	iron_mp4_u(1, 1); // direct_8x8_inference
	bool crop = iron_mp4_mbw * 16 != iron_mp4_w || iron_mp4_mbh * 16 != iron_mp4_h;
	iron_mp4_u(crop, 1);
	if (crop) {
		iron_mp4_ue(0);
		iron_mp4_ue((iron_mp4_mbw * 16 - iron_mp4_w) / 2);
		iron_mp4_ue(0);
		iron_mp4_ue((iron_mp4_mbh * 16 - iron_mp4_h) / 2);
	}
	iron_mp4_u(1, 1);        // vui_parameters_present
	iron_mp4_u(0, 2);        // aspect_ratio_info, overscan_info
	iron_mp4_u(1, 1);        // video_signal_type_present
	iron_mp4_u(5, 3);        // video_format: unspecified
	iron_mp4_u(0, 1);        // video_full_range
	iron_mp4_u(1, 1);        // colour_description_present
	iron_mp4_u(0x10101, 24); // bt.709 primaries, transfer, matrix
	iron_mp4_u(0, 5);        // chroma_loc, timing, nal_hrd, vcl_hrd, pic_struct
	iron_mp4_u(0, 1);        // bitstream_restriction
	iron_mp4_sps_len = iron_mp4_rbsp_end(iron_mp4_sps, 0x67);

	// Picture parameter set: cavlc, deblocking disabled in slice headers
	iron_mp4_rbsp_begin();
	iron_mp4_ue(0);   // pic_parameter_set_id
	iron_mp4_ue(0);   // seq_parameter_set_id
	iron_mp4_u(0, 2); // entropy_coding_mode, bottom_field_pic_order_in_frame_present
	iron_mp4_ue(0);   // num_slice_groups_minus1
	iron_mp4_ue(0);   // num_ref_idx_l0_default_active_minus1
	iron_mp4_ue(0);   // num_ref_idx_l1_default_active_minus1
	iron_mp4_u(0, 3); // weighted_pred, weighted_bipred_idc
	iron_mp4_ue(0);   // pic_init_qp_minus26
	iron_mp4_ue(0);   // pic_init_qs_minus26
	iron_mp4_ue(0);   // chroma_qp_index_offset
	iron_mp4_u(1, 1); // deblocking_filter_control_present
	iron_mp4_u(0, 2); // constrained_intra_pred, redundant_pic_cnt_present
	iron_mp4_pps_len = iron_mp4_rbsp_end(iron_mp4_pps, 0x68);

	// ftyp, then mdat with a 64-bit size patched in iron_mp4_end
	uint8_t head[40] = {0,   0,   0,   24,  'f', 't', 'y', 'p', 'i', 's', 'o', 'm', 0,   0,   2,   0,
	                    'i', 's', 'o', 'm', 'a', 'v', 'c', '1', 0,   0,   0,   1,   'm', 'd', 'a', 't'};
	fwrite(head, 1, 40, iron_mp4_fp);
}

void iron_mp4_encode(buffer_t *pixels, f64 time) {
	if (iron_mp4_fp == NULL) {
		return;
	}
	if (iron_mp4_count == iron_mp4_cap) {
		iron_mp4_cap *= 2;
		iron_mp4_sizes = realloc(iron_mp4_sizes, iron_mp4_cap * sizeof(uint32_t));
		iron_mp4_times = realloc(iron_mp4_times, iron_mp4_cap * sizeof(double));
	}

#ifdef IRON_BGRA
	int ri = 2, bi = 0;
#else
	int ri = 0, bi = 2;
#endif

	// Slice header, idr picture or p picture referencing the previous frame
	bool key = iron_mp4_count % IRON_MP4_KEY_INTERVAL == 0;
	iron_mp4_rbsp_begin();
	iron_mp4_ue(0);                                             // first_mb_in_slice
	iron_mp4_ue(key ? 7 : 5);                                   // slice_type: I or P
	iron_mp4_ue(0);                                             // pic_parameter_set_id
	iron_mp4_u(iron_mp4_count % IRON_MP4_KEY_INTERVAL % 16, 4); // frame_num
	if (key) {
		iron_mp4_ue(0);   // idr_pic_id
		iron_mp4_u(0, 2); // no_output_of_prior_pics, long_term_reference
	}
	else {
		iron_mp4_u(0, 3); // num_ref_idx_active_override, ref_pic_list_modification, adaptive_ref_pic_marking
	}
	iron_mp4_ue(0); // slice_qp_delta
	iron_mp4_ue(1); // disable_deblocking_filter_idc

	int skip = 0;
	for (int mby = 0; mby < iron_mp4_mbh; ++mby) {
		for (int mbx = 0; mbx < iron_mp4_mbw; ++mbx) {
			// Skipped macroblocks copy the previous frame (zero motion vector)
			bool same = !key;
			int  x0   = mbx * 16;
			int  x1   = x0 + 16 < iron_mp4_w ? x0 + 16 : iron_mp4_w;
			for (int py = mby * 16; same && py < mby * 16 + 16 && py < iron_mp4_h; ++py) {
				int at = (py * iron_mp4_stride + x0) * 4;
				same   = memcmp(pixels->buffer + at, iron_mp4_prev + at, (x1 - x0) * 4) == 0;
			}
			if (same) {
				skip++;
				continue;
			}
			if (!key) {
				iron_mp4_ue(skip); // mb_skip_run
				skip = 0;
			}

			uint8_t y[256], cb[64], cr[64];
			float   sum[64][3] = {0};
			for (int j = 0; j < 16; ++j) {
				for (int i = 0; i < 16; ++i) {
					int      px = mbx * 16 + i < iron_mp4_w ? mbx * 16 + i : iron_mp4_w - 1;
					int      py = mby * 16 + j < iron_mp4_h ? mby * 16 + j : iron_mp4_h - 1;
					uint8_t *c  = pixels->buffer + (py * iron_mp4_stride + px) * 4;
					float    r = c[ri], g = c[1], b = c[bi];
					y[j * 16 + i] = (uint8_t)(16.0f + (0.2126f * r + 0.7152f * g + 0.0722f * b) * (219.0f / 255.0f) + 0.5f);
					float *s      = sum[(j / 2) * 8 + i / 2];
					s[0] += r;
					s[1] += g;
					s[2] += b;
				}
			}
			for (int k = 0; k < 64; ++k) {
				float r = sum[k][0] / 4.0f, g = sum[k][1] / 4.0f, b = sum[k][2] / 4.0f;
				float l = 0.2126f * r + 0.7152f * g + 0.0722f * b;
				cb[k]   = (uint8_t)(128.0f + (b - l) / 1.8556f * (224.0f / 255.0f) + 0.5f);
				cr[k]   = (uint8_t)(128.0f + (r - l) / 1.5748f * (224.0f / 255.0f) + 0.5f);
			}
			iron_mp4_ue(key ? 25 : 30); // mb_type: I_PCM
			iron_mp4_align(false);
			memcpy(iron_mp4_p + iron_mp4_pos, y, 256);
			memcpy(iron_mp4_p + iron_mp4_pos + 256, cb, 64);
			memcpy(iron_mp4_p + iron_mp4_pos + 320, cr, 64);
			iron_mp4_pos += 384;
		}
	}

	if (skip > 0) {
		iron_mp4_ue(skip);
	}
	memcpy(iron_mp4_prev, pixels->buffer, iron_mp4_prev_size);

	int size        = iron_mp4_rbsp_end(iron_mp4_nal + 4, key ? 0x65 : 0x61);
	iron_mp4_nal[0] = size >> 24;
	iron_mp4_nal[1] = size >> 16;
	iron_mp4_nal[2] = size >> 8;
	iron_mp4_nal[3] = size;
	fwrite(iron_mp4_nal, 1, size + 4, iron_mp4_fp);
	iron_mp4_sizes[iron_mp4_count] = size + 4;
	iron_mp4_times[iron_mp4_count] = time;
	iron_mp4_count++;
	iron_mp4_mdat_size += size + 4;
}

void iron_mp4_end() {
	if (iron_mp4_fp == NULL) {
		return;
	}

	// Frame durations from capture times
	int       n         = iron_mp4_count;
	uint32_t  timescale = 90000;
	uint32_t *durations = malloc((n + 1) * sizeof(uint32_t));
	uint32_t  duration  = 0;
	for (int i = 0; i < n; ++i) {
		uint32_t next = (uint32_t)((iron_mp4_times[i + 1 < n ? i + 1 : i] - iron_mp4_times[0]) * timescale + 0.5);
		durations[i]  = i + 1 < n ? next - duration : (i > 0 ? durations[i - 1] : timescale / 60);
		duration += durations[i];
	}

	iron_mp4_p   = malloc(1024 + n * 13);
	iron_mp4_pos = 0;
	iron_mp4_bit = 0;
	int moov     = iron_mp4_box("moov");
	int mvhd     = iron_mp4_fullbox("mvhd", 0);
	iron_mp4_zero(8); // creation, modification time
	iron_mp4_w32(timescale);
	iron_mp4_w32(duration);
	iron_mp4_w32(0x10000);   // rate
	iron_mp4_w32(0x1000000); // volume, reserved
	iron_mp4_zero(8);
	iron_mp4_matrix();
	iron_mp4_zero(24);
	iron_mp4_w32(2); // next_track_id
	iron_mp4_box_end(mvhd);

	int trak = iron_mp4_box("trak");
	int tkhd = iron_mp4_fullbox("tkhd", 3); // enabled, in movie
	iron_mp4_zero(8);
	iron_mp4_w32(1); // track_id
	iron_mp4_zero(4);
	iron_mp4_w32(duration);
	iron_mp4_zero(16); // reserved, layer, alternate_group, volume
	iron_mp4_matrix();
	iron_mp4_w32(iron_mp4_w << 16);
	iron_mp4_w32(iron_mp4_h << 16);
	iron_mp4_box_end(tkhd);

	int mdia = iron_mp4_box("mdia");
	int mdhd = iron_mp4_fullbox("mdhd", 0);
	iron_mp4_zero(8);
	iron_mp4_w32(timescale);
	iron_mp4_w32(duration);
	iron_mp4_w32(0x55c40000); // language: und
	iron_mp4_box_end(mdhd);
	int hdlr = iron_mp4_fullbox("hdlr", 0);
	iron_mp4_zero(4);
	iron_mp4_w32('v' << 24 | 'i' << 16 | 'd' << 8 | 'e');
	iron_mp4_zero(13); // reserved, empty name
	iron_mp4_box_end(hdlr);

	int minf = iron_mp4_box("minf");
	int vmhd = iron_mp4_fullbox("vmhd", 1);
	iron_mp4_zero(8);
	iron_mp4_box_end(vmhd);
	int dinf = iron_mp4_box("dinf");
	int dref = iron_mp4_fullbox("dref", 0);
	iron_mp4_w32(1);
	iron_mp4_box_end(iron_mp4_fullbox("url ", 1)); // data in this file
	iron_mp4_box_end(dref);
	iron_mp4_box_end(dinf);

	int stbl = iron_mp4_box("stbl");
	int stsd = iron_mp4_fullbox("stsd", 0);
	iron_mp4_w32(1);
	int avc1 = iron_mp4_box("avc1");
	iron_mp4_zero(6);
	iron_mp4_u(1, 16); // data_reference_index
	iron_mp4_zero(16);
	iron_mp4_u(iron_mp4_w, 16);
	iron_mp4_u(iron_mp4_h, 16);
	iron_mp4_w32(0x480000); // 72 dpi
	iron_mp4_w32(0x480000);
	iron_mp4_zero(4);
	iron_mp4_u(1, 16);        // frame_count
	iron_mp4_zero(32);        // compressor name
	iron_mp4_u(0x18ffff, 32); // depth, pre_defined -1
	int avcc = iron_mp4_box("avcC");
	iron_mp4_w8(1);
	iron_mp4_w8(iron_mp4_sps[1]); // profile, compatibility, level
	iron_mp4_w8(iron_mp4_sps[2]);
	iron_mp4_w8(iron_mp4_sps[3]);
	iron_mp4_w8(0xff); // 4-byte nal lengths
	iron_mp4_w8(0xe1); // 1 sps
	iron_mp4_u(iron_mp4_sps_len, 16);
	memcpy(iron_mp4_p + iron_mp4_pos, iron_mp4_sps, iron_mp4_sps_len);
	iron_mp4_pos += iron_mp4_sps_len;
	iron_mp4_w8(1); // 1 pps
	iron_mp4_u(iron_mp4_pps_len, 16);
	memcpy(iron_mp4_p + iron_mp4_pos, iron_mp4_pps, iron_mp4_pps_len);
	iron_mp4_pos += iron_mp4_pps_len;
	iron_mp4_box_end(avcc);
	iron_mp4_box_end(avc1);
	iron_mp4_box_end(stsd);

	int stts       = iron_mp4_fullbox("stts", 0);
	int stts_count = iron_mp4_pos;
	int runs       = 0;
	iron_mp4_w32(0);
	for (int i = 0; i < n;) {
		int k = i;
		while (k < n && durations[k] == durations[i]) {
			k++;
		}
		iron_mp4_w32(k - i);
		iron_mp4_w32(durations[i]);
		runs++;
		i = k;
	}
	int end      = iron_mp4_pos;
	iron_mp4_pos = stts_count;
	iron_mp4_w32(runs);
	iron_mp4_pos = end;
	iron_mp4_box_end(stts);

	int stsc = iron_mp4_fullbox("stsc", 0); // all samples in one chunk
	iron_mp4_w32(1);
	iron_mp4_w32(1);
	iron_mp4_w32(n);
	iron_mp4_w32(1);
	iron_mp4_box_end(stsc);
	int stsz = iron_mp4_fullbox("stsz", 0);
	iron_mp4_w32(0);
	iron_mp4_w32(n);
	for (int i = 0; i < n; ++i) {
		iron_mp4_w32(iron_mp4_sizes[i]);
	}
	iron_mp4_box_end(stsz);
	int co64 = iron_mp4_fullbox("co64", 0);
	iron_mp4_w32(1);
	iron_mp4_w32(0);
	iron_mp4_w32(24 + 16); // after ftyp and mdat header
	iron_mp4_box_end(co64);
	int stss = iron_mp4_fullbox("stss", 0); // idr samples
	iron_mp4_w32((n + IRON_MP4_KEY_INTERVAL - 1) / IRON_MP4_KEY_INTERVAL);
	for (int i = 0; i < n; i += IRON_MP4_KEY_INTERVAL) {
		iron_mp4_w32(i + 1);
	}
	iron_mp4_box_end(stss);
	iron_mp4_box_end(stbl);
	iron_mp4_box_end(minf);
	iron_mp4_box_end(mdia);
	iron_mp4_box_end(trak);
	iron_mp4_box_end(moov);
	fwrite(iron_mp4_p, 1, iron_mp4_pos, iron_mp4_fp);

	uint8_t size64[8];
	for (int i = 0; i < 8; ++i) {
		size64[i] = (uint8_t)(iron_mp4_mdat_size >> (56 - i * 8));
	}
	fseek(iron_mp4_fp, 24 + 8, SEEK_SET);
	fwrite(size64, 1, 8, iron_mp4_fp);
	fclose(iron_mp4_fp);
	iron_mp4_fp = NULL;

	free(iron_mp4_p);
	free(durations);
	free(iron_mp4_rbsp);
	free(iron_mp4_nal);
	free(iron_mp4_sizes);
	free(iron_mp4_times);
	free(iron_mp4_prev);
}
#endif
