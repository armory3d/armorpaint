
#include "../global.h"

char *value_node_value(ui_node_t *node, ui_node_socket_t *socket) {
	return parser_material_vec1(node->outputs->buffer[0]->default_value->buffer[0]);
}

void value_node_init() {

	ui_node_t *value_node_def =
	    ALLOC_INIT(ui_node_t, {.id      = 0,
	                           .name    = _tr("Value"),
	                           .type    = "VALUE",
	                           .x       = 0,
	                           .y       = 0,
	                           .color   = 0xffb34f5a,
	                           .inputs  = any_array_create_from_raw((void *[]){}, 0),
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
	                                   ALLOC_INIT(ui_node_button_t, {.name          = _tr("Value"),
	                                                                 .type          = "VALUE",
	                                                                 .output        = 0,
	                                                                 .default_value = f32_array_create_x(0),
	                                                                 .data          = NULL,
	                                                                 .min           = 0.0,
	                                                                 .max           = 10.0,
	                                                                 .precision     = 100,
	                                                                 .height        = 0}),
	                               },
	                               1),
	                           .width = 0,
	                           .flags = 0});

	any_array_push(nodes_material_input, value_node_def);
	any_map_set(parser_material_node_values, "VALUE", value_node_value);
}
