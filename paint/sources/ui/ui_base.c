
#include "../global.h"

void ui_base_init_on_next_frame(void *_) {
	layers_init();
}

static tab_draw_t *ui_base_all_tabs[TAB_COUNT];

tab_draw_t *ui_base_get_tab(i32 id) {
	return id >= 0 && id < TAB_COUNT ? ui_base_all_tabs[id] : NULL;
}

// Windows drawn once per 3D view display the same content, keep them redrawing together
void ui_window_sync_redraws(ui_window_t *handle, ui_window_t *split_handle, bool split) {
	if (split) {
		i32 redraws           = math_max(handle->redraws, split_handle->redraws);
		handle->redraws       = redraws;
		split_handle->redraws = redraws;
	}
	else {
		split_handle->redraws = 2;
	}
}

bool ui_base_picker_button() {
	return ui_icon_button("", ICON_PICKER, UI_ALIGN_CENTER);
}

void ui_base_init() {
	ui_toolbar_init();
	g_context->text_tool_text = string_copy(tr("Text"));
	ui_header_init();
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
		array_free(b);
		free(b);
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
	        "icons.k",
	        "icons05x.k",
	    },
	    2);
	resource_load(resources);

	f32           scale = g_config->window_scale;
	ui_options_t *ops   = ALLOC_INIT(ui_options_t, {.theme = g_theme, .font = g_font, .scale_factor = scale});

	g_ui = ui_create(ops);

	ui_on_tab_drop         = ui_dock_on_tab_drop;
	ui_on_tab_dock         = ui_dock_on_tab_dock;
	ui_on_tab_context_menu = ui_dock_on_tab_context_menu;
	ui_dock_load();
	if (UI_SCALE() > 1) {
		ui_base_set_icon_scale();
	}

	ui_picker_button = ui_base_picker_button;

	g_context->gizmo             = scene_get_child(".Gizmo");
	array_remove(scene_meshes, g_context->gizmo->ext); // Drawn by render_gizmo_draw()
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
}

