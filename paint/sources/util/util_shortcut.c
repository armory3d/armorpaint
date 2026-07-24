
#include "../global.h"

static f32 util_shortcut_undo_tap_time = 0.0;
static f32 util_shortcut_redo_tap_time = 0.0;

void util_shortcut_undo_redo() {
	bool undo_pressed = operator_shortcut(any_map_get(g_keymap, "edit_undo"), SHORTCUT_TYPE_STARTED);
	bool redo_pressed = operator_shortcut(any_map_get(g_keymap, "edit_redo"), SHORTCUT_TYPE_STARTED) || (keyboard_down("control") && keyboard_started("y"));

	// Two-finger tap to undo, three-finger tap to redo
	if (context_in_3d_view() && g_config->touch_ui) {
		if (mouse_started("middle")) {
			util_shortcut_redo_tap_time = sys_time();
		}
		else if (mouse_started("right")) {
			util_shortcut_undo_tap_time = sys_time();
		}
		else if (mouse_released("middle") && sys_time() - util_shortcut_redo_tap_time < 0.1) {
			util_shortcut_redo_tap_time = util_shortcut_undo_tap_time = 0;
			redo_pressed                                              = true;
		}
		else if (mouse_released("right") && sys_time() - util_shortcut_undo_tap_time < 0.1) {
			util_shortcut_redo_tap_time = util_shortcut_undo_tap_time = 0;
			undo_pressed                                              = true;
		}
	}

	if (undo_pressed) {
		history_undo();
	}
	else if (redo_pressed) {
		history_redo();
	}
}

void util_shortcut_global() {
	if (!g_ui->is_typing) {
		if (operator_shortcut(any_map_get(g_keymap, "toggle_node_editor"), SHORTCUT_TYPE_STARTED)) {
			ui_nodes_canvas_type == CANVAS_TYPE_MATERIAL ? ui_base_show_material_nodes() : ui_base_show_brush_nodes();
		}
		else if (operator_shortcut(any_map_get(g_keymap, "toggle_browser"), SHORTCUT_TYPE_STARTED)) {
			ui_base_toggle_browser();
		}
		else if (operator_shortcut(any_map_get(g_keymap, "toggle_2d_view"), SHORTCUT_TYPE_STARTED)) {
			ui_base_show_2d_view(VIEW_2D_TYPE_LAYER);
		}
	}

	if (operator_shortcut(any_map_get(g_keymap, "file_save_as"), SHORTCUT_TYPE_STARTED)) {
		project_save_as(false);
	}
	else if (operator_shortcut(any_map_get(g_keymap, "file_save"), SHORTCUT_TYPE_STARTED)) {
		project_save(false);
	}
	else if (operator_shortcut(any_map_get(g_keymap, "file_open"), SHORTCUT_TYPE_STARTED)) {
		project_open();
	}
	else if (operator_shortcut(any_map_get(g_keymap, "file_open_recent"), SHORTCUT_TYPE_STARTED)) {
		box_projects_show();
	}
	else if (operator_shortcut(any_map_get(g_keymap, "file_reimport_mesh"), SHORTCUT_TYPE_STARTED)) {
		project_reimport_mesh();
	}
	else if (operator_shortcut(any_map_get(g_keymap, "file_reimport_textures"), SHORTCUT_TYPE_STARTED)) {
		project_reimport_textures();
	}
	else if (operator_shortcut(any_map_get(g_keymap, "file_new"), SHORTCUT_TYPE_STARTED)) {
		project_new_box();
	}
	else if (operator_shortcut(any_map_get(g_keymap, "file_export_textures"), SHORTCUT_TYPE_STARTED)) {
		if (string_equals(g_context->texture_export_path, "")) { // First export, ask for path
			g_context->layers_export = EXPORT_MODE_VISIBLE;
			box_export_show_textures();
		}
		else {
			sys_notify_on_next_frame(&ui_base_update_next_frame, NULL);
		}
	}
	else if (operator_shortcut(any_map_get(g_keymap, "file_export_textures_as"), SHORTCUT_TYPE_STARTED)) {
		g_context->layers_export = EXPORT_MODE_VISIBLE;
		box_export_show_textures();
	}
	else if (operator_shortcut(any_map_get(g_keymap, "file_import_assets"), SHORTCUT_TYPE_STARTED)) {
		project_import_asset(NULL, true);
	}
	else if (operator_shortcut(any_map_get(g_keymap, "edit_prefs"), SHORTCUT_TYPE_STARTED)) {
		box_preferences_show();
	}
	if (keyboard_started(any_map_get(g_keymap, "view_distract_free")) || (keyboard_started("escape") && !ui_base_show && !ui_box_show)) {
		bool allow = true;
#ifdef IRON_WASM
		allow = g_config->workspace != WORKSPACE_PLAYER;
#endif
		if (allow) {
			ui_base_toggle_distract_free();
		}
	}
	if (keyboard_started("f5") && g_config->workspace != WORKSPACE_PLAYER) {
		// g_config->workspace = WORKSPACE_PLAYER;
		// base_update_workspace();
		base_run_in_player();
	}

#ifdef IRON_LINUX
	if (g_ui->is_alt_down && g_ui->is_key_pressed && g_ui->key_code == KEY_CODE_RETURN) {
		base_toggle_fullscreen();
	}
#endif
}

