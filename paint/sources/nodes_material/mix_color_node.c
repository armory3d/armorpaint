
#include "../global.h"

char *mix_color_node_vector(ui_node_t *node, ui_node_socket_t *socket) {
	char *fac     = parser_material_parse_value_input(node->inputs->buffer[0], false);
	char *fac_var = string_tmp("%s_fac", parser_material_node_name(node, NULL));
	parser_material_write(parser_material_kong, string_tmp("var %s: float = %s;", fac_var, fac));
	char             *col1  = parser_material_parse_vector_input(node->inputs->buffer[1]);
	char             *col2  = parser_material_parse_vector_input(node->inputs->buffer[2]);
	ui_node_button_t *but   = node->buttons->buffer[0]; // blend_type
	char             *blend = to_upper_case(u8_array_string_at(but->data, but->default_value->buffer[0]));
	blend                   = string_copy(string_replace_all(blend, " ", "_"));
	bool clamp_factor       = node->buttons->buffer[1]->default_value->buffer[0] > 0;
	bool clamp_result       = node->buttons->buffer[2]->default_value->buffer[0] > 0;
	if (clamp_factor) {
		parser_material_write(parser_material_kong, string_tmp("%s = clamp(%s, 0.0, 1.0);", fac_var, fac_var));
	}
	char *out_col = "";
	if (string_equals(blend, "MIX")) {
		out_col = string_tmp("lerp3(%s, %s, %s)", col1, col2, fac_var);
	}
	else if (string_equals(blend, "DARKEN")) {
		out_col = string_tmp("min3(%s, %s * %s)", col1, col2, fac_var);
	}
	else if (string_equals(blend, "MULTIPLY")) {
		out_col = string_tmp("lerp3(%s, %s * %s, %s)", col1, col1, col2, fac_var);
	}
	else if (string_equals(blend, "BURN")) {
		out_col = string_tmp("lerp3(%s, float3(1.0, 1.0, 1.0) - (float3(1.0, 1.0, 1.0) - %s) / %s, %s)", col1, col1, col2, fac_var);
	}
	else if (string_equals(blend, "LIGHTEN")) {
		out_col = string_tmp("max3(%s, %s * %s)", col1, col2, fac_var);
	}
	else if (string_equals(blend, "SCREEN")) {
		char *v3 = parser_material_to_vec3(string_tmp("1.0 - %s", fac_var));
		out_col  = string_tmp("(float3(1.0, 1.0, 1.0) - (%s + %s * (float3(1.0, 1.0, 1.0) - %s)) * (float3(1.0, 1.0, 1.0) - %s))", v3, fac_var, col2, col1);
	}
	else if (string_equals(blend, "DODGE")) {
		out_col = string_tmp("lerp3(%s, %s / (float3(1.0, 1.0, 1.0) - %s), %s)", col1, col1, col2, fac_var);
	}
	else if (string_equals(blend, "ADD")) {
		out_col = string_tmp("lerp3(%s, %s + %s, %s)", col1, col1, col2, fac_var);
	}
	else if (string_equals(blend, "OVERLAY")) {
		// out_col = "lerp3(" + col1 + ", float3( \
		// 	" + col1 + ".r < 0.5 ? 2.0 * " + col1 + ".r * " + col2 + ".r : 1.0 - 2.0 * (1.0 - " + col1 + ".r) * (1.0 - " + col2 + ".r), \
		// 	" + col1 + ".g < 0.5 ? 2.0 * " + col1 + ".g * " + col2 + ".g : 1.0 - 2.0 * (1.0 - " + col1 + ".g) * (1.0 - " + col2 + ".g), \
		// 	" + col1 + ".b < 0.5 ? 2.0 * " + col1 + ".b * " + col2 + ".b : 1.0 - 2.0 * (1.0 - " + col1 + ".b) * (1.0 - " + col2 + ".b) \
		// ), " + fac_var + ")";
		char *node_name = parser_material_node_name(node, NULL);
		char *res_r     = string_tmp("%s_res_r", node_name);
		char *res_g     = string_tmp("%s_res_g", node_name);
		char *res_b     = string_tmp("%s_res_b", node_name);
		parser_material_write(parser_material_kong, string_tmp("var %s: float;", res_r));
		parser_material_write(parser_material_kong, string_tmp("var %s: float;", res_g));
		parser_material_write(parser_material_kong, string_tmp("var %s: float;", res_b));
		parser_material_write(parser_material_kong,
		                      string_tmp("if (%s.r < 0.5) { %s = 2.0 * %s.r * %s.r; } else { %s = 1.0 - 2.0 * (1.0 - %s.r) * (1.0 - %s.r); }", col1, res_r,
		                                 col1, col2, res_r, col1, col2));
		parser_material_write(parser_material_kong,
		                      string_tmp("if (%s.g < 0.5) { %s = 2.0 * %s.g * %s.g; } else { %s = 1.0 - 2.0 * (1.0 - %s.g) * (1.0 - %s.g); }", col1, res_g,
		                                 col1, col2, res_g, col1, col2));
		parser_material_write(parser_material_kong,
		                      string_tmp("if (%s.b < 0.5) { %s = 2.0 * %s.b * %s.b; } else { %s = 1.0 - 2.0 * (1.0 - %s.b) * (1.0 - %s.b); }", col1, res_b,
		                                 col1, col2, res_b, col1, col2));
		out_col = string_tmp("lerp3(%s, float3(%s, %s, %s), %s)", col1, res_r, res_g, res_b, fac_var);
	}
	else if (string_equals(blend, "SOFT_LIGHT")) {
		out_col = string_tmp("((1.0 - %s) * %s + %s * ((float3(1.0, 1.0, 1.0) - %s) * %s * %s + %s * (float3(1.0, 1.0, 1.0) - (float3(1.0, 1.0, 1.0) - %s) * "
		                     "(float3(1.0, 1.0, 1.0) - %s))))",
		                     fac_var, col1, fac_var, col1, col2, col1, col1, col2, col1);
	}
	else if (string_equals(blend, "LINEAR_LIGHT")) {
		out_col = string_tmp("(%s + %s * (float3(2.0, 2.0, 2.0) * (%s - float3(0.5, 0.5, 0.5))))", col1, fac_var, col2);
	}
	else if (string_equals(blend, "DIFFERENCE")) {
		out_col = string_tmp("lerp3(%s, abs3(%s - %s), %s)", col1, col1, col2, fac_var);
	}
	else if (string_equals(blend, "EXCLUSION")) {
		out_col = string_tmp("lerp3(%s, %s + %s - 2.0 * %s * %s, %s)", col1, col1, col2, col1, col2, fac_var);
	}
	else if (string_equals(blend, "SUBTRACT")) {
		out_col = string_tmp("lerp3(%s, %s - %s, %s)", col1, col1, col2, fac_var);
	}
	else if (string_equals(blend, "DIVIDE")) {
		f32   eps   = 0.000001;
		char *eps_s = f32_to_string(eps);
		col2        = string_tmp("max3(%s, float3(%s, %s, %s))", col2, eps_s, eps_s, eps_s);
		char *v3    = string_tmp("(float3(1.0, 1.0, 1.0) - float3(%s, %s, %s)) * %s + float3(%s, %s, %s) * %s / %s", fac_var, fac_var, fac_var, col1, fac_var,
		                         fac_var, fac_var, col1, col2);
		out_col     = string_tmp("(%s)", v3);
	}
	else if (string_equals(blend, "HUE")) {
		node_shader_add_function(parser_material_kong, str_hue_sat);
		out_col = string_tmp("lerp3(%s, hsv_to_rgb(float3(rgb_to_hsv(%s).r, rgb_to_hsv(%s).g, rgb_to_hsv(%s).b)), %s)", col1, col2, col1, col1, fac_var);
	}
	else if (string_equals(blend, "SATURATION")) {
		node_shader_add_function(parser_material_kong, str_hue_sat);
		out_col = string_tmp("lerp3(%s, hsv_to_rgb(float3(rgb_to_hsv(%s).r, rgb_to_hsv(%s).g, rgb_to_hsv(%s).b)), %s)", col1, col1, col2, col1, fac_var);
	}
	else if (string_equals(blend, "COLOR")) {
		node_shader_add_function(parser_material_kong, str_hue_sat);
		out_col = string_tmp("lerp3(%s, hsv_to_rgb(float3(rgb_to_hsv(%s).r, rgb_to_hsv(%s).g, rgb_to_hsv(%s).b)), %s)", col1, col2, col2, col1, fac_var);
	}
	else if (string_equals(blend, "VALUE")) {
		node_shader_add_function(parser_material_kong, str_hue_sat);
		out_col = string_tmp("lerp3(%s, hsv_to_rgb(float3(rgb_to_hsv(%s).r, rgb_to_hsv(%s).g, rgb_to_hsv(%s).b)), %s)", col1, col1, col1, col2, fac_var);
	}
	if (clamp_result) {
		return string_tmp("clamp3(%s, float3(0.0, 0.0, 0.0), float3(1.0, 1.0, 1.0))", out_col);
	}
	else {
		return out_col;
	}
}

