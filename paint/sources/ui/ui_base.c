
#include "../global.h"

extern gpu_texture_t *base_color_wheel;
extern gpu_texture_t *base_color_wheel_gradient;

void ui_base_init_on_next_frame(void *_) {
	layers_init();
}

void ui_base_view_top() {
	bool is_typing = g_ui->is_typing;

	if (context_in_paint_area() && !is_typing) {
		if (mouse_view_x() < sys_w()) {
			viewport_set_view(0, 0, 1, 0, 0, 0);
		}
	}
}

void ui_base_on_border_hover(ui_handle_t *handle, i32 side) {
	if (!base_ui_enabled) {
		return;
	}

	if (handle != ui_base_hwnds->buffer[TAB_AREA_SIDEBAR0] && handle != ui_base_hwnds->buffer[TAB_AREA_SIDEBAR1] &&
	    handle != ui_base_hwnds->buffer[TAB_AREA_STATUS] && handle != ui_nodes_hwnd && handle != ui_view2d_hwnd) {
		return; // Scalable handles
	}
	if (handle == ui_view2d_hwnd && side != BORDER_SIDE_LEFT) {
		return;
	}
	if (handle == ui_nodes_hwnd && side == BORDER_SIDE_TOP && !ui_view2d_show) {
		return;
	}
	if (handle == ui_base_hwnds->buffer[TAB_AREA_SIDEBAR0] && side == BORDER_SIDE_TOP) {
		return;
	}

	if (handle == ui_nodes_hwnd && side != BORDER_SIDE_LEFT && side != BORDER_SIDE_TOP) {
		return;
	}
	if (handle == ui_base_hwnds->buffer[TAB_AREA_STATUS] && side != BORDER_SIDE_TOP) {
		return;
	}
	if (side == BORDER_SIDE_RIGHT) {
		return; // UI is snapped to the right side
	}

	side == BORDER_SIDE_LEFT || side == BORDER_SIDE_RIGHT ? iron_mouse_set_cursor(IRON_CURSOR_SIZEWE) : iron_mouse_set_cursor(IRON_CURSOR_SIZENS);

	if (g_ui->input_started) {
		ui_base_border_started = side;
		gc_unroot(ui_base_border_handle);
		ui_base_border_handle = handle;
		gc_root(ui_base_border_handle);
		base_is_resizing = true;
	}
}

void ui_base_on_tab_drop(ui_handle_t *to, i32 to_position, ui_handle_t *from, i32 from_position) {
	i32 i = -1;
	i32 j = -1;
	for (i32 k = 0; k < ui_base_htabs->length; ++k) {
		if (ui_base_htabs->buffer[k] == to) {
			i = k;
		}
		if (ui_base_htabs->buffer[k] == from) {
			j = k;
		}
	}
	if (i == j && to_position == from_position) {
		return;
	}
	if (i > -1 && j > -1) {
		tab_draw_t_array_t *tabsi = ui_base_hwnd_tabs->buffer[i];
		tab_draw_t_array_t *tabsj = ui_base_hwnd_tabs->buffer[j];
		if (tabsj->length == 1) {
			return; // Keep at least one tab in place
		}
		tab_draw_t *element = tabsj->buffer[from_position];
		array_splice(tabsj, from_position, 1);
		array_insert(tabsi, to_position, element);
		ui_base_hwnds->buffer[i]->redraws = 2;
		ui_base_hwnds->buffer[j]->redraws = 2;
	}
}

bool ui_base_picker_button() {
	return ui_icon_button("", ICON_PICKER, UI_ALIGN_CENTER);
}

