
#include "../global.h"

void util_cursor_render(void *_) {
	if (!base_ui_enabled) {
		return;
	}

	if (g_context->tool == TOOL_TYPE_MATERIAL) {
		return;
	}

	draw_begin(NULL, false, 0);
	draw_set_color(0xffffffff);

	g_context->view_index = g_context->view_index_last;
	i32 mx                = base_x() + g_context->paint_vec.x * base_w();
	i32 my                = base_y() + g_context->paint_vec.y * base_h();
	g_context->view_index = -1;

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

	gpu_texture_t *cursor_img = resource_get("cursor.k");
	i32            psize      = math_floor(182 * (g_context->brush_radius * 0.5 * g_context->brush_nodes_radius) * UI_SCALE());

	// Clone source cursor
	if (g_context->tool == TOOL_TYPE_CLONE && !keyboard_down("alt") && (mouse_down("left") || pen_down("tip"))) {
		draw_set_color(0x66ffffff);
		draw_scaled_image(cursor_img, mx + g_context->clone_delta_x * sys_w() - psize / 2.0, my + g_context->clone_delta_y * sys_h() - psize / 2.0, psize,
		                  psize);
		draw_set_color(0xffffffff);
	}

	bool decal = context_is_decal();

	if (context_in_2d_view(VIEW_2D_TYPE_LAYER) || decal) {
		bool decal_mask = context_is_decal_mask();
		if (decal && !context_in_nodes()) {
			f32 decal_alpha = 0.5;
			if (!decal_mask) {
				g_context->decal_x = g_context->paint_vec.x;
				g_context->decal_y = g_context->paint_vec.y;
				decal_alpha        = g_context->brush_opacity;
			}

			if (!g_config->brush_live && (!context_is_decal() || context_in_2d_view(VIEW_2D_TYPE_LAYER))) {
				i32 psizex = math_floor(182 * 0.5 * 0.92 * UI_SCALE() * (g_context->brush_radius * g_context->brush_nodes_radius * g_context->brush_scale_x) *
				                        ui_view2d_pan_scale);
				i32 psizey = math_floor(182 * 0.5 * 0.92 * UI_SCALE() * (g_context->brush_radius * g_context->brush_nodes_radius) * ui_view2d_pan_scale);

				g_context->view_index = g_context->view_index_last;
				f32 decalx            = base_x() + g_context->decal_x * base_w() - psizex / 2.0;
				f32 decaly            = base_y() + g_context->decal_y * base_h() - psizey / 2.0;
				g_context->view_index = -1;

				draw_set_color(color_from_floats(1, 1, 1, decal_alpha));
				f32 angle = (g_context->brush_angle + g_context->brush_nodes_angle) * (math_pi() / 180.0);
				draw_set_transform(mat3_multmat(mat3_multmat(mat3_translation(0.5, 0.5), mat3_rotation(angle)), mat3_translation(-0.5, -0.5)));
				draw_scaled_image(g_context->decal_image, decalx, decaly, psizex, psizey);
				draw_set_transform(mat3_nan());
				draw_set_color(0xffffffff);
			}
		}
		if (g_context->tool == TOOL_TYPE_BRUSH || g_context->tool == TOOL_TYPE_ERASER || g_context->tool == TOOL_TYPE_CLONE ||
		    g_context->tool == TOOL_TYPE_BLUR || g_context->tool == TOOL_TYPE_PARTICLE || (decal_mask && context_in_2d_view(VIEW_2D_TYPE_LAYER))) {
			if (decal_mask) {
				psize = math_floor(cursor_img->width * (g_context->brush_decal_mask_radius * g_context->brush_nodes_radius) * UI_SCALE());
			}
			if (context_in_2d_view(VIEW_2D_TYPE_LAYER)) {
				psize = math_floor(psize * ui_view2d_pan_scale);
			}
			draw_scaled_image(cursor_img, mx - psize / 2.0, my - psize / 2.0, psize, psize);
		}
	}

	if (g_context->brush_lazy_radius > 0 && !g_context->brush_locked && !slot_layer_is_path(g_context->layer) &&
	    (g_context->tool == TOOL_TYPE_BRUSH || g_context->tool == TOOL_TYPE_ERASER || g_context->tool == TOOL_TYPE_DECAL || g_context->tool == TOOL_TYPE_TEXT ||
	     g_context->tool == TOOL_TYPE_CLONE || g_context->tool == TOOL_TYPE_BLUR || g_context->tool == TOOL_TYPE_PARTICLE)) {
		draw_filled_rect(mx - 1, my - 1, 2, 2);
		mx         = g_context->brush_lazy_x * base_w() + base_x();
		my         = g_context->brush_lazy_y * base_h() + base_y();
		f32 radius = g_context->brush_lazy_radius * util_layer_brush_screen_radius() * 3.0 * 2.0 * sys_h();
		draw_set_color(0xff666666);
		draw_scaled_image(cursor_img, mx - radius / 2.0, my - radius / 2.0, radius, radius);
		draw_set_color(0xffffffff);
	}
	draw_end();
}
