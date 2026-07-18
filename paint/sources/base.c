
#include "global.h"

i32            base_drag_tint  = 0xffffffff;
i32            base_drag_size  = -1;
rect_t        *base_drag_rect  = NULL;
f32            base_drag_start = 0.0;
f32            base_drop_x     = 0.0;
f32            base_drop_y     = 0.0;
gpu_texture_t *base_color_wheel;
gpu_texture_t *base_color_wheel_gradient;
i32            base_appx               = 0;
i32            base_appy               = 0;
i32            base_last_window_width  = 0;
i32            base_last_window_height = 0;
bool           base_start_arm_found    = false;
i32            _base_material_count;

void base_on_shutdown() {
#if defined(IRON_ANDROID) || defined(IRON_IOS)
	project_save(false);
#endif
	config_save();
}

void base_on_background() {
	// Release keys after alt-tab / win-tab
	_key_up(KEY_CODE_ALT, NULL);
	_key_up(KEY_CODE_WIN, NULL);
	_key_up(KEY_CODE_CONTROL, NULL);
	_key_up(KEY_CODE_SHIFT, NULL);
}

void base_on_foreground() {
	g_context->foreground_event = true;
	g_context->last_paint_x     = -1;
	g_context->last_paint_y     = -1;
}

void base_on_drop_files(char *drop_path) {
	drop_path = string_copy(trim_end(drop_path));
	any_array_push(base_drop_paths, drop_path);
}

void base_init_on_start_arm(void *_) {
	if (base_start_arm_found) {
		import_arm_run_project(g_project->_->filepath);
	}
	g_context->tool = TOOL_TYPE_CURSOR;
	// Auto-run main script
	if (g_project->script_datas != NULL && g_project->script_datas->length > 0) {
		minic_ctx_t *ctx = minic_eval(g_project->script_datas->buffer[0]);
	}
	tab_timeline_play();
}

void base_save_window_rect() {
	g_config->window_w = iron_window_width();
	g_config->window_h = iron_window_height();
	g_config->window_x = iron_window_x();
	g_config->window_y = iron_window_y();
	config_save();
}

void base_save_and_quit_callback(bool save) {
	base_save_window_rect();
	if (save) {
		project_save(true);
	}
	else {
		iron_stop();
	}
}

void base_material_dropped() {
	// Material drag and dropped onto viewport or layers tab
	if (context_in_3d_view()) {
		uv_type_t uv_type   = keyboard_down("control") ? UV_TYPE_PROJECT : UV_TYPE_UVMAP;
		mat4_t    decal_mat = uv_type == UV_TYPE_PROJECT ? util_render_get_decal_mat() : mat4_nan();
		layers_create_fill_layer(uv_type, decal_mat, -1);
	}
	if (context_in_layers() && tab_layers_can_drop_new_layer(g_context->drag_dest)) {
		uv_type_t uv_type   = keyboard_down("control") ? UV_TYPE_PROJECT : UV_TYPE_UVMAP;
		mat4_t    decal_mat = uv_type == UV_TYPE_PROJECT ? util_render_get_decal_mat() : mat4_nan();
		layers_create_fill_layer(uv_type, decal_mat, g_context->drag_dest);
	}
	else if (context_in_nodes()) {
		ui_nodes_accept_material_drop(array_index_of(g_project->_->materials, base_drag_material));
	}
	else if (context_in_materials()) {
		tab_materials_accept_material_drop(base_drag_material);
	}
	gc_unroot(base_drag_material);
	base_drag_material = NULL;
}

void base_update_import_asset_done() {
	// Asset was material
	if (g_project->_->materials->length > _base_material_count) {
		gc_unroot(base_drag_material);
		base_drag_material = g_context->material;
		gc_root(base_drag_material);
		base_material_dropped();
	}
}

void base_handle_drop_paths() {
	if (base_drop_paths->length > 0) {
		bool wait = false;
#if defined(IRON_LINUX) || defined(IRON_MACOS)
		wait = !mouse_moved; // Mouse coords not updated during drag
#endif
		if (!wait) {
			base_drop_x     = mouse_x;
			base_drop_y     = mouse_y;
			char *drop_path = array_shift(base_drop_paths);
			import_asset_run(drop_path, base_drop_x, base_drop_y, true, true, NULL);
		}
	}
}