void util_shortcut_brush() {
	if (!g_context->brush_locked) {
		return;
	}

	bool decal_mask = context_is_decal_mask();

	bool adjusting =
	    operator_shortcut(any_map_get(g_keymap, "brush_radius"), SHORTCUT_TYPE_DOWN) ||
	    operator_shortcut(any_map_get(g_keymap, "brush_opacity"), SHORTCUT_TYPE_DOWN) ||
	    operator_shortcut(any_map_get(g_keymap, "brush_angle"), SHORTCUT_TYPE_DOWN) ||
	    (decal_mask && operator_shortcut(string("%s+%s", any_map_get(g_keymap, "decal_mask"), any_map_get(g_keymap, "brush_radius")), SHORTCUT_TYPE_DOWN));

	if (!adjusting) {
		iron_mouse_unlock();
		g_context->last_paint_x = -1;
		g_context->last_paint_y = -1;
		g_context->brush_locked = false;
		return;
	}

	if (!mouse_moved) {
		return;
	}

	if (operator_shortcut(any_map_get(g_keymap, "brush_opacity"), SHORTCUT_TYPE_DOWN)) {
		g_context->brush_opacity += mouse_movement_x / 500.0;
		g_context->brush_opacity = math_max(0.0, math_min(1.0, g_context->brush_opacity));
		g_context->brush_opacity = math_round(g_context->brush_opacity * 100) / 100.0;
	}
	else if (operator_shortcut(any_map_get(g_keymap, "brush_angle"), SHORTCUT_TYPE_DOWN)) {
		g_context->brush_angle += mouse_movement_x / 5.0;
		i32 i                  = math_floor(g_context->brush_angle);
		g_context->brush_angle = i % 360;
		if (g_context->brush_angle < 0)
			g_context->brush_angle += 360;
		make_material_parse_paint_material(true);
	}
	else if (decal_mask && operator_shortcut(string("%s+%s", any_map_get(g_keymap, "decal_mask"), any_map_get(g_keymap, "brush_radius")), SHORTCUT_TYPE_DOWN)) {
		g_context->brush_decal_mask_radius += mouse_movement_x / 150.0;
		g_context->brush_decal_mask_radius = math_max(0.01, math_min(4.0, g_context->brush_decal_mask_radius));
		g_context->brush_decal_mask_radius = math_round(g_context->brush_decal_mask_radius * 100) / 100.0;
	}
	else {
		g_context->brush_radius += mouse_movement_x / 150.0;
		g_context->brush_radius = math_max(0.01, math_min(4.0, g_context->brush_radius));
		g_context->brush_radius = math_round(g_context->brush_radius * 100) / 100.0;
	}
	ui_header_handle->redraws = 2;
}

