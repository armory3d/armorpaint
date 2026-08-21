
#include "../global.h"

char *invert_color_node_vector(ui_node_t *node, ui_node_socket_t *socket) {
	char *fac     = parser_material_parse_value_input(node->inputs->buffer[0], false);
	char *out_col = parser_material_parse_vector_input(node->inputs->buffer[1]);
	return string_tmp("lerp3(%s, float3(1.0, 1.0, 1.0) - (%s), %s)", out_col, out_col, fac);
}

void invert_color_node_init() {

	ui_node_t *invert_color_node_def = ALLOC_INIT(ui_node_t, {.id     = 0,
	                                                          .name   = _tr("Invert Color"),
	                                                          .type   = "INVERT_COLOR",
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
	                                                                                                .default_value = f32_array_create_xyzw(0.0, 0.0, 0.0, 1.0),
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
	                                                                                                .default_value = f32_array_create_xyzw(0.8, 0.8, 0.8, 1.0),
	                                                                                                .min           = 0.0,
	                                                                                                .max           = 1.0,
	                                                                                                .precision     = 100,
	                                                                                                .display       = 0}),
	                                                              },
	                                                              1),
	                                                          .buttons = any_array_create_from_raw((void *[]){}, 0),
	                                                          .width   = 0,
	                                                          .flags   = 0});

	any_array_push(nodes_material_color, invert_color_node_def);
	any_map_set(parser_material_node_vectors, "INVERT_COLOR", invert_color_node_vector);
}
