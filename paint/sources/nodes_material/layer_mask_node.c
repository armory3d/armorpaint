
#include "../global.h"

char *layer_mask_node_value(ui_node_t *node, ui_node_socket_t *socket) {
	i32 l = node->buttons->buffer[0]->default_value->buffer[0];
	node_shader_add_texture(parser_material_kong, string_tmp("texpaint%s", i32_to_string(l)), string_tmp("_texpaint%s", i32_to_string(l)));
	return string_tmp("sample(texpaint%s, sampler_linear, tex_coord).r", i32_to_string(l));
}

void layer_mask_node_init() {

	ui_node_t *layer_mask_node_def =
	    ALLOC_INIT(ui_node_t, {.id      = 0,
	                           .name    = _tr("Layer Mask"),
	                           .type    = "LAYER_MASK", // extension
	                           .x       = 0,
	                           .y       = 0,
	                           .color   = 0xff4982a0,
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
	                                   ALLOC_INIT(ui_node_button_t, {.name          = _tr("Layer"),
	                                                                 .type          = "ENUM",
	                                                                 .output        = -1,
	                                                                 .default_value = f32_array_create_x(0),
	                                                                 .data          = u8_array_create_from_string(""),
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .height        = 0}),
	                               },
	                               1),
	                           .width = 0,
	                           .flags = 0});

	any_array_push(nodes_material_input, layer_mask_node_def);
	any_map_set(parser_material_node_values, "LAYER_MASK", layer_mask_node_value);
}
