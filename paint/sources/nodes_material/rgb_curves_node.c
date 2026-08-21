
#include "../global.h"

// Layout: buffer[channel * 32 + point_index * 2 + 0/1] = x/y of point
//         buffer[128 + channel] = number of points for that channel
// Channels: 0=C(combined), 1=R, 2=G, 3=B
// Max 16 points per channel (32 floats / 2 per point)

static void rgb_curves_init_channel(f32_array_t *val, i32 ch) {
	val->buffer[ch * 32 + 0] = 0.0f;
	val->buffer[ch * 32 + 1] = 0.0f;
	val->buffer[ch * 32 + 2] = 1.0f;
	val->buffer[ch * 32 + 3] = 1.0f;
	val->buffer[128 + ch]    = 2.0f;
}

char *rgb_curves_node_vector(ui_node_t *node, ui_node_socket_t *socket) {
	char        *fac    = parser_material_parse_value_input(node->inputs->buffer[0], false);
	char        *col    = parser_material_parse_vector_input(node->inputs->buffer[1]);
	f32_array_t *curves = node->buttons->buffer[0]->default_value;

	// Initialize default identity curves if counts are unset
	for (i32 ch = 0; ch < 4; ch++) {
		if (curves->buffer[128 + ch] == 0.0f) {
			rgb_curves_init_channel(curves, ch);
		}
	}

	char *name = parser_material_node_name(node, NULL);
	i32   nc   = (i32)curves->buffer[128]; // C (combined)
	i32   nr   = (i32)curves->buffer[129]; // R
	i32   ng   = (i32)curves->buffer[130]; // G
	i32   nb   = (i32)curves->buffer[131]; // B

	// Apply C curve to each channel, then per-channel curves
	char *cr = vector_curves_eval(string_tmp("%s_cr", name), string_tmp("%s.x", col), curves->buffer + 32 * 0, nc);
	char *cg = vector_curves_eval(string_tmp("%s_cg", name), string_tmp("%s.y", col), curves->buffer + 32 * 0, nc);
	char *cb = vector_curves_eval(string_tmp("%s_cb", name), string_tmp("%s.z", col), curves->buffer + 32 * 0, nc);
	char *rr = vector_curves_eval(string_tmp("%s_rr", name), cr, curves->buffer + 32 * 1, nr);
	char *gg = vector_curves_eval(string_tmp("%s_gg", name), cg, curves->buffer + 32 * 2, ng);
	char *bb = vector_curves_eval(string_tmp("%s_bb", name), cb, curves->buffer + 32 * 3, nb);

	return string_tmp("lerp3(%s, float3(%s, %s, %s), %s)", col, rr, gg, bb, fac);
}

