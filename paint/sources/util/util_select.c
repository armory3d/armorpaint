
#include "../global.h"

// Mouse position in layer texture uv space of the 2D view, rounded to pixel edges
static void util_select_uv_2d(f32 *u, f32 *v) {
	f32 res_x = config_get_texture_res_x();
	f32 res_y = config_get_texture_res_y();
	f32 tx, ty, tw, th;
	ui_view2d_tex_rect(res_y / res_x, &tx, &ty, &tw, &th);
	f32 x = (mouse_x - ui_view2d_wx - tx) / tw;
	f32 y = (mouse_y - ui_view2d_wy - ty) / th;
	*u    = math_round(math_max(0.0, math_min(1.0, x)) * res_x) / res_x;
	*v    = math_round(math_max(0.0, math_min(1.0, y)) * res_y) / res_y;
}

static bool util_select_in_2d_view() {
	if (!context_in_2d_view(VIEW_2D_TYPE_LAYER) || g_context->layer->texpaint_sculpt != NULL) {
		return false;
	}
	// Header
	if (ui_base_show && mouse_y < ui_view2d_wy + UI_ELEMENT_H() * 1.4) {
		return false;
	}
	// The toolbar floats in the 2D view when there is no 3D view
	if (!base_view3d_show && mouse_x < ui_view2d_wx + ui_toolbar_x() + ui_toolbar_w(false)) {
		return false;
	}
	return true;
}

void util_select_update() {
	if (g_context->tool == TOOL_TYPE_SELECT) {
		if (base_ui_enabled && !base_is_dragging && !base_is_resizing) {
			f32  mx          = mouse_view_x();
			f32  my          = mouse_view_y();
			bool in_viewport = context_in_3d_view();
			bool in_2d       = util_select_in_2d_view();

			if (context_is_floating_toolbar() && mx < ui_toolbar_x() + ui_toolbar_w(false)) {
				in_viewport = false;
			}

			if (mouse_started("left") && (in_viewport || in_2d)) {
				g_context->select_dragging = true;
				if (in_2d) {
					util_select_uv_2d(&g_context->select_start_x, &g_context->select_start_y);
				}
				else {
					g_context->select_start_x = mx / (float)sys_w();
					g_context->select_start_y = my / (float)sys_h();
				}
				g_context->select_x1 = g_context->select_start_x;
				g_context->select_y1 = g_context->select_start_y;
				g_context->select_x2 = g_context->select_start_x;
				g_context->select_y2 = g_context->select_start_y;
				// A new selection in either view replaces the other one
				if (g_context->select_2d != in_2d) {
					bool was_active          = g_context->select_active;
					g_context->select_2d     = in_2d;
					g_context->select_active = false;
					if (was_active) {
						make_material_parse_paint_material(false);
					}
				}
			}

			if (g_context->select_dragging && mouse_down("left")) {
				f32 cx, cy;
				if (g_context->select_2d) {
					util_select_uv_2d(&cx, &cy);
				}
				else {
					cx = math_max(0.0, math_min(1.0, mx / (float)sys_w()));
					cy = math_max(0.0, math_min(1.0, my / (float)sys_h()));
				}
				g_context->select_x1 = math_min(g_context->select_start_x, cx);
				g_context->select_y1 = math_min(g_context->select_start_y, cy);
				g_context->select_x2 = math_max(g_context->select_start_x, cx);
				g_context->select_y2 = math_max(g_context->select_start_y, cy);
				if (g_context->select_2d) {
					ui_view2d_hwnd->redraws = 2;
				}
			}

			if (g_context->select_dragging && mouse_released("left")) {
				g_context->select_dragging = false;
				bool was_active            = g_context->select_active;
				// Drag to enable, click to cancel
				if (g_context->select_2d) {
					// At least one pixel
					g_context->select_active = g_context->select_x2 > g_context->select_x1 && g_context->select_y2 > g_context->select_y1;
					ui_view2d_hwnd->redraws  = 2;
				}
				else {
					f32 dx                   = math_abs(g_context->select_x2 - g_context->select_x1) * sys_w();
					f32 dy                   = math_abs(g_context->select_y2 - g_context->select_y1) * sys_h();
					g_context->select_active = dx > 3 && dy > 3;
				}
				if (was_active != g_context->select_active) {
					make_material_parse_paint_material(false);
				}
			}
		}
	}
	else {
		g_context->select_dragging = false;
	}
}
