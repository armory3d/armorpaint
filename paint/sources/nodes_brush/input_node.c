
#include "../global.h"

typedef struct input_node {
	struct logic_node *base;
} input_node_t;

f32 input_node_start_x = 0.0;
f32 input_node_start_y = 0.0;
// Brush ruler
bool   input_node_lock_begin   = false;
bool   input_node_lock_x       = false;
bool   input_node_lock_y       = false;
f32    input_node_lock_start_x = 0.0;
f32    input_node_lock_start_y = 0.0;
bool   input_node_registered   = false;
vec4_t input_node_coords       = (vec4_t){0.0, 0.0, 0.0, 1.0};

static void input_node_grid_snap() {
	bool snap_shortcut = operator_shortcut(any_map_get(g_keymap, "grid_snap"), SHORTCUT_TYPE_DOWN);
	bool in_2d         = context_in_2d_view(VIEW_2D_TYPE_LAYER);
	bool in_3d         = context_in_3d_view();

	if (!in_2d && !in_3d) {
		return;
	}
	if (!snap_shortcut && !(g_config->view2d_grid_snap && in_2d)) {
		return;
	}

	slot_layer_t  *layer = g_context->layer;
	gpu_texture_t *tex   = layer->texpaint;
	if (tex == NULL) {
		return;
	}

	f32 wx;
	f32 wy;
	f32 ww;
	f32 wh;
	f32 pan_x;
	f32 pan_y;
	f32 pan_scale;
	if (in_2d) {
		i32 headerh = g_config->layout->buffer[LAYOUT_SIZE_HEADER] == 1 ? ui_header_h * 2 : ui_header_h;
		wh          = iron_window_height() - g_config->layout->buffer[LAYOUT_SIZE_STATUS_H] + headerh;
		if (!base_view3d_show) {
			wh = base_h();
		}
		wx        = ui_view2d_wx;
		wy        = ui_view2d_wy;
		ww        = ui_view2d_ww;
		pan_x     = ui_view2d_pan_x;
		pan_y     = ui_view2d_pan_y;
		pan_scale = ui_view2d_pan_scale;
	}
	else {
		wx        = base_x();
		wy        = base_y();
		ww        = base_w();
		wh        = base_h();
		pan_x     = 0.0;
		pan_y     = 0.0;
		pan_scale = 1.0;
	}

	f32 wm = fmin(ww, wh);
	f32 tw = wm * 0.9 * pan_scale;
	f32 th = tw * (tex->height / (float)tex->width);
	f32 tx = wx + ww / 2.0 - tw / 2.0 + pan_x;
	f32 ty = wy + wh / 2.0 - th / 2.0 + pan_y;
	f32 sx = input_node_coords.x * base_w() + base_x();
	f32 sy = input_node_coords.y * base_h() + base_y();

	// Snap to nearest cell center
	f32 u    = (sx - tx) / tw;
	f32 v    = (sy - ty) / th;
	i32 cell = g_config->view2d_grid_cell;
	f32 px   = (math_floor(u * tex->width / cell) + 0.5) * cell;
	f32 py   = (math_floor(v * tex->height / cell) + 0.5) * cell;
	sx       = tx + (px / tex->width) * tw;
	sy       = ty + (py / tex->height) * th;

	input_node_coords.x = (sx - base_x()) / base_w();
	input_node_coords.y = (sy - base_y()) / base_h();
}

