
#include "../global.h"

// Layout: buffer[axis * 32 + point_index * 2 + 0/1] = x/y of point
//         buffer[96 + axis] = number of points for that axis
// Max 16 points per axis (32 floats / 2 per point)

static void vector_curves_init_axis(f32_array_t *val, i32 axis) {
	val->buffer[axis * 32 + 0] = 0.0f; // point 0: x=0, y=0
	val->buffer[axis * 32 + 1] = 0.0f;
	val->buffer[axis * 32 + 2] = 1.0f; // point 1: x=1, y=1
	val->buffer[axis * 32 + 3] = 1.0f;
	val->buffer[96 + axis]     = 2.0f;
}

static void vector_curves_sort(i32 *sorted, i32 num, f32 *points) {
	for (i32 i = 1; i < num; i++) {
		i32 key   = sorted[i];
		f32 key_x = points[key * 2];
		i32 j     = i - 1;
		while (j >= 0 && points[sorted[j] * 2] > key_x) {
			sorted[j + 1] = sorted[j];
			j--;
		}
		sorted[j + 1] = key;
	}
}

f32 vector_curves_eval_cpu(f32 *points, i32 num, f32 t) {
	if (num <= 0)
		return t;
	if (num == 1)
		return points[1];

	i32 sorted[num];
	for (i32 i = 0; i < num; i++)
		sorted[i] = i;
	vector_curves_sort(sorted, num, points);

	// Find the segment: last index whose x <= t
	i32 idx = 0;
	for (i32 i = 1; i < num; i++) {
		if (t > points[sorted[i] * 2])
			idx = i;
	}

	if (idx >= num - 1)
		return points[sorted[num - 1] * 2 + 1]; // clamp to last y

	i32 s0 = sorted[idx];
	i32 s1 = sorted[idx + 1];
	f32 x0 = points[s0 * 2], y0 = points[s0 * 2 + 1];
	f32 x1 = points[s1 * 2], y1 = points[s1 * 2 + 1];
	f32 range = x1 - x0;
	f32 f     = range > 0.00001f ? (t - x0) / range : 0.0f;
	if (f < 0.0f)
		f = 0.0f;
	if (f > 1.0f)
		f = 1.0f;
	return y0 * (1.0f - f) + y1 * f;
}

char *vector_curves_eval(char *name, char *fac, f32 *points, i32 num) {
	char *result_var = string_tmp("%s_result", name);
	char *fac_var    = string_tmp("%s_fac", name);
	parser_material_write(parser_material_kong, string_tmp("var %s: float = %s;", fac_var, fac));

	if (num <= 0) {
		parser_material_write(parser_material_kong, string_tmp("var %s: float = %s;", result_var, fac_var));
		return result_var;
	}

	if (num == 1) {
		char *y = f32_to_string_with_zeros(points[1]);
		parser_material_write(parser_material_kong, string_tmp("var %s: float = %s;", result_var, y));
		return result_var;
	}

	// Sort by x
	i32 sorted[num];
	for (i32 i = 0; i < num; i++)
		sorted[i] = i;
	vector_curves_sort(sorted, num, points);

	// Initial value: lerp of first segment
	i32   s0 = sorted[0], s1 = sorted[1];
	f32   x0 = points[s0 * 2], y0 = points[s0 * 2 + 1];
	f32   x1 = points[s1 * 2], y1 = points[s1 * 2 + 1];
	char *b01 = string_tmp("clamp((%s - %s) / max(%s, 0.00001), 0.0, 1.0)", fac_var, f32_to_string_with_zeros(x0), f32_to_string_with_zeros(x1 - x0));
	parser_material_write(parser_material_kong,
	                      string_tmp("var %s: float = lerp(%s, %s, %s);", result_var, f32_to_string_with_zeros(y0), f32_to_string_with_zeros(y1), b01));

	// Override for each subsequent segment
	for (i32 i = 1; i < num - 1; i++) {
		i32   si = sorted[i], si1 = sorted[i + 1];
		f32   xi = points[si * 2], yi = points[si * 2 + 1];
		f32   xi1 = points[si1 * 2], yi1 = points[si1 * 2 + 1];
		char *blend = string_tmp("clamp((%s - %s) / max(%s, 0.00001), 0.0, 1.0)", fac_var, f32_to_string_with_zeros(xi), f32_to_string_with_zeros(xi1 - xi));
		parser_material_write(parser_material_kong, string_tmp("if (%s > %s) { %s = lerp(%s, %s, %s); }", fac_var, f32_to_string_with_zeros(xi), result_var,
		                                                   f32_to_string_with_zeros(yi), f32_to_string_with_zeros(yi1), blend));
	}

	return result_var;
}

