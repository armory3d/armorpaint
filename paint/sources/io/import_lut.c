
#include "../global.h"
#include <stdlib.h>

void import_lut_free(void) {
	if (lut_image != NULL) {
		gpu_texture_destroy(lut_image);
		free(lut_image);
		lut_image = NULL;
		lut_size  = 0;
	}
}

void import_lut_run(const char *path) {
	iron_file_reader_t reader;
	iron_file_reader_open(&reader, path, IRON_FILE_TYPE_ASSET);

	i32   file_size = (i32)iron_file_reader_size(&reader);
	char *text      = (char *)malloc(file_size + 1);
	iron_file_reader_read(&reader, text, file_size);
	iron_file_reader_close(&reader);
	text[file_size] = '\0';

	i32  n          = 0;
	f32 *cube       = NULL;
	i32  data_count = 0;

	char *line = text;
	while (*line != '\0') {
		char *eol = line;
		while (*eol != '\n' && *eol != '\r' && *eol != '\0')
			eol++;
		char saved = *eol;
		*eol       = '\0';

		while (*line == ' ' || *line == '\t')
			line++;

		if (*line != '#' && *line != '\0') {
			if (strncmp(line, "LUT_3D_SIZE", 11) == 0) {
				n    = (int)strtol(line + 11, NULL, 10);
				cube = (f32 *)malloc(n * n * n * 3 * sizeof(f32));
			}
			else if (n > 0 && cube != NULL && data_count < n * n * n) {
				if ((*line >= '0' && *line <= '9') || *line == '-' || *line == '.') {
					char *p;
					f32   r = strtof(line, &p);
					f32   g = strtof(p, &p);
					f32   b = strtof(p, &p);
					if (p != line) {
						cube[data_count * 3 + 0] = r;
						cube[data_count * 3 + 1] = g;
						cube[data_count * 3 + 2] = b;
						data_count++;
					}
				}
			}
		}

		*eol = saved;
		line = eol;
		while (*line == '\n' || *line == '\r')
			line++;
	}

	free(text);

	if (n == 0 || cube == NULL || data_count != n * n * n) {
		free(cube);
		return;
	}

	// Pack 3d lut into 2d texture
	i32 strip_w = n * n;
	i32 strip_h = n;
	u8 *pixels  = (u8 *)malloc(strip_w * strip_h * 4);

	for (i32 b = 0; b < n; b++) {
		for (i32 g = 0; g < n; g++) {
			for (i32 r = 0; r < n; r++) {
				i32 cube_idx        = (b * n * n + g * n + r) * 3;
				i32 px              = b * n + r;
				i32 py              = g;
				i32 pix_idx         = (py * strip_w + px) * 4;
				pixels[pix_idx + 0] = (u8)(fminf(fmaxf(cube[cube_idx + 0], 0.0f), 1.0f) * 255.0f + 0.5f);
				pixels[pix_idx + 1] = (u8)(fminf(fmaxf(cube[cube_idx + 1], 0.0f), 1.0f) * 255.0f + 0.5f);
				pixels[pix_idx + 2] = (u8)(fminf(fmaxf(cube[cube_idx + 2], 0.0f), 1.0f) * 255.0f + 0.5f);
				pixels[pix_idx + 3] = 255;
			}
		}
	}

	free(cube);
	import_lut_free();
	lut_image = (gpu_texture_t *)malloc(sizeof(gpu_texture_t));
	gpu_texture_init_from_bytes(lut_image, pixels, strip_w, strip_h, GPU_TEXTURE_FORMAT_RGBA32, false);
	free(pixels);
	lut_size = n;
}
