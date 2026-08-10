
#ifdef WITH_PLUGINS

#include "engine.h"
#include "iron_array.h"
#include "iron_map.h"
#include "iron_obj.h"
#include "iron_ui.h"
#include <stdlib.h>

void *io_svg_parse(char *buf);
void *io_exr_parse(char *buf, size_t len);
void *io_psd_parse(uint8_t *buf, size_t len, const char *filename);
void *io_tiff_parse(uint8_t *buf, size_t len);
void *io_gltf_parse(char *buf, size_t size, const char *path);
void *io_gltf_parse_skinned(char *buf, size_t size, const char *path, int frame);
void *io_fbx_parse(char *buf, size_t size);
void *io_fbx_parse_skinned(char *buf, size_t size, int frame);
void  proc_uv_unwrap(void *mesh);

typedef struct asset {
	i32   id;
	char *name;
	char *file;
} asset_t;

extern any_map_t      *import_mesh_importers;
extern string_array_t *_path_mesh_formats;
string_array_t        *path_mesh_formats(void);
extern any_map_t      *import_texture_importers;
extern string_array_t *_path_texture_formats;
string_array_t        *path_texture_formats(void);
extern any_map_t      *util_mesh_unwrappers;
extern any_map_t      *data_cached_textures;
void                   import_texture_run(char *path, bool hdr_as_envmap);
any_array_t           *project_get_assets(void);
void                   tab_textures_delete_texture(asset_t *asset);

int plugins_skinning_frame = -1;
int plugins_split_by       = 0;

void io_psd_import_layer(char *file_name, char *layer_name, void *tex) {
	char *path = string("%s.%s.png", file_name, layer_name);
	any_map_set(data_cached_textures, path, tex);
	import_texture_run(path, false);
}

static void *import_exr(char *path) {
	buffer_t *b = data_get_blob(path);
	data_delete_blob(path);
	return io_exr_parse((char *)b->buffer, b->length);
}

static void *import_psd(char *path) {
	char *filename = substring(path, string_last_index_of(path, PATH_SEP) + 1, string_length(path));

	// Delete existing layers so they can be re-imported
	char *prefix = string("%s.", filename);
	any_array_t *project_assets = project_get_assets();
	for (int i = project_assets->length - 1; i >= 0; --i) {
		asset_t *a = project_assets->buffer[i];
		if (starts_with(a->name, prefix)) {
			tab_textures_delete_texture(a);
		}
	}

	buffer_t *b = data_get_blob(path);
	data_delete_blob(path);
	return io_psd_parse((uint8_t *)b->buffer, b->length, filename);
}

static void *import_tiff(char *path) {
	buffer_t *b = data_get_blob(path);
	data_delete_blob(path);
	return io_tiff_parse((uint8_t *)b->buffer, b->length);
}

static void *import_svg(char *path) {
	buffer_t *b = data_get_blob(path);
	data_delete_blob(path);
	return io_svg_parse((char *)b->buffer);
}

static void plugins_free_raw_mesh(raw_mesh_t *raw) {
	i16_array_t *arrays[] = {raw->posa, raw->nora, raw->texa, raw->texa1, raw->cola};
	for (int i = 0; i < 5; ++i) {
		if (arrays[i] != NULL) {
			free(arrays[i]->buffer);
			free(arrays[i]);
		}
	}
	if (raw->inda != NULL) {
		free(raw->inda->buffer);
		free(raw->inda);
	}
	free(raw->name);
	free(raw);
}

bool plugins_skin_data_apply(buffer_t *blob, int frame, i16_array_t *posa, i16_array_t *nora, float *scale_pos) {
	if (blob == NULL) {
		return false;
	}

	raw_mesh_t *raw = io_gltf_parse_skinned((char *)blob->buffer, blob->length, NULL, frame);
	if (raw == NULL) {
		return false;
	}

	memcpy(posa->buffer, raw->posa->buffer, posa->length * sizeof(int16_t));
	memcpy(nora->buffer, raw->nora->buffer, nora->length * sizeof(int16_t));
	*scale_pos = raw->scale_pos;

	plugins_free_raw_mesh(raw);
	return true;
}

static void *import_gltf_glb(char *path) {
	buffer_t *b = data_get_blob(path);
	data_delete_blob(path);
	if (plugins_skinning_frame == -1) {
		return io_gltf_parse((char *)b->buffer, b->length, path);
	}
	else {
		raw_mesh_t *raw = io_gltf_parse_skinned((char *)b->buffer, b->length, path, plugins_skinning_frame);
		raw->blob = b;
		gc_root(b);
		return raw;
	}
}

static void *import_fbx(char *path) {
	buffer_t *b = data_get_blob(path);
	data_delete_blob(path);
	if (plugins_skinning_frame == -1) {
		return io_fbx_parse((char *)b->buffer, b->length);
	}
	else {
		return io_fbx_parse_skinned((char *)b->buffer, b->length, plugins_skinning_frame);
	}
}

void plugins_init() {
	path_texture_formats(); // Init array
	any_map_set(import_texture_importers, "exr", import_exr);
	any_array_push(_path_texture_formats, "exr");
	any_map_set(import_texture_importers, "psd", import_psd);
	any_array_push(_path_texture_formats, "psd");
	any_map_set(import_texture_importers, "svg", import_svg);
	any_array_push(_path_texture_formats, "svg");
	any_map_set(import_texture_importers, "tiff", import_tiff);
	any_array_push(_path_texture_formats, "tiff");
	any_map_set(import_texture_importers, "tif", import_tiff);
	any_array_push(_path_texture_formats, "tif");

	path_mesh_formats(); // Init array
	any_map_set(import_mesh_importers, "gltf", import_gltf_glb);
	any_array_push(_path_mesh_formats, "gltf");
	any_map_set(import_mesh_importers, "glb", import_gltf_glb);
	any_array_push(_path_mesh_formats, "glb");
	any_map_set(import_mesh_importers, "fbx", import_fbx);
	any_array_push(_path_mesh_formats, "fbx");

	any_map_set(util_mesh_unwrappers, "uv_unwrap", proc_uv_unwrap);
}

#endif
