
#include "../global.h"

char *blur_node_input_alpha(ui_node_t *node) {
	if (node->inputs->length < 3) {
		return "1.0";
	}
	return parser_material_parse_value_input(node->inputs->buffer[1], false);
}

i32 blur_node_seamless_passes(ui_node_t *node) {
	if (node->buttons->length == 0 || node->buttons->buffer[0]->default_value->buffer[0] == 0) {
		return 0;
	}
	f32 strength = node->inputs->buffer[node->inputs->length - 1]->default_value->buffer[0];
	f32 r        = math_floor(strength * 10.0 + 1.0);
	return (i32)math_ceil(r * (r + 1.0) / 1.2);
}

static void blur_node_write_blurred(ui_node_t *node, char *store) {
	char *tex_name = string_tmp("texblur_%s", make_material_node_preview_id(node, NULL));
	node_shader_add_texture(parser_material_kong, tex_name, string_tmp("_%s", tex_name));
	if (blur_node_seamless_passes(node) > 0) {
		// Blurred along the surface at bake time
		parser_material_write(parser_material_kong, string_tmp("float4 %s_s = sample(%s, sampler_linear, tex_coord);", store, tex_name));
		parser_material_write(parser_material_kong, string_tmp("float3 %s_res = %s_s.rgb / max(%s_s.a, 0.0001);", store, store, store)); // Premultiplied
		parser_material_write(parser_material_kong, string_tmp("float %s_a = %s_s.a;", store, store));
		return;
	}
	char *strength = parser_material_parse_value_input(node->inputs->buffer[node->inputs->length - 1], false);
	char *steps    = string_tmp("(%s * 10.0 + 1.0)", strength);
	node_shader_add_constant(parser_material_kong, string_tmp("float2 %s_size", tex_name), string_tmp("_size(_%s)", tex_name));
	parser_material_write(parser_material_kong, string_tmp("float3 %s_res = float3(0.0, 0.0, 0.0);", store));
	parser_material_write(parser_material_kong, string_tmp("float %s_a = 0.0;", store));
	parser_material_write(parser_material_kong, string_tmp("float %s_n = 0.0;", store));
	parser_material_write(parser_material_kong, string_tmp("for (int i = 0; i <= int(%s * 2.0); i += 1) {", steps));
	parser_material_write(parser_material_kong, string_tmp("for (int j = 0; j <= int(%s * 2.0); j += 1) {", steps));
	parser_material_write(parser_material_kong,
	                      string_tmp("float4 %s_s = sample(%s, sampler_linear, tex_coord + float2(float(i) - %s, float(j) - %s) / constants.%s_size);", store,
	                                 tex_name, steps, steps, tex_name));
	parser_material_write(parser_material_kong, string_tmp("%s_res += %s_s.rgb;", store, store));
	parser_material_write(parser_material_kong, string_tmp("%s_a += %s_s.a;", store, store));
	parser_material_write(parser_material_kong, string_tmp("%s_n += 1.0;", store));
	parser_material_write(parser_material_kong, "}");
	parser_material_write(parser_material_kong, "}");
	parser_material_write(parser_material_kong, string_tmp("%s_res = %s_res / max(%s_a, 0.0001);", store, store, store)); // Premultiplied
	parser_material_write(parser_material_kong, string_tmp("%s_a = %s_a / %s_n;", store, store, store));
}

static char *blur_node_write(ui_node_t *node) {
	char *store = parser_material_store_var_name(node);
	char *key   = string_tmp("%s_blur", store);
	if (string_array_index_of(parser_material_parsed, key) >= 0) {
		return store;
	}
	any_array_push(parser_material_parsed, key);
	blur_node_write_blurred(node, store);

	ui_node_socket_t *strength_socket = node->inputs->buffer[node->inputs->length - 1];
	if (parser_material_get_input_link(strength_socket) == NULL && strength_socket->default_value->buffer[0] >= 0.1) {
		return store;
	}
	char *strength = parser_material_parse_value_input(strength_socket, false);
	char *col      = parser_material_parse_vector_input(node->inputs->buffer[0]);
	char *alpha    = blur_node_input_alpha(node);
	parser_material_write(parser_material_kong, string_tmp("float %s_f = clamp(%s * 10.0, 0.0, 1.0);", store, strength));
	parser_material_write(parser_material_kong, string_tmp("%s_res = lerp(%s, %s_res, %s_f);", store, col, store, store));
	parser_material_write(parser_material_kong, string_tmp("%s_a = lerp(%s, %s_a, %s_f);", store, alpha, store, store));
	return store;
}

char *blur_node_vector(ui_node_t *node, ui_node_socket_t *socket) {
	if (parser_material_blur_passthrough) {
		return parser_material_parse_vector_input(node->inputs->buffer[0]);
	}
	return string_tmp("%s_res", blur_node_write(node));
}

char *blur_node_value(ui_node_t *node, ui_node_socket_t *socket) {
	if (parser_material_blur_passthrough) {
		return blur_node_input_alpha(node);
	}
	return string_tmp("%s_a", blur_node_write(node));
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
	                                                                 .name          = _tr("Strength"),
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
	                               },
	                               2),
	                           .buttons = any_array_create_from_raw(
	                               (void *[]){
	                                   ALLOC_INIT(ui_node_button_t, {.name          = _tr("Seamless"),
	                                                                 .type          = "BOOL",
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

	any_array_push(nodes_material_color, blur_node_def);
	any_map_set(parser_material_node_vectors, "BLUR", blur_node_vector);
	any_map_set(parser_material_node_values, "BLUR", blur_node_value);
}