void ui_base_menu_draw_viewport_mode() {
	i32  mode         = g_context->viewport_mode;
	bool mode_changed = false;
	ui_text(tr("Viewport Mode"), UI_ALIGN_RIGHT, 0x00000000);

	string_array_t *modes     = base_get_viewport_modes();
	string_array_t *shortcuts = base_get_viewport_mode_shortcuts();
	for (i32 i = 0; i < modes->length; ++i) {
		ui_radio(&mode, i, modes->buffer[i], shortcuts->buffer[i]);
		mode_changed |= ui_item_changed();
	}

	i32 index = string_array_index_of(shortcuts, keyboard_key_code(g_ui->key_code));
	if (g_ui->is_key_pressed && index != -1) {
		g_ui->changed = true;
		context_set_viewport_mode(index);
	}
	else if (mode_changed) {
		context_set_viewport_mode(mode);
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

// Nothing docked is visible
static void ui_base_draw_empty() {
	draw_begin(NULL, true, g_theme->SEPARATOR_COL);
	gpu_texture_t *img = data_get_texture("badge_bw.k");
	draw_set_color(0x22ffffff);
	draw_image(img, iron_window_width() / 2.0 - img->width / 2.0, iron_window_height() / 2.0 - img->height / 2.0);
	draw_set_color(0xffffffff);
	draw_end();
}

void ui_base_update(void *_) {

	if (console_message_timer > 0) {
		console_message_timer -= sys_delta();
		if (console_message_timer <= 0) {
			ui_dock_redraw(TAB_AREA_STATUS);
		}
	}

	ui_sidebar_w_mini = math_floor(ui_sidebar_default_w_mini * UI_SCALE());

	if (base_ui_enabled) {
		render_gizmo_update();
		util_layer_update_path();
		util_touch_update();
		util_stencil_transform();
		util_select_update();
		util_shape_tool_update();
		util_text_tool_update();
		util_path_tool_update();
		util_layer_check_path_grab();
		util_brush_update();
		util_layer_update_preview();
		util_shortcut_undo_redo();
		util_shortcut_global();
		util_shortcut_brush();
		util_shortcut_viewport();
		ui_toolbar_update();
		util_particle_update();
	}

	string_array_t *keys = map_keys(g_plugins);
	for (i32 i = 0; i < keys->length; ++i) {
		plugin_t *p = any_map_get(g_plugins, keys->buffer[i]);
		if (p->on_update != NULL) {
			minic_ctx_call_fn(p->ctx, p->on_update, NULL, 0);
		}
	}
	array_free(keys);
	free(keys);

	util_cursor_render(_);

	if (!ui_base_show && g_config->touch_ui) {
		g_ui->input_enabled = true;
		ui_begin(g_ui);
		static ui_window_t window = {0};
		if (ui_window(&window, 0, 0, 150, math_floor(UI_ELEMENT_H() + UI_ELEMENT_OFFSET() + 1), false)) {
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

	// Nothing to display
	if (ui_dock_is_empty()) {
		ui_base_draw_empty();
	}

	ui_begin(g_ui);
	ui_menubar_update_w();
	ui_dock_render_ui();
	ui_toolbar_render_ui();
	ui_menubar_render_ui();
	ui_header_render_ui();
	ui_end();
	g_ui->input_enabled = true;
}

tab_draw_t *_draw_callback_create(void (*f)(i32 *), i32 id, char *name, i32 area) {
	tab_draw_t *cb       = ALLOC_INIT(tab_draw_t, {.f = f, .id = id, .name = name, .area = area});
	ui_base_all_tabs[id] = cb;
	return cb;
}

void ui_base_init_tabs() {
	_draw_callback_create(tab_layers_draw, TAB_LAYERS, "Layers", TAB_AREA_SIDEBAR0);
	_draw_callback_create(tab_meshes_draw, TAB_MESHES, "Meshes", TAB_AREA_SIDEBAR0);
	_draw_callback_create(tab_scripts_draw, TAB_SCRIPTS, "Scripts", TAB_AREA_SIDEBAR0);
	_draw_callback_create(tab_materials_draw, TAB_MATERIALS, "Materials", TAB_AREA_SIDEBAR1);
	_draw_callback_create(tab_brushes_draw, TAB_BRUSHES, "Brushes", TAB_AREA_SIDEBAR1);
	_draw_callback_create(tab_plugins_draw, TAB_PLUGINS, "Plugins", TAB_AREA_SIDEBAR1);
	_draw_callback_create(tab_browser_draw, TAB_BROWSER, "Browser", TAB_AREA_STATUS);
	_draw_callback_create(tab_textures_draw, TAB_TEXTURES, "Textures", TAB_AREA_STATUS);
	_draw_callback_create(tab_fonts_draw, TAB_FONTS, "Fonts", TAB_AREA_STATUS);
	_draw_callback_create(tab_sounds_draw, TAB_SOUNDS, "Sounds", TAB_AREA_STATUS);
	_draw_callback_create(tab_swatches_draw, TAB_SWATCHES, "Swatches", TAB_AREA_STATUS);
	_draw_callback_create(tab_timeline_draw, TAB_TIMELINE, "Timeline", TAB_AREA_STATUS);
	_draw_callback_create(tab_console_draw, TAB_CONSOLE, "Console", TAB_AREA_STATUS);
#ifndef NDEBUG
	_draw_callback_create(tab_debug_draw, TAB_DEBUG, "Debug", TAB_AREA_SIDEBAR0)->closed_default = true;
#endif
	// Views draw their own windows
	_draw_callback_create(tab_view3d_draw, TAB_VIEW3D, "3D View", -1);
	_draw_callback_create(tab_view2d_draw, TAB_VIEW2D, "2D View", -1)->closed_default = true;
	_draw_callback_create(tab_nodes_draw, TAB_NODES, "Nodes", -1)->closed_default    = true;

#ifdef IRON_IOS
	if (config_is_iphone()) {
		ui_base_all_tabs[TAB_TIMELINE]->closed_default = true;
		ui_base_all_tabs[TAB_SWATCHES]->closed_default = true;
		ui_base_all_tabs[TAB_SOUNDS]->closed_default   = true;
	}
#endif

#ifdef IRON_ANDROID
	if (iron_window_width() <= 1080) {
		ui_base_all_tabs[TAB_TIMELINE]->closed_default = true;
		ui_base_all_tabs[TAB_SOUNDS]->closed_default   = true;
	}
#endif
}

void ui_base_toggle_distract_free() {
	if (player_in_editor) {
		player_stop();
		return;
	}
	if (base_player_lock) {
		return;
	}

	ui_base_show = !ui_base_show;
	if (ui_base_show && base_player) {
		base_player = false;
		base_set_workspace(WORKSPACE_PRESET_NONE);
	}
	base_resize();
}

static bool ui_base_is_portrait() {
	return g_config->touch_ui && iron_window_width() < iron_window_height();
}

static void ui_base_show_nodes(canvas_type_t type) {
	// Clear input state as ui receives input events even when not drawn
	ui_end_input();

	bool show            = !ui_nodes_show || ui_nodes_canvas_type != type;
	ui_nodes_canvas_type = type;
	ui_dock_show_view(TAB_NODES, show);

	if (g_config->touch_ui && ui_view2d_show && base_view3d_show) {
		ui_dock_show_view(TAB_VIEW2D, false);
	}
	if (ui_base_is_portrait()) {
		if (show) {
			ui_dock_show_view(TAB_VIEW2D, false);
		}
		ui_dock_show_view(TAB_VIEW3D, !show);
	}

	base_resize();
}

void ui_base_show_material_nodes() {
	ui_base_show_nodes(CANVAS_TYPE_MATERIAL);
}

void ui_base_show_brush_nodes() {
	ui_base_show_nodes(CANVAS_TYPE_BRUSH);
}

void ui_base_show_mesh_nodes() {
	ui_base_show_nodes(CANVAS_TYPE_MESH);
}

void ui_base_show_2d_view(view_2d_type_t type) {
	// Clear input state as ui receives input events even when not drawn
	ui_end_input();
	bool show               = ui_view2d_type != type || !ui_view2d_show;
	ui_view2d_type          = type;
	ui_view2d_hwnd->redraws = 2;
	ui_dock_show_view(TAB_VIEW2D, show);

	if (g_config->touch_ui && ui_nodes_show && base_view3d_show) {
		ui_dock_show_view(TAB_NODES, false);
	}
	if (ui_base_is_portrait()) {
		if (show) {
			ui_dock_show_view(TAB_NODES, false);
		}
		ui_dock_show_view(TAB_VIEW3D, !show);
	}

	base_resize();
}

void ui_base_show_3d_view() {
	if (!base_view3d_show) {
		if (g_config->touch_ui && ui_nodes_show && ui_view2d_show) {
			ui_dock_show_view(TAB_VIEW2D, false);
		}
		if (ui_base_is_portrait()) {
			ui_dock_show_view(TAB_NODES, false);
			ui_dock_show_view(TAB_VIEW2D, false);
		}
	}

	ui_dock_show_view(TAB_VIEW3D, !base_view3d_show);
	base_resize();
}
