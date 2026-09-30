
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

#include <minih264e.h>
#include <minimp4.h>

buffer_t *iron_load_blob(char *file);

static FILE            *iron_mp4_fp;
static int              iron_mp4_w;
static int              iron_mp4_h;
static int              iron_mp4_stride;
static H264E_persist_t *iron_mp4_enc     = NULL;
static H264E_scratch_t *iron_mp4_scratch = NULL;
static char             iron_mp4_path[512];
static char             iron_mp4_path_264[512];
static uint8_t         *iron_mp4_yuv_buf;

static size_t iron_mp4_get_nal_size(uint8_t *buf, size_t size) {
	size_t pos = 3;
	while ((size - pos) > 3) {
		if (buf[pos] == 0 && buf[pos + 1] == 0 && buf[pos + 2] == 1) {
			return pos;
		}
		if (buf[pos] == 0 && buf[pos + 1] == 0 && buf[pos + 2] == 0 && buf[pos + 3] == 1) {
			return pos;
		}
		pos++;
	}
	return size;
}

static int iron_mp4_write_callback(int64_t offset, const void *buffer, size_t size, void *token) {
	FILE *f = (FILE *)token;
	fseek(f, offset, SEEK_SET);
	return fwrite(buffer, 1, size, f) != size;
}

void iron_mp4_begin(char *path, i32 w, i32 h) {
	strcpy(iron_mp4_path, path);
	strcpy(iron_mp4_path_264, path);
	int len                    = strlen(iron_mp4_path_264);
	iron_mp4_path_264[len - 1] = '4';
	iron_mp4_path_264[len - 2] = '6';
	iron_mp4_path_264[len - 3] = '2';

	iron_mp4_stride = w;
	iron_mp4_w      = w - w % 16;
	iron_mp4_h      = h - h % 16;

	H264E_create_param_t create_param = {0};
	create_param.width                = iron_mp4_w;
	create_param.height               = iron_mp4_h;
	int sizeof_persist                = 0;
	int sizeof_scratch                = 0;
	H264E_sizeof(&create_param, &sizeof_persist, &sizeof_scratch);

	iron_mp4_enc     = (H264E_persist_t *)malloc(sizeof_persist);
	iron_mp4_scratch = (H264E_scratch_t *)malloc(sizeof_scratch);
	H264E_init(iron_mp4_enc, &create_param);

	iron_mp4_fp      = fopen(iron_mp4_path_264, "wb");
	int frame_size   = (int)(iron_mp4_w * iron_mp4_h * 1.5);
	iron_mp4_yuv_buf = malloc(frame_size);
}

void iron_mp4_end() {
	if (iron_mp4_fp == NULL) {
		return;
	}

	buffer_t         *blob     = iron_load_blob(iron_mp4_path_264);
	uint8_t          *buf      = blob->buffer;
	size_t            buf_size = blob->length;
	FILE             *fout     = fopen(iron_mp4_path, "wb");
	MP4E_mux_t       *mux      = MP4E_open(0, 0, fout, iron_mp4_write_callback);
	mp4_h26x_writer_t mp4wr;
	mp4_h26x_write_init(&mp4wr, mux, iron_mp4_w, iron_mp4_h, false);

	while (buf_size > 0) {
		size_t nal_size = iron_mp4_get_nal_size(buf, buf_size);
		if (nal_size < 4) {
			buf += 1;
			buf_size -= 1;
			continue;
		}

		int fps = 24;
		mp4_h26x_write_nal(&mp4wr, buf, nal_size, 90000 / fps);
		buf += nal_size;
		buf_size -= nal_size;
	}

	MP4E_close(mux);
	mp4_h26x_write_close(&mp4wr);
	free(iron_mp4_enc);
	free(iron_mp4_scratch);
	free(iron_mp4_yuv_buf);
	fclose(fout);
	fclose(iron_mp4_fp);
	iron_mp4_fp = NULL;
}

void iron_mp4_encode(buffer_t *pixels) {
	// rgba to yuv420p
	for (int i = 0; i < iron_mp4_w; ++i) {
		for (int j = 0; j < iron_mp4_h; ++j) {
			int     k                                                                     = i + j * iron_mp4_stride;
			uint8_t r                                                                     = pixels->buffer[k * 4];
			uint8_t g                                                                     = pixels->buffer[k * 4 + 1];
			uint8_t b                                                                     = pixels->buffer[k * 4 + 2];
			uint8_t y                                                                     = ((66 * r + 129 * g + 25 * b + 128) / 256) + 16;
			uint8_t u                                                                     = ((-38 * r - 74 * g + 112 * b + 128) / 256) + 128;
			uint8_t v                                                                     = ((112 * r - 94 * g - 18 * b + 128) / 256) + 128;
			int     l                                                                     = i + j * iron_mp4_w;
			int     m                                                                     = i / 2 + j / 2 * (iron_mp4_w / 2);
			iron_mp4_yuv_buf[l]                                                           = y;
			iron_mp4_yuv_buf[iron_mp4_w * iron_mp4_h + m]                                 = u;
			iron_mp4_yuv_buf[iron_mp4_w * iron_mp4_h + (iron_mp4_w * iron_mp4_h) / 4 + m] = v;
		}
	}

	H264E_run_param_t run_param   = {0};
	run_param.frame_type          = 0;
	run_param.encode_speed        = H264E_SPEED_SLOWEST;        // H264E_SPEED_FASTEST;
	run_param.desired_frame_bytes = (2048 * 4) * 1000 / 8 / 30; // 2048 * 4 kbps
	run_param.qp_min              = 10;
	run_param.qp_max              = 50;

	H264E_io_yuv_t yuv;
	yuv.yuv[0]    = iron_mp4_yuv_buf;
	yuv.stride[0] = iron_mp4_w;
	yuv.yuv[1]    = iron_mp4_yuv_buf + iron_mp4_w * iron_mp4_h;
	yuv.stride[1] = iron_mp4_w / 2;
	yuv.yuv[2]    = iron_mp4_yuv_buf + (int)(iron_mp4_w * iron_mp4_h * 1.25);
	yuv.stride[2] = iron_mp4_w / 2;

	uint8_t *coded_data;
	int      sizeof_coded_data;
	H264E_encode(iron_mp4_enc, iron_mp4_scratch, &run_param, &yuv, &coded_data, &sizeof_coded_data);
	fwrite(coded_data, sizeof_coded_data, 1, iron_mp4_fp);
}
#endif