char *vector_curves_node_vector(ui_node_t *node, ui_node_socket_t *socket) {
	char        *fac    = parser_material_parse_value_input(node->inputs->buffer[0], false);
	char        *vec    = parser_material_parse_vector_input(node->inputs->buffer[1]);
	f32_array_t *curves = node->buttons->buffer[0]->default_value;

	// Initialize default identity curves if counts are unset
	for (i32 axis = 0; axis < 3; axis++) {
		if (curves->buffer[96 + axis] == 0.0f) {
			vector_curves_init_axis(curves, axis);
		}
	}

	char *name = parser_material_node_name(node, NULL);
	i32   nx   = (i32)curves->buffer[96];
	i32   ny   = (i32)curves->buffer[97];
	i32   nz   = (i32)curves->buffer[98];
	char *vc0  = vector_curves_eval(string_tmp("%s_x", name), string_tmp("%s.x", vec), curves->buffer + 32 * 0, nx);
	char *vc1  = vector_curves_eval(string_tmp("%s_y", name), string_tmp("%s.y", vec), curves->buffer + 32 * 1, ny);
	char *vc2  = vector_curves_eval(string_tmp("%s_z", name), string_tmp("%s.z", vec), curves->buffer + 32 * 2, nz);
	// Blend between original and mapped using factor
	return string_tmp("lerp3(%s, float3(%s, %s, %s), %s)", vec, vc0, vc1, vc2, fac);
}

void nodes_material_vector_curves_button(i32 node_id) {
	ui_node_t        *node    = ui_get_node(ui_nodes_get_canvas(true)->nodes, node_id);
	ui_node_button_t *but     = node->buttons->buffer[0];
	ui_handle_t      *nhandle = ui_nest(ui_handle(__ID__), node->id);
	f32_array_t      *val     = but->default_value;
	f32               sw      = g_ui->_w / (float)UI_NODES_SCALE();

	// Axis selector
	ui_row3();
	ui_radio(ui_nest(ui_nest(nhandle, 0), 1), 0, "X", "");
	ui_radio(ui_nest(ui_nest(nhandle, 0), 1), 1, "Y", "");
	ui_radio(ui_nest(ui_nest(nhandle, 0), 1), 2, "Z", "");
	i32 axis = ui_nest(ui_nest(nhandle, 0), 1)->i;

	// Initialize on first use
	if (val->buffer[96 + axis] == 0.0f) {
		vector_curves_init_axis(val, axis);
	}
	i32 num = (i32)val->buffer[96 + axis];

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
	f32 *points = val->buffer + axis * 32;
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
		i32 last                             = (num - 1) * 2;
		val->buffer[axis * 32 + num * 2 + 0] = val->buffer[axis * 32 + last + 0];
		val->buffer[axis * 32 + num * 2 + 1] = val->buffer[axis * 32 + last + 1];
		num++;
		val->buffer[96 + axis] = (f32)num;
	}
	if (ui_button("-", UI_ALIGN_CENTER, "") && num > 1) {
		num--;
		val->buffer[96 + axis] = (f32)num;
	}
	ui_handle_t *ihandle = ui_nest(ui_nest(ui_nest(nhandle, 0), 2), axis);
	i32          i       = math_floor(ui_slider(ihandle, "Index", 0, num - 1, false, 1, true, UI_ALIGN_LEFT, true));
	if (i >= num || i < 0) {
		ihandle->f = i = num - 1;
	}
	ui_row2();
	ui_handle_t *h1                    = ui_nest(ui_nest(nhandle, 0), 3);
	ui_handle_t *h2                    = ui_nest(ui_nest(nhandle, 0), 4);
	h1->f                              = val->buffer[axis * 32 + i * 2 + 0];
	h2->f                              = val->buffer[axis * 32 + i * 2 + 1];
	val->buffer[axis * 32 + i * 2 + 0] = ui_slider(h1, "X", 0, 1, true, 100, true, UI_ALIGN_LEFT, true);
	val->buffer[axis * 32 + i * 2 + 1] = ui_slider(h2, "Y", 0, 1, true, 100, true, UI_ALIGN_LEFT, true);
}

void vector_curves_node_init() {

	ui_node_t *vector_curves_node_def =
	    ALLOC_INIT(ui_node_t, {.id     = 0,
	                              .name   = _tr("Vector Curves"),
	                              .type   = "CURVE_VEC",
	                              .x      = 0,
	                              .y      = 0,
	                              .color  = 0xff522c99,
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
	                                                                       .name          = _tr("Vector"),
	                                                                       .type          = "VECTOR",
	                                                                       .color         = 0xff6363c7,
	                                                                       .default_value = f32_array_create_xyz(0.0, 0.0, 0.0),
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
	                                                                       .name          = _tr("Vector"),
	                                                                       .type          = "VECTOR",
	                                                                       .color         = 0xff6363c7,
	                                                                       .default_value = f32_array_create_xyz(0.0, 0.0, 0.0),
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .display       = 0}),
	                                  },
	                                  1),
	                              .buttons = any_array_create_from_raw(
	                                  (void *[]){
	                                      ALLOC_INIT(ui_node_button_t, {.name          = "nodes_material_vector_curves_button",
	                                                                       .type          = "CUSTOM",
	                                                                       .output        = 0,
	                                                                       .default_value = f32_array_create(96 + 3),
	                                                                       .data          = NULL,
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .height        = 7.2}),
	                                  },
	                                  1),
	                              .width = 0,
	                              .flags = 0});

	any_array_push(nodes_material_utilities, vector_curves_node_def);
	any_map_set(parser_material_node_vectors, "CURVE_VEC", vector_curves_node_vector);
	any_map_set(ui_nodes_custom_buttons, "nodes_material_vector_curves_button", nodes_material_vector_curves_button);
}