gpu_texture_t *base_get_drag_image() {
	base_drag_tint = 0xffffffff;
	base_drag_size = -1;
	gc_unroot(base_drag_rect);
	base_drag_rect = NULL;
	if (base_drag_asset != NULL) {
		return project_get_image(base_drag_asset);
	}
	if (base_drag_swatch != NULL) {
		base_drag_tint = base_drag_swatch->base;
		base_drag_size = 26;
		return tab_swatches_empty_get();
	}
	if (base_drag_file != NULL) {
		if (base_drag_file_icon != NULL) {
			return base_drag_file_icon;
		}
		gpu_texture_t *icons = resource_get("icons.k");
		gc_unroot(base_drag_rect);
		base_drag_rect = string_index_of(base_drag_file, ".") > 0 ? resource_tile50(icons, ICON_FILE) : resource_tile50(icons, ICON_FOLDER_FULL);
		gc_root(base_drag_rect);
		base_drag_tint = g_theme->HIGHLIGHT_COL;
		return icons;
	}

	if (base_drag_material != NULL) {
		return base_drag_material->image_icon;
	}
	if (base_drag_brush != NULL) {
		return base_drag_brush->image_icon;
	}
	if (base_drag_font != NULL) {
		return base_drag_font->image;
	}
	if (base_drag_layer != NULL && slot_layer_is_group(base_drag_layer)) {
		gpu_texture_t *icons         = resource_get("icons.k");
		rect_t        *folder_closed = resource_tile50(icons, ICON_FOLDER_FULL);
		rect_t        *folder_open   = resource_tile50(icons, ICON_FOLDER_OPEN);
		gc_unroot(base_drag_rect);
		base_drag_rect = base_drag_layer->show_panel ? folder_open : folder_closed;
		gc_root(base_drag_rect);
		base_drag_tint = base_darker(g_theme->LABEL_COL, 0x00202020);
		return icons;
	}
	if (base_drag_layer != NULL && slot_layer_is_mask(base_drag_layer) && base_drag_layer->fill_material == NULL) {
		tab_layers_make_mask_preview_rgba32(base_drag_layer);
		return g_context->mask_preview_rgba32;
	}
	if (base_drag_layer != NULL) {
		return base_drag_layer->fill_material != NULL ? base_drag_layer->fill_material->image_icon : base_drag_layer->texpaint_preview;
	}
	if (base_drag_mesh != NULL) {
		gpu_texture_t *preview = tab_meshes_preview_map != NULL ? any_map_get(tab_meshes_preview_map, i32_to_string(base_drag_mesh->base->uid)) : NULL;
		if (preview != NULL) {
			return preview;
		}
		gpu_texture_t *icons = resource_get("icons.k");
		gc_unroot(base_drag_rect);
		base_drag_rect = resource_tile50(icons, ICON_CUBE);
		gc_root(base_drag_rect);
		base_drag_tint = g_theme->BUTTON_COL;
		return icons;
	}
	return NULL;
}

rect_t *base_get_drag_background() {
	gpu_texture_t *icons = resource_get("icons.k");
	if (base_drag_layer != NULL && !slot_layer_is_group(base_drag_layer) && base_drag_layer->fill_material == NULL) {
		return resource_tile50(icons, ICON_CHECKER);
	}
	return NULL;
}

void base_init_undo_layers() {
	if (history_undo_layers == NULL) {
		gc_unroot(history_undo_layers);
		history_undo_layers = any_array_create_from_raw((void *[]){}, 0);
		gc_root(history_undo_layers);
		for (i32 i = 0; i < g_config->undo_steps; ++i) {
			i32           len = history_undo_layers->length;
			char         *ext = string("_undo%s", i32_to_string(len));
			slot_layer_t *l   = slot_layer_create(ext, LAYER_SLOT_TYPE_LAYER, NULL);
			any_array_push(history_undo_layers, l);
		}
	}
}

