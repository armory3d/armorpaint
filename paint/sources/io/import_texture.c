
#include "../global.h"

typedef struct import_texture_data {
	char               *path;
	struct gpu_texture *image;
} import_texture_data_t;

gpu_texture_t *import_texture_default_importer(char *path) {
	return data_get_texture(path);
}

void import_texture_run_on_next_frame(import_texture_data_t *itd) {
	import_envmap_run(itd->path, itd->image);
	free(itd);
}

void import_texture_run(char *path, bool hdr_as_envmap) {
	if (!path_is_texture(path)) {
		if (!context_enable_import_plugin(path)) {
			console_error(strings_unknown_asset_format());
			return;
		}
	}

	for (i32 i = 0; i < g_project->_->assets->length; ++i) {
		asset_t *a = g_project->_->assets->buffer[i];
		// Already imported
		if (string_equals(a->file, path)) {
			// Set as envmap
			if (hdr_as_envmap && ends_with(to_lower_case(path), ".hdr")) {
				gpu_texture_t         *image = data_get_texture(path);
				import_texture_data_t *itd   = ALLOC_INIT(import_texture_data_t, {.path = path, .image = image});
				sys_notify_on_next_frame(&import_texture_run_on_next_frame, itd); // Make sure file browser process did finish
			}
			console_info(strings_asset_already_imported());
			return;
		}
	}

	char *ext                              = substring(path, string_last_index_of(path, ".") + 1, string_length(path));
	gpu_texture_t *(*importer)(char *path) = any_map_get(import_texture_importers, ext);

	bool           cached = any_map_get(data_cached_textures, path) != NULL; // Already loaded or pink texture for missing file
	gpu_texture_t *image;
	if (importer == NULL || cached) {
		image = import_texture_default_importer(path);
	}
	else {
		image = importer(path);
	}

	if (image == NULL) {
		return;
	}

	any_map_set(data_cached_textures, path, image);
	string_array_t *ar    = string_split(path, PATH_SEP);
	char           *name  = ar->buffer[ar->length - 1];
	asset_t        *asset = ALLOC_INIT(asset_t, {.name = name, .file = path, .id = g_project->_->next_asset_id++});
	any_array_push(g_project->_->assets, asset);
	if (g_context->texture == NULL) {
		g_context->texture = asset;
	}
	asset->image                                    = image;
	ui_base_hwnds->buffer[TAB_AREA_STATUS]->redraws = 2;
	console_info(string("%s %s", tr("Texture imported:"), name));

	// Set as envmap
	if (hdr_as_envmap && ends_with(to_lower_case(path), ".hdr")) {
		import_texture_data_t *itd = ALLOC_INIT(import_texture_data_t, {.path = path, .image = image});
		sys_notify_on_next_frame(&import_texture_run_on_next_frame, itd); // Make sure file browser process did finish
	}
}
