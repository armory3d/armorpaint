
#include "../global.h"

string_array_t  *box_preferences_files_keymap = NULL;
string_array_t  *box_preferences_locales      = NULL;
string_array_t  *box_preferences_themes       = NULL;
char            *_box_preferences_f;
ui_color_state_t _box_preferences_color_state;
i32              _box_preferences_i;
f32              box_preferences_scale          = 0.0;
char            *box_preferences_theme_search   = "";
char            *box_preferences_keymap_search  = "";
char            *box_preferences_plugins_search = "";
char            *box_preferences_new_theme      = "new_theme";
char            *box_preferences_new_keymap     = "new_keymap";
char            *box_preferences_new_plugin     = "new_plugin";

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
	if (g_config->plugins != NULL) {
		for (i32 i = 0; i < g_config->plugins->length; ++i) {
			char *f = g_config->plugins->buffer[i];
			plugin_stop(f);
		}
	}
	config_restore();
	box_preferences_set_scale();
	box_preferences_files_plugin = NULL;
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
		box_preferences_locales = translator_get_supported_locales();
	}

	i32 locale = string_array_index_of(box_preferences_locales, g_config->locale);
	ui_set_next_id((ui_id_t)&g_config->locale);
	ui_combo(&locale, box_preferences_locales, tr("Language"), true, UI_ALIGN_LEFT, true);
	if (ui_item_changed()) {
		char *locale_code = box_preferences_locales->buffer[locale];
		g_config->locale  = string_copy(locale_code);
		translator_load_translations(locale_code);
		base_redraw_ui();
	}

	// Scale is applied once the slider is released
	if (!g_context->hscale_was_changed) {
		box_preferences_scale = g_config->window_scale;
	}
	ui_slider(&box_preferences_scale, tr("UI Scale"), 1.0, 4.0, true, 10, true, UI_ALIGN_RIGHT, true);
	bool scale_changed = ui_item_changed();
	if (g_context->hscale_was_changed && !g_ui->input_down) {
		g_context->hscale_was_changed = false;
		if (box_preferences_scale < 1.0) {
			box_preferences_scale = 1.0;
		}
		if (box_preferences_scale > 4.0) {
			box_preferences_scale = 4.0;
		}
		g_config->window_scale = box_preferences_scale;
		box_preferences_set_scale();
	}
	if (scale_changed) {
		g_context->hscale_was_changed = true;
	}

	ui_check(&g_config->node_previews, tr("Node Previews"), "");
	if (ui_item_changed()) {
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

	ui_check(&g_config->wrap_mouse, tr("Wrap Mouse"), "");
	if (g_ui->is_hovered) {
		ui_tooltip(tr("Wrap mouse around view boundaries during camera control"));
	}

	g_ui->changed = false;
	ui_check(&g_config->show_asset_names, tr("Show Asset Names"), "");
	if (g_ui->changed) {
		base_redraw_ui();
	}

	g_ui->changed = false;
	ui_check(&g_config->touch_ui, tr("Touch UI"), "");
	if (g_ui->changed) {
		ui_touch_control = g_config->touch_ui;
		config_load_theme(g_config->theme, true);
		box_preferences_set_scale();
		base_redraw_ui();
		g_context->hscale_was_changed = true;
	}

	ui_check(&g_config->splash_screen, tr("Splash Screen"), "");

	ui_check(&g_config->grid_snap, tr("Grid Snap"), "");
	ui_nodes_grid_snap = g_config->grid_snap;

	ui_check(&g_config->experimental, tr("Experimental Features"), "");

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
	g_ui->changed  = false;
	u32 *u32_theme = g_theme;
	ui_color_wheel(u32_theme + _box_preferences_i, &_box_preferences_color_state, false, -1, 11 * g_theme->ELEMENT_H * UI_SCALE(), true, NULL, NULL);
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
	char *theme_name = ui_text_input(&box_preferences_new_theme, tr("Name"), UI_ALIGN_LEFT, true, false);
	if (ui_icon_button(tr("OK"), ICON_CHECK, UI_ALIGN_CENTER) || g_ui->is_return_down) {
		char *template = box_preferences_theme_to_json(g_theme);
		if (!ends_with(theme_name, ".json")) {
			theme_name = string("%s.json", theme_name);
		}
		char *path = string("%s%sthemes%s%s", path_data(), PATH_SEP, PATH_SEP, theme_name);
		iron_file_save_bytes(path, sys_string_to_buffer(template), 0);
		box_preferences_fetch_themes(); // Refresh file list
		g_config->theme = string_copy(theme_name);
		ui_box_hide();
		box_preferences_tab = 1; // Themes
		box_preferences_show();
	}
}

