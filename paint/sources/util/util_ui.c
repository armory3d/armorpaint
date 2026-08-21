
#include "../global.h"

f32 ui_base_get_radius_increment() {
	return 0.1;
}

rect_t *ui_base_get_brush_stencil_rect() {
	i32     w = math_floor(g_context->brush_stencil_image->width * (base_h() / (float)g_context->brush_stencil_image->height) * g_context->brush_stencil_scale);
	i32     h = math_floor(base_h() * g_context->brush_stencil_scale);
	i32     x = math_floor(base_x() + g_context->brush_stencil_x * base_w());
	i32     y = math_floor(base_y() + g_context->brush_stencil_y * base_h());
	rect_t *r = ALLOC_INIT(rect_t, {.w = w, .h = h, .x = x, .y = y});
	return r;
}

bool ui_base_hit_rect(f32 mx, f32 my, i32 x, i32 y, i32 w, i32 h) {
	return mx > x && mx < x + w && my > y && my < y + h;
}

void ui_base_make_empty_envmap(i32 col) {
	ui_base_viewport_col    = col;
	u8_array_t *b           = u8_array_create(4);
	b->buffer[0]            = color_get_rb(col);
	b->buffer[1]            = color_get_gb(col);
	b->buffer[2]            = color_get_bb(col);
	b->buffer[3]            = 255;
	g_context->empty_envmap = gpu_create_texture_from_bytes(b, 1, 1, GPU_TEXTURE_FORMAT_RGBA32);
	array_free(b);
	free(b);
}

void ui_base_set_icon_scale() {
	if (UI_SCALE() > 1) {
		string_array_t *res = any_array_create_from_raw(
		    (void *[]){
		        "icons.k",
		        "icons05x.k",
		        "icons2x.k",
		    },
		    3);
		resource_load(res);
		any_map_set(resource_bundled, "icons05x.k", resource_get("icons.k"));
		any_map_set(resource_bundled, "icons.k", resource_get("icons2x.k"));
	}
	else {
		string_array_t *res = any_array_create_from_raw(
		    (void *[]){
		        "icons.k",
		        "icons05x.k",
		    },
		    2);
		resource_load(res);
	}
}

void ui_base_set_viewport_col(i32 col) {
	ui_base_make_empty_envmap(col);
	g_context->ddirty = 2;
	if (!g_context->show_envmap) {
		scene_world->_->envmap = g_context->empty_envmap;
	}
}
