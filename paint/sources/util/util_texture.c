
#include "../global.h"

void util_texture_capture_output(gpu_texture_t *img, char *basename) {
	if (img == NULL) {
		return;
	}

	if (g_project->packed_assets == NULL) {
		g_project->packed_assets = any_array_create_from_raw((void *[]){}, 0);
	}

	bool  is_hdr = img->format == GPU_TEXTURE_FORMAT_RGBA64;
	char *ext    = is_hdr ? ".hdr" : ".png";
	i32   num    = 0;
	char *abs    = string("/packed/%s0%s", basename, ext);
	for (i32 i = 0; i < g_project->packed_assets->length; ++i) {
		packed_asset_t *pa = g_project->packed_assets->buffer[i];
		if (string_equals(pa->name, abs)) {
			i = 0;
			num++;
			abs = string("/packed/%s%d%s", basename, num, ext);
		}
	}

	buffer_t       *buf   = gpu_get_texture_pixels(img);
	u8_array_t     *bytes = is_hdr ? iron_encode_hdr(buf, img->width, img->height) : iron_encode_png(buf, img->width, img->height, 0);
	packed_asset_t *pa    = ALLOC_INIT(packed_asset_t, {.name = abs, .bytes = bytes});
	any_array_push(g_project->packed_assets, pa);
	gpu_texture_t *copy = gpu_create_texture_from_encoded_bytes(bytes, ext);
	any_map_set(data_cached_textures, abs, copy);
	import_texture_run(abs, true);
}