void base_update(void *_) {
	if (mouse_movement_x != 0 || mouse_movement_y != 0) {
		iron_mouse_set_cursor(IRON_CURSOR_ARROW);
	}

	bool has_drag = base_drag_asset != NULL || base_drag_material != NULL || base_drag_layer != NULL || base_drag_file != NULL || base_drag_swatch != NULL ||
	                base_drag_brush != NULL || base_drag_font != NULL || base_drag_mesh != NULL;

	if (g_config->touch_ui) {
		// Touch and hold to activate dragging
		if (base_drag_start < 0.2) {
			if (has_drag && mouse_down("left")) {
				base_drag_start += sys_real_delta();
			}
			else {
				base_drag_start = 0;
			}
			has_drag = false;
		}
		if (mouse_released("left")) {
			base_drag_start = 0;
		}
		bool moved = math_abs(mouse_movement_x) > 1 && math_abs(mouse_movement_y) > 1;
		if ((mouse_released("left") || moved) && !has_drag) {
			gc_unroot(base_drag_asset);
			base_drag_asset = NULL;
			gc_unroot(base_drag_swatch);
			base_drag_swatch = NULL;
			gc_unroot(base_drag_file);
			base_drag_file = NULL;
			gc_unroot(base_drag_file_icon);
			base_drag_file_icon = NULL;
			base_is_dragging    = false;
			gc_unroot(base_drag_material);
			base_drag_material = NULL;
			gc_unroot(base_drag_layer);
			base_drag_layer = NULL;
			gc_unroot(base_drag_mesh);
			base_drag_mesh = NULL;
			gc_unroot(base_drag_brush);
			base_drag_brush = NULL;
			gc_unroot(base_drag_font);
			base_drag_font = NULL;
		}
		// Disable touch scrolling while dragging is active
		ui_touch_control = !base_is_dragging;
	}

	if (has_drag && (mouse_movement_x != 0 || mouse_movement_y != 0)) {
		base_is_dragging = true;
	}
	if (mouse_released("left") && has_drag) {
		if (base_drag_asset != NULL) {

			// Create image texture
			if (context_in_nodes()) {
				ui_nodes_accept_asset_drop(array_index_of(g_project->_->assets, base_drag_asset));
			}
			else if (context_in_3d_view()) {
				if (ends_with(to_lower_case(base_drag_asset->file), ".hdr")) {
					gpu_texture_t *image = project_get_image(base_drag_asset);
					import_envmap_run(base_drag_asset->file, image);
				}
			}
			// Create mask
			else if (context_in_layers() || context_in_2d_view(VIEW_2D_TYPE_LAYER)) {
				layers_create_image_mask(base_drag_asset);
			}
			else if (context_in_textures()) {
				tab_textures_accept_asset_drop(base_drag_asset);
			}
			gc_unroot(base_drag_asset);
			base_drag_asset = NULL;
		}
		else if (base_drag_swatch != NULL) {
			// Create RGB node
			if (context_in_nodes()) {
				ui_nodes_accept_swatch_drop(base_drag_swatch);
			}
			else if (context_in_swatches()) {
				tab_swatches_accept_swatch_drop(base_drag_swatch);
			}
			else if (context_in_materials()) {
				tab_materials_accept_swatch_drop(base_drag_swatch);
			}
			else if (context_in_3d_view()) {
				i32 color = base_drag_swatch->base;
				color     = color_set_ab(color, base_drag_swatch->opacity * 255);
				layers_create_color_layer(color, base_drag_swatch->occlusion, base_drag_swatch->roughness, base_drag_swatch->metallic, -1);
			}
			else if (context_in_layers() && tab_layers_can_drop_new_layer(g_context->drag_dest)) {
				i32 color = base_drag_swatch->base;
				color     = color_set_ab(color, base_drag_swatch->opacity * 255);
				layers_create_color_layer(color, base_drag_swatch->occlusion, base_drag_swatch->roughness, base_drag_swatch->metallic, g_context->drag_dest);
			}

			gc_unroot(base_drag_swatch);
			base_drag_swatch = NULL;
		}
		else if (base_drag_file != NULL) {
			if (!context_in_browser()) {
				base_drop_x = mouse_x;
				base_drop_y = mouse_y;

				_base_material_count = g_project->_->materials->length;
				import_asset_run(base_drag_file, base_drop_x, base_drop_y, true, true, &base_update_import_asset_done);
			}

			gc_unroot(base_drag_file);
			base_drag_file = NULL;
			gc_unroot(base_drag_file_icon);
			base_drag_file_icon = NULL;
		}
		else if (base_drag_material != NULL) {
			base_material_dropped();
		}
		else if (base_drag_layer != NULL) {
			if (context_in_nodes()) {
				ui_nodes_accept_layer_drop(array_index_of(g_project->_->layers, base_drag_layer));
			}
			else if (context_in_layers() && base_is_dragging) {
				slot_layer_move(base_drag_layer, g_context->drag_dest);
				make_material_parse_mesh_material();
			}
			gc_unroot(base_drag_layer);
			base_drag_layer = NULL;
		}
		else if (base_drag_mesh != NULL) {
			if (context_in_meshes() && base_is_dragging) {
				tab_meshes_accept_mesh_drop(base_drag_mesh);
			}
			gc_unroot(base_drag_mesh);
			base_drag_mesh = NULL;
		}
		else if (base_drag_brush != NULL) {
			if (context_in_brushes()) {
				tab_brushes_accept_brush_drop(base_drag_brush);
			}
			gc_unroot(base_drag_brush);
			base_drag_brush = NULL;
		}
		else if (base_drag_font != NULL) {
			if (context_in_fonts()) {
				tab_fonts_accept_font_drop(base_drag_font);
			}
			gc_unroot(base_drag_font);
			base_drag_font = NULL;
		}

		iron_mouse_set_cursor(IRON_CURSOR_ARROW);
		base_is_dragging = false;
	}
	if (g_context->color_picker_callback != NULL && (mouse_released("left") || mouse_released("right"))) {
		g_context->color_picker_callback = NULL;
		context_select_tool(g_context->color_picker_previous_tool);
	}

	base_handle_drop_paths();

	if (g_context->ddirty < 0) {
		g_context->ddirty = 0;
	}

	if (g_context->tool == TOOL_TYPE_CURSOR && context_in_3d_view()) {
		if (keyboard_down("control") && keyboard_started("d")) {
			sim_duplicate();
		}
		if (keyboard_started("delete")) {
			sim_delete();
		}
	}

	// Live material when using sys_time() script node
	if (g_context->tool == TOOL_TYPE_MATERIAL) {
		bool              has_script_node = false;
		ui_node_canvas_t *canvas          = g_context->material->canvas;
		for (i32 i = 0; i < canvas->nodes->length; ++i) {
			ui_node_t *n = canvas->nodes->buffer[i];
			if (string_equals(n->type, "SCRIPT_CPU")) {
				has_script_node = true;
				break;
			}
		}
		if (has_script_node) {
			layers_update_fill_layers();
			iron_delay_idle_sleep();
		}
	}

	render_compass_update();

	// if (g_config->workspace == WORKSPACE_PLAYER) {
	if (args_player) {
		player_update();
	}

	ui_view2d_update(NULL);
	ui_nodes_update(NULL);
	ui_base_update(NULL);
	camera_update(NULL);

	if (g_config->workspace == WORKSPACE_PLAYER) {
		sim_init();
		if (!sim_running) {
			sim_play();
		}
		sim_update();
	}
	else if (sim_running) {
		sim_stop();
	}

	if (g_context->frame == 2) {
		util_render_make_material_preview();
		ui_base_hwnds->buffer[TAB_AREA_SIDEBAR1]->redraws = 2;
		base_init_undo_layers();

		make_material_parse_mesh_material();
		make_material_parse_paint_material(true);
		g_context->ddirty = 0;
	}
	else if (g_context->frame == 3) {
		g_context->ddirty = 3;
	}

	g_context->frame++;

	if (base_is_dragging) {
		iron_mouse_set_cursor(IRON_CURSOR_HAND);
		gpu_texture_t *img          = base_get_drag_image();
		f32            scale_factor = UI_SCALE();
		f32            size         = (base_drag_size == -1 ? 50 : base_drag_size) * scale_factor;
		f32            ratio        = size / (float)img->width;
		f32            h            = img->height * ratio;
		i32            inv          = 0;

		draw_begin(NULL, false, 0);
		draw_set_color(base_drag_tint);

		rect_t *bg_rect = base_get_drag_background();
		if (bg_rect != NULL) {
			draw_scaled_sub_image(resource_get("icons.k"), bg_rect->x, bg_rect->y, bg_rect->w, bg_rect->h, mouse_x + base_drag_off_x,
			                      mouse_y + base_drag_off_y + inv, size, h - inv * 2);
		}

		base_drag_rect == NULL ? draw_scaled_image(img, mouse_x + base_drag_off_x, mouse_y + base_drag_off_y + inv, size, h - inv * 2)
		                       : draw_scaled_sub_image(img, base_drag_rect->x, base_drag_rect->y, base_drag_rect->w, base_drag_rect->h,
		                                               mouse_x + base_drag_off_x, mouse_y + base_drag_off_y + inv, size, h - inv * 2);
		draw_set_color(0xffffffff);
		draw_end();
	}

	// Select tool rectangle mask
	if (g_context->select_active || g_context->select_dragging) {
		f32 x1 = g_context->select_x1 * sys_w() + sys_x();
		f32 y1 = g_context->select_y1 * sys_h() + sys_y();
		f32 x2 = g_context->select_x2 * sys_w() + sys_x();
		f32 y2 = g_context->select_y2 * sys_h() + sys_y();
		draw_begin(NULL, false, 0);
		draw_set_color(g_context->select_dragging ? 0xffffffff : 0xaaffffff);
		draw_rect(x1, y1, x2 - x1, y2 - y1, 1.0);
		draw_set_color(0xffffffff);
		draw_end();
	}

	bool using_menu = ui_menu_show && mouse_y > ui_header_h;
	base_ui_enabled = !ui_box_show && !using_menu && g_ui->combo_selected_handle == NULL;

	if (ui_box_show) {
		ui_box_render();
	}
	if (ui_menu_show) {
		ui_menu_render();
	}

#if defined(IRON_ANDROID) || defined(IRON_IOS)
	// No mouse move events for touch, re-init last paint position on touch start
	if (!mouse_down("left")) {
		g_context->last_paint_x = -1;
		g_context->last_paint_y = -1;
	}
#endif
}

