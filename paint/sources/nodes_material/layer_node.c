
#include "../global.h"

char *layer_node_vector(ui_node_t *node, ui_node_socket_t *socket) {
	i32   l  = node->buttons->buffer[0]->default_value->buffer[0];
	char *ls = i32_to_string(l);
	if (socket == node->outputs->buffer[0]) { // Base
		node_shader_add_texture(parser_material_kong, string_tmp("texpaint%s", ls), string_tmp("_texpaint%s", ls));
		return string_tmp("sample(texpaint%s, sampler_linear, tex_coord).rgb", ls);
	}
	else { // Normal
		node_shader_add_texture(parser_material_kong, string_tmp("texpaint_nor%s", ls), string_tmp("_texpaint_nor%s", ls));
		return string_tmp("sample(texpaint_nor%s, sampler_linear, tex_coord).rgb", ls);
	}
}

char *layer_node_value(ui_node_t *node, ui_node_socket_t *socket) {
	i32   l  = node->buttons->buffer[0]->default_value->buffer[0];
	char *ls = i32_to_string(l);
	if (socket == node->outputs->buffer[1]) { // Opac
		node_shader_add_texture(parser_material_kong, string_tmp("texpaint%s", ls), string_tmp("_texpaint%s", ls));
		return string_tmp("sample(texpaint%s, sampler_linear, tex_coord).a", ls);
	}
	else if (socket == node->outputs->buffer[2]) { // Occ
		node_shader_add_texture(parser_material_kong, string_tmp("texpaint_pack%s", ls), string_tmp("_texpaint_pack%s", ls));
		return string_tmp("sample(texpaint_pack%s, sampler_linear, tex_coord).r", ls);
	}
	else if (socket == node->outputs->buffer[3]) { // Rough
		node_shader_add_texture(parser_material_kong, string_tmp("texpaint_pack%s", ls), string_tmp("_texpaint_pack%s", ls));
		return string_tmp("sample(texpaint_pack%s, sampler_linear, tex_coord).g", ls);
	}
	else if (socket == node->outputs->buffer[4]) { // Metal
		node_shader_add_texture(parser_material_kong, string_tmp("texpaint_pack%s", ls), string_tmp("_texpaint_pack%s", ls));
		return string_tmp("sample(texpaint_pack%s, sampler_linear, tex_coord).b", ls);
	}
	else if (socket == node->outputs->buffer[6]) {
		return "0.0";
	} // Emission
	else if (socket == node->outputs->buffer[7]) { // Height
		node_shader_add_texture(parser_material_kong, string_tmp("texpaint_pack%s", ls), string_tmp("_texpaint_pack%s", ls));
		return string_tmp("sample(texpaint_pack%s, sampler_linear, tex_coord).a", ls);
	}
	else {
		return "0.0";
	} // Subsurface
}

void layer_node_init() {

	ui_node_t *layer_node_def =
	    ALLOC_INIT(ui_node_t, {.id      = 0,
	                              .name    = _tr("Layer"),
	                              .type    = "LAYER", // extension
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

	any_array_push(nodes_material_input, layer_node_def);
	any_map_set(parser_material_node_vectors, "LAYER", layer_node_vector);
	any_map_set(parser_material_node_values, "LAYER", layer_node_value);
}
