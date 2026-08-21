
#include "../global.h"

char *mapping_node_vector(ui_node_t *node, ui_node_socket_t *socket) {
	char *out              = parser_material_parse_vector_input(node->inputs->buffer[0]);
	char *node_translation = parser_material_parse_vector_input(node->inputs->buffer[1]);
	char *node_rotation    = parser_material_parse_vector_input(node->inputs->buffer[2]);
	char *node_scale       = parser_material_parse_vector_input(node->inputs->buffer[3]);
	if (!string_equals(node_scale, "float3(1.0, 1.0, 1.0)")) {
		out = string_tmp("(%s * %s)", out, node_scale);
	}
	if (!string_equals(node_rotation, "float3(0.0, 0.0, 0.0)")) {
		char *name = parser_material_store_var_name(node);
		char *v    = string_tmp("%s_v", name);
		char *rx   = string_tmp("%s_rx", name);
		char *ry   = string_tmp("%s_ry", name);
		char *rz   = string_tmp("%s_rz", name);
		parser_material_write(parser_material_kong, string_tmp("var %s: float3 = %s;", v, out));
		parser_material_write(parser_material_kong, string_tmp("var %s: float = %s.x * (3.14159265 / 180.0);", rx, node_rotation));
		parser_material_write(parser_material_kong, string_tmp("var %s: float = %s.y * (3.14159265 / 180.0);", ry, node_rotation));
		parser_material_write(parser_material_kong, string_tmp("var %s: float = %s.z * (3.14159265 / 180.0);", rz, node_rotation));
		parser_material_write(parser_material_kong,
		                      string_tmp("%s = float3(%s.x * cos(%s) - %s.y * sin(%s), %s.x * sin(%s) + %s.y * cos(%s), %s.z);", v, v, rz, v, rz, v, rz, v, rz, v));
		parser_material_write(parser_material_kong, string_tmp("%s = float3(%s.x * cos(%s) + %s.z * sin(%s), %s.y, -%s.x * sin(%s) + %s.z * cos(%s));", v, v, ry, v,
		                                                   ry, v, v, ry, v, ry));
		parser_material_write(parser_material_kong,
		                      string_tmp("%s = float3(%s.x, %s.y * cos(%s) - %s.z * sin(%s), %s.y * sin(%s) + %s.z * cos(%s));", v, v, v, rx, v, rx, v, rx, v, rx));
		out = v;
	}
	if (!string_equals(node_translation, "float3(0.0, 0.0, 0.0)")) {
		out = string_tmp("(%s + %s)", out, node_translation);
	}
	return out;
}

void mapping_node_init() {

	ui_node_t *mapping_node_def =
	    ALLOC_INIT(ui_node_t, {.id     = 0,
	                              .name   = _tr("Mapping"),
	                              .type   = "MAPPING",
	                              .x      = 0,
	                              .y      = 0,
	                              .color  = 0xff522c99,
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
	                                      ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                       .node_id       = 0,
	                                                                       .name          = _tr("Location"),
	                                                                       .type          = "VECTOR",
	                                                                       .color         = 0xff6363c7,
	                                                                       .default_value = f32_array_create_xyz(0.0, 0.0, 0.0),
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .display       = 1}),
	                                      ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                       .node_id       = 0,
	                                                                       .name          = _tr("Rotation"),
	                                                                       .type          = "VECTOR",
	                                                                       .color         = 0xff6363c7,
	                                                                       .default_value = f32_array_create_xyz(0.0, 0.0, 0.0),
	                                                                       .min           = 0.0,
	                                                                       .max           = 360.0,
	                                                                       .precision     = 10,
	                                                                       .display       = 1}),
	                                      ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                       .node_id       = 0,
	                                                                       .name          = _tr("Scale"),
	                                                                       .type          = "VECTOR",
	                                                                       .color         = 0xff6363c7,
	                                                                       .default_value = f32_array_create_xyz(1.0, 1.0, 1.0),
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .display       = 1}),
	                                  },
	                                  4),
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
	                              .buttons = any_array_create_from_raw((void *[]){}, 0),
	                              .width   = 0,
	                              .flags   = 0});

	any_array_push(nodes_material_utilities, mapping_node_def);
	any_map_set(parser_material_node_vectors, "MAPPING", mapping_node_vector);
}