void base_init() {
	base_last_window_width  = iron_window_width();
	base_last_window_height = iron_window_height();

	sys_notify_on_drop_files(&base_on_drop_files);
	sys_notify_on_app_state(&base_on_foreground, &base_on_background, &base_on_shutdown);
	iron_set_save_and_quit_callback(base_save_and_quit_callback);

	g_font = data_get_font("font.ttf");
	gc_root(g_font);

	base_color_wheel = data_get_texture("color_wheel.k");
	gc_root(base_color_wheel);

	base_color_wheel_gradient = data_get_texture("color_wheel_gradient.k");
	gc_root(base_color_wheel_gradient);
	config_load_theme(g_config->theme, false);
	base_default_element_w = g_theme->ELEMENT_W;
	base_default_element_h = g_theme->ELEMENT_H;
	base_default_font_size = g_theme->FONT_SIZE;
	translator_load_translations(g_config->locale);

	ui_files_filename = string_copy(tr("untitled"));
	gc_root(ui_files_filename);
#if defined(IRON_ANDROID) || defined(IRON_IOS)
	sys_title_set(tr("untitled"));
#endif

	// Baked font for fast startup
	if (string_equals(g_config->locale, "en")) {
		draw_font_13(g_font);
	}
	else {
		draw_font_init(g_font);
	}

	ui_nodes_enum_texts    = base_combo_enum_texts;
	ui_nodes_enum_textures = base_combo_enum_textures;
	gc_root(ui_nodes_enum_texts);

	// Init plugins
	if (g_config->plugins != NULL) {
		for (i32 i = 0; i < g_config->plugins->length; ++i) {
			char *plugin = g_config->plugins->buffer[i];
			plugin_start(plugin);
		}
	}

	if (!g_config->texture_filter) {
		gpu_use_linear_sampling(false);
	}

	args_parse();
	camera_init();
	ui_base_init();
	ui_viewnodes_init();
	ui_view2d_init();

	sys_notify_on_update(base_update, NULL);

	base_appx = ui_toolbar_w(true);
	base_appy = 0;
	if (g_config->layout->buffer[LAYOUT_SIZE_HEADER] == 1) {
		base_appy = ui_header_h * 2;
	}
	scene_camera->data->fov = math_floor(scene_camera->data->fov * 100) / 100.0;
	camera_object_build_proj(scene_camera, -1.0);

	args_run();

	if (g_config->workspace != WORKSPACE_PAINT_3D) {
		base_update_workspace();
	}
	if (g_config->workflow != WORKFLOW_PBR) {
		base_update_workflow();
	}

	g_context->camera_pivot    = g_config->camera_pivot;
	g_context->camera_controls = g_config->camera_controls;

	bool has_projects = g_config->recent_projects->length > 0;
	if (g_config->splash_screen && has_projects) {
		box_projects_show();
	}

	// Startup project
	char *start_arm = string("%s/start.arm", iron_internal_files_location());
	if (iron_file_exists(start_arm)) {
		g_project->_->filepath = start_arm;
		base_start_arm_found   = true;
		args_player            = true;
	}

	if (args_player) {
		// base_player_lock = true;
		g_config->workspace = WORKSPACE_PLAYER;
		base_update_workspace();
		make_material_parse_paint_material(true);
		sys_notify_on_next_frame(&base_init_on_start_arm, NULL);
	}
}

