
#include "../global.h"

string_array_t *box_preferences_files_keymap = NULL;
string_array_t *box_preferences_locales      = NULL;
string_array_t *box_preferences_themes       = NULL;
char           *_box_preferences_f;
ui_handle_t    *_box_preferences_h;
i32             _box_preferences_i;

void box_preferences_set_scale() {
	f32 scale = g_config->window_scale;
	ui_set_scale(scale);
	ui_header_h                                    = math_floor(ui_header_default_h * scale);
	g_config->layout->buffer[LAYOUT_SIZE_STATUS_H] = math_floor(ui_statusbar_default_h * scale);
	ui_menubar_w                                   = math_floor(ui_menubar_default_w * scale);
	ui_base_set_icon_scale();
	base_resize();
	g_config->layout->buffer[LAYOUT_SIZE_SIDEBAR_W] = math_floor(ui_sidebar_default_w * scale);
}

// ██╗███╗   ██╗████████╗███████╗██████╗ ███████╗ █████╗  ██████╗███████╗
// ██║████╗  ██║╚══██╔══╝██╔════╝██╔══██╗██╔════╝██╔══██╗██╔════╝██╔════╝
// ██║██╔██╗ ██║   ██║   █████╗  ██████╔╝█████╗  ███████║██║     █████╗
// ██║██║╚██╗██║   ██║   ██╔══╝  ██╔══██╗██╔══╝  ██╔══██║██║     ██╔══╝
// ██║██║ ╚████║   ██║   ███████╗██║  ██║██║     ██║  ██║╚██████╗███████╗
// ╚═╝╚═╝  ╚═══╝   ╚═╝   ╚══════╝╚═╝  ╚═╝╚═╝     ╚═╝  ╚═╝ ╚═════╝╚══════╝

void box_preferences_interface_tab_reset_layout_menu() {
	if (ui_menu_button(tr("Confirm"), "", ICON_CHECK)) {
		config_init_layout();
		config_save();
	}
}

void box_preferences_interface_tab_restore_menu_import_on_next_frame(config_t *raw) {
	g_theme->ELEMENT_H = base_default_element_h;
	config_import_from(raw);
	box_preferences_set_scale();
	make_material_parse_mesh_material();
	make_material_parse_paint_material(true);
}

void box_preferences_interface_tab_restore_menu_import(char *path) {
	buffer_t *b   = data_get_blob(path);
	config_t *raw = json_parse(sys_buffer_to_string(b));
	sys_notify_on_next_frame(&box_preferences_interface_tab_restore_menu_import_on_next_frame, raw);
}

void box_preferences_interface_tab_restore_menu_confirm(void *_) {
	g_theme->ELEMENT_H = base_default_element_h;
	config_restore();
	box_preferences_set_scale();
	if (g_config->plugins != NULL) {
		for (i32 i = 0; i < g_config->plugins->length; ++i) {
			char *f = g_config->plugins->buffer[i];
			plugin_stop(f);
		}
	}
	gc_unroot(box_preferences_files_plugin);
	box_preferences_files_plugin = NULL;
	gc_unroot(box_preferences_files_keymap);
	box_preferences_files_keymap = NULL;
	make_material_parse_mesh_material();
	make_material_parse_paint_material(true);
	ui_base_set_viewport_col(g_theme->VIEWPORT_COL);
}

void box_preferences_interface_tab_restore_menu() {
	if (ui_menu_button(tr("Confirm"), "", ICON_CHECK)) {
		sys_notify_on_next_frame(&box_preferences_interface_tab_restore_menu_confirm, NULL);
	}
	if (ui_menu_button(tr("Import..."), "", ICON_IMPORT)) {
		ui_files_show("json", false, false, &box_preferences_interface_tab_restore_menu_import);
	}
}

void box_preferences_interface_tab() {
	if (box_preferences_locales == NULL) {
		gc_unroot(box_preferences_locales);
		box_preferences_locales = translator_get_supported_locales();
		gc_root(box_preferences_locales);
	}

	ui_handle_t *h_locale = ui_handle(__ID__);
	h_locale->i           = string_array_index_of(box_preferences_locales, g_config->locale);
	ui_combo(h_locale, box_preferences_locales, tr("Language"), true, UI_ALIGN_LEFT, true);
	if (h_locale->changed) {
		char *locale_code = box_preferences_locales->buffer[h_locale->i];
		g_config->locale  = string_copy(locale_code);
		translator_load_translations(locale_code);
		base_redraw_ui();
	}

	ui_handle_t *h_scale = ui_handle(__ID__);
	if (h_scale->init) {
		h_scale->f = g_config->window_scale;
	}
	ui_slider(h_scale, tr("UI Scale"), 1.0, 4.0, true, 10, true, UI_ALIGN_RIGHT, true);
	if (g_context->hscale_was_changed && !g_ui->input_down) {
		g_context->hscale_was_changed = false;
		if (h_scale->f == 0.0) {
			h_scale->f = 1.0;
		}
		g_config->window_scale = h_scale->f;
		box_preferences_set_scale();
	}
	if (h_scale->changed) {
		g_context->hscale_was_changed = true;
	}

	ui_handle_t *h_node_previews = ui_handle(__ID__);
	h_node_previews->b           = g_config->node_previews;
	g_config->node_previews      = ui_check(h_node_previews, tr("Node Previews"), "");
	if (h_node_previews->changed) {
		for (i32 i = 0; i < g_project->_->materials->length; ++i) {
			ui_node_canvas_t *c = g_project->_->materials->buffer[i]->canvas;
			for (i32 j = 0; j < c->nodes->length; ++j) {
				ui_node_t *n = c->nodes->buffer[j];
				if (g_config->node_previews) {
					n->flags |= UI_NODE_FLAG_PREVIEW;
				}
				else {
					n->flags &= ~UI_NODE_FLAG_PREVIEW;
				}
			}
		}
		ui_nodes_hwnd->redraws = 2;
	}
	if (g_ui->is_hovered) {
		ui_tooltip(tr("Show node preview on each node by default"));
	}

	ui_handle_t *h_wrap_mouse = ui_handle(__ID__);
	h_wrap_mouse->b           = g_config->wrap_mouse;
	g_config->wrap_mouse      = ui_check(h_wrap_mouse, tr("Wrap Mouse"), "");
	if (g_ui->is_hovered) {
		ui_tooltip(tr("Wrap mouse around view boundaries during camera control"));
	}

	g_ui->changed                   = false;
	ui_handle_t *h_show_asset_names = ui_handle(__ID__);
	h_show_asset_names->b           = g_config->show_asset_names;
	g_config->show_asset_names      = ui_check(h_show_asset_names, tr("Show Asset Names"), "");
	if (g_ui->changed) {
		base_redraw_ui();
	}

	g_ui->changed           = false;
	ui_handle_t *h_touch_ui = ui_handle(__ID__);
	h_touch_ui->b           = g_config->touch_ui;
	g_config->touch_ui      = ui_check(h_touch_ui, tr("Touch UI"), "");
	if (g_ui->changed) {
		ui_touch_control = g_config->touch_ui;
		config_load_theme(g_config->theme, true);
		box_preferences_set_scale();
		base_redraw_ui();
		g_context->hscale_was_changed = true;
	}

	ui_handle_t *h_splash_screen = ui_handle(__ID__);
	h_splash_screen->b           = g_config->splash_screen;
	g_config->splash_screen      = ui_check(h_splash_screen, tr("Splash Screen"), "");

	ui_handle_t *h_grid_snap = ui_handle(__ID__);
	h_grid_snap->b           = g_config->grid_snap;
	g_config->grid_snap      = ui_check(h_grid_snap, tr("Grid Snap"), "");
	ui_nodes_grid_snap       = g_config->grid_snap;

	ui_handle_t *h_experimental = ui_handle(__ID__);
	h_experimental->b           = g_config->experimental;
	g_config->experimental      = ui_check(h_experimental, tr("Experimental Features"), "");

	ui_end_element();

	ui_row2();
	if (ui_icon_button(tr("Restore"), ICON_REPLAY, UI_ALIGN_CENTER) && !ui_menu_show) {
		ui_menu_draw(&box_preferences_interface_tab_restore_menu, -1, -1);
	}
	if (ui_button(tr("Reset Layout"), UI_ALIGN_CENTER, "") && !ui_menu_show) {
		ui_menu_draw(&box_preferences_interface_tab_reset_layout_menu, -1, -1);
	}
}

