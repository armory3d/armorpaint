
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

static void util_select_cursor(bool in_2d, f32 mx, f32 my, f32 *x, f32 *y) {
	if (in_2d) {
		util_select_uv_2d(x, y);
	}
	else {
		*x = math_max(0.0, math_min(1.0, mx / (float)sys_w()));
		*y = math_max(0.0, math_min(1.0, my / (float)sys_h()));
	}
}

static void util_select_mask_size(i32 *w, i32 *h) {
	if (g_context->select_2d) {
		*w      = config_get_texture_res_x();
		*h      = config_get_texture_res_y();
		f32 max = math_max(*w, *h);
		if (max > 4096) {
			*w = math_max(1, math_round(*w * 4096 / max));
			*h = math_max(1, math_round(*h * 4096 / max));
		}
	}
	else {
		*w = sys_w();
		*h = sys_h();
	}
}

static void util_select_build_mask() {
	f32_array_t *pts = g_context->select_points;
	i32          n   = pts->length / 2;
	i32          w, h;
	util_select_mask_size(&w, &h);
	buffer_t *bytes = buffer_create(w * h);
	memset(bytes->buffer, 0, w * h);
	f32_array_t *xs = f32_array_create(0);
	for (i32 y = 0; y < h; ++y) {
		f32 py     = (y + 0.5) / (float)h;
		xs->length = 0;
		for (i32 i = 0; i < n; ++i) {
			i32 j  = (i + 1) % n;
			f32 x0 = pts->buffer[i * 2];
			f32 y0 = pts->buffer[i * 2 + 1];
			f32 x1 = pts->buffer[j * 2];
			f32 y1 = pts->buffer[j * 2 + 1];
			if ((y0 <= py && y1 > py) || (y1 <= py && y0 > py)) {
				f32_array_push(xs, (x0 + (py - y0) / (y1 - y0) * (x1 - x0)) * w);
			}
		}
		for (i32 i = 1; i < xs->length; ++i) {
			f32 v = xs->buffer[i];
			i32 k = i - 1;
			while (k >= 0 && xs->buffer[k] > v) {
				xs->buffer[k + 1] = xs->buffer[k];
				--k;
			}
			xs->buffer[k + 1] = v;
		}
		for (i32 i = 0; i + 1 < xs->length; i += 2) {
			i32 a = math_max(0, math_ceil(xs->buffer[i] - 0.5));
			i32 b = math_min(w - 1, math_ceil(xs->buffer[i + 1] - 0.5) - 1);
			for (i32 x = a; x <= b; ++x) {
				bytes->buffer[y * w + x] = 255;
			}
		}
	}
	if (g_context->select_mask_image != NULL) {
		gpu_delete_texture(g_context->select_mask_image);
	}
	g_context->select_mask_image = gpu_create_texture_from_bytes(bytes, w, h, GPU_TEXTURE_FORMAT_R8);
	array_free(xs);
	free(xs);
	array_free(bytes);
	free(bytes);
}