void nodes_material_rgb_curves_button(i32 node_id) {
	ui_node_t        *node    = ui_get_node(ui_nodes_get_canvas(true)->nodes, node_id);
	ui_node_button_t *but     = node->buttons->buffer[0];
	ui_handle_t      *nhandle = ui_nest(ui_handle(__ID__), node->id);
	f32_array_t      *val     = but->default_value;
	f32               sw      = g_ui->_w / (float)UI_NODES_SCALE();

	// Channel selector: C, R, G, B
	ui_row4();
	ui_radio(ui_nest(ui_nest(nhandle, 0), 1), 0, "C", "");
	ui_radio(ui_nest(ui_nest(nhandle, 0), 1), 1, "R", "");
	ui_radio(ui_nest(ui_nest(nhandle, 0), 1), 2, "G", "");
	ui_radio(ui_nest(ui_nest(nhandle, 0), 1), 3, "B", "");
	i32 ch = ui_nest(ui_nest(nhandle, 0), 1)->i;

	// Initialize on first use
	if (val->buffer[128 + ch] == 0.0f) {
		rgb_curves_init_channel(val, ch);
	}
	i32 num = (i32)val->buffer[128 + ch];

	// Curve preview
	f32 ph  = UI_LINE_H() * 4 - 2 * UI_NODES_SCALE();
	f32 phs = ph * UI_SCALE();
	f32 pws = sw * UI_SCALE();
	f32 bx  = g_ui->_x;
	f32 by  = g_ui->_y;
	// Background
	ui_fill(0, 0, sw, ph, 0xff1a1a1a);
	// Grid lines at midpoints
	ui_fill(0, ph * 0.5f, sw, 1.0f / UI_NODES_SCALE(), 0xff333333);
	ui_fill(sw * 0.5f, 0, 1.0f / UI_NODES_SCALE(), ph, 0xff333333);
	// Draw the curve
	f32 *points = val->buffer + ch * 32;
	draw_set_color(0xffffffff);
	f32 prev_ax = 0.0f, prev_ay = 0.0f;
	for (i32 s = 0; s <= 64; s++) {
		f32 t       = (f32)s / 64.0f;
		f32 curve_y = vector_curves_eval_cpu(points, num, t);
		f32 ax      = bx + t * pws;
		f32 ay      = by + (1.0f - curve_y) * phs;
		if (ay < by)
			ay = by;
		if (ay > by + phs)
			ay = by + phs;
		if (s > 0) {
			draw_line_aa(prev_ax, prev_ay, ax, ay, 1.5f);
		}
		prev_ax = ax;
		prev_ay = ay;
	}
	draw_set_color(0xffffffff);
	g_ui->_y += UI_LINE_H() * 4;

	// Edit controls
	f32_array_t *row = f32_array_create_from_raw_tmp(
	    (f32[]){
	        1 / 5.0,
	        1 / 5.0,
	        3 / 5.0,
	    },
	    3);
	ui_row(row);
	if (ui_button("+", UI_ALIGN_CENTER, "") && num < 16) {
		i32 last                           = (num - 1) * 2;
		val->buffer[ch * 32 + num * 2 + 0] = val->buffer[ch * 32 + last + 0];
		val->buffer[ch * 32 + num * 2 + 1] = val->buffer[ch * 32 + last + 1];
		num++;
		val->buffer[128 + ch] = (f32)num;
	}
	if (ui_button("-", UI_ALIGN_CENTER, "") && num > 1) {
		num--;
		val->buffer[128 + ch] = (f32)num;
	}
	ui_handle_t *ihandle = ui_nest(ui_nest(ui_nest(nhandle, 0), 2), ch);
	i32          i       = math_floor(ui_slider(ihandle, "Index", 0, num - 1, false, 1, true, UI_ALIGN_LEFT, true));
	if (i >= num || i < 0) {
		ihandle->f = i = num - 1;
	}
	ui_row2();
	ui_handle_t *h1                  = ui_nest(ui_nest(nhandle, 0), 3);
	ui_handle_t *h2                  = ui_nest(ui_nest(nhandle, 0), 4);
	h1->f                            = val->buffer[ch * 32 + i * 2 + 0];
	h2->f                            = val->buffer[ch * 32 + i * 2 + 1];
	val->buffer[ch * 32 + i * 2 + 0] = ui_slider(h1, "X", 0, 1, true, 100, true, UI_ALIGN_LEFT, true);
	val->buffer[ch * 32 + i * 2 + 1] = ui_slider(h2, "Y", 0, 1, true, 100, true, UI_ALIGN_LEFT, true);
}

void rgb_curves_node_init() {

	ui_node_t *rgb_curves_node_def =
	    ALLOC_INIT(ui_node_t, {.id     = 0,
	                              .name   = _tr("RGB Curves"),
	                              .type   = "CURVE_RGB",
	                              .x      = 0,
	                              .y      = 0,
	                              .color  = 0xff448c6d,
	                              .inputs = any_array_create_from_raw(
	                                  (void *[]){
	                                      ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                       .node_id       = 0,
	                                                                       .name          = _tr("Fac"),
	                                                                       .type          = "VALUE",
	                                                                       .color         = 0xffa1a1a1,
	                                                                       .default_value = f32_array_create_x(1.0),
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .display       = 0}),
	                                      ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                       .node_id       = 0,
	                                                                       .name          = _tr("Color"),
	                                                                       .type          = "RGBA",
	                                                                       .color         = 0xffc7c729,
	                                                                       .default_value = f32_array_create_xyzw(1.0, 1.0, 1.0, 1.0),
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .display       = 0}),
	                                  },
	                                  2),
	                              .outputs = any_array_create_from_raw(
	                                  (void *[]){
	                                      ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                       .node_id       = 0,
	                                                                       .name          = _tr("Color"),
	                                                                       .type          = "RGBA",
	                                                                       .color         = 0xffc7c729,
	                                                                       .default_value = f32_array_create_xyzw(1.0, 1.0, 1.0, 1.0),
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .display       = 0}),
	                                  },
	                                  1),
	                              .buttons = any_array_create_from_raw(
	                                  (void *[]){
	                                      ALLOC_INIT(ui_node_button_t, {.name          = "nodes_material_rgb_curves_button",
	                                                                       .type          = "CUSTOM",
	                                                                       .output        = 0,
	                                                                       .default_value = f32_array_create(128 + 4),
	                                                                       .data          = NULL,
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .height        = 7.2}),
	                                  },
	                                  1),
	                              .width = 0,
	                              .flags = 0});

	any_array_push(nodes_material_color, rgb_curves_node_def);
	any_map_set(parser_material_node_vectors, "CURVE_RGB", rgb_curves_node_vector);
	any_map_set(ui_nodes_custom_buttons, "nodes_material_rgb_curves_button", nodes_material_rgb_curves_button);
}