void ui_base_init() {
	ui_toolbar_init();
	g_context->text_tool_text = string_copy(tr("Text"));
	ui_header_init();
	ui_statusbar_init();
	ui_menubar_init();

	ui_header_h  = math_floor(ui_header_default_h * g_config->window_scale);
	ui_menubar_w = math_floor(ui_menubar_default_w * g_config->window_scale);

	if (g_context->empty_envmap == NULL) {
		ui_base_make_empty_envmap(g_theme->VIEWPORT_COL);
	}
	if (g_context->preview_envmap == NULL) {
		u8_array_t *b             = u8_array_create(4);
		b->buffer[0]              = 0;
		b->buffer[1]              = 0;
		b->buffer[2]              = 0;
		b->buffer[3]              = 255;
		g_context->preview_envmap = gpu_create_texture_from_bytes(b, 1, 1, GPU_TEXTURE_FORMAT_RGBA32);
	}

	if (g_context->saved_envmap == NULL) {
		// raw.saved_envmap = scene_world._envmap;
		g_context->default_irradiance       = scene_world->_->irradiance;
		g_context->default_radiance         = scene_world->_->radiance;
		g_context->default_radiance_mipmaps = scene_world->_->radiance_mipmaps;
	}
	scene_world->_->envmap = g_context->show_envmap ? g_context->saved_envmap : g_context->empty_envmap;
	g_context->ddirty      = 1;

	string_array_t *resources = any_array_create_from_raw(
	    (void *[]){
	        "cursor.k",
	        "icons.k",
	        "icons05x.k",
	    },
	    3);
	resource_load(resources);

	f32           scale = g_config->window_scale;
	ui_options_t *ops   = GC_ALLOC_INIT(
        ui_options_t,
        {.theme = g_theme, .font = g_font, .scale_factor = scale, .color_wheel = base_color_wheel, .black_white_gradient = base_color_wheel_gradient});

	g_ui = ui_create(ops);
	gc_root(g_ui);

	ui_on_border_hover = ui_base_on_border_hover;
	gc_root(ui_on_border_hover);

	ui_on_tab_drop = ui_base_on_tab_drop;
	gc_root(ui_on_tab_drop);
	if (UI_SCALE() > 1) {
		ui_base_set_icon_scale();
	}

	ui_picker_button = ui_base_picker_button;

	g_context->gizmo             = scene_get_child(".Gizmo");
	g_context->gizmo_translate_x = object_get_child(g_context->gizmo, ".TranslateX");
	g_context->gizmo_translate_y = object_get_child(g_context->gizmo, ".TranslateY");
	g_context->gizmo_translate_z = object_get_child(g_context->gizmo, ".TranslateZ");
	g_context->gizmo_scale_x     = object_get_child(g_context->gizmo, ".ScaleX");
	g_context->gizmo_scale_y     = object_get_child(g_context->gizmo, ".ScaleY");
	g_context->gizmo_scale_z     = object_get_child(g_context->gizmo, ".ScaleZ");
	g_context->gizmo_rotate_x    = object_get_child(g_context->gizmo, ".RotateX");
	g_context->gizmo_rotate_y    = object_get_child(g_context->gizmo, ".RotateY");
	g_context->gizmo_rotate_z    = object_get_child(g_context->gizmo, ".RotateZ");

	project_new(false);

	if (string_equals(g_project->_->filepath, "")) {
		sys_notify_on_next_frame(&ui_base_init_on_next_frame, NULL);
	}

	operator_register("view_top", ui_base_view_top);
}

void ui_base_menu_draw_viewport_mode() {
	ui_handle_t *mode_handle = ui_handle(__ID__);
	mode_handle->i           = g_context->viewport_mode;
	ui_text(tr("Viewport Mode"), UI_ALIGN_RIGHT, 0x00000000);

	string_array_t *modes     = base_get_viewport_modes();
	string_array_t *shortcuts = base_get_viewport_mode_shortcuts();
	if (gpu_raytrace_supported()) {
		any_array_push(modes, tr("Path Traced"));
		any_array_push(shortcuts, "p");
	}
	for (i32 i = 0; i < modes->length; ++i) {
		ui_radio(mode_handle, i, modes->buffer[i], shortcuts->buffer[i]);
	}

	i32 index = string_array_index_of(shortcuts, keyboard_key_code(g_ui->key_code));
	if (g_ui->is_key_pressed && index != -1) {
		mode_handle->i = index;
		g_ui->changed  = true;
		context_set_viewport_mode(mode_handle->i);
	}
	else if (mode_handle->changed) {
		context_set_viewport_mode(mode_handle->i);
		g_ui->changed = true;
	}
}

void ui_base_update_next_frame(void *_) {
	export_texture_run(g_context->texture_export_path, false);
}

void ui_base_update_ui_on_next_frame(void *_) {
	layers_update_fill_layer(true);
	make_material_parse_paint_material(false);
}