i32 base_w() {
	// Drawing material preview
	if (g_context->material_preview) {
		return util_render_material_preview_size;
	}

	// Drawing decal preview
	if (g_context->decal_preview) {
		return util_render_decal_preview_size;
	}

	if (g_context->paint2d_view) {
		return ui_view2d_ww;
	}

	// 3D view is hidden
	if (!base_view3d_show) {
		return 1;
	}

	i32 res = base_view3d_w();
	return res > 0 ? res : 1; // App was minimized, force render path resize
}

i32 base_view3d_w() {
	i32 res = 0;
	if (g_config->layout == NULL) {
		i32 sidebarw = ui_sidebar_default_w;
		res          = iron_window_width() - sidebarw - ui_toolbar_default_w;
	}
	else if (ui_nodes_show || ui_view2d_show) {
		res = iron_window_width() - g_config->layout->buffer[LAYOUT_SIZE_SIDEBAR_W] - g_config->layout->buffer[LAYOUT_SIZE_NODES_W] - ui_toolbar_w(true);
	}
	else if (ui_base_show) {
		res = iron_window_width() - g_config->layout->buffer[LAYOUT_SIZE_SIDEBAR_W] - ui_toolbar_w(true);
	}
	else { // Distract free
		res = iron_window_width();
	}
	if (g_context->view_index > -1) {
		res = math_ceil(res / 2.0);
	}
	if (g_context->paint2d_view) {
		res = ui_view2d_ww;
	}
	return res;
}

i32 base_h() {
	// Drawing material preview
	if (g_context->material_preview) {
		return util_render_material_preview_size;
	}

	// Drawing decal preview
	if (g_context->decal_preview) {
		return util_render_decal_preview_size;
	}

	i32 res = iron_window_height();

	if (g_config->layout == NULL) {
		res -= ui_header_default_h * 2 + ui_statusbar_default_h;
#if defined(IRON_ANDROID) || defined(IRON_IOS)
		res += ui_header_h;
#endif
	}
	else if (ui_base_show && res > 0) {
		i32 statush = g_config->layout->buffer[LAYOUT_SIZE_STATUS_H];
		res -= math_floor(ui_header_default_h * 2 * g_config->window_scale) + statush;

		if (g_config->layout->buffer[LAYOUT_SIZE_HEADER] == 0) {
			res += ui_header_h * 2;
		}
		if (!base_view3d_show) {
			res += ui_header_h * 4;
		}
	}

	return res > 0 ? res : 1; // App was minimized, force render path resize
}

i32 base_x() {
	return g_context->view_index == 1 ? base_appx + base_w() : base_appx;
}

i32 base_y() {
	return base_appy;
}

