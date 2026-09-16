
#include "../global.h"

char *texture_coordinate_node_vector(ui_node_t *node, ui_node_socket_t *socket) {
	if (socket == node->outputs->buffer[0]) { // Generated - bounds
		parser_material_kong->frag_bposition = true;
		return "bposition";
	}
	else if (socket == node->outputs->buffer[1]) { // Normal
		parser_material_kong->frag_n = true;
		return "n";
	}
	else if (socket == node->outputs->buffer[2]) { // UV
		node_shader_context_add_elem(parser_material_kong->context, "tex", "short2norm");
		return "float3(tex_coord.x, tex_coord.y, 0.0)";
	}
	else if (socket == node->outputs->buffer[3]) { // Object
		parser_material_kong->frag_mposition = true;
		return "input.mposition";
	}
	else if (socket == node->outputs->buffer[4]) { // Camera
		parser_material_kong->frag_vposition = true;
		return "input.vposition";
	}
	else if (socket == node->outputs->buffer[5]) { // Window
		parser_material_kong->frag_wvpposition = true;
		return "input.wvpposition.xyz";
	}
	return "";
}

void texture_coordinate_node_init() {

	ui_node_t *texture_coordinate_node_def = ALLOC_INIT(ui_node_t, {.id      = 0,
	                                                                .name    = _tr("Texture Coordinate"),
	                                                                .type    = "TEX_COORD",
	                                                                .x       = 0,
	                                                                .y       = 0,
	                                                                .color   = 0xffb34f5a,
	                                                                .inputs  = any_array_create_from_raw((void *[]){}, 0),
	                                                                .outputs = any_array_create_from_raw(
	                                                                    (void *[]){
	                                                                        ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                                                      .node_id       = 0,
	                                                                                                      .name          = _tr("Generated"),
	                                                                                                      .type          = "VECTOR",
	                                                                                                      .color         = 0xff6363c7,
	                                                                                                      .default_value = f32_array_create_xyz(0.0, 0.0, 0.0),
	                                                                                                      .min           = 0.0,
	                                                                                                      .max           = 1.0,
	                                                                                                      .precision     = 100,
	                                                                                                      .display       = 0}),
	                                                                        ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                                                      .node_id       = 0,
	                                                                                                      .name          = _tr("Normal"),
	                                                                                                      .type          = "VECTOR",
	                                                                                                      .color         = 0xff6363c7,
	                                                                                                      .default_value = f32_array_create_xyz(0.0, 0.0, 0.0),
	                                                                                                      .min           = 0.0,
	                                                                                                      .max           = 1.0,
	                                                                                                      .precision     = 100,
	                                                                                                      .display       = 0}),
	                                                                        ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                                                      .node_id       = 0,
	                                                                                                      .name          = _tr("UV"),
	                                                                                                      .type          = "VECTOR",
	                                                                                                      .color         = 0xff6363c7,
	                                                                                                      .default_value = f32_array_create_xyz(0.0, 0.0, 0.0),
	                                                                                                      .min           = 0.0,
	                                                                                                      .max           = 1.0,
	                                                                                                      .precision     = 100,
	                                                                                                      .display       = 0}),
	                                                                        ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                                                      .node_id       = 0,
	                                                                                                      .name          = _tr("Object"),
	                                                                                                      .type          = "VECTOR",
	                                                                                                      .color         = 0xff6363c7,
	                                                                                                      .default_value = f32_array_create_xyz(0.0, 0.0, 0.0),
	                                                                                                      .min           = 0.0,
	                                                                                                      .max           = 1.0,
	                                                                                                      .precision     = 100,
	                                                                                                      .display       = 0}),
	                                                                        ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                                                      .node_id       = 0,
	                                                                                                      .name          = _tr("Camera"),
	                                                                                                      .type          = "VECTOR",
	                                                                                                      .color         = 0xff6363c7,
	                                                                                                      .default_value = f32_array_create_xyz(0.0, 0.0, 0.0),
	                                                                                                      .min           = 0.0,
	                                                                                                      .max           = 1.0,
	                                                                                                      .precision     = 100,
	                                                                                                      .display       = 0}),
	                                                                        ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                                                      .node_id       = 0,
	                                                                                                      .name          = _tr("Window"),
	                                                                                                      .type          = "VECTOR",
	                                                                                                      .color         = 0xff6363c7,
	                                                                                                      .default_value = f32_array_create_xyz(0.0, 0.0, 0.0),
	                                                                                                      .min           = 0.0,
	                                                                                                      .max           = 1.0,
	                                                                                                      .precision     = 100,
	                                                                                                      .display       = 0}),
	                                                                    },
	                                                                    6),
	                                                                .buttons = any_array_create_from_raw((void *[]){}, 0)});

	any_array_push(nodes_material_input, texture_coordinate_node_def);
	any_map_set(parser_material_node_vectors, "TEX_COORD", texture_coordinate_node_vector);
}
