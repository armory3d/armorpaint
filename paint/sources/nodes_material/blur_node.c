
#include "../global.h"

char *blur_node_vector(ui_node_t *node, ui_node_socket_t *socket) {
	if (parser_material_blur_passthrough) {
		return parser_material_parse_vector_input(node->inputs->buffer[0]);
	}
	char *strength = parser_material_parse_value_input(node->inputs->buffer[1], false);
	char *steps    = string_tmp("(%s * 10.0 + 1.0)", strength);
	char *tex_name = string_tmp("texblur_%s", parser_material_node_name(node, NULL));
	node_shader_add_texture(parser_material_kong, tex_name, string_tmp("_%s", tex_name));
	node_shader_add_constant(parser_material_kong, string_tmp("%s_size: float2", tex_name), string_tmp("_size(_%s)", tex_name));
	char *store = parser_material_store_var_name(node);
	parser_material_write(parser_material_kong, string_tmp("var %s_res: float3 = float3(0.0, 0.0, 0.0);", store));
	parser_material_write(parser_material_kong, string_tmp("for (var i: int = 0; i <= int(%s * 2.0); i += 1) {", steps));
	parser_material_write(parser_material_kong, string_tmp("for (var j: int = 0; j <= int(%s * 2.0); j += 1) {", steps));
	parser_material_write(parser_material_kong,
	                      string_tmp("%s_res += sample(%s, sampler_linear, tex_coord + float2(float(i) - %s, float(j) - %s) / constants.%s_size).rgb;", store,
	                                 tex_name, steps, steps, tex_name));
	parser_material_write(parser_material_kong, "}");
	parser_material_write(parser_material_kong, "}");
	parser_material_write(parser_material_kong, string_tmp("%s_res = %s_res / ((%s * 2.0 + 1.0) * (%s * 2.0 + 1.0));", store, store, steps, steps));
	return string_tmp("%s_res", store);
}

void blur_node_init() {

	ui_node_t *blur_node_def =
	    ALLOC_INIT(ui_node_t, {.id     = 0,
	                           .name   = _tr("Blur"),
	                           .type   = "BLUR", // extension
	                           .x      = 0,
	                           .y      = 0,
	                           .color  = 0xff448c6d,
	                           .inputs = any_array_create_from_raw(
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
	                                                                 .name          = _tr("Strength"),
	                                                                 .type          = "VALUE",
	                                                                 .color         = 0xffa1a1a1,
	                                                                 .default_value = f32_array_create_x(0.5),
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

	any_array_push(nodes_material_color, blur_node_def);
	any_map_set(parser_material_node_vectors, "BLUR", blur_node_vector);
}