void base_on_resize() {
	if (iron_window_width() == 0 || iron_window_height() == 0) {
		return;
	}

	f32 ratio_w             = iron_window_width() / (float)base_last_window_width;
	base_last_window_width  = iron_window_width();
	f32 ratio_h             = iron_window_height() / (float)base_last_window_height;
	base_last_window_height = iron_window_height();

	g_config->layout->buffer[LAYOUT_SIZE_NODES_W]    = math_floor(g_config->layout->buffer[LAYOUT_SIZE_NODES_W] * ratio_w);
	g_config->layout->buffer[LAYOUT_SIZE_SIDEBAR_H0] = math_floor(g_config->layout->buffer[LAYOUT_SIZE_SIDEBAR_H0] * ratio_h);
	g_config->layout->buffer[LAYOUT_SIZE_SIDEBAR_H1] = iron_window_height() - g_config->layout->buffer[LAYOUT_SIZE_SIDEBAR_H0];

	base_resize();
	base_save_window_rect();
}

void base_resize() {
	if (iron_window_width() == 0 || iron_window_height() == 0) {
		return;
	}

	camera_object_t *cam = scene_camera;
	if (cam->data->ortho != NULL) {
		cam->data->ortho->buffer[2] = -2 * (sys_h() / (float)sys_w());
		cam->data->ortho->buffer[3] = 2 * (sys_h() / (float)sys_w());
	}
	camera_object_build_proj(cam, -1.0);
	scene_camera->frame = 0;

	if (g_context->camera_type == CAMERA_TYPE_ORTHOGRAPHIC) {
		viewport_update_camera_type(g_context->camera_type);
	}

	g_context->ddirty = 2;

	if (ui_base_show && base_view3d_show) {
		base_appx = ui_toolbar_w(true);
		base_appy = 0;
		if (g_config->layout->buffer[LAYOUT_SIZE_HEADER] == 1) {
			base_appy = ui_header_h * 2;
		}
	}
	else {
		base_appx = -1;
		base_appy = 0;
	}

	ui_nodes_grid_redraw  = true;
	ui_view2d_grid_redraw = true;

	base_redraw_ui();
}

string_array_t *base_combo_enum_texts(char *node_type) {
	if (string_equals(node_type, "TEX_IMAGE")) {
		if (g_project->_->assets->length > 0) {
			string_array_t *asset_names = any_array_create_from_raw((void *[]){}, 0);
			for (i32 i = 0; i < g_project->_->assets->length; ++i) {
				any_array_push(asset_names, g_project->_->assets->buffer[i]->name);
			}
			return asset_names;
		}
		else {
			string_array_t *empty = any_array_create_from_raw(
			    (void *[]){
			        "",
			    },
			    1);
			return empty;
		}
	}

	if (string_equals(node_type, "LAYER") || string_equals(node_type, "LAYER_MASK")) {
		string_array_t *layer_names = any_array_create_from_raw((void *[]){}, 0);
		for (i32 i = 0; i < g_project->_->layers->length; ++i) {
			slot_layer_t *l = g_project->_->layers->buffer[i];
			any_array_push(layer_names, l->name);
		}
		return layer_names;
	}

	if (string_equals(node_type, "MATERIAL")) {
		string_array_t *material_names = any_array_create_from_raw((void *[]){}, 0);
		for (i32 i = 0; i < g_project->_->materials->length; ++i) {
			slot_material_t *m = g_project->_->materials->buffer[i];
			any_array_push(material_names, m->canvas->name);
		}
		return material_names;
	}

	if (string_equals(node_type, "image_texture_node")) {
		if (g_project->_->assets->length > 0) {
			string_array_t *asset_names = any_array_create_from_raw((void *[]){}, 0);
			for (i32 i = 0; i < g_project->_->assets->length; ++i) {
				any_array_push(asset_names, g_project->_->assets->buffer[i]->name);
			}
			return asset_names;
		}
		else {
			string_array_t *empty = any_array_create_from_raw(
			    (void *[]){
			        "",
			    },
			    1);
			return empty;
		}
	}

	return NULL;
}

any_array_t *base_combo_enum_textures(char *node_type) {
	if (string_equals(node_type, "TEX_IMAGE")) {
		any_array_t *ar = any_array_create(0);
		for (i32 i = 0; i < g_project->_->assets->length; ++i) {
			asset_t       *asset = g_project->_->assets->buffer[i];
			gpu_texture_t *img   = project_get_image(asset);
			any_array_push(ar, img);
		}
		return ar;
	}

	if (string_equals(node_type, "LAYER") || string_equals(node_type, "LAYER_MASK")) {
		any_array_t *ar = any_array_create(0);
		for (i32 i = 0; i < g_project->_->layers->length; ++i) {
			slot_layer_t *l = g_project->_->layers->buffer[i];
			any_array_push(ar, l->texpaint_preview);
		}
		return ar;
	}

	if (string_equals(node_type, "MATERIAL")) {
		any_array_t *ar = any_array_create(0);
		for (i32 i = 0; i < g_project->_->materials->length; ++i) {
			slot_material_t *m = g_project->_->materials->buffer[i];
			any_array_push(ar, m->image_icon);
		}
		return ar;
	}

	if (string_equals(node_type, "image_texture_node")) {
		any_array_t *ar = any_array_create(0);
		for (i32 i = 0; i < g_project->_->assets->length; ++i) {
			asset_t       *asset = g_project->_->assets->buffer[i];
			gpu_texture_t *img   = project_get_image(asset);
			any_array_push(ar, img);
		}
		return ar;
	}

	return NULL;
}