void ui_base_update(void *_) {

	if (console_message_timer > 0) {
		console_message_timer -= sys_delta();
		if (console_message_timer <= 0) {
			ui_base_hwnds->buffer[TAB_AREA_STATUS]->redraws = 2;
		}
	}

	ui_sidebar_w_mini = math_floor(ui_sidebar_default_w_mini * UI_SCALE());

	if (base_ui_enabled) {
		render_gizmo_update();
		util_layer_update_path();
		util_touch_update();
		util_stencil_transform();
		util_select_update();
		util_layer_check_path_grab();
		util_brush_update();
		util_layer_update_preview();
		util_shortcut_undo_redo();
		util_shortcut_global();
		util_shortcut_brush();
		util_shortcut_viewport();
		util_resize_borders();
		util_particle_update();
		operator_update();
	}

	string_array_t *keys = map_keys(g_plugins);
	for (i32 i = 0; i < keys->length; ++i) {
		plugin_t *p = any_map_get(g_plugins, keys->buffer[i]);
		if (p->on_update != NULL) {
			minic_ctx_call_fn(p->ctx, p->on_update, NULL, 0);
		}
	}

	if (!mouse_down("left")) {
		gc_unroot(ui_base_border_handle);
		ui_base_border_handle = NULL;
		base_is_resizing      = false;
	}

	util_cursor_render(_);

	if (!ui_base_show && g_config->touch_ui) {
		g_ui->input_enabled = true;
		ui_begin(g_ui);
		if (ui_window(ui_handle(__ID__), 0, 0, 150, math_floor(UI_ELEMENT_H() + UI_ELEMENT_OFFSET() + 1), false)) {
			if (ui_button(tr("Close"), UI_ALIGN_CENTER, "")) {
				ui_base_toggle_distract_free();
			}
		}
		ui_end();
	}

	if (!ui_base_show) {
		return;
	}

	g_ui->input_enabled = base_ui_enabled;

	// Remember last tab positions
	for (i32 i = 0; i < ui_base_htabs->length; ++i) {
		if (ui_base_htabs->buffer[i]->changed) {
			g_config->layout_tabs->buffer[i] = ui_base_htabs->buffer[i]->i;
			config_save();
		}
	}

	// Set tab positions
	for (i32 i = 0; i < ui_base_htabs->length; ++i) {
		ui_base_htabs->buffer[i]->i = g_config->layout_tabs->buffer[i];
	}

	// Nothing to display in the main area
	if (!base_view3d_show && !ui_nodes_show && !ui_view2d_show) {
		draw_begin(NULL, true, g_theme->SEPARATOR_COL);
		gpu_texture_t *img = data_get_texture("badge_bw.k");
		draw_set_color(0x22ffffff);
		draw_image(img, base_view3d_w() / 2.0 - img->width / 2.0, base_h() / 2.0 - img->height / 2.0);
		draw_end();
	}

	ui_begin(g_ui);
	ui_toolbar_render_ui();
	ui_menubar_render_ui();
	ui_header_render_ui();
	ui_statusbar_render_ui();
	ui_sidebar_render_ui();
	ui_end();
	g_ui->input_enabled = true;
}

ui_handle_t_array_t *ui_base_init_hwnds() {
	ui_handle_t_array_t *hwnds = any_array_create_from_raw(
	    (void *[]){
	        ui_handle_create(),
	        ui_handle_create(),
	        ui_handle_create(),
	    },
	    3);
	return hwnds;
}

ui_handle_t_array_t *ui_base_init_htabs() {
	ui_handle_t_array_t *htabs = any_array_create_from_raw(
	    (void *[]){
	        ui_handle_create(),
	        ui_handle_create(),
	        ui_handle_create(),
	    },
	    3);
	return htabs;
}

tab_draw_t *_draw_callback_create(void (*f)(ui_handle_t *)) {
	tab_draw_t *cb = GC_ALLOC_INIT(tab_draw_t, {.f = f});
	return cb;
}

tab_draw_array_t_array_t *ui_base_init_hwnd_tabs() {
	tab_draw_array_t *a0 = any_array_create_from_raw(
	    (void *[]){
	        _draw_callback_create(tab_layers_draw),
	        _draw_callback_create(tab_meshes_draw),
	        _draw_callback_create(tab_scripts_draw),
	    },
	    3);
	tab_draw_array_t *a1 = any_array_create_from_raw(
	    (void *[]){
	        _draw_callback_create(tab_materials_draw),
	        _draw_callback_create(tab_brushes_draw),
	        _draw_callback_create(tab_plugins_draw),
	    },
	    3);
	tab_draw_array_t *a2 = any_array_create_from_raw(
	    (void *[]){
	        _draw_callback_create(tab_browser_draw),
	        _draw_callback_create(tab_textures_draw),
	        _draw_callback_create(tab_fonts_draw),
	        _draw_callback_create(tab_sounds_draw),
	        _draw_callback_create(tab_swatches_draw),
	        _draw_callback_create(tab_timeline_draw),
	        _draw_callback_create(tab_console_draw),
	        _draw_callback_create(ui_statusbar_draw_version_tab),
	    },
	    8);

#ifdef IRON_IOS
	if (config_is_iphone()) {
		array_splice(a2, 5, 1); // Timeline
		array_splice(a2, 4, 1); // Swatches
		array_splice(a2, 3, 1); // Sounds
	}
#endif

#ifdef IRON_ANDROID
	if (iron_window_width() <= 1080) {
		array_splice(a2, 5, 1); // Timeline
		array_splice(a2, 3, 1); // Sounds
	}
#endif

#ifdef is_debug
	any_array_push(a0, _draw_callback_create(tab_debug_draw));
#endif

	tab_draw_array_t_array_t *r = any_array_create_from_raw((void *[]){}, 0);
	any_array_push(r, a0);
	any_array_push(r, a1);
	any_array_push(r, a2);
	return r;
}

