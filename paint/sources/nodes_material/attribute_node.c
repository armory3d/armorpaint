
#include "../global.h"

char *attribute_node_vector(ui_node_t *node, ui_node_socket_t *socket) {
	if (socket == node->outputs->buffer[0]) { // Color
		if (parser_material_kong->context->allow_vcols) {
			node_shader_context_add_elem(parser_material_kong->context, "col", "short4norm"); // Vcols only for now
			return "input.vcolor";
		}
		else {
			return ("float3(0.0, 0.0, 0.0)");
		}
	}
	else {                                                                                // Vector
		node_shader_context_add_elem(parser_material_kong->context, "tex", "short2norm"); // UVMaps only for now
		return "float3(tex_coord.x, tex_coord.y, 0.0)";
	}
}

char *attribute_node_value(ui_node_t *node, ui_node_socket_t *socket) {
	node_shader_add_constant(parser_material_kong, "time: float", "_time");
	return "constants.time";
}

void attribute_node_init() {

	ui_node_t *attribute_node_def =
	    ALLOC_INIT(ui_node_t, {.id      = 0,
	                              .name    = _tr("Attribute"),
	                              .type    = "ATTRIBUTE",
	                              .x       = 0,
	                              .y       = 0,
	                              .color   = 0xffb34f5a,
	                              .inputs  = any_array_create_from_raw((void *[]){}, 0),
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
	                                                                       .name          = _tr("Vector"),
	                                                                       .type          = "VECTOR",
	                                                                       .color         = 0xff6363c7,
	                                                                       .default_value = f32_array_create_xyz(0.0, 0.0, 0.0),
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .display       = 0}),
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
	                                      ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                       .node_id       = 0,
	                                                                       .name          = _tr("Alpha"),
	                                                                       .type          = "VALUE",
	                                                                       .color         = 0xffa1a1a1,
	                                                                       .default_value = f32_array_create_x(0.0),
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .display       = 0}),
	                                  },
	                                  4),
	                              .buttons = any_array_create_from_raw(
	                                  (void *[]){
	                                      ALLOC_INIT(ui_node_button_t, {.name          = _tr("Name"),
	                                                                       .type          = "STRING",
	                                                                       .output        = -1,
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

	any_array_push(nodes_material_input, attribute_node_def);
	any_map_set(parser_material_node_vectors, "ATTRIBUTE", attribute_node_vector);
	any_map_set(parser_material_node_values, "ATTRIBUTE", attribute_node_value);
}
