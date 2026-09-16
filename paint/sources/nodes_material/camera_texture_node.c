
#include "../global.h"

char *camera_texture_node_vector(ui_node_t *node, ui_node_socket_t *socket) {
	char *tex_name = string_tmp("texcamera_%s", parser_material_node_name(node, NULL));
	node_shader_add_texture(parser_material_kong, tex_name, "_camera_texture");
	char *store = parser_material_store_var_name(node);
	parser_material_write(parser_material_kong, string_tmp("var %s_res: float3 = sample(%s, sampler_linear, tex_coord).rgb;", store, tex_name));
	return string_tmp("%s_res", store);
}

void camera_texture_node_init() {

	ui_node_t *camera_texture_node_def =
	    ALLOC_INIT(ui_node_t, {.id      = 0,
	                           .name    = _tr("Camera Texture"),
	                           .type    = "TEX_CAMERA", // extension
	                           .x       = 0,
	                           .y       = 0,
	                           .color   = 0xff4982a0,
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
	                               },
	                               1),
	                           .buttons = any_array_create_from_raw((void *[]){}, 0),
	                           .width   = 0,
	                           .flags   = 0});

	any_array_push(nodes_material_texture, camera_texture_node_def);
	any_map_set(parser_material_node_vectors, "TEX_CAMERA", camera_texture_node_vector);
}