void box_preferences_theme_tab() {
	if (box_preferences_themes == NULL) {
		box_preferences_fetch_themes();
	}

	ui_begin_sticky();
	ui_row4();

	i32 theme = box_preferences_get_theme_index();
	ui_set_next_id((ui_id_t)&g_config->theme);
	ui_combo(&theme, box_preferences_themes, tr("Theme"), false, UI_ALIGN_LEFT, true);
	if (ui_item_changed()) {
		g_config->theme = string("%s.json", box_preferences_themes->buffer[theme]);
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

	if (!string_equals(box_preferences_theme_search, "")) {
		ui_row(f32_array_create_from_raw_tmp((f32[]){0.85, 0.15}, 2));
	}
	ui_text_input(&box_preferences_theme_search, tr("Search"), UI_ALIGN_LEFT, true, true);
	if (!string_equals(box_preferences_theme_search, "") && (ui_button(tr("X"), UI_ALIGN_CENTER, "") || g_ui->is_escape_down)) {
		box_preferences_theme_search = "";
	}

	ui_end_sticky();

	// Theme fields
	char *theme_search  = to_lower_case(box_preferences_theme_search);
	u32  *u32_theme     = g_theme;
	g_ui->input_enabled = !ui_menu_show;
	for (i32 i = 0; i < ui_theme_keys_count; ++i) {
		char *key = ui_theme_keys[i];
		if (!string_equals(theme_search, "") && string_index_of(to_lower_case(key), theme_search) == -1) {
			continue;
		}
		u32 *field  = u32_theme + i;
		u32  val    = *field;
		bool is_hex = ends_with(key, "_COL");

		if (is_hex) {
			f32_array_t *row = f32_array_create_from_raw_tmp(
			    (f32[]){
			        1 / 8.0,
			        7 / 8.0,
			    },
			    2);
			ui_row(row);
			ui_text("", 0, val);
			if (g_ui->is_hovered && g_ui->input_released) {
				_box_preferences_i = i;
				ui_menu_draw(&box_preferences_theme_tab_theme_field_menu, -1, -1);
			}

			if (string_equals(key, "VIEWPORT_COL") && ui_base_viewport_col != val) {
				ui_base_set_viewport_col(val);
			}
		}

		g_ui->changed = false;

		if (string_equals(key, "FILL_BUTTON_BG") || string_equals(key, "FULL_TABS") || string_equals(key, "SHADOWS")) {
			bool b = val > 0;
			ui_check(&b, key, "");
			*field = b;
		}
		else if (string_equals(key, "LINK_STYLE")) {
			string_array_t *styles = any_array_create_from_raw(
			    (void *[]){
			        tr("Straight"),
			        tr("Curved"),
			    },
			    2);
			i32 pos = val;
			ui_set_next_id((ui_id_t)field);
			ui_combo(&pos, styles, key, true, UI_ALIGN_LEFT, true);
			*field = pos;
		}
		else {
			char *text = is_hex ? i32_to_string_hex(val) : i32_to_string(val);
			ui_set_next_id((ui_id_t)field);
			ui_text_input(&text, key, UI_ALIGN_LEFT, true, false);
			if (is_hex) {
				*field = parse_int_hex(text);
			}
			else {
				*field = parse_int(text);
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
	ui_slider_int(&g_config->undo_steps, tr("Undo Steps"), 1, 64, false, true, UI_ALIGN_RIGHT, true);
	if (g_config->undo_steps < 1) {
		g_config->undo_steps = 1;
	}
	if (ui_item_changed()) {
		gpu_texture_t *current = _draw_current;
		draw_end();

		if (history_undo_layers != NULL) {
			while (history_undo_layers->length < g_config->undo_steps) {
				i32           len = history_undo_layers->length;
				slot_layer_t *l   = slot_layer_create_undo(string("_undo%s", i32_to_string(len)));
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

	ui_slider_int(&g_config->dilate_radius, tr("Dilate Radius"), 0.0, 16.0, true, true, UI_ALIGN_RIGHT, true);
	if (g_ui->is_hovered) {
		ui_tooltip(tr("Dilate painted textures to prevent seams"));
	}

	string_array_t *res_combo = any_array_create_from_raw_tmp(
	    (void *[]){
	        "2048",
	        "4096",
	        "8192",
	        "16384",
	    },
	    4);
	ui_combo(&g_config->layer_res, res_combo, tr("Default Layer Resolution"), true, UI_ALIGN_LEFT, true);

	ui_text_input(&g_config->server, tr("Cloud Server"), UI_ALIGN_LEFT, true, false);

	ui_check(&g_config->material_live, tr("Live Material Preview"), "");
	if (g_ui->is_hovered) {
		ui_tooltip(tr("Instantly update material preview on node change"));
	}

	ui_check(&g_config->brush_live, tr("Live Brush Preview"), "");
	if (ui_item_changed()) {
		g_context->ddirty = 2;
	}
	if (g_ui->is_hovered) {
		ui_tooltip(tr("Draw live brush preview in viewport"));
	}

	ui_check(&g_config->brush_depth_reject, tr("Depth Reject"), "");
	if (ui_item_changed()) {
		make_material_parse_paint_material(true);
	}

	ui_row2();

	ui_check(&g_config->brush_angle_reject, tr("Angle Reject"), "");
	if (ui_item_changed()) {
		make_material_parse_paint_material(true);
	}

	if (!g_config->brush_angle_reject) {
		g_ui->enabled = false;
	}

	ui_slider(&g_context->brush_angle_reject_dot, tr("Angle"), 0.0, 1.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
	if (ui_item_changed()) {
		make_material_parse_paint_material(true);
	}

	g_ui->enabled = true;

	ui_slider(&g_config->brush_alpha_discard, tr("Alpha Discard"), 0.0, 1.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
	if (ui_item_changed()) {
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
	string_array_t *camera_pivot_combo = any_array_create_from_raw_tmp(
	    (void *[]){
	        tr("Cursor"),
	        tr("Center"),
	    },
	    2);
	ui_combo(&g_config->camera_pivot, camera_pivot_combo, tr("Default Camera Pivot"), true, UI_ALIGN_LEFT, true);

	string_array_t *camera_controls_combo = any_array_create_from_raw_tmp(
	    (void *[]){
	        tr("Orbit"),
	        tr("Rotate"),
	        tr("Fly"),
	    },
	    3);
	ui_combo(&g_config->camera_controls, camera_controls_combo, tr("Default Camera Controls"), true, UI_ALIGN_LEFT, true);

	ui_slider(&g_config->camera_fov, tr("Default Camera FoV"), 0.3, 1.4, true, 100.0, true, UI_ALIGN_RIGHT, true);
	ui_slider(&g_config->camera_zoom_speed, tr("Camera Zoom Speed"), 0.1, 4.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
	ui_slider(&g_config->camera_rotation_speed, tr("Camera Rotation Speed"), 0.1, 4.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
	ui_slider(&g_config->camera_pan_speed, tr("Camera Pan Speed"), 0.1, 4.0, true, 100.0, true, UI_ALIGN_RIGHT, true);

	string_array_t *zoom_direction_combo = any_array_create_from_raw_tmp(
	    (void *[]){
	        tr("Vertical"),
	        tr("Vertical Inverted"),
	        tr("Horizontal"),
	        tr("Horizontal Inverted"),
	        tr("Vertical and Horizontal"),
	        tr("Vertical and Horizontal Inverted"),
	    },
	    6);
	ui_combo(&g_config->zoom_direction, zoom_direction_combo, tr("Direction to Zoom"), true, UI_ALIGN_LEFT, true);

	ui_check(&g_config->camera_upside_down, tr("Allow Upside Down Camera"), "");
}

// ██████╗ ███████╗███╗   ██╗
// ██╔══██╗██╔════╝████╗  ██║
// ██████╔╝█████╗  ██╔██╗ ██║
// ██╔═══╝ ██╔══╝  ██║╚██╗██║
// ██║     ███████╗██║ ╚████║
// ╚═╝     ╚══════╝╚═╝  ╚═══╝

void box_preferences_pen_tab() {
	ui_text(tr("Pressure controls"), UI_ALIGN_LEFT, 0x00000000);

	ui_slider(&g_config->pressure_sensitivity, tr("Sensitivity"), 0.0, 10.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
	ui_check(&g_config->pressure_radius, tr("Brush Radius"), "");
	ui_check(&g_config->pressure_hardness, tr("Brush Hardness"), "");
	ui_check(&g_config->pressure_opacity, tr("Brush Opacity"), "");
	ui_check(&g_config->pressure_angle, tr("Brush Angle"), "");

	ui_end_element();
	f32_array_t *row = f32_array_create_from_raw_tmp(
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

static void box_preferences_section(char *title) {
	ui_separator(8, false);
	ui_text(title, UI_ALIGN_LEFT, 0x00000000);
	ui_separator(2, true);
	ui_separator(4, false);
}

void box_preferences_viewport_tab() {
	box_preferences_section(tr("Display"));

	ui_row2();

	string_array_t *mode_combo = base_get_viewport_modes();
	ui_combo(&g_config->viewport_mode, mode_combo, tr("Default Mode"), true, UI_ALIGN_LEFT, true);

	string_array_t *render_mode_combo = any_array_create_from_raw_tmp(
	    (void *[]){
	        tr("Desktop"),
	        tr("Mobile"),
	    },
	    2);
	ui_combo((int *)&g_config->render_mode, render_mode_combo, tr("Renderer"), true, UI_ALIGN_LEFT, true);
	if (ui_item_changed()) {
		context_set_render_path();
	}

	ui_row2();

	i32             supersample       = config_get_super_sample_quality(g_config->rp_supersample);
	string_array_t *supersample_combo = any_array_create_from_raw_tmp(
	    (void *[]){
	        "0.25x",
	        "0.5x",
	        "1.0x",
	        "1.5x",
	        "2.0x",
	        "4.0x",
	    },
	    6);
	ui_set_next_id((ui_id_t)&g_config->rp_supersample);
	ui_combo(&supersample, supersample_combo, tr("Super Sample"), true, UI_ALIGN_LEFT, true);
	if (ui_item_changed()) {
		g_config->rp_supersample = config_get_super_sample_size(supersample);
		config_apply();
	}

	ui_check(&g_config->texture_filter, tr("Filter Textures"), "");
	if (ui_item_changed()) {
		gpu_use_linear_sampling(g_config->texture_filter);
	}

	box_preferences_section(tr("Path Tracer"));

	ui_row2();

	string_array_t *pathtrace_mode_combo = any_array_create_from_raw_tmp(
	    (void *[]){
	        tr("Fast"),
	        tr("Quality"),
	    },
	    2);
	ui_combo(&g_config->pathtrace_mode, pathtrace_mode_combo, tr("Quality"), true, UI_ALIGN_LEFT, true);
	if (ui_item_changed()) {
		render_path_raytrace_ready       = false;
		render_path_raytrace_init_shader = true;
		g_context->ddirty                = 2;
	}

	ui_slider_int(&g_config->pathtrace_frames, tr("Frames"), 1, 128, false, true, UI_ALIGN_RIGHT, true);
	if (g_config->pathtrace_frames < 1) {
		g_config->pathtrace_frames = 1;
	}
	if (g_config->pathtrace_frames > 128) {
		// Samples repeat after 128 frames
		g_config->pathtrace_frames = 128;
	}
	if (ui_item_changed()) {
		g_context->ddirty = 2;
	}

	box_preferences_section(tr("Post Process"));

	bool post_changed = false;

	if (g_config->render_mode == RENDER_MODE_DEFERRED) {
		ui_row2();
		ui_slider(&g_config->rp_ssao, tr("SSAO"), 0.0, 1.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
		post_changed |= ui_item_changed();
		ui_slider(&g_config->rp_bloom, tr("Bloom"), 0.0, 1.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
		post_changed |= ui_item_changed();
	}

	ui_row2();
	ui_slider(&g_config->rp_contrast, tr("Contrast"), 0.0, 2.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
	post_changed |= ui_item_changed();
	ui_slider(&g_config->rp_gamma, tr("Gamma"), 0.0, 2.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
	post_changed |= ui_item_changed();

	ui_row2();
	ui_slider(&g_config->rp_vignette, tr("Vignette"), 0.0, 1.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
	post_changed |= ui_item_changed();
	ui_slider(&g_config->rp_grain, tr("Noise Grain"), 0.0, 1.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
	post_changed |= ui_item_changed();

	if (post_changed) {
		g_context->ddirty = 2;
	}

	f32_array_t *lut_ar = f32_array_create_from_raw_tmp(
	    (f32[]){
	        7 / 8.0,
	        1 / 8.0,
	    },
	    2);
	ui_row(lut_ar);
	ui_text_input(&g_config->lut_path, tr(".cube LUT"), UI_ALIGN_RIGHT, true, false);
	if (ui_item_changed()) {
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

	box_preferences_section(tr("Clipping"));

	camera_object_t *cam     = scene_camera;
	camera_data_t   *cam_raw = cam->data;

	ui_row2();
	ui_slider(&cam_raw->near_plane, tr("Clip Start"), 0.001, 1.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
	bool clip_changed = ui_item_changed();
	ui_slider(&cam_raw->far_plane, tr("Clip End"), 50.0, 100.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
	clip_changed |= ui_item_changed();
	if (clip_changed) {
		camera_object_build_proj(cam, -1.0);
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
		ui_files_filename = string("%s.json", ui_files_filename);
	}
	char *path = string("%s%skeymap_presets%s%s", path_data(), PATH_SEP, PATH_SEP, g_config->keymap);
	file_copy(path, string("%s%s%s", dest, PATH_SEP, ui_files_filename));
}

void box_preferences_keymap_tab_import(char *path) {
	import_keymap_run(path);
}

void box_preferences_keymap_tab_new_box() {
	ui_row2();
	char *keymap_name = ui_text_input(&box_preferences_new_keymap, tr("Name"), UI_ALIGN_LEFT, true, false);
	if (ui_icon_button(tr("OK"), ICON_CHECK, UI_ALIGN_CENTER) || g_ui->is_return_down) {
		char *template = keymap_to_json(keymap_get_default());
		if (!ends_with(keymap_name, ".json")) {
			keymap_name = string("%s.json", keymap_name);
		}
		char *path = string("%s%skeymap_presets%s%s", path_data(), PATH_SEP, PATH_SEP, keymap_name);
		iron_file_save_bytes(path, sys_string_to_buffer(template), 0);
		box_preferences_fetch_keymaps(); // Refresh file list
		g_config->keymap = string_copy(keymap_name);
		ui_box_hide();
		box_preferences_tab = 5; // Keymap
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
	if (ui_panel(&m->expanded, m->name, false, true, false)) {
		if (ui_text(string_tmp("%s: %s (%s)", tr("source"), m->web, m->license), UI_ALIGN_LEFT, 0x00000000) == UI_STATE_RELEASED) {
			iron_load_url(m->web);
		}
		ui_text(string_tmp("%s: %s", tr("gpu memory"), m->memory), UI_ALIGN_LEFT, 0x00000000);
		ui_text(string_tmp("%s: %s", tr("nodes"), m->nodes), UI_ALIGN_LEFT, 0x00000000);

		char *url       = m->urls->buffer[0];
		char *file_name = box_preferences_file_name_from_url(url);
		bool  found     = box_preferences_model_exists(file_name);

		if (neural_node_downloading > 0) {
			g_ui->enabled = false;

			u64   u              = iron_net_bytes_downloaded;
			i32   i              = (u / 1000000000.0) * 100;
			f32   f              = i / 100.0;
			char *downloaded     = string("%sGB", f32_to_string(f));
			ui_box_hwnd->redraws = 2;
			iron_delay_idle_sleep();

			i32 _BUTTON_COL     = g_theme->BUTTON_COL;
			g_theme->BUTTON_COL = g_theme->HIGHLIGHT_COL;

			f32 progress = f / (float)parse_float(m->size);
			ui_slider(&progress, string_tmp("%s / %s", downloaded, m->size), 0.0, 1.0, true, 100, false, UI_ALIGN_CENTER, true);

			g_theme->BUTTON_COL = _BUTTON_COL;

			g_ui->enabled = true;
		}
		else if (!found && ui_icon_button(string_tmp("%s (%s)", tr("Download"), m->size), ICON_ARROW_DOWN, UI_ALIGN_CENTER)) {
			neural_node_download_models(m->urls);
			console_info(tr("Downloading"));
		}
		else if (found && ui_icon_button(string_tmp("%s (%s)", tr("Remove"), m->size), ICON_DELETE, UI_ALIGN_CENTER)) {
			for (i32 i = 0; i < m->urls->length; ++i) {
				char *url       = m->urls->buffer[i];
				char *file_name = box_preferences_file_name_from_url(url);
				iron_delete_file(string("%s%s%s", neural_node_dir(), PATH_SEP, file_name));
			}
		}
	}
}

void box_preferences_neural_tab() {
	i32             neural_res       = g_config->neural_res == 2048 ? 2 : (g_config->neural_res == 1024 ? 1 : 0);
	string_array_t *neural_res_combo = any_array_create_from_raw_tmp(
	    (void *[]){
	        "512",
	        "1024",
	        "2048",
	    },
	    3);
	ui_set_next_id((ui_id_t)&g_config->neural_res);
	ui_combo(&neural_res, neural_res_combo, tr("Resolution"), true, UI_ALIGN_LEFT, true);
	g_config->neural_res = neural_res == 2 ? 2048 : (neural_res == 1 ? 1024 : 512);

	string_array_t *console_model_combo = any_array_create_from_raw_tmp(
	    (void *[]){
	        "Qwen",
	        "Claude",
	        "Grok",
	        "Codex",
	    },
	    4);
	ui_combo(&g_config->console_model, console_model_combo, tr("Console Model"), true, UI_ALIGN_LEFT, true);

	ui_text(tr("Models"), UI_ALIGN_LEFT, 0x00000000);

	if (neural_node_models == NULL) {
		neural_node_models_init();
	}

	for (i32 i = 0; i < neural_node_models->length; ++i) {
		box_preferences_model_panel(neural_node_models->buffer[i]);
	}

	f32_array_t *row = f32_array_create_from_raw_tmp(
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

	if (g_config->console_model == 0) {
		ui_text(tr("All processing is done locally on device"), UI_ALIGN_LEFT, 0x00000000);
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

	i32 preset = box_preferences_get_preset_index();
	ui_set_next_id((ui_id_t)&g_config->keymap);
	ui_combo(&preset, box_preferences_files_keymap, tr("Preset"), false, UI_ALIGN_LEFT, true);
	if (ui_item_changed()) {
		g_config->keymap = string("%s.json", box_preferences_files_keymap->buffer[preset]);
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

	if (!string_equals(box_preferences_keymap_search, "")) {
		ui_row(f32_array_create_from_raw_tmp((f32[]){0.85, 0.15}, 2));
	}
	ui_text_input(&box_preferences_keymap_search, tr("Search"), UI_ALIGN_LEFT, true, true);
	if (!string_equals(box_preferences_keymap_search, "") && (ui_button(tr("X"), UI_ALIGN_CENTER, ""))) {
		box_preferences_keymap_search = "";
	}

	ui_end_sticky();

	ui_separator(8, false);

	g_ui->changed        = false;
	string_array_t *keys = map_keys(g_keymap);
	array_sort(keys, NULL);
	char *search = to_lower_case(box_preferences_keymap_search);
	for (i32 i = 0; i < keys->length; ++i) {
		char *key   = keys->buffer[i];
		char *value = any_map_get(g_keymap, key);
		if (!string_equals(search, "") && string_index_of(to_lower_case(key), search) == -1 && string_index_of(to_lower_case(value), search) == -1) {
			continue;
		}
		ui_set_next_id((ui_id_t)key);
		ui_text_input(&value, key, UI_ALIGN_LEFT, true, false);
		any_map_set(g_keymap, key, value);
	}
	array_free(keys);
	free(keys);
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
		ui_files_filename = string("%s.c", ui_files_filename);
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
		ui_base_tabs->buffer[TAB_AREA_SIDEBAR0]           = 2; // Scripts tab
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
	char *plugin_name = ui_text_input(&box_preferences_new_plugin, tr("Name"), UI_ALIGN_LEFT, true, false);
	if (ui_icon_button(tr("OK"), ICON_CHECK, UI_ALIGN_CENTER) || g_ui->is_return_down) {
		char *template = "\
bool expanded = false;\n\
void on_ui() {\n\
	if (ui_panel(&expanded, \"New Plugin\", false, false, false)) {\n\
		if (ui_button(\"Button\", UI_ALIGN_CENTER, \"\")) {\n\
			console_info(\"Hello\");\n\
		}\n\
	}\n\
}\n\
void main() {\n\
	void *plugin = plugin_create();\n\
	plugin_notify_on_ui(plugin, on_ui);\n\
}\n\
";
		if (!ends_with(plugin_name, ".c")) {
			plugin_name = string("%s.c", plugin_name);
		}
		char *path = string("%s%splugins%s%s", path_data(), PATH_SEP, PATH_SEP, plugin_name);
		iron_file_save_bytes(path, sys_string_to_buffer(template), 0);
		box_preferences_files_plugin = NULL; // Refresh file list
		ui_box_hide();
		box_preferences_tab = PREFERENCES_TAB_PLUGINS;
		box_preferences_show();
	}
}

void box_preferences_plugins_tab() {
	ui_begin_sticky();
	f32_array_t *row = f32_array_create_from_raw_tmp(
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

	if (!string_equals(box_preferences_plugins_search, "")) {
		ui_row(f32_array_create_from_raw_tmp((f32[]){0.85, 0.15}, 2));
	}
	ui_text_input(&box_preferences_plugins_search, tr("Search"), UI_ALIGN_LEFT, true, true);
	if (!string_equals(box_preferences_plugins_search, "") && (ui_button(tr("X"), UI_ALIGN_CENTER, "") || g_ui->is_escape_down)) {
		box_preferences_plugins_search = "";
	}

	ui_end_sticky();

	if (box_preferences_files_plugin == NULL) {
		box_preferences_fetch_plugins();
	}

	char *plugin_search = to_lower_case(box_preferences_plugins_search);
	if (g_config->plugins == NULL) {
		g_config->plugins = any_array_create_from_raw((void *[]){}, 0);
	}
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
		bool checked = enabled;
		ui_check(&checked, tag, "");
		if (checked != enabled) {
			checked ? config_enable_plugin(f) : config_disable_plugin(f);
			base_redraw_ui();
		}
		if (g_ui->is_hovered && g_ui->input_released_r) {
			_box_preferences_f = string_copy(f);
			ui_menu_draw(&box_preferences_plugins_tab_plugin_menu, -1, -1);
		}
	}
}

void box_preferences_show_on_hide() {
	config_save();
}

void box_preferences_show_box() {
	if (ui_tab(&box_preferences_tab, tr("Interface"), true, -1, false)) {
		box_preferences_interface_tab();
	}
	if (ui_tab(&box_preferences_tab, tr("Theme"), true, -1, false)) {
		box_preferences_theme_tab();
	}
	if (ui_tab(&box_preferences_tab, tr("Usage"), true, -1, false)) {
		box_preferences_usage_tab();
	}
	if (ui_tab(&box_preferences_tab, tr("Camera"), true, -1, false)) {
		box_preferences_camera_tab();
	}

#ifdef IRON_IOS
	char *pen_name = tr("Pencil");
#else
	char *pen_name = tr("Pen");
#endif
	if (ui_tab(&box_preferences_tab, pen_name, true, -1, false)) {
		box_preferences_pen_tab();
	}

	if (ui_tab(&box_preferences_tab, tr("Viewport"), true, -1, false)) {
		box_preferences_viewport_tab();
	}
	if (ui_tab(&box_preferences_tab, tr("Keymap"), true, -1, false)) {
		box_preferences_keymap_tab();
	}
#if defined(IRON_WINDOWS) || defined(IRON_LINUX) || defined(IRON_MACOS)
	if (ui_tab(&box_preferences_tab, tr("Neural"), true, -1, false)) {
		box_preferences_neural_tab();
	}
#endif
	if (ui_tab(&box_preferences_tab, tr("Plugins"), true, -1, false)) {
		box_preferences_plugins_tab();
	}
}

void box_preferences_show() {
	ui_box_show_custom(&box_preferences_show_box, 720, 520, &box_preferences_show_on_hide, true, tr("Preferences"));
}

void box_preferences_fetch_themes() {
	box_preferences_themes = file_read_directory(string("%s%sthemes", path_data(), PATH_SEP));
	for (i32 i = 0; i < box_preferences_themes->length; ++i) {
		char *s                           = box_preferences_themes->buffer[i];
		box_preferences_themes->buffer[i] = substring(box_preferences_themes->buffer[i], 0, string_length(s) - 5); // Strip .json
	}
	array_insert(box_preferences_themes, 0, "default");
}

void box_preferences_fetch_keymaps() {
	box_preferences_files_keymap = file_read_directory(string("%s%skeymap_presets", path_data(), PATH_SEP));
	for (i32 i = 0; i < box_preferences_files_keymap->length; ++i) {
		char *s                                 = box_preferences_files_keymap->buffer[i];
		box_preferences_files_keymap->buffer[i] = substring(box_preferences_files_keymap->buffer[i], 0, string_length(s) - 5); // Strip .json
	}
	array_insert(box_preferences_files_keymap, 0, "default");
}

void box_preferences_fetch_plugins() {
	box_preferences_files_plugin = file_read_directory(string("%s%splugins", path_data(), PATH_SEP));
}

i32 box_preferences_get_theme_index() {
	return string_array_index_of(box_preferences_themes, substring(g_config->theme, 0, string_length(g_config->theme) - 5)); // Strip .json
}

i32 box_preferences_get_preset_index() {
	return string_array_index_of(box_preferences_files_keymap, substring(g_config->keymap, 0, string_length(g_config->keymap) - 5)); // Strip .json
}
