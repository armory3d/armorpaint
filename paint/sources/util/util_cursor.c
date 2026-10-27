
#include "../global.h"

void util_cursor_draw_2d(f32 cx, f32 cy, f32 radius, i32 color) {
	f32 half = math_min(1.5 * UI_SCALE(), math_max(0.2, radius * 0.06));
	f32 r    = radius + half + 1.0;
	draw_set_pipeline(pipes_cursor_2d);
	gpu_set_float2(pipes_cursor_2d_center, cx, cy);
	gpu_set_float2(pipes_cursor_2d_size, _draw_current != NULL ? _draw_current->width : iron_window_width(),
	               _draw_current != NULL ? _draw_current->height : iron_window_height());
	gpu_set_float(pipes_cursor_2d_radius, radius);
	gpu_set_float(pipes_cursor_2d_width, half * 2.0);
	draw_set_color(color);
	draw_filled_rect(cx - r, cy - r, r * 2.0, r * 2.0);
	draw_set_pipeline(NULL);
	draw_set_color(0xffffffff);
}

void util_cursor_render(void *_) {
	if (!base_ui_enabled) {
		return;
	}

	if (g_context->tool == TOOL_TYPE_MATERIAL) {
		return;
	}

	draw_begin(NULL, false, 0);
	draw_set_color(0xffffffff);

	i32 mx = base_paint_screen_x(g_context->paint_vec.x);
	i32 my = base_paint_screen_y(g_context->paint_vec.y);

	if (g_context->brush_stencil_image != NULL && g_context->tool != TOOL_TYPE_PICKER && g_context->tool != TOOL_TYPE_COLORID) {
		rect_t *r = ui_base_get_brush_stencil_rect();
		if (!keymap_shortcut(any_map_get(g_keymap, "stencil_hide"), SHORTCUT_TYPE_DOWN)) {
			draw_set_color(0x88ffffff);
			f32 angle = g_context->brush_stencil_angle;
			draw_set_transform(mat3_multmat(mat3_multmat(mat3_translation(0.5, 0.5), mat3_rotation(-angle)), mat3_translation(-0.5, -0.5)));
			draw_scaled_image(g_context->brush_stencil_image, r->x, r->y, r->w, r->h);
			draw_set_transform(mat3_nan());
			draw_set_color(0xffffffff);
		}
		bool transform = keymap_shortcut(any_map_get(g_keymap, "stencil_transform"), SHORTCUT_TYPE_DOWN);
		if (transform) {
			// Outline
			draw_rect(r->x, r->y, r->w, r->h, 1.0);
			// Scale
			draw_rect(r->x - 8, r->y - 8, 16, 16, 1.0);
			draw_rect(r->x - 8 + r->w, r->y - 8, 16, 16, 1.0);
			draw_rect(r->x - 8, r->y - 8 + r->h, 16, 16, 1.0);
			draw_rect(r->x - 8 + r->w, r->y - 8 + r->h, 16, 16, 1.0);
			// Rotate
			f32 cosa = math_cos(-g_context->brush_stencil_angle);
			f32 sina = math_sin(-g_context->brush_stencil_angle);
			f32 ox   = 0;
			f32 oy   = -r->h / 2.0;
			f32 x    = ox * cosa - oy * sina;
			f32 y    = ox * sina + oy * cosa;
			x += r->x + r->w / 2.0;
			y += r->y + r->h / 2.0;
			draw_filled_circle(x, y, 8, 0);
		}
	}

	if (g_context->tool == TOOL_TYPE_PICKER) {
		// Show picker icon when picking
		if (g_context->color_picker_callback != NULL) {
			gpu_texture_t *img  = resource_get("icons.k");
			rect_t        *rect = resource_tile50(img, TOOL_TYPE_PICKER);
			draw_sub_image(img, rect->x, rect->y, rect->w, rect->h, mx + 10, my + 10);
		}
		// Show picked material next to cursor
		else if (g_context->picker_select_material) {
			gpu_texture_t *img = g_context->material->image_icon;
			draw_image(img, mx + 10, my + 10);
		}
	}

	// The 2D view sees the layer plane, brush radius is in world units as in the 3D view
	bool in_2d   = context_in_2d_view(VIEW_2D_TYPE_LAYER);
	f32  scale2d = (900 / (f32)base_paint_view_h()) * g_config->window_scale;
	f32  world2d = ui_view2d_world_scale();

	// Clone source cursor
	if (g_context->tool == TOOL_TYPE_CLONE && !keyboard_down("alt") && (mouse_down("left") || pen_down("tip"))) {
		f32 radius =
		    in_2d ? g_context->brush_radius / 15.0 * 2.0 * g_context->brush_nodes_radius * world2d : util_layer_brush_screen_radius() * base_paint_view_h();
		util_cursor_draw_2d(mx + g_context->clone_delta_x * base_paint_view_w(), my + g_context->clone_delta_y * base_paint_view_h(), radius, 0x66ffffff);
	}

	bool decal = context_is_decal();

	if (in_2d || decal) {
		bool decal_mask = context_is_decal_mask();
		if (decal && !context_in_nodes()) {
			f32 decal_alpha = 0.5;
			if (!decal_mask) {
				g_context->decal_x = g_context->paint_vec.x;
				g_context->decal_y = g_context->paint_vec.y;
				decal_alpha        = g_context->brush_opacity;
			}

			if (!g_config->brush_live && (!context_is_decal() || in_2d)) {
				// Decal image spans twice the radius
				f32 radius = g_context->brush_radius * g_context->brush_nodes_radius / 15.0 * scale2d * 2.0;
				i32 psizey = math_floor(radius * 2.0 * world2d);
				i32 psizex = math_floor(psizey * g_context->brush_scale_x);

				f32 decalx = base_paint_screen_x(g_context->decal_x) - psizex / 2.0;
				f32 decaly = base_paint_screen_y(g_context->decal_y) - psizey / 2.0;

				draw_set_color(color_from_floats(1, 1, 1, decal_alpha));
				f32 angle = (g_context->brush_angle + g_context->brush_nodes_angle) * (math_pi() / 180.0);
				draw_set_transform(mat3_multmat(mat3_multmat(mat3_translation(0.5, 0.5), mat3_rotation(angle)), mat3_translation(-0.5, -0.5)));
				draw_scaled_image(g_context->decal_image, decalx, decaly, psizex, psizey);
				draw_set_transform(mat3_nan());
				draw_set_color(0xffffffff);
			}
		}
		if (g_context->tool == TOOL_TYPE_BRUSH || g_context->tool == TOOL_TYPE_ERASER || g_context->tool == TOOL_TYPE_CLONE ||
		    g_context->tool == TOOL_TYPE_BLUR || g_context->tool == TOOL_TYPE_PARTICLE || (decal_mask && in_2d)) {
			// The 3D view draws its own cursor
			f32 radius = decal_mask ? g_context->brush_decal_mask_radius * 2.0 / 15.0 * scale2d : g_context->brush_radius / 15.0 * 2.0;
			util_cursor_draw_2d(mx, my, radius * g_context->brush_nodes_radius * world2d, 0x99ffffff);
		}
	}

	bool in_paint_area = context_in_paint_area() && !context_in_nodes();
	if (in_paint_area && g_context->brush_lazy_radius > 0 && !g_context->brush_locked && !slot_layer_is_path(g_context->layer) &&
	    (g_context->tool == TOOL_TYPE_BRUSH || g_context->tool == TOOL_TYPE_ERASER || g_context->tool == TOOL_TYPE_DECAL || g_context->tool == TOOL_TYPE_TEXT ||
	     g_context->tool == TOOL_TYPE_CLONE || g_context->tool == TOOL_TYPE_BLUR || g_context->tool == TOOL_TYPE_PARTICLE)) {
		draw_filled_rect(mx - 1, my - 1, 2, 2);
		mx         = base_paint_screen_x(g_context->brush_lazy_x);
		my         = base_paint_screen_y(g_context->brush_lazy_y);
		f32 radius = g_context->brush_lazy_radius * util_layer_brush_screen_radius() * 3.0 * 2.0 * sys_h();
		util_cursor_draw_2d(mx, my, radius * 0.46, 0xff666666);
	}
	render_gizmo_render_bones();
	render_gizmo_render_rect();
	util_shape_tool_render();
	util_text_tool_render();
	util_path_tool_render();
	draw_end();
}