i32 base_get_asset_index(char *file_name) {
	for (i32 i = 0; i < g_project->_->assets->length; ++i) {
		if (string_equals(g_project->_->assets->buffer[i]->name, file_name)) {
			return i;
		}
	}
	return 0;
}

void base_toggle_fullscreen() {
	if (iron_window_get_mode() == IRON_WINDOW_MODE_WINDOW) {
		g_config->window_w = iron_window_width();
		g_config->window_h = iron_window_height();
		g_config->window_x = iron_window_x();
		g_config->window_y = iron_window_y();
		iron_window_change_mode(IRON_WINDOW_MODE_FULLSCREEN);
	}
	else {
		iron_window_change_mode(IRON_WINDOW_MODE_WINDOW);
		iron_window_resize(g_config->window_w, g_config->window_h);
		iron_window_move(g_config->window_x, g_config->window_y);
	}
	g_context->ddirty = 3;
}

bool base_is_decal_layer() {
	bool is_painting = g_context->tool != TOOL_TYPE_MATERIAL;
	return is_painting && g_context->layer->fill_material != NULL && g_context->layer->uv_type == UV_TYPE_PROJECT;
}

void base_redraw_status() {
	ui_base_hwnds->buffer[TAB_AREA_STATUS]->redraws = 2;
}

void base_redraw_console() {
	ui_base_hwnds->buffer[TAB_AREA_STATUS]->redraws = 2;
}

void base_redraw_ui() {
	ui_header_handle->redraws                       = 2;
	ui_base_hwnds->buffer[TAB_AREA_STATUS]->redraws = 2;
	ui_menubar_menu_handle->redraws                 = 2;
	ui_menubar_hwnd->redraws                        = 2;
	ui_nodes_hwnd->redraws                          = 2;
	ui_box_hwnd->redraws                            = 2;
	ui_view2d_hwnd->redraws                         = 2;
	// Redraw viewport
	if (g_context->ddirty < 0) {
		g_context->ddirty = 0;
	}
	ui_base_hwnds->buffer[TAB_AREA_SIDEBAR0]->redraws = 2;
	ui_base_hwnds->buffer[TAB_AREA_SIDEBAR1]->redraws = 2;
	ui_toolbar_handle->redraws                        = 2;
	if (g_context->split_view) {
		g_context->ddirty = 1;
	}
}

void base_update_workspace() {
	config_init_layout();

	if (g_config->workspace == WORKSPACE_PAINT_3D) {
		base_view3d_show  = true;
		ui_menubar_tab->i = 0;
		ui_view2d_show    = false;
		ui_nodes_show     = false;
	}
	else if (g_config->workspace == WORKSPACE_PAINT_2D) {
		base_view3d_show  = false;
		ui_menubar_tab->i = -1;
		ui_view2d_show    = true;
		ui_nodes_show     = false;
	}
	else if (g_config->workspace == WORKSPACE_NODES) {
		base_view3d_show  = false;
		ui_menubar_tab->i = -1;
		ui_view2d_show    = false;
		ui_nodes_show     = true;

		ui_sidebar_show(false);
	}
	else if (g_config->workspace == WORKSPACE_SCRIPT) {
		base_view3d_show  = true;
		ui_menubar_tab->i = 0;
		ui_view2d_show    = false;
		ui_nodes_show     = false;

		ui_base_htabs->buffer[TAB_AREA_STATUS]->i        = 5; // Console
		g_config->layout_tabs->buffer[TAB_AREA_STATUS]   = 5;
		ui_base_htabs->buffer[TAB_AREA_SIDEBAR0]->i      = 2; // Script
		g_config->layout_tabs->buffer[TAB_AREA_SIDEBAR0] = 2;

		g_config->layout->buffer[LAYOUT_SIZE_STATUS_H]  = iron_window_height() * 0.2;
		g_config->layout->buffer[LAYOUT_SIZE_SIDEBAR_W] = iron_window_width() * 0.52;
		float h                                         = UI_ELEMENT_H() + UI_ELEMENT_OFFSET() + 2;
		h += UI_ELEMENT_H() * 3.5; // Small material panel
		g_config->layout->buffer[LAYOUT_SIZE_SIDEBAR_H0] = iron_window_height() - h;
		g_config->layout->buffer[LAYOUT_SIZE_SIDEBAR_H1] = h;
		render_path_resize();
	}
	else if (g_config->workspace == WORKSPACE_PLAYER) {
		ui_base_show = false;
		render_path_resize();
	}

	if (g_config->touch_ui) {
		g_config->layout->buffer[LAYOUT_SIZE_HEADER] = 0;
		if (g_config->workspace == WORKSPACE_PAINT_2D || g_config->workspace == WORKSPACE_PAINT_3D) {
			ui_sidebar_show(true);
			g_config->layout->buffer[LAYOUT_SIZE_SIDEBAR_W] = ui_sidebar_default_w_mini;
			g_config->layout->buffer[LAYOUT_SIZE_SIDEBAR_W] = math_floor(g_config->layout->buffer[LAYOUT_SIZE_SIDEBAR_W] * UI_SCALE());
		}
	}

	base_resize();
}

