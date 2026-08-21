
#include "../global.h"

char *parser_material_get_gradient(char *grad, char *co) {
	if (string_equals(grad, "LINEAR")) {
		return string_tmp("%s.x", co);
	}
	else if (string_equals(grad, "QUADRATIC")) {
		return "0.0";
	}
	else if (string_equals(grad, "EASING")) {
		return "0.0";
	}
	else if (string_equals(grad, "DIAGONAL")) {
		return string_tmp("(%s.x + %s.y) * 0.5", co, co);
	}
	else if (string_equals(grad, "RADIAL")) {
		return string_tmp("atan2(%s.x, %s.y) / (3.141592 * 2.0) + 0.5", co, co);
	}
	else if (string_equals(grad, "QUADRATIC_SPHERE")) {
		return "0.0";
	}
	else { // "SPHERICAL"
		return string_tmp("max(1.0 - sqrt(%s.x * %s.x + %s.y * %s.y + %s.z * %s.z), 0.0)", co, co, co, co, co, co);
	}
}

char *gradient_texture_node_vector(ui_node_t *node, ui_node_socket_t *socket) {
	char             *co   = parser_material_get_coord(node);
	ui_node_button_t *but  = node->buttons->buffer[0]; // gradient_type;
	char             *grad = to_upper_case(u8_array_string_at(but->data, but->default_value->buffer[0]));
	grad                   = string_copy(string_replace_all(grad, " ", "_"));
	char *f                = parser_material_get_gradient(grad, co);
	return parser_material_to_vec3(string_tmp("clamp(%s, 0.0, 1.0)", f));
}

char *gradient_texture_node_value(ui_node_t *node, ui_node_socket_t *socket) {
	char             *co   = parser_material_get_coord(node);
	ui_node_button_t *but  = node->buttons->buffer[0]; // gradient_type;
	char             *grad = to_upper_case(u8_array_string_at(but->data, but->default_value->buffer[0]));
	grad                   = string_copy(string_replace_all(grad, " ", "_"));
	char *f                = parser_material_get_gradient(grad, co);
	return string_tmp("(clamp(%s, 0.0, 1.0))", f);
}

void gradient_texture_node_init() {

	char      *gradient_type_data = string_tmp("%s\n%s\n%s\n%s", _tr("Linear"), _tr("Diagonal"), _tr("Radial"), _tr("Spherical"));
	ui_node_t *gradient_texture_node_def =
	    ALLOC_INIT(ui_node_t, {.id     = 0,
	                              .name   = _tr("Gradient Texture"),
	                              .type   = "TEX_GRADIENT",
	                              .x      = 0,
	                              .y      = 0,
	                              .color  = 0xff4982a0,
	                              .inputs = any_array_create_from_raw(
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
	                                  },
	                                  2),
	                              .buttons = any_array_create_from_raw(
	                                  (void *[]){
	                                      ALLOC_INIT(ui_node_button_t, {.name          = _tr("gradient_type"),
	                                                                       .type          = "ENUM",
	                                                                       .output        = 0,
	                                                                       .default_value = f32_array_create_x(0),
	                                                                       .data          = u8_array_create_from_string(gradient_type_data),
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .height        = 0}),
	                                  },
	                                  1),
	                              .width = 0,
	                              .flags = 0});

	any_array_push(nodes_material_texture, gradient_texture_node_def);
	any_map_set(parser_material_node_vectors, "TEX_GRADIENT", gradient_texture_node_vector);
	any_map_set(parser_material_node_values, "TEX_GRADIENT", gradient_texture_node_value);
}
