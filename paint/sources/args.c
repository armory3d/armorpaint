
#include "global.h"

bool  args_use                    = false;
char *args_asset_path             = "";
bool  args_background             = false;
bool  args_export_textures        = false;
char *args_export_textures_type   = "";
char *args_export_textures_preset = "";
char *args_export_textures_path   = "";
bool  args_reimport_mesh          = false;
bool  args_export_mesh            = false;
char *args_export_mesh_path       = "";
bool  args_export_material        = false;
char *args_export_material_path   = "";

void args_parse() {
	if (iron_get_arg_count() > 1) {
		args_use = true;

		i32 i = 1;
		while (i < iron_get_arg_count()) {
			// Process each arg
			char *current_arg = iron_get_arg(i);

			if (path_is_project(current_arg)) {
				g_project->_->filepath = string_copy(current_arg);
			}
			else if (string_equals(current_arg, "--background")) {
				args_background = true;
			}
			else if (string_equals(current_arg, "--player")) {
				args_player = true;
			}
			else if (path_is_texture(current_arg)) {
				args_asset_path = string_copy(current_arg);
			}
			else if (string_equals(current_arg, "--export-textures") && (i + 3) <= iron_get_arg_count()) {
				args_export_textures = true;
				++i;
				args_export_textures_type = string_copy(iron_get_arg(i));
				++i;
				args_export_textures_preset = string_copy(iron_get_arg(i));
				++i;
				args_export_textures_path = string_copy(iron_get_arg(i));
			}
			else if (string_equals(current_arg, "--reload-mesh")) {
				args_reimport_mesh = true;
			}
			else if (string_equals(current_arg, "--export-mesh") && (i + 1) <= iron_get_arg_count()) {
				args_export_mesh = true;
				++i;
				args_export_mesh_path = string_copy(iron_get_arg(i));
			}
			else if (path_is_mesh(current_arg) || iron_is_directory(current_arg)) {
				args_asset_path = string_copy(current_arg);
			}
			else if (string_equals(current_arg, "--export-material") && (i + 1) <= iron_get_arg_count()) {
				args_export_material = true;
				++i;
				args_export_material_path = string_copy(iron_get_arg(i));
			}
			else if (string_equals(current_arg, "--help")) {
				printf("Usage: armorpaint [options] [file]\n");
				printf("Options:\n");
				printf("  --background                      Run without displaying the window\n");
				printf("  --export-textures <type> <preset> <path>\n");
				printf("                                    Export textures to path\n");
				printf("                                    type: png, jpg, exr16, exr32\n");
				printf("  --export-mesh <path>              Export mesh to path\n");
				printf("  --export-material <path>          Export material to path\n");
				printf("  --reload-mesh                     Reimport mesh on startup\n");
				printf("  --player                          Run in player mode\n");
				printf("  --help                            Show this help message\n");
				exit(1);
			}
			++i;
		}
	}
}

void args_run_export_queue(void *_) {
	export_texture_run(args_export_textures_path, false);
}

void args_run_on_next_frame(void *_) {
	if (!string_equals(g_project->_->filepath, "")) {
		import_arm_run_project(g_project->_->filepath);
	}
	else if (!string_equals(args_asset_path, "")) {
		import_asset_run(args_asset_path, -1, -1, false, true, NULL);
		if (path_is_texture(args_asset_path)) {
			ui_base_show_2d_view(VIEW_2D_TYPE_ASSET);
		}
	}
	else if (args_reimport_mesh) {
		project_reimport_mesh();
	}

	if (args_export_textures) {
		if (!path_is_folder(args_export_textures_path)) {
			iron_log(tr("Invalid export directory"));
		}

		if (string_equals(args_export_textures_type, "png")) {
			base_bits_handle->i    = TEXTURE_BITS_BITS8;
			g_context->format_type = TEXTURE_LDR_FORMAT_PNG;
		}
		else if (string_equals(args_export_textures_type, "jpg")) {
			base_bits_handle->i    = TEXTURE_BITS_BITS8;
			g_context->format_type = TEXTURE_LDR_FORMAT_JPG;
		}
		else if (string_equals(args_export_textures_type, "exr16")) {
			base_bits_handle->i = TEXTURE_BITS_BITS16;
		}
		else if (string_equals(args_export_textures_type, "exr32")) {
			base_bits_handle->i = TEXTURE_BITS_BITS32;
		}
		else {
			iron_log(tr("Invalid texture type"));
		}

		g_context->layers_export = EXPORT_MODE_VISIBLE;

		// Get export preset and apply the correct one from args
		box_export_files = file_read_directory(string("%s%sexport_presets", path_data(), PATH_SEP));
		for (i32 i = 0; i < box_export_files->length; ++i) {
			char *s                     = box_export_files->buffer[i];
			box_export_files->buffer[i] = substring(s, 0, string_length(s) - 5); // Strip .json
		}

		char *file = string("export_presets/%s.json", box_export_files->buffer[0]);
		for (i32 i = 0; i < box_export_files->length; ++i) {
			char *f = box_export_files->buffer[i];
			if (string_equals(f, args_export_textures_preset)) {
				file = string("export_presets/%s.json", box_export_files->buffer[array_index_of(box_export_files, f)]);
			}
		}

		buffer_t *blob    = data_get_blob(file);
		box_export_preset = json_parse(sys_buffer_to_string(blob));
		data_delete_blob(file);

		// Export queue
		sys_notify_on_next_frame(&args_run_export_queue, NULL);
	}
	else if (args_export_mesh) {
		if (!path_is_folder(args_export_mesh_path)) {
			iron_log(tr("Invalid export directory"));
		}

		char *f = ui_files_filename;
		if (string_equals(f, "")) {
			f = string_copy(tr("untitled"));
		}
		export_mesh_run(string("%s%s%s", args_export_mesh_path, PATH_SEP, f), NULL, true);
	}
	else if (args_export_material) {
		g_context->write_icon_on_export = true;
		export_arm_run_material(args_export_material_path);
	}

	if (args_background) {
		iron_stop();
	}
}

void args_run() {
	if (args_use) {
		sys_notify_on_next_frame(&args_run_on_next_frame, NULL);
	}
}
