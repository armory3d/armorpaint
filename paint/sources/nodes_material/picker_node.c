
#include "../global.h"

char *picker_node_vector(ui_node_t *node, ui_node_socket_t *socket) {
	if (socket == node->outputs->buffer[0]) { // Base
		node_shader_add_constant(parser_material_kong, "picker_base: float3", "_picker_base");
		return "constants.picker_base";
	}
	else { // Normal
		node_shader_add_constant(parser_material_kong, "picker_normal: float3", "_picker_normal");
		return "constants.picker_normal";
	}
}

char *picker_node_value(ui_node_t *node, ui_node_socket_t *socket) {
	if (socket == node->outputs->buffer[1]) {
		node_shader_add_constant(parser_material_kong, "picker_opacity: float", "_picker_opacity");
		return "constants.picker_opacity";
	}
	else if (socket == node->outputs->buffer[2]) {
		node_shader_add_constant(parser_material_kong, "picker_occlusion: float", "_picker_occlusion");
		return "constants.picker_occlusion";
	}
	else if (socket == node->outputs->buffer[3]) {
		node_shader_add_constant(parser_material_kong, "picker_roughness: float", "_picker_roughness");
		return "constants.picker_roughness";
	}
	else if (socket == node->outputs->buffer[4]) {
		node_shader_add_constant(parser_material_kong, "picker_metallic: float", "_picker_metallic");
		return "constants.picker_metallic";
	}
	else if (socket == node->outputs->buffer[6]) {
		return "0.0";
	} // Emission
	else if (socket == node->outputs->buffer[7]) {
		node_shader_add_constant(parser_material_kong, "picker_height: float", "_picker_height");
		return "constants.picker_height";
	}
	else {
		return "0.0";
	} // Subsurface
}

void picker_node_init() {

	ui_node_t *picker_node_def = ALLOC_INIT(ui_node_t, {.id      = 0,
	                                                    .name    = _tr("Picker"),
	                                                    .type    = "PICKER", // extension
	                                                    .x       = 0,
	                                                    .y       = 0,
	                                                    .color   = 0xff4982a0,
	                                                    .inputs  = any_array_create_from_raw((void *[]){}, 0),
	                                                    .outputs = any_array_create_from_raw(
	                                                        (void *[]){
	                                                            ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                                          .node_id       = 0,
	                                                                                          .name          = _tr("Base Color"),
	                                                                                          .type          = "RGBA",
	                                                                                          .color         = 0xffc7c729,
	                                                                                          .default_value = f32_array_create_xyzw(0.0, 0.0, 0.0, 1.0),
	                                                                                          .min           = 0.0,
	                                                                                          .max           = 1.0,
	                                                                                          .precision     = 100,
	                                                                                          .display       = 0}),
	                                                            ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                                          .node_id       = 0,
	                                                                                          .name          = _tr("Opacity"),
	                                                                                          .type          = "VALUE",
	                                                                                          .color         = 0xffa1a1a1,
	                                                                                          .default_value = f32_array_create_x(1.0),
	                                                                                          .min           = 0.0,
	                                                                                          .max           = 1.0,
	                                                                                          .precision     = 100,
	                                                                                          .display       = 0}),
	                                                            ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                                          .node_id       = 0,
	                                                                                          .name          = _tr("Occlusion"),
	                                                                                          .type          = "VALUE",
	                                                                                          .color         = 0xffa1a1a1,
	                                                                                          .default_value = f32_array_create_x(1.0),
	                                                                                          .min           = 0.0,
	                                                                                          .max           = 1.0,
	                                                                                          .precision     = 100,
	                                                                                          .display       = 0}),
	                                                            ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                                          .node_id       = 0,
	                                                                                          .name          = _tr("Roughness"),
	                                                                                          .type          = "VALUE",
	                                                                                          .color         = 0xffa1a1a1,
	                                                                                          .default_value = f32_array_create_x(1.0),
	                                                                                          .min           = 0.0,
	                                                                                          .max           = 1.0,
	                                                                                          .precision     = 100,
	                                                                                          .display       = 0}),
	                                                            ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                                          .node_id       = 0,
	                                                                                          .name          = _tr("Metallic"),
	                                                                                          .type          = "VALUE",
	                                                                                          .color         = 0xffa1a1a1,
	                                                                                          .default_value = f32_array_create_x(1.0),
	                                                                                          .min           = 0.0,
	                                                                                          .max           = 1.0,
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
	                                                            ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                                          .node_id       = 0,
	                                                                                          .name          = _tr("Emission"),
	                                                                                          .type          = "VALUE",
	                                                                                          .color         = 0xffa1a1a1,
	                                                                                          .default_value = f32_array_create_x(1.0),
	                                                                                          .min           = 0.0,
	                                                                                          .max           = 1.0,
	                                                                                          .precision     = 100,
	                                                                                          .display       = 0}),
	                                                            ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                                          .node_id       = 0,
	                                                                                          .name          = _tr("Height"),
	                                                                                          .type          = "VALUE",
	                                                                                          .color         = 0xffa1a1a1,
	                                                                                          .default_value = f32_array_create_x(1.0),
	                                                                                          .min           = 0.0,
	                                                                                          .max           = 1.0,
	                                                                                          .precision     = 100,
	                                                                                          .display       = 0}),
	                                                            ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                                          .node_id       = 0,
	                                                                                          .name          = _tr("Subsurface"),
	                                                                                          .type          = "VALUE",
	                                                                                          .color         = 0xffa1a1a1,
	                                                                                          .default_value = f32_array_create_x(1.0),
	                                                                                          .min           = 0.0,
	                                                                                          .max           = 1.0,
	                                                                                          .precision     = 100,
	                                                                                          .display       = 0}),
	                                                        },
	                                                        9),
	                                                    .buttons = any_array_create_from_raw((void *[]){}, 0),
	                                                    .width   = 0,
	                                                    .flags   = 0});

	any_array_push(nodes_material_input, picker_node_def);
	any_map_set(parser_material_node_vectors, "PICKER", picker_node_vector);
	any_map_set(parser_material_node_values, "PICKER", picker_node_value);
}
