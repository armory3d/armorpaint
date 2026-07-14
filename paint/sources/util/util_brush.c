
#include "../global.h"

void util_brush_update() {
	bool set_clone_source =
	    g_context->tool == TOOL_TYPE_CLONE &&
	    operator_shortcut(string("%s+%s", any_map_get(g_keymap, "set_clone_source"), any_map_get(g_keymap, "action_paint")), SHORTCUT_TYPE_DOWN);

	bool decal_mask = context_is_decal_mask_paint();

	bool down = operator_shortcut(any_map_get(g_keymap, "action_paint"), SHORTCUT_TYPE_DOWN) || decal_mask || set_clone_source ||
	            operator_shortcut(string("%s+%s", any_map_get(g_keymap, "brush_ruler"), any_map_get(g_keymap, "action_paint")), SHORTCUT_TYPE_DOWN) ||
	            (pen_down("tip") && !keyboard_down("alt"));

	if (g_config->touch_ui) {
		if (pen_down("tip")) {
			g_context->pen_painting_only = true;
		}
		else if (g_context->pen_painting_only) {
			down = false;
		}
	}

	if (g_context->tool == TOOL_TYPE_PARTICLE) {
		down = false;
	}

	if (g_context->tool == TOOL_TYPE_SELECT) {
		down = false;
	}

	if (down) {
		i32 mx = mouse_view_x();
		i32 my = mouse_view_y();
		i32 ww = sys_w();

		if (g_context->paint2d) {
			mx -= sys_w();
			ww = ui_view2d_ww;
		}

		if (mx < ww && mx > sys_x() && my < sys_h() && my > sys_y()) {

			if (set_clone_source) {
				g_context->clone_start_x = mx;
				g_context->clone_start_y = my;
			}
			else {
				if (g_context->brush_time == 0 && !base_is_dragging && !base_is_resizing && g_ui->combo_selected_handle == NULL) { // Paint started

					// Draw line
					if (operator_shortcut(string("%s+%s", any_map_get(g_keymap, "brush_ruler"), any_map_get(g_keymap, "action_paint")), SHORTCUT_TYPE_DOWN)) {
						g_context->last_paint_vec_x = g_context->last_paint_x;
						g_context->last_paint_vec_y = g_context->last_paint_y;
					}

					history_push_undo = true;

					if (g_context->tool == TOOL_TYPE_CLONE && g_context->clone_start_x >= 0.0) { // Clone delta
						g_context->clone_delta_x = (g_context->clone_start_x - mx) / (float)ww;
						g_context->clone_delta_y = (g_context->clone_start_y - my) / (float)sys_h();
						g_context->clone_start_x = -1;
					}
					else if (g_context->tool == TOOL_TYPE_FILL && g_context->fill_type == FILL_TYPE_UV_ISLAND) {
						util_uv_uvislandmap_cached = false;
					}
				}

				g_context->brush_time += sys_delta();
				brush_output_node_run();
			}
		}
	}
	else if (g_context->brush_time > 0) { // Brush released
		g_context->brush_time          = 0;
		g_context->prev_paint_vec_x    = -1;
		g_context->prev_paint_vec_y    = -1;
		g_context->brush_blend_dirty   = true; // Update brush mask
		g_context->layer_preview_dirty = true; // Update layer preview

		// New color id picked, update fill layer
		if (g_context->tool == TOOL_TYPE_COLORID && g_context->layer->fill_material != NULL) {
			sys_notify_on_next_frame(&ui_base_update_ui_on_next_frame, NULL);
		}
	}
}
