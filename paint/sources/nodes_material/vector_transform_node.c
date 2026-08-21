
#include "../global.h"

char *vector_transform_node_vector(ui_node_t *node, ui_node_socket_t *socket) {
	char *vec  = parser_material_parse_vector_input(node->inputs->buffer[0]);
	char *type = to_upper_case(u8_array_string_at(node->buttons->buffer[0]->data, node->buttons->buffer[0]->default_value->buffer[0]));
	char *from = to_upper_case(u8_array_string_at(node->buttons->buffer[1]->data, node->buttons->buffer[1]->default_value->buffer[0]));
	char *to   = to_upper_case(u8_array_string_at(node->buttons->buffer[2]->data, node->buttons->buffer[2]->default_value->buffer[0]));

	if (string_equals(from, "OBJECT") && string_equals(to, "WORLD")) {
		if (string_equals(type, "NORMAL")) {
			node_shader_add_constant(parser_material_kong, "N: float3x3", "_normal_matrix");
			return string_tmp("normalize(constants.N * %s)", vec);
		}
		node_shader_add_constant(parser_material_kong, "W: float4x4", "_world_matrix");
		char *w = string_equals(type, "POINT") ? "1.0" : "0.0";
		return string_tmp("(constants.W * float4(%s, %s)).xyz", vec, w);
	}

	if (string_equals(from, "OBJECT") && string_equals(to, "CAMERA")) {
		node_shader_add_constant(parser_material_kong, "WV: float4x4", "_world_view_matrix");
		if (string_equals(type, "NORMAL")) {
			return string_tmp("normalize((constants.WV * float4(%s, 0.0)).xyz)", vec);
		}
		char *w = string_equals(type, "POINT") ? "1.0" : "0.0";
		return string_tmp("(constants.WV * float4(%s, %s)).xyz", vec, w);
	}

	return string_copy(vec);
}

void vector_transform_node_init() {

	char *type_data  = string_tmp("%s\n%s\n%s", _tr("Vector"), _tr("Point"), _tr("Normal"));
	char *space_data = string_tmp("%s\n%s\n%s", _tr("World"), _tr("Object"), _tr("Camera"));

	ui_node_t *vector_transform_node_def =
	    ALLOC_INIT(ui_node_t, {.id     = 0,
	                              .name   = _tr("Vector Transform"),
	                              .type   = "VECT_TRANSFORM",
	                              .x      = 0,
	                              .y      = 0,
	                              .color  = 0xff62676d,
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
	                                                                       .display       = 1}),
	                                  },
	                                  1),
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
	                                      ALLOC_INIT(ui_node_button_t, {.name          = _tr("type"),
	                                                                       .type          = "ENUM",
	                                                                       .output        = 0,
	                                                                       .default_value = f32_array_create_x(0),
	                                                                       .data          = u8_array_create_from_string(type_data),
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .height        = 0}),
	                                      ALLOC_INIT(ui_node_button_t, {.name          = _tr("Convert From"),
	                                                                       .type          = "ENUM",
	                                                                       .output        = 0,
	                                                                       .default_value = f32_array_create_x(1), // default: Object
	                                                                       .data          = u8_array_create_from_string(space_data),
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .height        = 0}),
	                                      ALLOC_INIT(ui_node_button_t, {.name          = _tr("Convert To"),
	                                                                       .type          = "ENUM",
	                                                                       .output        = 0,
	                                                                       .default_value = f32_array_create_x(0), // default: World
	                                                                       .data          = u8_array_create_from_string(space_data),
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .height        = 0}),
	                                  },
	                                  3),
	                              .width = 0,
	                              .flags = 0});

	any_array_push(nodes_material_utilities, vector_transform_node_def);
	any_map_set(parser_material_node_vectors, "VECT_TRANSFORM", vector_transform_node_vector);
}