void mix_color_node_init() {

	char *mix_color_blend_type_data =
	    string_tmp("%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s", _tr("Mix"), _tr("Darken"), _tr("Multiply"), _tr("Burn"),
	               _tr("Lighten"), _tr("Screen"), _tr("Dodge"), _tr("Add"), _tr("Overlay"), _tr("Soft Light"), _tr("Linear Light"), _tr("Difference"),
	               _tr("Exclusion"), _tr("Subtract"), _tr("Divide"), _tr("Hue"), _tr("Saturation"), _tr("Color"), _tr("Value"));
	ui_node_t *mix_color_node_def =
	    ALLOC_INIT(ui_node_t, {.id     = 0,
	                           .name   = _tr("Mix Color"),
	                           .type   = "MIX_RGB",
	                           .x      = 0,
	                           .y      = 0,
	                           .color  = 0xff448c6d,
	                           .inputs = any_array_create_from_raw(
	                               (void *[]){
	                                   ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                 .node_id       = 0,
	                                                                 .name          = _tr("Factor"),
	                                                                 .type          = "VALUE",
	                                                                 .color         = 0xffa1a1a1,
	                                                                 .default_value = f32_array_create_x(0.5),
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .display       = 0}),
	                                   ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                 .node_id       = 0,
	                                                                 .name          = _tr("Color 1"),
	                                                                 .type          = "RGBA",
	                                                                 .color         = 0xffc7c729,
	                                                                 .default_value = f32_array_create_xyzw(0.5, 0.5, 0.5, 1.0),
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .display       = 0}),
	                                   ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                 .node_id       = 0,
	                                                                 .name          = _tr("Color 2"),
	                                                                 .type          = "RGBA",
	                                                                 .color         = 0xffc7c729,
	                                                                 .default_value = f32_array_create_xyzw(0.5, 0.5, 0.5, 1.0),
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .display       = 0}),
	                               },
	                               3),
	                           .outputs = any_array_create_from_raw(
	                               (void *[]){
	                                   ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                 .node_id       = 0,
	                                                                 .name          = _tr("Color"),
	                                                                 .type          = "RGBA",
	                                                                 .color         = 0xffc7c729,
	                                                                 .default_value = f32_array_create_xyzw(0.8, 0.8, 0.8, 1.0),
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .display       = 0}),
	                               },
	                               1),
	                           .buttons = any_array_create_from_raw(
	                               (void *[]){
	                                   ALLOC_INIT(ui_node_button_t, {.name          = _tr("blend_type"),
	                                                                 .type          = "ENUM",
	                                                                 .output        = 0,
	                                                                 .default_value = f32_array_create_x(0),
	                                                                 .data          = u8_array_create_from_string(mix_color_blend_type_data),
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .height        = 0}),
	                                   ALLOC_INIT(ui_node_button_t, {.name          = _tr("Clamp Factor"),
	                                                                 .type          = "BOOL",
	                                                                 .output        = 0,
	                                                                 .default_value = f32_array_create_x(0),
	                                                                 .data          = NULL,
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .height        = 0}),
	                                   ALLOC_INIT(ui_node_button_t, {.name          = _tr("Clamp Result"),
	                                                                 .type          = "BOOL",
	                                                                 .output        = 0,
	                                                                 .default_value = f32_array_create_x(0),
	                                                                 .data          = NULL,
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .height        = 0}),
	                               },
	                               3),
	                           .width = 0,
	                           .flags = 0});

	any_array_push(nodes_material_color, mix_color_node_def);
	any_map_set(parser_material_node_vectors, "MIX_RGB", mix_color_node_vector);
}