void input_node_update(float_node_t *self) {
	if (g_context->split_view) {
		g_context->view_index = mouse_view_x() > base_w() / 2.0 ? 1 : 0;
	}

	bool  decal_mask  = context_is_decal_mask_paint();
	char *ruler_paint = string("%s+%s", any_map_get(g_keymap, "brush_ruler"), any_map_get(g_keymap, "action_paint"));
	bool  lazy_paint  = g_context->brush_lazy_radius > 0 && (operator_shortcut(any_map_get(g_keymap, "action_paint"), SHORTCUT_TYPE_DOWN) ||
                                                           operator_shortcut(ruler_paint, SHORTCUT_TYPE_DOWN) || decal_mask);

	f32 paint_x = mouse_view_x() / (float)sys_w();
	f32 paint_y = mouse_view_y() / (float)sys_h();

	if (mouse_started("left")) {
		input_node_start_x = mouse_view_x() / (float)sys_w();
		input_node_start_y = mouse_view_y() / (float)sys_h();
	}

	if (pen_down("tip")) {
		paint_x = pen_view_x() / (float)sys_w();
		paint_y = pen_view_y() / (float)sys_h();
	}
	if (pen_started("tip")) {
		input_node_start_x = pen_view_x() / (float)sys_w();
		input_node_start_y = pen_view_y() / (float)sys_h();
	}

	if (operator_shortcut(ruler_paint, SHORTCUT_TYPE_DOWN)) {
		if (input_node_lock_x) {
			paint_x = input_node_start_x;
		}
		if (input_node_lock_y) {
			paint_y = input_node_start_y;
		}
	}

	if (g_context->brush_lazy_radius > 0) {
		g_context->brush_lazy_x = paint_x;
		g_context->brush_lazy_y = paint_y;
	}
	if (!lazy_paint || slot_layer_is_path(g_context->layer)) {
		input_node_coords.x = paint_x;
		input_node_coords.y = paint_y;
	}

	if (g_context->split_view) {
		g_context->view_index = -1;
	}

	if (input_node_lock_begin) {
		f32 dx = math_abs(input_node_lock_start_x - mouse_view_x());
		f32 dy = math_abs(input_node_lock_start_y - mouse_view_y());
		if (dx > 1 || dy > 1) {
			input_node_lock_begin = false;
			if (dx > dy) {
				input_node_lock_y = true;
			}
			else {
				input_node_lock_x = true;
			}
		}
	}

	if (keyboard_started(any_map_get(g_keymap, "brush_ruler"))) {
		input_node_lock_start_x = mouse_view_x();
		input_node_lock_start_y = mouse_view_y();
		input_node_lock_begin   = true;
	}
	else if (keyboard_released(any_map_get(g_keymap, "brush_ruler"))) {
		input_node_lock_x = input_node_lock_y = input_node_lock_begin = false;
	}

	if (g_context->brush_lazy_radius > 0 && !slot_layer_is_path(g_context->layer)) {
		f32    aspect = sys_w() / (f32)sys_h();
		vec4_t v1     = (vec4_t){g_context->brush_lazy_x * aspect, g_context->brush_lazy_y, 0.0, 1.0};
		vec4_t v2     = (vec4_t){input_node_coords.x * aspect, input_node_coords.y, 0.0, 1.0};
		f32    d      = vec4_dist(v1, v2);
		f32    r      = g_context->brush_lazy_radius * util_layer_brush_screen_radius() * 3.0;
		if (d > r) {
			vec4_t v3           = (vec4_t){0.0, 0.0, 0.0, 1.0};
			v3                  = vec4_sub(v2, v1);
			v3                  = vec4_norm(v3);
			v3                  = vec4_mult(v3, 1.0 - g_context->brush_lazy_step);
			v3                  = vec4_mult(v3, r);
			v2                  = vec4_add(v1, v3);
			input_node_coords.x = v2.x / aspect;
			input_node_coords.y = v2.y;
			// Parse brush inputs once on next draw
			g_context->painted = -1;
		}
		g_context->last_paint_x = -1;
		g_context->last_paint_y = -1;
	}

	input_node_grid_snap();

	brush_output_node_parse_inputs();
}

logic_node_value_t *input_node_get(input_node_t *self, i32 from) {
	g_context->brush_lazy_radius = logic_node_input_get(self->base->inputs->buffer[0])->_f32;
	g_context->brush_lazy_step   = logic_node_input_get(self->base->inputs->buffer[1])->_f32;
	logic_node_value_t *v        = GC_ALLOC_INIT(logic_node_value_t, {._vec4 = input_node_coords});
	return v;
}

void *input_node_create(ui_node_t *raw, f32_array_t *args) {
	float_node_t *n = GC_ALLOC_INIT(float_node_t, {0});
	n->base         = logic_node_create(n);
	n->base->get    = input_node_get;

	if (!input_node_registered) {
		input_node_registered = true;
		sys_notify_on_update(input_node_update, n);
	}

	return n;
}

void input_node_init() {
	ui_node_t *input_node_def = GC_ALLOC_INIT(ui_node_t, {.id     = 0,
	                                                      .name   = _tr("Input"),
	                                                      .type   = "input_node",
	                                                      .x      = 0,
	                                                      .y      = 0,
	                                                      .color  = 0xff4982a0,
	                                                      .inputs = any_array_create_from_raw(
	                                                          (void *[]){
	                                                              GC_ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                                               .node_id       = 0,
	                                                                                               .name          = _tr("Lazy Radius"),
	                                                                                               .type          = "VALUE",
	                                                                                               .color         = 0xffa1a1a1,
	                                                                                               .default_value = f32_array_create_x(0.0),
	                                                                                               .min           = 0.0,
	                                                                                               .max           = 1.0,
	                                                                                               .precision     = 100,
	                                                                                               .display       = 0}),
	                                                              GC_ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                                               .node_id       = 0,
	                                                                                               .name          = _tr("Lazy Step"),
	                                                                                               .type          = "VALUE",
	                                                                                               .color         = 0xffa1a1a1,
	                                                                                               .default_value = f32_array_create_x(0.0),
	                                                                                               .min           = 0.0,
	                                                                                               .max           = 1.0,
	                                                                                               .precision     = 100,
	                                                                                               .display       = 0}),
	                                                          },
	                                                          2),
	                                                      .outputs = any_array_create_from_raw(
	                                                          (void *[]){
	                                                              GC_ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                                               .node_id       = 0,
	                                                                                               .name          = _tr("Position"),
	                                                                                               .type          = "VECTOR",
	                                                                                               .color         = 0xff63c763,
	                                                                                               .default_value = f32_array_create_xyz(0.0, 0.0, 0.0),
	                                                                                               .min           = 0.0,
	                                                                                               .max           = 1.0,
	                                                                                               .precision     = 100,
	                                                                                               .display       = 0}),
	                                                          },
	                                                          1),
	                                                      .buttons = any_array_create_from_raw((void *[]){}, 0),
	                                                      .width   = 0,
	                                                      .flags   = 0});

	any_array_push(nodes_brush_category0, input_node_def);
	any_map_set(nodes_brush_creates, "input_node", input_node_create);
}