static void util_select_set_view(bool in_2d) {
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

static bool util_select_large_enough() {
	if (g_context->select_2d) {
		// At least one pixel
		return g_context->select_x2 > g_context->select_x1 && g_context->select_y2 > g_context->select_y1;
	}
	f32 dx = math_abs(g_context->select_x2 - g_context->select_x1) * sys_w();
	f32 dy = math_abs(g_context->select_y2 - g_context->select_y1) * sys_h();
	return dx > 3 && dy > 3;
}

static f32 util_select_screen_dist(f32 x0, f32 y0, f32 x1, f32 y1) {
	f32 sx = 1.0;
	f32 sy = 1.0;
	if (g_context->select_2d) {
		f32 tx, ty;
		ui_view2d_tex_rect(config_get_texture_res_y() / (float)config_get_texture_res_x(), &tx, &ty, &sx, &sy);
	}
	else {
		sx = sys_w();
		sy = sys_h();
	}
	return math_sqrt((x1 - x0) * sx * (x1 - x0) * sx + (y1 - y0) * sy * (y1 - y0) * sy);
}

static void util_select_polygon_close() {
	g_context->select_dragging = false;
	f32_array_t *pts           = g_context->select_points;
	bool         was_active    = g_context->select_active;
	g_context->select_active   = false;
	if (pts->length >= 6) {
		f32 x1 = 1.0;
		f32 y1 = 1.0;
		f32 x2 = 0.0;
		f32 y2 = 0.0;
		for (i32 i = 0; i < pts->length; i += 2) {
			x1 = math_min(x1, pts->buffer[i]);
			y1 = math_min(y1, pts->buffer[i + 1]);
			x2 = math_max(x2, pts->buffer[i]);
			y2 = math_max(y2, pts->buffer[i + 1]);
		}
		g_context->select_x1 = x1;
		g_context->select_y1 = y1;
		g_context->select_x2 = x2;
		g_context->select_y2 = y2;
		if (util_select_large_enough()) {
			util_select_build_mask();
			g_context->select_active = true;
		}
	}
	if (was_active != g_context->select_active) {
		make_material_parse_paint_material(false);
	}
	ui_view2d_hwnd->redraws = 2;
}

static void util_select_update_polygon(f32 mx, f32 my, bool in_viewport, bool in_2d) {
	if (g_context->select_points == NULL) {
		g_context->select_points = f32_array_create(0);
	}
	f32_array_t *pts = g_context->select_points;

	if (g_context->select_dragging) {
		if (keyboard_started("escape") || mouse_started("right")) {
			g_context->select_dragging = false;
			ui_view2d_hwnd->redraws    = 2;
			return;
		}
		if (keyboard_started("enter")) {
			util_select_polygon_close();
			return;
		}
		util_select_cursor(g_context->select_2d, mx, my, &g_context->select_cursor_x, &g_context->select_cursor_y);
		if (g_context->select_2d) {
			ui_view2d_hwnd->redraws = 2;
		}
	}

	if (!mouse_started("left") || !(in_viewport || in_2d)) {
		return;
	}

	if (!g_context->select_dragging || g_context->select_2d != in_2d) {
		util_select_set_view(in_2d);
		if (g_context->select_active) {
			g_context->select_active = false;
			make_material_parse_paint_material(false);
		}
		g_context->select_dragging = true;
		pts->length                = 0;
	}

	f32 cx, cy;
	util_select_cursor(in_2d, mx, my, &cx, &cy);
	g_context->select_cursor_x = cx;
	g_context->select_cursor_y = cy;
	f32 snap                   = 8.0 * UI_SCALE();
	i32 n                      = pts->length / 2;
	f64 time                   = sys_time();
	if (n >= 3 && util_select_screen_dist(cx, cy, pts->buffer[0], pts->buffer[1]) < snap) {
		util_select_polygon_close();
	}
	else if (n >= 3 && time - g_context->select_click_time < 0.3 &&
	         util_select_screen_dist(cx, cy, pts->buffer[(n - 1) * 2], pts->buffer[(n - 1) * 2 + 1]) < snap) {
		util_select_polygon_close();
	}
	else if (n == 0 || util_select_screen_dist(cx, cy, pts->buffer[(n - 1) * 2], pts->buffer[(n - 1) * 2 + 1]) > 0) {
		f32_array_push(pts, cx);
		f32_array_push(pts, cy);
	}
	g_context->select_click_time = time;
	ui_view2d_hwnd->redraws      = 2;
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

			if (g_context->select_mode == SELECT_MODE_POLYGON) {
				util_select_update_polygon(mx, my, in_viewport, in_2d);
				return;
			}

			if (mouse_started("left") && (in_viewport || in_2d)) {
				g_context->select_dragging = true;
				util_select_cursor(in_2d, mx, my, &g_context->select_start_x, &g_context->select_start_y);
				g_context->select_x1 = g_context->select_start_x;
				g_context->select_y1 = g_context->select_start_y;
				g_context->select_x2 = g_context->select_start_x;
				g_context->select_y2 = g_context->select_start_y;
				util_select_set_view(in_2d);
			}

			if (g_context->select_dragging && mouse_down("left")) {
				f32 cx, cy;
				util_select_cursor(g_context->select_2d, mx, my, &cx, &cy);
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
				g_context->select_active = util_select_large_enough();
				if (g_context->select_2d) {
					ui_view2d_hwnd->redraws = 2;
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

void util_select_set_mode(select_mode_t mode) {
	g_context->select_mode     = mode;
	g_context->select_dragging = false;
	if (g_context->select_active) {
		g_context->select_active = false;
		make_material_parse_paint_material(false);
	}
	ui_view2d_hwnd->redraws = 2;
}

void util_select_shader_mask(node_shader_t *kong, char *p) {
	node_shader_add_constant(kong, "float4 select_mask", "_select_mask");
	node_shader_write_frag(kong, string_tmp("float2 select_p = %s;", p));
	if (g_context->select_mode == SELECT_MODE_ELLIPSE) {
		node_shader_write_frag(kong, "float2 select_c = (constants.select_mask.xy + constants.select_mask.zw) * 0.5;");
		node_shader_write_frag(kong, "float2 select_r = max((constants.select_mask.zw - constants.select_mask.xy) * 0.5, float2(0.000001, 0.000001));");
		node_shader_write_frag(kong, "float2 select_d = (select_p - select_c) / select_r;");
		node_shader_write_frag(kong, "if (dot(select_d, select_d) > 1.0) { discard; }");
		return;
	}
	node_shader_write_frag(kong, "if (select_p.x < constants.select_mask.x || select_p.x > constants.select_mask.z || select_p.y < constants.select_mask.y || "
	                             "select_p.y > constants.select_mask.w) { discard; }");
	if (g_context->select_mode == SELECT_MODE_POLYGON) {
		node_shader_add_texture(kong, "texselectmask", "_texselectmask");
		node_shader_write_frag(kong, "if (sample_lod(texselectmask, sampler_linear, select_p, 0.0).r < 0.5) { discard; }");
	}
}