// ████████╗██╗  ██╗███████╗███╗   ███╗███████╗
// ╚══██╔══╝██║  ██║██╔════╝████╗ ████║██╔════╝
//    ██║   ███████║█████╗  ██╔████╔██║█████╗
//    ██║   ██╔══██║██╔══╝  ██║╚██╔╝██║██╔══╝
//    ██║   ██║  ██║███████╗██║ ╚═╝ ██║███████╗
//    ╚═╝   ╚═╝  ╚═╝╚══════╝╚═╝     ╚═╝╚══════╝

void box_preferences_theme_tab_theme_field_menu() {
	g_ui->changed                     = false;
	i32  color                        = ui_color_wheel(_box_preferences_h, false, -1, 11 * g_theme->ELEMENT_H * UI_SCALE(), true, NULL, NULL);
	u32 *u32_theme                    = g_theme;
	*(u32_theme + _box_preferences_i) = color;
	if (g_ui->changed) {
		ui_menu_keep_open = true;
	}
}

char *box_preferences_theme_to_json(ui_theme_t *theme) {
	json_encode_begin();
	u32 *u32_theme = theme;
	for (i32 i = 0; i < ui_theme_keys_count; ++i) {
		char *key = ui_theme_keys[i];
		u32   val = *(u32_theme + i);
		json_encode_i32(key, val);
	}
	return json_encode_end();
}

void box_preferences_theme_tab_export(char *path) {
	path = string("%s%s%s", path, PATH_SEP, ui_files_filename);
	if (!ends_with(path, ".json")) {
		path = string("%s.json", path);
	}
	iron_file_save_bytes(path, sys_string_to_buffer(box_preferences_theme_to_json(g_theme)), 0);
}

void box_preferences_theme_tab_import(char *path) {
	import_theme_run(path);
}

void box_preferences_theme_tab_new_box() {
	ui_row2();
	ui_handle_t *h = ui_handle(__ID__);
	if (h->init) {
		h->text = "new_theme";
	}
	char *theme_name = ui_text_input(h, tr("Name"), UI_ALIGN_LEFT, true, false);
	if (ui_icon_button(tr("OK"), ICON_CHECK, UI_ALIGN_CENTER) || g_ui->is_return_down) {
		char *template = box_preferences_theme_to_json(g_theme);
		if (!ends_with(theme_name, ".json")) {
			theme_name = string("%s.json", theme_name);
		}
		char *path = string("%s%sthemes%s%s", path_data(), PATH_SEP, PATH_SEP, theme_name);
		iron_file_save_bytes(path, sys_string_to_buffer(template), 0);
		box_preferences_fetch_themes(); // Refresh file list
		g_config->theme            = string_copy(theme_name);
		box_preferences_h_theme->i = box_preferences_get_theme_index();
		ui_box_hide();
		box_preferences_htab->i = 1; // Themes
		box_preferences_show();
	}
}

void box_preferences_theme_tab() {
	if (box_preferences_themes == NULL) {
		box_preferences_fetch_themes();
	}

	ui_begin_sticky();
	ui_row4();

	gc_unroot(box_preferences_h_theme);
	box_preferences_h_theme = ui_handle(__ID__);
	gc_root(box_preferences_h_theme);
	box_preferences_h_theme->i = box_preferences_get_theme_index();
	ui_combo(box_preferences_h_theme, box_preferences_themes, tr("Theme"), false, UI_ALIGN_LEFT, true);
	if (box_preferences_h_theme->changed) {
		g_config->theme = string("%s.json", box_preferences_themes->buffer[box_preferences_h_theme->i]);
		config_load_theme(g_config->theme, true);
	}

	if (ui_icon_button(tr("New"), ICON_PLUS, UI_ALIGN_CENTER)) {
		ui_box_show_custom(&box_preferences_theme_tab_new_box, 400, 200, NULL, true, tr("New Theme"));
	}

	if (ui_icon_button(tr("Import"), ICON_IMPORT, UI_ALIGN_CENTER)) {
		ui_files_show("json", false, false, &box_preferences_theme_tab_import);
	}

	if (ui_icon_button(tr("Export"), ICON_EXPORT, UI_ALIGN_CENTER)) {
		ui_files_show("json", true, false, &box_preferences_theme_tab_export);
	}

	ui_handle_t *box_preferences_theme_hsearch = ui_handle(__ID__);
	if (!string_equals(box_preferences_theme_hsearch->text, "")) {
		ui_row(f32_array_create_from_raw((f32[]){0.85, 0.15}, 2));
	}
	box_preferences_theme_hsearch->text = string_copy(ui_text_input(box_preferences_theme_hsearch, tr("Search"), UI_ALIGN_LEFT, true, true));
	if (!string_equals(box_preferences_theme_hsearch->text, "") && (ui_button(tr("X"), UI_ALIGN_CENTER, "") || g_ui->is_escape_down)) {
		box_preferences_theme_hsearch->text = "";
	}

	ui_end_sticky();

	// Theme fields
	char        *theme_search = to_lower_case(box_preferences_theme_hsearch->text);
	ui_handle_t *h_list       = ui_handle(__ID__);
	u32         *u32_theme    = g_theme;
	g_ui->input_enabled       = !ui_menu_show;
	for (i32 i = 0; i < ui_theme_keys_count; ++i) {
		char *key = ui_theme_keys[i];
		if (!string_equals(theme_search, "") && string_index_of(to_lower_case(key), theme_search) == -1) {
			continue;
		}
		ui_handle_t *h      = ui_nest(h_list, i);
		u32          val    = *(u32_theme + i);
		bool         is_hex = ends_with(key, "_COL");

		if (is_hex) {
			f32_array_t *row = f32_array_create_from_raw(
			    (f32[]){
			        1 / 8.0,
			        7 / 8.0,
			    },
			    2);
			ui_row(row);
			ui_text("", 0, val);
			if (g_ui->is_hovered && g_ui->input_released) {
				h->color = val;
				gc_unroot(_box_preferences_h);
				_box_preferences_h = h;
				gc_root(_box_preferences_h);
				_box_preferences_i = i;
				ui_menu_draw(&box_preferences_theme_tab_theme_field_menu, -1, -1);
			}

			if (string_equals(key, "VIEWPORT_COL") && ui_base_viewport_col != val) {
				ui_base_set_viewport_col(val);
			}
		}

		g_ui->changed = false;

		if (string_equals(key, "FILL_BUTTON_BG") || string_equals(key, "FULL_TABS") || string_equals(key, "SHADOWS")) {
			h->b             = val > 0;
			bool b           = ui_check(h, key, "");
			*(u32_theme + i) = b;
		}
		else if (string_equals(key, "LINK_STYLE")) {
			string_array_t *styles = any_array_create_from_raw(
			    (void *[]){
			        tr("Straight"),
			        tr("Curved"),
			    },
			    2);
			h->i             = val;
			i32 pos          = ui_combo(h, styles, key, true, UI_ALIGN_LEFT, true);
			*(u32_theme + i) = pos;
		}
		else {
			h->text = is_hex ? i32_to_string_hex(val) : i32_to_string(val);
			ui_text_input(h, key, UI_ALIGN_LEFT, true, false);
			if (is_hex) {
				*(u32_theme + i) = parse_int_hex(h->text);
			}
			else {
				*(u32_theme + i) = parse_int(h->text);
			}
		}
		if (g_ui->changed) {
			g_ui->elements_baked = false;
			g_ui->font_size      = g_theme->FONT_SIZE;
		}
	}
	g_ui->input_enabled = true;
}

