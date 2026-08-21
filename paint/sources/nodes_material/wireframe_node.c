
#include "../global.h"

char *wireframe_node_value(ui_node_t *node, ui_node_socket_t *socket) {
	node_shader_add_texture(parser_material_kong, "texuvmap", "_texuvmap");
	// let use_pixel_size: bool = node.buttons[0].default_value[0] > 0.0;
	// let pixel_size: f32 = parse_value_input(node.inputs[0]);
	return "sample_lod(texuvmap, sampler_linear, tex_coord, 0.0).r";
}

void wireframe_node_init() {

	ui_node_t *wireframe_node_def =
	    ALLOC_INIT(ui_node_t, {.id     = 0,
	                              .name   = _tr("Wireframe"),
	                              .type   = "WIREFRAME",
	                              .x      = 0,
	                              .y      = 0,
	                              .color  = 0xffb34f5a,
	                              .inputs = any_array_create_from_raw(
	                                  (void *[]){
	                                      ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                       .node_id       = 0,
	                                                                       .name          = _tr("Size"),
	                                                                       .type          = "VALUE",
	                                                                       .color         = 0xffa1a1a1,
	                                                                       .default_value = f32_array_create_x(0.01),
	                                                                       .min           = 0.0,
	                                                                       .max           = 0.1,
	                                                                       .precision     = 100,
	                                                                       .display       = 0}),
	                                  },
	                                  1),
	                              .outputs = any_array_create_from_raw(
	                                  (void *[]){
	                                      ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                       .node_id       = 0,
	                                                                       .name          = _tr("Factor"),
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
	                                      ALLOC_INIT(ui_node_button_t, {.name          = _tr("Pixel Size"),
	                                                                       .type          = "BOOL",
	                                                                       .output        = 0,
	                                                                       .default_value = f32_array_create_x(0),
	                                                                       .data          = NULL,
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .height        = 0}),
	                                  },
	                                  1),
	                              .width = 0,
	                              .flags = 0});

	any_array_push(nodes_material_input, wireframe_node_def);
	any_map_set(parser_material_node_values, "WIREFRAME", wireframe_node_value);
}