void base_update_workflow_create_sculpt_layer(void *_) {
	if (history_undo_layers != NULL) {
		sculpt_layers_create_sculpt_layer();
		sys_remove_update(base_update_workflow_create_sculpt_layer);
	}
}

void base_update_workflow_nodes() {
	// Update Material Output nodes
	for (i32 i = 0; i < g_project->_->materials->length; ++i) {
		ui_node_array_t *nodes = g_project->_->materials->buffer[i]->canvas->nodes;
		for (i32 j = 0; j < nodes->length; ++j) {
			if (string_equals(nodes->buffer[j]->type, "OUTPUT_MATERIAL_PBR")) {
				nodes->buffer[j]->inputs->length          = g_config->workflow == WORKFLOW_PBR ? 9 : 2;
				nodes->buffer[j]->inputs->buffer[0]->name = g_config->workflow == WORKFLOW_SCULPT ? tr("Displacement") : tr("Base Color");
			}
		}
	}
}

void base_update_workflow() {
	base_update_workflow_nodes();

	if (g_config->workflow == WORKFLOW_SCULPT) {
		slot_layer_t *first_sculpt = NULL;
		for (i32 i = g_project->_->layers->length - 1; i >= 0; --i) {
			if (g_project->_->layers->buffer[i]->texpaint_sculpt != NULL) {
				first_sculpt = g_project->_->layers->buffer[i];
				break;
			}
		}
		if (first_sculpt == NULL) {
			sys_remove_update(base_update_workflow_create_sculpt_layer);
			sys_notify_on_update(base_update_workflow_create_sculpt_layer, NULL);
		}
		else {
			context_set_layer(first_sculpt);
		}
	}
	else {
		for (i32 i = g_project->_->layers->length - 1; i >= 0; --i) {
			slot_layer_t *l = g_project->_->layers->buffer[i];
			if (l->texpaint_sculpt == NULL && slot_layer_is_layer(l)) {
				context_set_layer(l);
				break;
			}
		}
	}
}

void base_run_in_player() {
	if (string_equals(g_project->_->filepath, "")) {
		console_error(tr("Save project first"));
		return;
	}
	export_arm_run_project();
	char *bin = iron_get_arg(0);
	iron_sys_command(string("%s %s --player", bin, g_project->_->filepath));
	// iron_exec_async()
}

uint32_t base_darker(uint32_t x, uint32_t y) {
	uint32_t r  = ((x >> 16) & 0xff);
	uint32_t g  = ((x >> 8) & 0xff);
	uint32_t b  = ((x) & 0xff);
	uint32_t ry = ((y >> 16) & 0xff);
	uint32_t gy = ((y >> 8) & 0xff);
	uint32_t by = ((y) & 0xff);
	r           = r > ry ? r - ry : 0;
	g           = g > gy ? g - gy : 0;
	b           = b > by ? b - by : 0;
	return (x & 0xff000000) | (r << 16) | (g << 8) | b;
}

string_array_t *base_get_viewport_modes() {
	string_array_t *modes = any_array_create_from_raw(
	    (void *[]){
	        tr("Lit"),
	        tr("Base Color"),
	        tr("Normal"),
	        tr("Occlusion"),
	        tr("Roughness"),
	        tr("Metallic"),
	        tr("Opacity"),
	        tr("Height"),
	        tr("Emission"),
	        tr("Subsurface"),
	        tr("TexCoord"),
	        tr("Object Normal"),
	        tr("Material ID"),
	        tr("Object ID"),
	        tr("Mask"),
	    },
	    15);

	if (gpu_raytrace_supported()) {
		any_array_push(modes, tr("Path Traced"));
	}

	return modes;
}

string_array_t *base_get_viewport_mode_shortcuts() {
	string_array_t *shortcuts = any_array_create_from_raw(
	    (void *[]){
	        "l",
	        "b",
	        "n",
	        "o",
	        "r",
	        "m",
	        "a",
	        "h",
	        "e",
	        "s",
	        "t",
	        "1",
	        "2",
	        "3",
	        "4",
	    },
	    15);

	if (gpu_raytrace_supported()) {
		any_array_push(shortcuts, "p");
	}

	return shortcuts;
}
