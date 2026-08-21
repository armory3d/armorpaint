
#include "../global.h"

char *str_tex_checker = "\
fun tex_checker(co: float3, col1: float3, col2: float3, scale: float): float3 { \
	/* Prevent precision issues on unit coordinates */ \
	var p: float3 = (co + 0.000001 * 0.999999) * scale; \
	var xi: float = abs(floor(p.x)); \
	var yi: float = abs(floor(p.y)); \
	var zi: float = abs(floor(p.z)); \
	/* var check: bool = (xi % 2.0 == yi % 2.0) == zi % 2.0;*/ \
	var checka: int = 0; \
	var checkb: int = 0; \
	if (xi % 2.0 == yi % 2.0) { checka = 1; } \
	if (zi % 2.0 != 0.0) { checkb = 1; } \
	if (checka == checkb) { return col1; } return col2; \
} \
fun tex_checker_f(co: float3, scale: float): float { \
	var p: float3 = (co + 0.000001 * 0.999999) * scale; \
	var xi: float = abs(floor(p.x)); \
	var yi: float = abs(floor(p.y)); \
	var zi: float = abs(floor(p.z)); \
	/*return float((xi % 2.0 == yi % 2.0) == zi % 2.0);*/ \
	var checka: int = 0; \
	var checkb: int = 0; \
	if (xi % 2.0 == yi % 2.0) { checka = 1; } \
	if (zi % 2.0 != 0.0) { checkb = 1; } \
	if (checka == checkb) { return 1.0; } return 0.0; \
	\
} \
";

char *checker_texture_node_vector(ui_node_t *node, ui_node_socket_t *socket) {
	node_shader_add_function(parser_material_kong, str_tex_checker);
	char *co    = parser_material_get_coord(node);
	char *col1  = parser_material_parse_vector_input(node->inputs->buffer[1]);
	char *col2  = parser_material_parse_vector_input(node->inputs->buffer[2]);
	char *scale = parser_material_parse_value_input(node->inputs->buffer[3], false);
	return string_tmp("tex_checker(%s, %s, %s, %s)", co, col1, col2, scale);
}

char *checker_texture_node_value(ui_node_t *node, ui_node_socket_t *socket) {
	node_shader_add_function(parser_material_kong, str_tex_checker);
	char *co    = parser_material_get_coord(node);
	char *scale = parser_material_parse_value_input(node->inputs->buffer[3], false);
	return string_tmp("tex_checker_f(%s, %s)", co, scale);
}

void checker_texture_node_init() {

	ui_node_t *checker_texture_node_def =
	    ALLOC_INIT(ui_node_t, {.id     = 0,
	                              .name   = _tr("Checker Texture"),
	                              .type   = "TEX_CHECKER",
	                              .x      = 0,
	                              .y      = 0,
	                              .color  = 0xff4982a0,
	                              .inputs = any_array_create_from_raw(
	                                  (void *[]){
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
	                                                                       .name          = _tr("Color 1"),
	                                                                       .type          = "RGBA",
	                                                                       .color         = 0xffc7c729,
	                                                                       .default_value = f32_array_create_xyz(0.8, 0.8, 0.8),
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .display       = 0}),
	                                      ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                       .node_id       = 0,
	                                                                       .name          = _tr("Color 2"),
	                                                                       .type          = "RGBA",
	                                                                       .color         = 0xffc7c729,
	                                                                       .default_value = f32_array_create_xyz(0.2, 0.2, 0.2),
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .display       = 0}),
	                                      ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                       .node_id       = 0,
	                                                                       .name          = _tr("Scale"),
	                                                                       .type          = "VALUE",
	                                                                       .color         = 0xffa1a1a1,
	                                                                       .default_value = f32_array_create_x(5.0),
	                                                                       .min           = 0.0,
	                                                                       .max           = 10.0,
	                                                                       .precision     = 100,
	                                                                       .display       = 0}),
	                                  },
	                                  4),
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
	                                                                       .name          = _tr("Factor"),
	                                                                       .type          = "VALUE",
	                                                                       .color         = 0xffa1a1a1,
	                                                                       .default_value = f32_array_create_x(1.0),
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .display       = 0}),
	                                  },
	                                  2),
	                              .buttons = any_array_create_from_raw((void *[]){}, 0),
	                              .width   = 0,
	                              .flags   = 0});

	any_array_push(nodes_material_texture, checker_texture_node_def);
	any_map_set(parser_material_node_vectors, "TEX_CHECKER", checker_texture_node_vector);
	any_map_set(parser_material_node_values, "TEX_CHECKER", checker_texture_node_value);
}