// ██╗   ██╗███████╗ █████╗  ██████╗ ███████╗
// ██║   ██║██╔════╝██╔══██╗██╔════╝ ██╔════╝
// ██║   ██║███████╗███████║██║  ███╗█████╗
// ██║   ██║╚════██║██╔══██║██║   ██║██╔══╝
// ╚██████╔╝███████║██║  ██║╚██████╔╝███████╗
//  ╚═════╝ ╚══════╝╚═╝  ╚═╝ ╚═════╝ ╚══════╝

void box_preferences_usage_tab() {
	ui_handle_t *h_undo  = ui_handle(__ID__);
	h_undo->f            = g_config->undo_steps;
	g_config->undo_steps = ui_slider(h_undo, tr("Undo Steps"), 1, 64, false, 1, true, UI_ALIGN_RIGHT, true);
	if (g_config->undo_steps < 1) {
		g_config->undo_steps = h_undo->f = 1;
	}
	if (h_undo->changed) {
		gpu_texture_t *current = _draw_current;
		draw_end();

		if (history_undo_layers != NULL) {
			while (history_undo_layers->length < g_config->undo_steps) {
				i32           len = history_undo_layers->length;
				slot_layer_t *l   = slot_layer_create(string("_undo%s", i32_to_string(len)), LAYER_SLOT_TYPE_LAYER, NULL);
				any_array_push(history_undo_layers, l);
			}
			while (history_undo_layers->length > g_config->undo_steps) {
				slot_layer_t *l = array_pop(history_undo_layers);
				slot_layer_unload(l);
			}
		}

		history_reset();
		draw_begin(current, false, 0);
	}

	ui_handle_t *h_dilate_radius = ui_handle(__ID__);
	h_dilate_radius->f           = g_config->dilate_radius;
	g_config->dilate_radius      = ui_slider(h_dilate_radius, tr("Dilate Radius"), 0.0, 16.0, true, 1, true, UI_ALIGN_RIGHT, true);
	if (g_ui->is_hovered) {
		ui_tooltip(tr("Dilate painted textures to prevent seams"));
	}

	ui_handle_t *h_layer_res = ui_handle(__ID__);
	h_layer_res->i           = g_config->layer_res;

	string_array_t *res_combo = any_array_create_from_raw(
	    (void *[]){
	        "2048",
	        "4096",
	        "8192",
	        "16384",
	    },
	    4);
	g_config->layer_res = ui_combo(h_layer_res, res_combo, tr("Default Layer Resolution"), true, UI_ALIGN_LEFT, true);

	ui_handle_t *h_scene_atlas_res = ui_handle(__ID__);
	h_scene_atlas_res->i           = g_config->scene_atlas_res;
	g_config->scene_atlas_res      = ui_combo(h_scene_atlas_res, res_combo, tr("Scene Atlas Resolution"), true, UI_ALIGN_LEFT, true);

	ui_handle_t *h_server = ui_handle(__ID__);
	h_server->text        = string_copy(g_config->server);
	g_config->server      = string_copy(ui_text_input(h_server, tr("Cloud Server"), UI_ALIGN_LEFT, true, false));

	ui_handle_t *h_material_live = ui_handle(__ID__);
	h_material_live->b           = g_config->material_live;
	g_config->material_live      = ui_check(h_material_live, tr("Live Material Preview"), "");
	if (g_ui->is_hovered) {
		ui_tooltip(tr("Instantly update material preview on node change"));
	}

	ui_handle_t *h_brush_live = ui_handle(__ID__);
	h_brush_live->b           = g_config->brush_live;
	g_config->brush_live      = ui_check(h_brush_live, tr("Live Brush Preview"), "");
	if (g_ui->is_hovered) {
		ui_tooltip(tr("Draw live brush preview in viewport"));
	}
	if (h_brush_live->changed) {
		g_context->ddirty = 2;
	}

	ui_handle_t *h_brush_depth_reject = ui_handle(__ID__);
	h_brush_depth_reject->b           = g_config->brush_depth_reject;
	g_config->brush_depth_reject      = ui_check(h_brush_depth_reject, tr("Depth Reject"), "");
	if (h_brush_depth_reject->changed) {
		make_material_parse_paint_material(true);
	}

	ui_row2();

	ui_handle_t *h_brush_angle_reject = ui_handle(__ID__);
	h_brush_angle_reject->b           = g_config->brush_angle_reject;
	g_config->brush_angle_reject      = ui_check(h_brush_angle_reject, tr("Angle Reject"), "");
	if (h_brush_angle_reject->changed) {
		make_material_parse_paint_material(true);
	}

	if (!g_config->brush_angle_reject) {
		g_ui->enabled = false;
	}

	ui_handle_t *h_angle_dot          = ui_handle(__ID__);
	h_angle_dot->f                    = g_context->brush_angle_reject_dot;
	g_context->brush_angle_reject_dot = ui_slider(h_angle_dot, tr("Angle"), 0.0, 1.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
	if (h_angle_dot->changed) {
		make_material_parse_paint_material(true);
	}

	g_ui->enabled = true;

	ui_handle_t *h_alpha_discard  = ui_handle(__ID__);
	h_alpha_discard->f            = g_config->brush_alpha_discard;
	g_config->brush_alpha_discard = ui_slider(h_alpha_discard, tr("Alpha Discard"), 0.0, 1.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
	if (h_alpha_discard->changed) {
		make_material_parse_paint_material(true);
	}
}

//  ██████╗ █████╗ ███╗   ███╗███████╗██████╗  █████╗
// ██╔════╝██╔══██╗████╗ ████║██╔════╝██╔══██╗██╔══██╗
// ██║     ███████║██╔████╔██║█████╗  ██████╔╝███████║
// ██║     ██╔══██║██║╚██╔╝██║██╔══╝  ██╔══██╗██╔══██║
// ╚██████╗██║  ██║██║ ╚═╝ ██║███████╗██║  ██║██║  ██║
//  ╚═════╝╚═╝  ╚═╝╚═╝     ╚═╝╚══════╝╚═╝  ╚═╝╚═╝  ╚═╝

void box_preferences_camera_tab() {
	ui_handle_t *h_camera_pivot        = ui_handle(__ID__);
	h_camera_pivot->i                  = g_config->camera_pivot;
	string_array_t *camera_pivot_combo = any_array_create_from_raw(
	    (void *[]){
	        tr("Cursor"),
	        tr("Center"),
	    },
	    2);
	g_config->camera_pivot = ui_combo(h_camera_pivot, camera_pivot_combo, tr("Default Camera Pivot"), true, UI_ALIGN_LEFT, true);

	ui_handle_t *h_camera_controls        = ui_handle(__ID__);
	h_camera_controls->i                  = g_config->camera_controls;
	string_array_t *camera_controls_combo = any_array_create_from_raw(
	    (void *[]){
	        tr("Orbit"),
	        tr("Rotate"),
	        tr("Fly"),
	    },
	    3);
	g_config->camera_controls = ui_combo(h_camera_controls, camera_controls_combo, tr("Default Camera Controls"), true, UI_ALIGN_LEFT, true);

	ui_handle_t *h_fov   = ui_handle(__ID__);
	h_fov->f             = g_config->camera_fov;
	g_config->camera_fov = ui_slider(h_fov, tr("Default Camera FoV"), 0.3, 1.4, true, 100.0, true, UI_ALIGN_RIGHT, true);

	ui_handle_t *h_speed        = ui_handle(__ID__);
	h_speed->f                  = g_config->camera_zoom_speed;
	g_config->camera_zoom_speed = ui_slider(h_speed, tr("Camera Zoom Speed"), 0.1, 4.0, true, 100.0, true, UI_ALIGN_RIGHT, true);

	h_speed                         = ui_handle(__ID__);
	h_speed->f                      = g_config->camera_rotation_speed;
	g_config->camera_rotation_speed = ui_slider(h_speed, tr("Camera Rotation Speed"), 0.1, 4.0, true, 100.0, true, UI_ALIGN_RIGHT, true);

	h_speed                    = ui_handle(__ID__);
	h_speed->f                 = g_config->camera_pan_speed;
	g_config->camera_pan_speed = ui_slider(h_speed, tr("Camera Pan Speed"), 0.1, 4.0, true, 100.0, true, UI_ALIGN_RIGHT, true);

	ui_handle_t *h_zoom_direction        = ui_handle(__ID__);
	h_zoom_direction->i                  = g_config->zoom_direction;
	string_array_t *zoom_direction_combo = any_array_create_from_raw(
	    (void *[]){
	        tr("Vertical"),
	        tr("Vertical Inverted"),
	        tr("Horizontal"),
	        tr("Horizontal Inverted"),
	        tr("Vertical and Horizontal"),
	        tr("Vertical and Horizontal Inverted"),
	    },
	    6);
	g_config->zoom_direction = ui_combo(h_zoom_direction, zoom_direction_combo, tr("Direction to Zoom"), true, UI_ALIGN_LEFT, true);

	ui_handle_t *h_upside_down   = ui_handle(__ID__);
	h_upside_down->b             = g_config->camera_upside_down;
	g_config->camera_upside_down = ui_check(h_upside_down, tr("Allow Upside Down Camera"), "");
}

// ██████╗ ███████╗███╗   ██╗
// ██╔══██╗██╔════╝████╗  ██║
// ██████╔╝█████╗  ██╔██╗ ██║
// ██╔═══╝ ██╔══╝  ██║╚██╗██║
// ██║     ███████╗██║ ╚████║
// ╚═╝     ╚══════╝╚═╝  ╚═══╝

void box_preferences_pen_tab() {
	ui_text(tr("Pressure controls"), UI_ALIGN_LEFT, 0x00000000);

	ui_handle_t *h_pressure_sensitivity = ui_handle(__ID__);
	h_pressure_sensitivity->f           = g_config->pressure_sensitivity;
	g_config->pressure_sensitivity      = ui_slider(h_pressure_sensitivity, tr("Sensitivity"), 0.0, 10.0, true, 100.0, true, UI_ALIGN_RIGHT, true);

	ui_handle_t *h_pressure_radius = ui_handle(__ID__);
	h_pressure_radius->b           = g_config->pressure_radius;
	g_config->pressure_radius      = ui_check(h_pressure_radius, tr("Brush Radius"), "");

	ui_handle_t *h_pressure_hardness = ui_handle(__ID__);
	h_pressure_hardness->b           = g_config->pressure_hardness;
	g_config->pressure_hardness      = ui_check(h_pressure_hardness, tr("Brush Hardness"), "");

	ui_handle_t *h_pressure_opacity = ui_handle(__ID__);
	h_pressure_opacity->b           = g_config->pressure_opacity;
	g_config->pressure_opacity      = ui_check(h_pressure_opacity, tr("Brush Opacity"), "");

	ui_handle_t *h_pressure_angle = ui_handle(__ID__);
	h_pressure_angle->b           = g_config->pressure_angle;
	g_config->pressure_angle      = ui_check(h_pressure_angle, tr("Brush Angle"), "");

	ui_end_element();
	f32_array_t *row = f32_array_create_from_raw(
	    (f32[]){
	        1 / 4.0,
	    },
	    1);
	ui_row(row);
	if (ui_icon_button(tr("Help"), ICON_LINK, UI_ALIGN_CENTER)) {
		iron_load_url("https://armorpaint.org/manual#pen");
	}
}

void box_preferences_lut_picked(char *path) {
	g_config->lut_path = string_copy(path);
	if (iron_file_exists(g_config->lut_path)) {
		import_lut_run(path);
	}
	else {
		g_config->lut_path = "";
	}
	config_save();
	g_context->ddirty = 2;
}

// ██╗   ██╗██╗███████╗██╗    ██╗██████╗  ██████╗ ██████╗ ████████╗
// ██║   ██║██║██╔════╝██║    ██║██╔══██╗██╔═══██╗██╔══██╗╚══██╔══╝
// ██║   ██║██║█████╗  ██║ █╗ ██║██████╔╝██║   ██║██████╔╝   ██║
// ╚██╗ ██╔╝██║██╔══╝  ██║███╗██║██╔═══╝ ██║   ██║██╔══██╗   ██║
//  ╚████╔╝ ██║███████╗╚███╔███╔╝██║     ╚██████╔╝██║  ██║   ██║
//   ╚═══╝  ╚═╝╚══════╝ ╚══╝╚══╝ ╚═╝      ╚═════╝ ╚═╝  ╚═╝   ╚═╝

void box_preferences_viewport_tab() {
	ui_handle_t *h_mode        = ui_handle(__ID__);
	h_mode->i                  = g_config->viewport_mode;
	string_array_t *mode_combo = base_get_viewport_modes();
	ui_combo(h_mode, mode_combo, tr("Default Mode"), true, UI_ALIGN_LEFT, true);
	if (h_mode->changed) {
		g_config->viewport_mode = h_mode->i;
	}

	ui_handle_t *h_pathtrace_mode        = ui_handle(__ID__);
	h_pathtrace_mode->i                  = g_config->pathtrace_mode;
	string_array_t *pathtrace_mode_combo = any_array_create_from_raw(
	    (void *[]){
	        tr("Fast"),
	        tr("Quality"),
	    },
	    2);
	g_config->pathtrace_mode = ui_combo(h_pathtrace_mode, pathtrace_mode_combo, tr("Path Tracer"), true, UI_ALIGN_LEFT, true);
	if (h_pathtrace_mode->changed) {
		render_path_raytrace_ready       = false;
		render_path_raytrace_init_shader = true;
		g_context->ddirty                = 2;
	}

	ui_handle_t *h_render_mode        = ui_handle(__ID__);
	h_render_mode->i                  = g_config->render_mode;
	string_array_t *render_mode_combo = any_array_create_from_raw(
	    (void *[]){
	        tr("Desktop"),
	        tr("Mobile"),
	    },
	    2);
	g_config->render_mode = ui_combo(h_render_mode, render_mode_combo, tr("Renderer"), true, UI_ALIGN_LEFT, true);
	if (h_render_mode->changed) {
		context_set_render_path();
	}

	ui_handle_t *h_supersample        = ui_handle(__ID__);
	h_supersample->i                  = config_get_super_sample_quality(g_config->rp_supersample);
	string_array_t *supersample_combo = any_array_create_from_raw(
	    (void *[]){
	        "0.25x",
	        "0.5x",
	        "1.0x",
	        "1.5x",
	        "2.0x",
	        "4.0x",
	    },
	    6);
	ui_combo(h_supersample, supersample_combo, tr("Super Sample"), true, UI_ALIGN_LEFT, true);
	if (h_supersample->changed) {
		g_config->rp_supersample = config_get_super_sample_size(h_supersample->i);
		config_apply();
	}

	if (g_config->render_mode == RENDER_MODE_DEFERRED) {
		ui_handle_t *h_ssao = ui_handle(__ID__);
		h_ssao->b           = g_config->rp_ssao;
		g_config->rp_ssao   = ui_check(h_ssao, tr("SSAO"), "");
		if (h_ssao->changed) {
			config_apply();
		}

		ui_handle_t *h_bloom = ui_handle(__ID__);
		h_bloom->f           = g_config->rp_bloom;
		g_config->rp_bloom   = ui_slider(h_bloom, tr("Bloom"), 0.0, 1.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
		if (h_bloom->changed) {
			g_context->ddirty = 2;
		}
	}

	ui_handle_t *h_contrast = ui_handle(__ID__);
	h_contrast->f           = g_config->rp_contrast;
	g_config->rp_contrast   = ui_slider(h_contrast, tr("Contrast"), 0.0, 2.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
	if (h_contrast->changed) {
		g_context->ddirty = 2;
	}

	ui_handle_t *h_gamma = ui_handle(__ID__);
	h_gamma->f           = g_config->rp_gamma;
	g_config->rp_gamma   = ui_slider(h_gamma, tr("Gamma"), 0.0, 2.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
	if (h_gamma->changed) {
		g_context->ddirty = 2;
	}

	ui_handle_t *h_vignette = ui_handle(__ID__);
	h_vignette->f           = g_config->rp_vignette;
	g_config->rp_vignette   = ui_slider(h_vignette, tr("Vignette"), 0.0, 1.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
	if (h_vignette->changed) {
		g_context->ddirty = 2;
	}

	ui_handle_t *h_noise_grain = ui_handle(__ID__);
	h_noise_grain->f           = g_config->rp_grain;
	g_config->rp_grain         = ui_slider(h_noise_grain, tr("Noise Grain"), 0.0, 1.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
	if (h_noise_grain->changed) {
		g_context->ddirty = 2;
	}

	camera_object_t *cam     = scene_camera;
	camera_data_t   *cam_raw = cam->data;
	ui_handle_t     *h_near  = ui_handle(__ID__);
	ui_handle_t     *h_far   = ui_handle(__ID__);
	h_near->f                = math_floor(cam_raw->near_plane * 1000) / 1000.0;
	h_far->f                 = math_floor(cam_raw->far_plane * 100) / 100.0;
	cam_raw->near_plane      = ui_slider(h_near, tr("Clip Start"), 0.001, 1.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
	cam_raw->far_plane       = ui_slider(h_far, tr("Clip End"), 50.0, 100.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
	if (h_near->changed || h_far->changed) {
		camera_object_build_proj(cam, -1.0);
	}

	ui_text(tr(".cube LUT"), UI_ALIGN_LEFT, 0x00000000);
	f32_array_t *lut_ar = f32_array_create_from_raw(
	    (f32[]){
	        7 / 8.0,
	        1 / 8.0,
	    },
	    2);
	ui_row(lut_ar);
	ui_handle_t *h_lut = ui_handle(__ID__);
	h_lut->text        = string_copy(g_config->lut_path);
	g_config->lut_path = string_copy(ui_text_input(h_lut, "", UI_ALIGN_LEFT, true, false));
	if (h_lut->changed) {
		if (string_equals(g_config->lut_path, "")) {
			import_lut_free();
			config_save();
			g_context->ddirty = 2;
		}
		else {
			box_preferences_lut_picked(g_config->lut_path);
		}
	}
	if (ui_icon_button("", ICON_FOLDER_OPEN, UI_ALIGN_CENTER)) {
		ui_files_show("cube", false, false, &box_preferences_lut_picked);
	}

	ui_handle_t *filter_handle = ui_handle(__ID__);
	filter_handle->b           = g_config->texture_filter;
	g_config->texture_filter   = ui_check(filter_handle, string(" %s", tr("Filter Textures")), "");
	if (filter_handle->changed) {
		gpu_use_linear_sampling(g_config->texture_filter);
	}
}

// ██╗  ██╗███████╗██╗   ██╗███╗   ███╗ █████╗ ██████╗
// ██║ ██╔╝██╔════╝╚██╗ ██╔╝████╗ ████║██╔══██╗██╔══██╗
// █████╔╝ █████╗   ╚████╔╝ ██╔████╔██║███████║██████╔╝
// ██╔═██╗ ██╔══╝    ╚██╔╝  ██║╚██╔╝██║██╔══██║██╔═══╝
// ██║  ██╗███████╗   ██║   ██║ ╚═╝ ██║██║  ██║██║
// ╚═╝  ╚═╝╚══════╝   ╚═╝   ╚═╝     ╚═╝╚═╝  ╚═╝╚═╝

void box_preferences_keymap_tab_export(char *dest) {
	if (!ends_with(ui_files_filename, ".json")) {
		gc_unroot(ui_files_filename);
		ui_files_filename = string("%s.json", ui_files_filename);
		gc_root(ui_files_filename);
	}
	char *path = string("%s%skeymap_presets%s%s", path_data(), PATH_SEP, PATH_SEP, g_config->keymap);
	file_copy(path, string("%s%s%s", dest, PATH_SEP, ui_files_filename));
}

void box_preferences_keymap_tab_import(char *path) {
	import_keymap_run(path);
}

void box_preferences_keymap_tab_new_box() {
	ui_row2();
	ui_handle_t *h = ui_handle(__ID__);
	if (h->init) {
		h->text = "new_keymap";
	}
	char *keymap_name = ui_text_input(h, tr("Name"), UI_ALIGN_LEFT, true, false);
	if (ui_icon_button(tr("OK"), ICON_CHECK, UI_ALIGN_CENTER) || g_ui->is_return_down) {
		char *template = keymap_to_json(keymap_get_default());
		if (!ends_with(keymap_name, ".json")) {
			keymap_name = string("%s.json", keymap_name);
		}
		char *path = string("%s%skeymap_presets%s%s", path_data(), PATH_SEP, PATH_SEP, keymap_name);
		iron_file_save_bytes(path, sys_string_to_buffer(template), 0);
		box_preferences_fetch_keymaps(); // Refresh file list
		g_config->keymap            = string_copy(keymap_name);
		box_preferences_h_preset->i = box_preferences_get_preset_index();
		ui_box_hide();
		box_preferences_htab->i = 5; // Keymap
		box_preferences_show();
	}
}

// ███╗   ██╗███████╗██╗   ██╗██████╗  █████╗ ██╗
// ████╗  ██║██╔════╝██║   ██║██╔══██╗██╔══██╗██║
// ██╔██╗ ██║█████╗  ██║   ██║██████╔╝███████║██║
// ██║╚██╗██║██╔══╝  ██║   ██║██╔══██╗██╔══██║██║
// ██║ ╚████║███████╗╚██████╔╝██║  ██║██║  ██║███████╗
// ╚═╝  ╚═══╝╚══════╝ ╚═════╝ ╚═╝  ╚═╝╚═╝  ╚═╝╚══════╝

bool box_preferences_model_exists(char *file_name) {
	return iron_file_exists(string("%s%s%s", neural_node_dir(), PATH_SEP, file_name));
}

char *box_preferences_file_name_from_url(char *url) {
	return substring(url, string_last_index_of(url, "/") + 1, string_length(url));
}

char *box_preferneces_model_url_from_name(char *name) {
	if (neural_node_models == NULL) {
		neural_node_models_init();
	}

	for (i32 i = 0; i < neural_node_models->length; ++i) {
		if (string_equals(neural_node_models->buffer[i]->name, name)) {
			return neural_node_models->buffer[i]->urls->buffer[0];
		}
	}
	return NULL;
}

void box_preferences_model_panel(neural_node_model_t *m) {
	if (ui_panel(ui_handle(m->name), m->name, false, true, false)) {
		if (ui_text(string("%s: %s (%s)", tr("source"), m->web, m->license), UI_ALIGN_LEFT, 0x00000000) == UI_STATE_RELEASED) {
			iron_load_url(m->web);
		}
		ui_text(string("%s: %s", tr("gpu memory"), m->memory), UI_ALIGN_LEFT, 0x00000000);
		ui_text(string("%s: %s", tr("nodes"), m->nodes), UI_ALIGN_LEFT, 0x00000000);

		char *url       = m->urls->buffer[0];
		char *file_name = box_preferences_file_name_from_url(url);
		bool  found     = box_preferences_model_exists(file_name);

		if (neural_node_downloading > 0) {
			g_ui->enabled = false;

			u64   u                       = iron_net_bytes_downloaded;
			i32   i                       = (u / 1000000000.0) * 100;
			f32   f                       = i / 100.0;
			char *downloaded              = string("%sGB", f32_to_string(f));
			ui_box_hwnd->redraws          = 2;
			box_preferences_htab->redraws = 2;
			iron_delay_idle_sleep();

			i32 _BUTTON_COL     = g_theme->BUTTON_COL;
			g_theme->BUTTON_COL = g_theme->HIGHLIGHT_COL;

			ui_handle_t *h = ui_handle(__ID__);
			h->f           = f / (float)parse_float(m->size);
			ui_slider(h, string("%s / %s", downloaded, m->size), 0.0, 1.0, true, 100, false, UI_ALIGN_CENTER, true);

			g_theme->BUTTON_COL = _BUTTON_COL;

			g_ui->enabled = true;
		}
		else if (!found && ui_icon_button(string("%s (%s)", tr("Download"), m->size), ICON_ARROW_DOWN, UI_ALIGN_CENTER)) {
			neural_node_download_models(m->urls);
			console_info(tr("Downloading"));
		}
		else if (found && ui_icon_button(string("%s (%s)", tr("Remove"), m->size), ICON_DELETE, UI_ALIGN_CENTER)) {
			for (i32 i = 0; i < m->urls->length; ++i) {
				char *url       = m->urls->buffer[i];
				char *file_name = box_preferences_file_name_from_url(url);
				iron_delete_file(string("%s%s%s", neural_node_dir(), PATH_SEP, file_name));
			}
		}
	}
}

void box_preferences_neural_tab() {
	ui_text(tr("All processing is done locally on device"), UI_ALIGN_LEFT, 0x00000000);

	ui_handle_t *h_neural_res        = ui_handle(__ID__);
	h_neural_res->i                  = g_config->neural_res == 2048 ? 2 : (g_config->neural_res == 1024 ? 1 : 0);
	string_array_t *neural_res_combo = any_array_create_from_raw(
	    (void *[]){
	        "512",
	        "1024",
	        "2048",
	    },
	    3);
	i32 neural_res_sel   = ui_combo(h_neural_res, neural_res_combo, tr("Resolution"), true, UI_ALIGN_LEFT, true);
	g_config->neural_res = neural_res_sel == 2 ? 2048 : (neural_res_sel == 1 ? 1024 : 512);

	ui_text(tr("Models"), UI_ALIGN_LEFT, 0x00000000);

	if (neural_node_models == NULL) {
		neural_node_models_init();
	}

	for (i32 i = 0; i < neural_node_models->length; ++i) {
		box_preferences_model_panel(neural_node_models->buffer[i]);
	}

	f32_array_t *row = f32_array_create_from_raw(
	    (f32[]){
	        1 / 4.0,
	    },
	    1);
	ui_row(row);
	if (ui_icon_button(tr("Models Directory..."), ICON_FOLDER, UI_ALIGN_CENTER)) {
		if (string_equals(file_read_directory(neural_node_dir())->buffer[0], "")) {
			iron_create_directory(neural_node_dir());
		}
		file_start(neural_node_dir());
	}
}

// ██╗  ██╗███████╗██╗   ██╗███╗   ███╗ █████╗ ██████╗
// ██║ ██╔╝██╔════╝╚██╗ ██╔╝████╗ ████║██╔══██╗██╔══██╗
// █████╔╝ █████╗   ╚████╔╝ ██╔████╔██║███████║██████╔╝
// ██╔═██╗ ██╔══╝    ╚██╔╝  ██║╚██╔╝██║██╔══██║██╔═══╝
// ██║  ██╗███████╗   ██║   ██║ ╚═╝ ██║██║  ██║██║
// ╚═╝  ╚═╝╚══════╝   ╚═╝   ╚═╝     ╚═╝╚═╝  ╚═╝╚═╝

void box_preferences_keymap_tab() {
	if (box_preferences_files_keymap == NULL) {
		box_preferences_fetch_keymaps();
	}

	ui_begin_sticky();
	ui_row4();

	gc_unroot(box_preferences_h_preset);
	box_preferences_h_preset = ui_handle(__ID__);
	gc_root(box_preferences_h_preset);
	box_preferences_h_preset->i = box_preferences_get_preset_index();
	ui_combo(box_preferences_h_preset, box_preferences_files_keymap, tr("Preset"), false, UI_ALIGN_LEFT, true);
	if (box_preferences_h_preset->changed) {
		g_config->keymap = string("%s.json", box_preferences_files_keymap->buffer[box_preferences_h_preset->i]);
		config_apply();
		keymap_load();
	}

	if (ui_icon_button(tr("New"), ICON_PLUS, UI_ALIGN_CENTER)) {
		ui_box_show_custom(&box_preferences_keymap_tab_new_box, 400, 200, NULL, true, tr("New Keymap"));
	}

	if (ui_icon_button(tr("Import"), ICON_IMPORT, UI_ALIGN_CENTER)) {
		ui_files_show("json", false, false, &box_preferences_keymap_tab_import);
	}

	if (ui_icon_button(tr("Export"), ICON_EXPORT, UI_ALIGN_CENTER)) {
		ui_files_show("json", true, false, &box_preferences_keymap_tab_export);
	}

	ui_handle_t *box_preferences_keymap_hsearch = ui_handle(__ID__);
	if (!string_equals(box_preferences_keymap_hsearch->text, "")) {
		ui_row(f32_array_create_from_raw((f32[]){0.85, 0.15}, 2));
	}
	box_preferences_keymap_hsearch->text = string_copy(ui_text_input(box_preferences_keymap_hsearch, tr("Search"), UI_ALIGN_LEFT, true, true));
	if (!string_equals(box_preferences_keymap_hsearch->text, "") && (ui_button(tr("X"), UI_ALIGN_CENTER, ""))) {
		box_preferences_keymap_hsearch->text = "";
	}

	ui_end_sticky();

	ui_separator(8, false);

	i32 index            = 0;
	g_ui->changed        = false;
	string_array_t *keys = map_keys(g_keymap);
	array_sort(keys, NULL);
	char *search = to_lower_case(box_preferences_keymap_hsearch->text);
	for (i32 i = 0; i < keys->length; ++i) {
		char *key   = keys->buffer[i];
		char *value = any_map_get(g_keymap, key);
		if (!string_equals(search, "") && string_index_of(to_lower_case(key), search) == -1 && string_index_of(to_lower_case(value), search) == -1) {
			continue;
		}
		ui_handle_t *h = ui_nest(ui_handle(__ID__), index++);
		h->text        = string_copy(value);
		char *text     = ui_text_input(h, key, UI_ALIGN_LEFT, true, false);
		any_map_set(g_keymap, key, text);
	}
	if (g_ui->changed) {
		config_apply();
		keymap_save();
	}
}

// ██████╗ ██╗     ██╗   ██╗ ██████╗ ██╗███╗   ██╗███████╗
// ██╔══██╗██║     ██║   ██║██╔════╝ ██║████╗  ██║██╔════╝
// ██████╔╝██║     ██║   ██║██║  ███╗██║██╔██╗ ██║███████╗
// ██╔═══╝ ██║     ██║   ██║██║   ██║██║██║╚██╗██║╚════██║
// ██║     ███████╗╚██████╔╝╚██████╔╝██║██║ ╚████║███████║
// ╚═╝     ╚══════╝ ╚═════╝  ╚═════╝ ╚═╝╚═╝  ╚═══╝╚══════╝

void box_preferences_plugins_tab_plugin_menu_export(char *dest) {
	if (!ends_with(ui_files_filename, ".c")) {
		gc_unroot(ui_files_filename);
		ui_files_filename = string("%s.c", ui_files_filename);
		gc_root(ui_files_filename);
	}
	char *path = string("%s%splugins%s%s", path_data(), PATH_SEP, PATH_SEP, _box_preferences_f);
	file_copy(path, string("%s%s%s", dest, PATH_SEP, ui_files_filename));
}

void box_preferences_plugins_tab_plugin_menu() {
	char *path = string("%s%splugins%s%s", path_data(), PATH_SEP, PATH_SEP, _box_preferences_f);
	if (ui_menu_button(tr("Edit in Text Editor"), "", ICON_NONE)) {
		file_start(path);
	}
	if (ui_menu_button(tr("Edit in Script Tab"), "", ICON_NONE)) {
		buffer_t *blob = data_get_blob(string("plugins/%s", _box_preferences_f));
		tab_scripts_set(sys_buffer_to_string(blob));
		data_delete_blob(string("plugins/%s", _box_preferences_f));
		console_info(tr("Script opened"));
		ui_base_htabs->buffer[TAB_AREA_SIDEBAR0]->i       = 2; // Scripts tab
		ui_base_hwnds->buffer[TAB_AREA_SIDEBAR0]->redraws = 2;
		g_config->layout_tabs->buffer[TAB_AREA_SIDEBAR0]  = 2;
	}
	if (ui_menu_button(tr("Export"), "", ICON_EXPORT)) {
		ui_files_show("c", true, false, &box_preferences_plugins_tab_plugin_menu_export);
	}
	if (ui_menu_button(tr("Delete"), "", ICON_DELETE)) {
		if (string_array_index_of(g_config->plugins, _box_preferences_f) >= 0) {
			string_array_remove(g_config->plugins, _box_preferences_f);
			plugin_stop(_box_preferences_f);
		}
		string_array_remove(box_preferences_files_plugin, _box_preferences_f);
		iron_delete_file(path);
	}
}

void box_preferences_plugins_tab_import(char *path) {
	import_plugin_run(path);
}

void box_preferences_plugins_tab_new_box() {
	ui_row2();
	ui_handle_t *h = ui_handle(__ID__);
	if (h->init) {
		h->text = "new_plugin";
	}
	char *plugin_name = ui_text_input(h, tr("Name"), UI_ALIGN_LEFT, true, false);
	if (ui_icon_button(tr("OK"), ICON_CHECK, UI_ALIGN_CENTER) || g_ui->is_return_down) {
		char *template = "\
ui_handle_t *h1;\n\
void on_ui() {\n\
	if (ui_panel(h1, \"New Plugin\", false, false, false)) {\n\
		if (ui_button(\"Button\", UI_ALIGN_CENTER, "
		                 ")) {\n\
			console_info(\"Hello\");\n\
		}\n\
	}\n\
}\n\
void main() {\n\
	void *plugin = plugin_create();\n\
	plugin_notify_on_ui(plugin, on_ui);\n\
	h1 = ui_handle_create();\n\
}\n\
";
		if (!ends_with(plugin_name, ".c")) {
			plugin_name = string("%s.c", plugin_name);
		}
		char *path = string("%s%splugins%s%s", path_data(), PATH_SEP, PATH_SEP, plugin_name);
		iron_file_save_bytes(path, sys_string_to_buffer(template), 0);
		gc_unroot(box_preferences_files_plugin);
		box_preferences_files_plugin = NULL; // Refresh file list
		ui_box_hide();
		box_preferences_htab->i = PREFERENCES_TAB_PLUGINS;
		box_preferences_show();
	}
}

void box_preferences_plugins_tab() {
	ui_begin_sticky();
	f32_array_t *row = f32_array_create_from_raw(
	    (f32[]){
	        1 / 4.0,
	        1 / 4.0,
	    },
	    2);
	ui_row(row);
	if (ui_icon_button(tr("New"), ICON_PLUS, UI_ALIGN_CENTER)) {
		ui_box_show_custom(&box_preferences_plugins_tab_new_box, 400, 200, NULL, true, tr("New Plugin"));
	}
	if (ui_icon_button(tr("Import"), ICON_IMPORT, UI_ALIGN_CENTER)) {
		ui_files_show("c,zip", false, false, &box_preferences_plugins_tab_import);
	}

	ui_handle_t *box_preferences_plugins_hsearch = ui_handle(__ID__);
	if (!string_equals(box_preferences_plugins_hsearch->text, "")) {
		ui_row(f32_array_create_from_raw((f32[]){0.85, 0.15}, 2));
	}
	box_preferences_plugins_hsearch->text = string_copy(ui_text_input(box_preferences_plugins_hsearch, tr("Search"), UI_ALIGN_LEFT, true, true));
	if (!string_equals(box_preferences_plugins_hsearch->text, "") && (ui_button(tr("X"), UI_ALIGN_CENTER, "") || g_ui->is_escape_down)) {
		box_preferences_plugins_hsearch->text = "";
	}

	ui_end_sticky();

	if (box_preferences_files_plugin == NULL) {
		box_preferences_fetch_plugins();
	}

	char *plugin_search = to_lower_case(box_preferences_plugins_hsearch->text);
	if (g_config->plugins == NULL) {
		g_config->plugins = any_array_create_from_raw((void *[]){}, 0);
	}
	ui_handle_t *h = ui_handle(__ID__);
	for (i32 i = 0; i < box_preferences_files_plugin->length; ++i) {
		char *f    = box_preferences_files_plugin->buffer[i];
		bool  is_c = ends_with(f, ".c");
		if (!is_c) {
			continue;
		}
		char *tag = string_split(f, ".")->buffer[0];
		if (!string_equals(plugin_search, "") && string_index_of(to_lower_case(tag), plugin_search) == -1) {
			continue;
		}
		bool enabled = string_array_index_of(g_config->plugins, f) >= 0;
		h->b         = enabled;
		ui_check(h, tag, "");
		if (h->changed && h->b != enabled) {
			h->b ? config_enable_plugin(f) : config_disable_plugin(f);
			base_redraw_ui();
		}
		if (g_ui->is_hovered && g_ui->input_released_r) {
			gc_unroot(_box_preferences_f);
			_box_preferences_f = string_copy(f);
			gc_root(_box_preferences_f);
			ui_menu_draw(&box_preferences_plugins_tab_plugin_menu, -1, -1);
		}
	}
}

void box_preferences_show_on_hide() {
	config_save();
}

void box_preferences_show_box() {
	if (ui_tab(box_preferences_htab, tr("Interface"), true, -1, false)) {
		box_preferences_interface_tab();
	}
	if (ui_tab(box_preferences_htab, tr("Theme"), true, -1, false)) {
		box_preferences_theme_tab();
	}
	if (ui_tab(box_preferences_htab, tr("Usage"), true, -1, false)) {
		box_preferences_usage_tab();
	}
	if (ui_tab(box_preferences_htab, tr("Camera"), true, -1, false)) {
		box_preferences_camera_tab();
	}

#ifdef IRON_IOS
	char *pen_name = tr("Pencil");
#else
	char *pen_name = tr("Pen");
#endif
	if (ui_tab(box_preferences_htab, pen_name, true, -1, false)) {
		box_preferences_pen_tab();
	}

	if (ui_tab(box_preferences_htab, tr("Viewport"), true, -1, false)) {
		box_preferences_viewport_tab();
	}
	if (ui_tab(box_preferences_htab, tr("Keymap"), true, -1, false)) {
		box_preferences_keymap_tab();
	}
#if defined(IRON_WINDOWS) || defined(IRON_LINUX) || defined(IRON_MACOS)
	if (ui_tab(box_preferences_htab, tr("Neural"), true, -1, false)) {
		box_preferences_neural_tab();
	}
#endif
	if (ui_tab(box_preferences_htab, tr("Plugins"), true, -1, false)) {
		box_preferences_plugins_tab();
	}
}

void box_preferences_show() {
	ui_box_show_custom(&box_preferences_show_box, 720, 520, &box_preferences_show_on_hide, true, tr("Preferences"));
}

void box_preferences_fetch_themes() {
	gc_unroot(box_preferences_themes);
	box_preferences_themes = file_read_directory(string("%s%sthemes", path_data(), PATH_SEP));
	gc_root(box_preferences_themes);
	for (i32 i = 0; i < box_preferences_themes->length; ++i) {
		char *s                           = box_preferences_themes->buffer[i];
		box_preferences_themes->buffer[i] = substring(box_preferences_themes->buffer[i], 0, string_length(s) - 5); // Strip .json
	}
	array_insert(box_preferences_themes, 0, "default");
}

void box_preferences_fetch_keymaps() {
	gc_unroot(box_preferences_files_keymap);
	box_preferences_files_keymap = file_read_directory(string("%s%skeymap_presets", path_data(), PATH_SEP));
	gc_root(box_preferences_files_keymap);
	for (i32 i = 0; i < box_preferences_files_keymap->length; ++i) {
		char *s                                 = box_preferences_files_keymap->buffer[i];
		box_preferences_files_keymap->buffer[i] = substring(box_preferences_files_keymap->buffer[i], 0, string_length(s) - 5); // Strip .json
	}
	array_insert(box_preferences_files_keymap, 0, "default");
}

void box_preferences_fetch_plugins() {
	gc_unroot(box_preferences_files_plugin);
	box_preferences_files_plugin = file_read_directory(string("%s%splugins", path_data(), PATH_SEP));
	gc_root(box_preferences_files_plugin);
}

i32 box_preferences_get_theme_index() {
	return string_array_index_of(box_preferences_themes, substring(g_config->theme, 0, string_length(g_config->theme) - 5)); // Strip .json
}

i32 box_preferences_get_preset_index() {
	return string_array_index_of(box_preferences_files_keymap, substring(g_config->keymap, 0, string_length(g_config->keymap) - 5)); // Strip .json
}
