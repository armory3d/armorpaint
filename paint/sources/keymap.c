
#include "global.h"

void keymap_load() {
	g_keymap = keymap_get_default();
	if (!string_equals(g_config->keymap, "default.json")) {
		buffer_t       *blob       = data_get_blob(string("keymap_presets/%s", g_config->keymap));
		any_map_t      *new_keymap = json_parse_to_map(sys_buffer_to_string(blob));
		string_array_t *keys       = map_keys(new_keymap);
		for (i32 i = 0; i < keys->length; ++i) {
			char *key = keys->buffer[i];
			any_map_set(g_keymap, key, any_map_get(new_keymap, key));
		}
		array_free(keys);
		free(keys);
		map_free(new_keymap);
	}
}

void keymap_save() {
	if (string_equals(g_config->keymap, "default.json")) {
		return;
	}
	char     *path   = string("%skeymap_presets/%s", data_path(), g_config->keymap);
	char     *json   = keymap_to_json(g_keymap);
	buffer_t *buffer = sys_string_to_buffer(json);
	iron_file_save_bytes(path, buffer, 0);
	array_free(buffer);
	free(buffer);
	free(json);
}

char *keymap_to_json(any_map_t *keymap) {
	json_encode_begin();
	json_encode_map(keymap);
	return json_encode_end();
}

any_map_t *keymap_get_default() {
	any_map_t *keymap = any_map_create();
	any_map_set(keymap, "action_paint", "left");
	any_map_set(keymap, "action_rotate", "alt+left");
	any_map_set(keymap, "action_pan", "alt+middle");
	any_map_set(keymap, "action_zoom", "alt+right");
	any_map_set(keymap, "rotate_envmap", "ctrl+middle");
	any_map_set(keymap, "set_clone_source", "alt");
	any_map_set(keymap, "stencil_transform", "ctrl");
	any_map_set(keymap, "stencil_hide", "z");
	any_map_set(keymap, "brush_radius", "f");
	any_map_set(keymap, "brush_radius_decrease", "[");
	any_map_set(keymap, "brush_radius_increase", "]");
	any_map_set(keymap, "brush_ruler", "shift");
	any_map_set(keymap, "file_new", "ctrl+n");
	any_map_set(keymap, "file_open", "ctrl+o");
	any_map_set(keymap, "file_open_recent", "ctrl+shift+o");
	any_map_set(keymap, "file_save", "ctrl+s");
	any_map_set(keymap, "file_save_as", "ctrl+shift+s");
	any_map_set(keymap, "file_reimport_mesh", "ctrl+r");
	any_map_set(keymap, "file_reimport_textures", "ctrl+shift+r");
	any_map_set(keymap, "file_import_assets", "ctrl+i");
	any_map_set(keymap, "file_export_textures", "ctrl+e");
	any_map_set(keymap, "file_export_textures_as", "ctrl+shift+e");
	any_map_set(keymap, "edit_undo", "ctrl+z");
	any_map_set(keymap, "edit_redo", "ctrl+shift+z");
	any_map_set(keymap, "edit_prefs", "ctrl+k");
	any_map_set(keymap, "view_reset", "0");
	any_map_set(keymap, "view_front", "1");
	any_map_set(keymap, "view_back", "ctrl+1");
	any_map_set(keymap, "view_right", "3");
	any_map_set(keymap, "view_left", "ctrl+3");
	any_map_set(keymap, "view_top", "7");
	any_map_set(keymap, "view_bottom", "ctrl+7");
	any_map_set(keymap, "view_camera_type", "5");
	any_map_set(keymap, "view_orbit_left", "4");
	any_map_set(keymap, "view_orbit_right", "6");
	any_map_set(keymap, "view_orbit_up", "8");
	any_map_set(keymap, "view_orbit_down", "2");
	any_map_set(keymap, "view_orbit_opposite", "9");
	any_map_set(keymap, "view_zoom_in", "");
	any_map_set(keymap, "view_zoom_out", "");
	any_map_set(keymap, "view_distract_free", "f11");
	any_map_set(keymap, "view_pivot_center", "alt+middle");
	any_map_set(keymap, "viewport_mode", "ctrl+m");
	any_map_set(keymap, "toggle_node_editor", "tab");
	any_map_set(keymap, "toggle_2d_view", "shift+tab");
	any_map_set(keymap, "toggle_browser", "`");
	any_map_set(keymap, "node_overview", "z");
	any_map_set(keymap, "node_search", "space");
	any_map_set(keymap, "operator_search", "space");
	any_map_set(keymap, "decal_mask", "ctrl");
	any_map_set(keymap, "brush_camera_align", "z");
	any_map_set(keymap, "grid_snap", "x");
	any_map_set(keymap, "select_material", "shift+number");
	any_map_set(keymap, "select_layer", "alt+number");
	any_map_set(keymap, "brush_opacity", "shift+f");
	any_map_set(keymap, "brush_angle", "alt+f");
	any_map_set(keymap, "tool_brush", "b");
	any_map_set(keymap, "tool_eraser", "e");
	any_map_set(keymap, "tool_fill", "g");
	any_map_set(keymap, "tool_decal", "d");
	any_map_set(keymap, "tool_text", "t");
	any_map_set(keymap, "tool_clone", "l");
	any_map_set(keymap, "tool_blur", "u");
	any_map_set(keymap, "tool_particle", "p");
	any_map_set(keymap, "tool_colorid", "c");
	any_map_set(keymap, "tool_picker", "v");
	any_map_set(keymap, "tool_cursor", "r");
	any_map_set(keymap, "tool_select", "m");
	any_map_set(keymap, "tool_material", "");
	any_map_set(keymap, "swap_brush_eraser", "");
	return keymap;
}
