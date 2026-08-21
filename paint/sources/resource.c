
#include "global.h"

void resource_load(string_array_t *names) {
	for (i32 i = 0; i < names->length; ++i) {
		char          *s     = names->buffer[i];
		gpu_texture_t *image = data_get_texture(s);
		any_map_set(resource_bundled, s, image);
	}
}

gpu_texture_t *resource_get(char *name) {
	return any_map_get(resource_bundled, name);
}

rect_t *resource_tile50(gpu_texture_t *img, i32 i) {
	i32     x    = i % 12;
	i32     y    = i / 12.0;
	i32     size = g_config->window_scale > 1 ? 100 : 50;
	rect_t *r    = TMP_ALLOC_INIT(rect_t, {.x = x * size, .y = y * size, .w = size, .h = size}); // Read by the caller right away
	return r;
}

rect_t *resource_tile18(gpu_texture_t *img, i32 i) {
	i32     size = g_config->window_scale > 1 ? 36 : 18;
	rect_t *r    = TMP_ALLOC_INIT(rect_t, {.x = i * size, .y = img->height - size, .w = size, .h = size}); // Read by the caller right away
	return r;
}
