
#include "../global.h"

f32  _import_asset_drop_x;
f32  _import_asset_drop_y;
bool _import_asset_show_box;
bool _import_asset_hdr_as_envmap;
void (*_import_asset_done)(void);

void import_asset_run_cache_cloud_done(char *abs) {
	if (abs == NULL) {
		return;
	}
	import_asset_run(string_copy(abs), _import_asset_drop_x, _import_asset_drop_y, _import_asset_show_box, _import_asset_hdr_as_envmap, _import_asset_done);
}

void import_asset_run(char *path, f32 drop_x, f32 drop_y, bool show_box, bool hdr_as_envmap, void (*done)(void)) {
	if (starts_with(path, "cloud")) {
#ifdef IRON_ANDROID
		console_toast(tr("Downloading"));
#else
		console_info(tr("Downloading"));
#endif

		_import_asset_drop_x        = drop_x;
		_import_asset_drop_y        = drop_y;
		_import_asset_show_box      = show_box;
		_import_asset_hdr_as_envmap = hdr_as_envmap;
		gc_unroot(_import_asset_done);
		_import_asset_done = done;
		gc_root(_import_asset_done);
		file_cache_cloud(path, &import_asset_run_cache_cloud_done, g_config->server);

		return;
	}

	if (path_is_mesh(path)) {
		show_box ? project_import_mesh_box(path, true, true, false, NULL) : import_mesh_run(path, true, true, false);
		if (drop_x > 0) {
			ui_box_click_to_hide = false; // Prevent closing when going back to window after drag and drop
		}
	}
	else if (path_is_texture(path)) {
		import_texture_run(path, hdr_as_envmap);
		// Place image node
		i32 x0 = ui_nodes_wx;
		i32 x1 = ui_nodes_wx + ui_nodes_ww;
		if (ui_nodes_show && drop_x > x0 && drop_x < x1) {
			i32 asset_index = 0;
			for (i32 i = 0; i < g_project->_->assets->length; ++i) {
				if (string_equals(g_project->_->assets->buffer[i]->file, path)) {
					asset_index = i;
					break;
				}
			}
			ui_nodes_accept_asset_drop(asset_index);
			ui_nodes_get_nodes()->nodes_drag = false;
			ui_nodes_hwnd->redraws           = 2;
		}

		if (g_context->tool == TOOL_TYPE_COLORID && g_project->_->assets->length == 1) {
			ui_header_handle->redraws = 2;
			g_context->ddirty         = 2;
		}
	}
	else if (path_is_text(path)) {
		import_text_run(path);
	}
	else if (path_is_sound(path)) {
		import_sound_run(path);
	}
	else if (path_is_lut(path)) {
		box_preferences_lut_picked(path);
	}
	else if (path_is_project(path)) {
		import_arm_run_project(path);
	}
	else if (path_is_plugin(path)) {
		import_plugin_run(path);
	}
	else if (path_is_font(path)) {
		import_font_run(path);
	}
	else if (path_is_folder(path)) {
		import_folder_run(path);
	}
	else {
		if (context_enable_import_plugin(path)) {
			import_asset_run(path, drop_x, drop_y, show_box, true, NULL);
		}
		else {
			console_error(strings_unknown_asset_format());
		}
	}

	if (done != NULL) {
		done();
	}
}