void util_shortcut_viewport() {
	bool decal_mask = context_is_decal_mask();

	bool is_typing = g_ui->is_typing;
	if (!is_typing) {
		if (operator_shortcut(any_map_get(g_keymap, "select_material"), SHORTCUT_TYPE_DOWN)) {
			ui_base_hwnds->buffer[TAB_AREA_SIDEBAR1]->redraws = 2;
			for (i32 i = 1; i < 10; ++i) {
				if (keyboard_started(i32_to_string(i))) {
					context_select_material(i - 1);
				}
			}
		}
		else if (operator_shortcut(any_map_get(g_keymap, "select_layer"), SHORTCUT_TYPE_DOWN)) {
			ui_base_hwnds->buffer[TAB_AREA_SIDEBAR0]->redraws = 2;
			for (i32 i = 1; i < 10; ++i) {
				if (keyboard_started(i32_to_string(i))) {
					context_select_layer(i - 1);
				}
			}
		}
	}

	// Viewport shortcuts
	if (context_in_paint_area() && !is_typing) {

		if (!mouse_down("right")) { // Fly mode off
			if (operator_shortcut(any_map_get(g_keymap, "tool_brush"), SHORTCUT_TYPE_STARTED)) {
				context_select_tool(TOOL_TYPE_BRUSH);
			}
			else if (operator_shortcut(any_map_get(g_keymap, "tool_eraser"), SHORTCUT_TYPE_STARTED)) {
				context_select_tool(TOOL_TYPE_ERASER);
			}
			else if (operator_shortcut(any_map_get(g_keymap, "tool_fill"), SHORTCUT_TYPE_STARTED)) {
				context_select_tool(TOOL_TYPE_FILL);
			}
			else if (operator_shortcut(any_map_get(g_keymap, "tool_colorid"), SHORTCUT_TYPE_STARTED)) {
				context_select_tool(TOOL_TYPE_COLORID);
			}
			else if (operator_shortcut(any_map_get(g_keymap, "tool_decal"), SHORTCUT_TYPE_STARTED)) {
				context_select_tool(TOOL_TYPE_DECAL);
			}
			else if (operator_shortcut(any_map_get(g_keymap, "tool_text"), SHORTCUT_TYPE_STARTED)) {
				context_select_tool(TOOL_TYPE_TEXT);
			}
			else if (operator_shortcut(any_map_get(g_keymap, "tool_clone"), SHORTCUT_TYPE_STARTED)) {
				context_select_tool(TOOL_TYPE_CLONE);
			}
			else if (operator_shortcut(any_map_get(g_keymap, "tool_blur"), SHORTCUT_TYPE_STARTED)) {
				context_select_tool(TOOL_TYPE_BLUR);
			}
			else if (operator_shortcut(any_map_get(g_keymap, "tool_particle"), SHORTCUT_TYPE_STARTED)) {
				context_select_tool(TOOL_TYPE_PARTICLE);
			}
			else if (operator_shortcut(any_map_get(g_keymap, "tool_picker"), SHORTCUT_TYPE_STARTED)) {
				context_select_tool(TOOL_TYPE_PICKER);
			}
			else if (operator_shortcut(any_map_get(g_keymap, "tool_material"), SHORTCUT_TYPE_STARTED)) {
				context_select_tool(TOOL_TYPE_MATERIAL);
			}
			else if (operator_shortcut(any_map_get(g_keymap, "tool_cursor"), SHORTCUT_TYPE_STARTED)) {
				context_select_tool(TOOL_TYPE_CURSOR);
			}
			else if (operator_shortcut(any_map_get(g_keymap, "tool_select"), SHORTCUT_TYPE_STARTED)) {
				context_select_tool(TOOL_TYPE_SELECT);
			}
			else if (operator_shortcut(any_map_get(g_keymap, "swap_brush_eraser"), SHORTCUT_TYPE_STARTED)) {
				context_select_tool(g_context->tool == TOOL_TYPE_BRUSH ? TOOL_TYPE_ERASER : TOOL_TYPE_BRUSH);
			}
		}

		// Radius
		if (g_context->tool == TOOL_TYPE_BRUSH || g_context->tool == TOOL_TYPE_ERASER || g_context->tool == TOOL_TYPE_DECAL ||
		    g_context->tool == TOOL_TYPE_TEXT || g_context->tool == TOOL_TYPE_CLONE || g_context->tool == TOOL_TYPE_BLUR ||
		    g_context->tool == TOOL_TYPE_PARTICLE) {
			if (operator_shortcut(any_map_get(g_keymap, "brush_radius"), SHORTCUT_TYPE_STARTED) ||
			    operator_shortcut(any_map_get(g_keymap, "brush_opacity"), SHORTCUT_TYPE_STARTED) ||
			    operator_shortcut(any_map_get(g_keymap, "brush_angle"), SHORTCUT_TYPE_STARTED) ||
			    (decal_mask &&
			     operator_shortcut(string("%s+%s", any_map_get(g_keymap, "decal_mask"), any_map_get(g_keymap, "brush_radius")), SHORTCUT_TYPE_STARTED))) {
				g_context->brush_locked = true;
				if (!pen_connected) {
					iron_mouse_lock();
				}
			}
			else if (operator_shortcut(any_map_get(g_keymap, "brush_radius_decrease"), SHORTCUT_TYPE_REPEAT)) {
				g_context->brush_radius -= ui_base_get_radius_increment();
				g_context->brush_radius   = math_max(math_round(g_context->brush_radius * 100) / 100.0, 0.01);
				ui_header_handle->redraws = 2;
			}
			else if (operator_shortcut(any_map_get(g_keymap, "brush_radius_increase"), SHORTCUT_TYPE_REPEAT)) {
				g_context->brush_radius += ui_base_get_radius_increment();
				g_context->brush_radius   = math_round(g_context->brush_radius * 100) / 100.0;
				ui_header_handle->redraws = 2;
			}
			else if (decal_mask) {
				if (operator_shortcut(string("%s+%s", any_map_get(g_keymap, "decal_mask"), any_map_get(g_keymap, "brush_radius_decrease")),
				                      SHORTCUT_TYPE_REPEAT)) {
					g_context->brush_decal_mask_radius -= ui_base_get_radius_increment();
					g_context->brush_decal_mask_radius = math_max(math_round(g_context->brush_decal_mask_radius * 100) / 100.0, 0.01);
					ui_header_handle->redraws          = 2;
				}
				else if (operator_shortcut(string("%s+%s", any_map_get(g_keymap, "decal_mask"), any_map_get(g_keymap, "brush_radius_increase")),
				                           SHORTCUT_TYPE_REPEAT)) {
					g_context->brush_decal_mask_radius += ui_base_get_radius_increment();
					g_context->brush_decal_mask_radius = math_round(g_context->brush_decal_mask_radius * 100) / 100.0;
					ui_header_handle->redraws          = 2;
				}
			}
		}
		if (decal_mask && (operator_shortcut(any_map_get(g_keymap, "decal_mask"), SHORTCUT_TYPE_STARTED) ||
		                   operator_shortcut(any_map_get(g_keymap, "decal_mask"), SHORTCUT_TYPE_RELEASED))) {
			ui_header_handle->redraws = 2;
		}

		// Viewpoint
		if (mouse_view_x() < sys_w()) {
			if (operator_shortcut(any_map_get(g_keymap, "view_reset"), SHORTCUT_TYPE_STARTED)) {
				viewport_reset();
				viewport_scale_to_bounds(2.0);
			}
			else if (operator_shortcut(any_map_get(g_keymap, "view_back"), SHORTCUT_TYPE_STARTED)) {
				viewport_set_view(0, 1, 0, math_pi() / 2.0, 0, math_pi());
			}
			else if (operator_shortcut(any_map_get(g_keymap, "view_front"), SHORTCUT_TYPE_STARTED)) {
				viewport_set_view(0, -1, 0, math_pi() / 2.0, 0, 0);
			}
			else if (operator_shortcut(any_map_get(g_keymap, "view_left"), SHORTCUT_TYPE_STARTED)) {
				viewport_set_view(-1, 0, 0, math_pi() / 2.0, 0, -math_pi() / 2.0);
			}
			else if (operator_shortcut(any_map_get(g_keymap, "view_right"), SHORTCUT_TYPE_STARTED)) {
				viewport_set_view(1, 0, 0, math_pi() / 2.0, 0, math_pi() / 2.0);
			}
			else if (operator_shortcut(any_map_get(g_keymap, "view_bottom"), SHORTCUT_TYPE_STARTED)) {
				viewport_set_view(0, 0, -1, math_pi(), 0, math_pi());
			}
			else if (operator_shortcut(any_map_get(g_keymap, "view_camera_type"), SHORTCUT_TYPE_STARTED)) {
				g_context->camera_type = g_context->camera_type == CAMERA_TYPE_PERSPECTIVE ? CAMERA_TYPE_ORTHOGRAPHIC : CAMERA_TYPE_PERSPECTIVE;
				viewport_update_camera_type(g_context->camera_type);
			}
			else if (operator_shortcut(any_map_get(g_keymap, "view_orbit_left"), SHORTCUT_TYPE_REPEAT)) {
				viewport_orbit(-math_pi() / 12.0, 0);
			}
			else if (operator_shortcut(any_map_get(g_keymap, "view_orbit_right"), SHORTCUT_TYPE_REPEAT)) {
				viewport_orbit(math_pi() / 12.0, 0);
			}
			else if (operator_shortcut(any_map_get(g_keymap, "view_orbit_up"), SHORTCUT_TYPE_REPEAT)) {
				viewport_orbit(0, -math_pi() / 12.0);
			}
			else if (operator_shortcut(any_map_get(g_keymap, "view_orbit_down"), SHORTCUT_TYPE_REPEAT)) {
				viewport_orbit(0, math_pi() / 12.0);
			}
			else if (operator_shortcut(any_map_get(g_keymap, "view_orbit_opposite"), SHORTCUT_TYPE_STARTED)) {
				viewport_orbit_opposite();
			}
			else if (operator_shortcut(any_map_get(g_keymap, "view_zoom_in"), SHORTCUT_TYPE_REPEAT)) {
				viewport_zoom(0.2);
			}
			else if (operator_shortcut(any_map_get(g_keymap, "view_zoom_out"), SHORTCUT_TYPE_REPEAT)) {
				viewport_zoom(-0.2);
			}
			else if (operator_shortcut(any_map_get(g_keymap, "viewport_mode"), SHORTCUT_TYPE_STARTED)) {
				g_ui->is_key_pressed = false;
				ui_menu_draw(&ui_base_menu_draw_viewport_mode, -1, -1);
			}
		}
		if (operator_shortcut(any_map_get(g_keymap, "operator_search"), SHORTCUT_TYPE_STARTED)) {
			ui_base_operator_search();
		}
	}
}
