
#ifdef WITH_PLUGINS

#include "engine.h"
#include "iron_array.h"
#include "iron_map.h"
#include "iron_obj.h"
#include "iron_ui.h"
#include <stdlib.h>

void     *io_svg_parse(char *buf);
void     *io_exr_parse(char *buf, size_t len);
void     *io_psd_parse(uint8_t *buf, size_t len, const char *filename);
void     *io_tiff_parse(uint8_t *buf, size_t len);
void     *io_gltf_parse(char *buf, size_t size, const char *path);
buffer_t *io_gltf_skin_blob(char *buf, size_t size, const char *path);
void     *util_skin_raw_mesh(buffer_t *blob);
void     *io_fbx_parse(char *buf, size_t size);
void     *io_fbx_parse_skinned(char *buf, size_t size);

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
	buffer_t *b   = data_get_blob(path);
	void     *res = io_exr_parse((char *)b->buffer, b->length);
	data_delete_blob(path);
	return res;
}

static void *import_psd(char *path) {
	char *filename = substring(path, string_last_index_of(path, PATH_SEP) + 1, string_length(path));

	// Delete existing layers so they can be re-imported
	char        *prefix         = string("%s.", filename);
	any_array_t *project_assets = project_get_assets();
	for (int i = project_assets->length - 1; i >= 0; --i) {
		asset_t *a = project_assets->buffer[i];
		if (starts_with(a->name, prefix)) {
			tab_textures_delete_texture(a);
		}
	}

	buffer_t *b   = data_get_blob(path);
	void     *res = io_psd_parse((uint8_t *)b->buffer, b->length, filename);
	data_delete_blob(path);
	return res;
}

static void *import_tiff(char *path) {
	buffer_t *b   = data_get_blob(path);
	void     *res = io_tiff_parse((uint8_t *)b->buffer, b->length);
	data_delete_blob(path);
	return res;
}

static void *import_svg(char *path) {
	buffer_t *b   = data_get_blob(path);
	void     *res = io_svg_parse((char *)b->buffer);
	data_delete_blob(path);
	return res;
}

static void *import_gltf_glb(char *path) {
	buffer_t *b = data_get_blob(path);
	if (b == NULL) {
		return NULL;
	}
	if (plugins_skinning_frame != -1) {
		buffer_t *blob = io_gltf_skin_blob((char *)b->buffer, b->length, path);
		if (blob != NULL) {
			data_delete_blob(path);
			return util_skin_raw_mesh(blob);
		}
	}
	void *res = io_gltf_parse((char *)b->buffer, b->length, path);
	data_delete_blob(path);
	return res;
}

static void *import_fbx(char *path) {
	buffer_t *b = data_get_blob(path);
	if (b == NULL) {
		return NULL;
	}
	void *res = plugins_skinning_frame == -1 ? io_fbx_parse((char *)b->buffer, b->length) : io_fbx_parse_skinned((char *)b->buffer, b->length);
	data_delete_blob(path);
	return res;
}

#ifdef WITH_EXTERNAL
	void external_init();
#endif

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

#ifdef WITH_EXTERNAL
	external_init();
#endif
}

#endif
