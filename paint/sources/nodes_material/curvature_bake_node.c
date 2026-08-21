
#include "../global.h"

char *curvature_bake_node_value(ui_node_t *node, ui_node_socket_t *socket) {
	if (parser_material_bake_passthrough) {
		parser_material_bake_passthrough_strength = string_copy(parser_material_parse_value_input(node->inputs->buffer[0], false));
		parser_material_bake_passthrough_radius = string_copy(parser_material_parse_value_input(node->inputs->buffer[1], false));
		parser_material_bake_passthrough_offset = string_copy(parser_material_parse_value_input(node->inputs->buffer[2], false));
		return "0.0";
	}
	char *tex_name = string_tmp("texbake_%s", parser_material_node_name(node, NULL));
	node_shader_add_texture(parser_material_kong, tex_name, string_tmp("_%s", tex_name));
	char *store = parser_material_store_var_name(node);
	parser_material_write(parser_material_kong, string_tmp("var %s_res: float = sample(%s, sampler_linear, tex_coord).r;", store, tex_name));
	return string_tmp("%s_res", store);
}

void curvature_bake_node_init() {

	ui_node_t *curvature_bake_node_def =
	    ALLOC_INIT(ui_node_t, {.id     = 0,
	                              .name   = _tr("Curvature Texture"),
	                              .type   = "BAKE_CURVATURE", // extension
	                              .x      = 0,
	                              .y      = 0,
	                              .color  = 0xff4982a0,
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
	                                                                       .name          = _tr("Radius"),
	                                                                       .type          = "VALUE",
	                                                                       .color         = 0xffa1a1a1,
	                                                                       .default_value = f32_array_create_x(1.0),
	                                                                       .min           = 0.0,
	                                                                       .max           = 2.0,
	                                                                       .precision     = 100,
	                                                                       .display       = 0}),
	                                      ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                       .node_id       = 0,
	                                                                       .name          = _tr("Offset"),
	                                                                       .type          = "VALUE",
	                                                                       .color         = 0xffa1a1a1,
	                                                                       .default_value = f32_array_create_x(0.0),
	                                                                       .min           = -2.0,
	                                                                       .max           = 2.0,
	                                                                       .precision     = 100,
	                                                                       .display       = 0}),
	                                  },
	                                  3),
	                              .outputs = any_array_create_from_raw(
	                                  (void *[]){
	                                      ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                       .node_id       = 0,
	                                                                       .name          = _tr("Value"),
	                                                                       .type          = "VALUE",
	                                                                       .color         = 0xffa1a1a1,
	                                                                       .default_value = f32_array_create_x(1.0),
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .display       = 0}),
	                                  },
	                                  1),
	                              .buttons = any_array_create_from_raw((void *[]){}, 0),
	                              .width   = 0,
	                              .flags   = 0});

	any_array_push(nodes_material_texture, curvature_bake_node_def);
	any_map_set(parser_material_node_values, "BAKE_CURVATURE", curvature_bake_node_value);
}
