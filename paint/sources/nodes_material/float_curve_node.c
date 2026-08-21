
#include "../global.h"

// Layout: buffer[point_index * 2 + 0/1] = x/y of point
//         buffer[32] = number of points
// Max 16 points (32 floats / 2 per point)

static void float_curve_init(f32_array_t *val) {
	val->buffer[0]  = 0.0f;
	val->buffer[1]  = 0.0f;
	val->buffer[2]  = 1.0f;
	val->buffer[3]  = 1.0f;
	val->buffer[32] = 2.0f;
}

char *float_curve_node_value(ui_node_t *node, ui_node_socket_t *socket) {
	char        *fac    = parser_material_parse_value_input(node->inputs->buffer[0], false);
	char        *val    = parser_material_parse_value_input(node->inputs->buffer[1], false);
	f32_array_t *curves = node->buttons->buffer[0]->default_value;

	if (curves->buffer[32] == 0.0f) {
		float_curve_init(curves);
	}

	i32   num    = (i32)curves->buffer[32];
	char *name   = parser_material_node_name(node, NULL);
	char *mapped = vector_curves_eval(name, val, curves->buffer, num);
	return string_tmp("lerp(%s, %s, %s)", val, mapped, fac);
}

void nodes_material_float_curve_button(i32 node_id) {
	ui_node_t        *node    = ui_get_node(ui_nodes_get_canvas(true)->nodes, node_id);
	ui_node_button_t *but     = node->buttons->buffer[0];
	ui_handle_t      *nhandle = ui_nest(ui_handle(__ID__), node->id);
	f32_array_t      *val     = but->default_value;
	f32               sw      = g_ui->_w / (float)UI_NODES_SCALE();

	if (val->buffer[32] == 0.0f) {
		float_curve_init(val);
	}
	i32 num = (i32)val->buffer[32];

	// Curve preview
	f32 ph  = UI_LINE_H() * 4 - 2 * UI_NODES_SCALE();
	f32 phs = ph * UI_SCALE();
	f32 pws = sw * UI_SCALE();
	f32 bx  = g_ui->_x;
	f32 by  = g_ui->_y;
	ui_fill(0, 0, sw, ph, 0xff1a1a1a);
	ui_fill(0, ph * 0.5f, sw, 1.0f / UI_NODES_SCALE(), 0xff333333);
	ui_fill(sw * 0.5f, 0, 1.0f / UI_NODES_SCALE(), ph, 0xff333333);
	draw_set_color(0xffffffff);
	f32 prev_ax = 0.0f, prev_ay = 0.0f;
	for (i32 s = 0; s <= 64; s++) {
		f32 t       = (f32)s / 64.0f;
		f32 curve_y = vector_curves_eval_cpu(val->buffer, num, t);
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
		i32 last                 = (num - 1) * 2;
		val->buffer[num * 2 + 0] = val->buffer[last + 0];
		val->buffer[num * 2 + 1] = val->buffer[last + 1];
		num++;
		val->buffer[32] = (f32)num;
	}
	if (ui_button("-", UI_ALIGN_CENTER, "") && num > 1) {
		num--;
		val->buffer[32] = (f32)num;
	}
	ui_handle_t *ihandle = ui_nest(ui_nest(nhandle, 0), 2);
	i32          i       = math_floor(ui_slider(ihandle, "Index", 0, num - 1, false, 1, true, UI_ALIGN_LEFT, true));
	if (i >= num || i < 0) {
		ihandle->f = i = num - 1;
	}
	ui_row2();
	ui_handle_t *h1        = ui_nest(ui_nest(nhandle, 0), 3);
	ui_handle_t *h2        = ui_nest(ui_nest(nhandle, 0), 4);
	h1->f                  = val->buffer[i * 2 + 0];
	h2->f                  = val->buffer[i * 2 + 1];
	val->buffer[i * 2 + 0] = ui_slider(h1, "X", 0, 1, true, 100, true, UI_ALIGN_LEFT, true);
	val->buffer[i * 2 + 1] = ui_slider(h2, "Y", 0, 1, true, 100, true, UI_ALIGN_LEFT, true);
}

void float_curve_node_init() {

	ui_node_t *float_curve_node_def =
	    ALLOC_INIT(ui_node_t, {.id     = 0,
	                           .name   = _tr("Float Curve"),
	                           .type   = "FLOAT_CURVE",
	                           .x      = 0,
	                           .y      = 0,
	                           .color  = 0xff62676d,
	                           .inputs = any_array_create_from_raw(
	                               (void *[]){
	                                   ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                 .node_id       = 0,
	                                                                 .name          = _tr("Factor"),
	                                                                 .type          = "VALUE",
	                                                                 .color         = 0xffa1a1a1,
	                                                                 .default_value = f32_array_create_x(1.0),
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .display       = 0}),
	                                   ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                 .node_id       = 0,
	                                                                 .name          = _tr("Value"),
	                                                                 .type          = "VALUE",
	                                                                 .color         = 0xffa1a1a1,
	                                                                 .default_value = f32_array_create_x(1.0),
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
	                                                                 .name          = _tr("Value"),
	                                                                 .type          = "VALUE",
	                                                                 .color         = 0xffa1a1a1,
	                                                                 .default_value = f32_array_create_x(0.0),
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .display       = 0}),
	                               },
	                               1),
	                           .buttons = any_array_create_from_raw(
	                               (void *[]){
	                                   ALLOC_INIT(ui_node_button_t, {.name          = "nodes_material_float_curve_button",
	                                                                 .type          = "CUSTOM",
	                                                                 .output        = 0,
	                                                                 .default_value = f32_array_create(33),
	                                                                 .data          = NULL,
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .height        = 7.2}),
	                               },
	                               1),
	                           .width = 0,
	                           .flags = 0});

	any_array_push(nodes_material_utilities, float_curve_node_def);
	any_map_set(parser_material_node_values, "FLOAT_CURVE", float_curve_node_value);
	any_map_set(ui_nodes_custom_buttons, "nodes_material_float_curve_button", nodes_material_float_curve_button);
}
