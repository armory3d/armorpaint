
#include "../global.h"

char *rgb_to_bw_node_value(ui_node_t *node, ui_node_socket_t *socket) {
	char *col = parser_material_parse_vector_input(node->inputs->buffer[0]);
	return string_tmp("(((%s.r * 0.3 + %s.g * 0.59 + %s.b * 0.11) / 3.0) * 2.5)", col, col, col);
}

void rgb_to_bw_node_init() {

	ui_node_t *rgb_to_bw_node_def =
	    ALLOC_INIT(ui_node_t, {.id     = 0,
	                              .name   = _tr("RGB to BW"),
	                              .type   = "RGBTOBW",
	                              .x      = 0,
	                              .y      = 0,
	                              .color  = 0xff62676d,
	                              .inputs = any_array_create_from_raw(
	                                  (void *[]){
	                                      ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                       .node_id       = 0,
	                                                                       .name          = _tr("Color"),
	                                                                       .type          = "RGBA",
	                                                                       .color         = 0xffc7c729,
	                                                                       .default_value = f32_array_create_xyzw(0.5, 0.5, 0.5, 1.0),
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
	                                                                       .name          = _tr("Val"),
	                                                                       .type          = "VALUE",
	                                                                       .color         = 0xffa1a1a1,
	                                                                       .default_value = f32_array_create_x(0.0),
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .display       = 0}),
	                                  },
	                                  1),
	                              .buttons = any_array_create_from_raw((void *[]){}, 0),
	                              .width   = 0,
	                              .flags   = 0});

	any_array_push(nodes_material_color, rgb_to_bw_node_def);
	any_map_set(parser_material_node_values, "RGBTOBW", rgb_to_bw_node_value);
}
