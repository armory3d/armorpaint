
#include "../global.h"

char *normal_map_node_vector(ui_node_t *node, ui_node_socket_t *socket) {
	char *strength = parser_material_parse_value_input(node->inputs->buffer[0], false);
	char *norm     = parser_material_parse_vector_input(node->inputs->buffer[1]);

	char *store = parser_material_store_var_name(node);
	parser_material_write(parser_material_kong, string_tmp("var %s_texn: float3 = %s * 2.0 - 1.0;", store, norm));
	parser_material_write(parser_material_kong, string_tmp("%s_texn.xy = %s * %s_texn.xy;", store, strength, store));
	parser_material_write(parser_material_kong, string_tmp("%s_texn = normalize(%s_texn);", store, store));

	return string_tmp("(0.5 * %s_texn + 0.5)", store);
}

void normal_map_node_init() {

	ui_node_t *normal_map_node_def =
	    ALLOC_INIT(ui_node_t, {.id     = 0,
	                              .name   = _tr("Normal Map"),
	                              .type   = "NORMAL_MAP",
	                              .x      = 0,
	                              .y      = 0,
	                              .color  = 0xff522c99,
	                              .inputs = any_array_create_from_raw(
	                                  (void *[]){
	                                      ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                       .node_id       = 0,
	                                                                       .name          = _tr("Strength"),
	                                                                       .type          = "VALUE",
	                                                                       .color         = 0xffa1a1a1,
	                                                                       .default_value = f32_array_create_x(1.0),
	                                                                       .min           = 0.0,
	                                                                       .max           = 2.0,
	                                                                       .precision     = 100,
	                                                                       .display       = 0}),
	                                      ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                       .node_id       = 0,
	                                                                       .name          = _tr("Normal Map"),
	                                                                       .type          = "VECTOR",
	                                                                       .color         = -10238109,
	                                                                       .default_value = f32_array_create_xyz(0.5, 0.5, 1.0),
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
	                                                                       .name          = _tr("Normal Map"),
	                                                                       .type          = "VECTOR",
	                                                                       .color         = -10238109,
	                                                                       .default_value = f32_array_create_xyz(0.5, 0.5, 1.0),
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .display       = 0}),
	                                  },
	                                  1),
	                              .buttons = any_array_create_from_raw((void *[]){}, 0),
	                              .width   = 0,
	                              .flags   = 0});

	any_array_push(nodes_material_utilities, normal_map_node_def);
	any_map_set(parser_material_node_vectors, "NORMAL_MAP", normal_map_node_vector);
}
