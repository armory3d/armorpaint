
#include "../global.h"

char *warp_node_vector(ui_node_t *node, ui_node_socket_t *socket) {
	if (parser_material_warp_passthrough) {
		return parser_material_parse_vector_input(node->inputs->buffer[0]);
	}
	char *angle    = parser_material_parse_value_input(node->inputs->buffer[1], true);
	char *mask     = parser_material_parse_value_input(node->inputs->buffer[2], true);
	char *tex_name = string_tmp("texwarp_%s", parser_material_node_name(node, NULL));
	node_shader_add_texture(parser_material_kong, tex_name, string_tmp("_%s", tex_name));
	char *store = parser_material_store_var_name(node);
	f32   pi    = math_pi();
	parser_material_write(parser_material_kong, string_tmp("var %s_rad: float = %s * (%s / 180.0);", store, angle, f32_to_string(pi)));
	parser_material_write(parser_material_kong, string_tmp("var %s_x: float = cos(%s_rad);", store, store));
	parser_material_write(parser_material_kong, string_tmp("var %s_y: float = sin(%s_rad);", store, store));
	return string_tmp("sample(%s, sampler_linear, tex_coord + float2(%s_x, %s_y) * %s).rgb", tex_name, store, store, mask);
}

void warp_node_init() {

	ui_node_t *warp_node_def =
	    ALLOC_INIT(ui_node_t, {.id     = 0,
	                           .name   = _tr("Warp"),
	                           .type   = "DIRECT_WARP", // extension
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
	                                                                 .name          = _tr("Angle"),
	                                                                 .type          = "VALUE",
	                                                                 .color         = 0xffa1a1a1,
	                                                                 .default_value = f32_array_create_x(0.0),
	                                                                 .min           = 0.0,
	                                                                 .max           = 360.0,
	                                                                 .precision     = 10,
	                                                                 .display       = 0}),
	                                   ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                 .node_id       = 0,
	                                                                 .name          = _tr("Mask"),
	                                                                 .type          = "VALUE",
	                                                                 .color         = 0xffa1a1a1,
	                                                                 .default_value = f32_array_create_x(0.5),
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .display       = 0}),
	                               },
	                               3),
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

	any_array_push(nodes_material_color, warp_node_def);
	any_map_set(parser_material_node_vectors, "DIRECT_WARP", warp_node_vector);
}