void ui_base_toggle_distract_free() {
	if (base_player_lock) {
		return;
	}

	ui_base_show = !ui_base_show;
	if (ui_base_show) {
		g_config->workspace = WORKSPACE_PAINT_3D;
		base_update_workspace();
	}
	base_resize();
}

void ui_base_show_material_nodes() {
	// Clear input state as ui receives input events even when not drawn
	ui_end_input();

	ui_nodes_show        = !ui_nodes_show || ui_nodes_canvas_type != CANVAS_TYPE_MATERIAL;
	ui_nodes_canvas_type = CANVAS_TYPE_MATERIAL;

	if (g_config->touch_ui && ui_view2d_show && base_view3d_show) {
		ui_view2d_show = false;
	}

	if (g_config->touch_ui && ui_nodes_show && iron_window_width() < iron_window_height()) {
		ui_view2d_show   = false;
		base_view3d_show = false;
	}

	if (g_config->touch_ui && !ui_nodes_show && iron_window_width() < iron_window_height()) {
		base_view3d_show = true;
	}

	base_resize();
}

void ui_base_show_brush_nodes() {
	// Clear input state as ui receives input events even when not drawn
	ui_end_input();
	ui_nodes_show        = !ui_nodes_show || ui_nodes_canvas_type != CANVAS_TYPE_BRUSH;
	ui_nodes_canvas_type = CANVAS_TYPE_BRUSH;

	if (g_config->touch_ui && ui_view2d_show && base_view3d_show) {
		ui_view2d_show = false;
	}

	if (g_config->touch_ui && ui_nodes_show && iron_window_width() < iron_window_height()) {
		ui_view2d_show   = false;
		base_view3d_show = false;
	}

	if (g_config->touch_ui && !ui_nodes_show && iron_window_width() < iron_window_height()) {
		base_view3d_show = true;
	}

	base_resize();
}

void ui_base_show_2d_view(view_2d_type_t type) {
	// Clear input state as ui receives input events even when not drawn
	ui_end_input();
	if (ui_view2d_type != type) {
		ui_view2d_show = true;
	}
	else {
		ui_view2d_show = !ui_view2d_show;
	}
	ui_view2d_type          = type;
	ui_view2d_hwnd->redraws = 2;

	if (g_config->touch_ui && ui_nodes_show && base_view3d_show) {
		ui_nodes_show = false;
	}

	if (g_config->touch_ui && ui_view2d_show && iron_window_width() < iron_window_height()) {
		ui_nodes_show    = false;
		base_view3d_show = false;
	}

	if (g_config->touch_ui && !ui_view2d_show && iron_window_width() < iron_window_height()) {
		base_view3d_show = true;
	}

	base_resize();
}

void ui_base_show_3d_view() {
	if (!base_view3d_show) {
		if (g_config->touch_ui && ui_nodes_show && ui_view2d_show) {
			ui_view2d_show = false;
		}
		if (g_config->touch_ui && (ui_nodes_show || ui_view2d_show) && iron_window_width() < iron_window_height()) {
			ui_nodes_show  = false;
			ui_view2d_show = false;
		}
	}

	base_view3d_show = !base_view3d_show;
	base_resize();
}

void ui_base_toggle_browser() {
	bool minimized                                 = g_config->layout->buffer[LAYOUT_SIZE_STATUS_H] <= (ui_statusbar_default_h * g_config->window_scale);
	g_config->layout->buffer[LAYOUT_SIZE_STATUS_H] = minimized ? 240 : ui_statusbar_default_h;
	g_config->layout->buffer[LAYOUT_SIZE_STATUS_H] = math_floor(g_config->layout->buffer[LAYOUT_SIZE_STATUS_H] * g_config->window_scale);
	base_resize();
}
