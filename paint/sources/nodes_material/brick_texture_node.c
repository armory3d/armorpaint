
#include "../global.h"

char *str_tex_brick = "\
fun tex_brick_noise(n: int): float { \
	n = (n >> 13) ^ n; \
	var nn: int = (n * (n * n * 60493 + 19990303) + 1376312589) & 2147483647; \
	return float(nn) / 2147483647.0; \
} \
fun tex_brick(co: float3, c1: float3, c2: float3, cm: float3, scale: float, mortar_size: float, mortar_smooth: float, bias: float, brick_width: float, row_height: float, offset_amount: float, offset_frequency: float, squash_amount: float, squash_frequency: float): float3 { \
	var p: float3 = co * scale; \
	var row: float = floor(p.y / row_height); \
	var bw: float = brick_width; \
	var offset: float = 0.0; \
	if (offset_frequency != 0.0 && squash_frequency != 0.0) { \
		if ((row % squash_frequency) == 0.0) { bw = bw * squash_amount; } \
		if ((row % offset_frequency) == 0.0) { offset = bw * offset_amount; } \
	} \
	var col: float = floor((p.x + offset) / bw); \
	var x: float = (p.x + offset) - bw * col; \
	var y: float = p.y - row_height * row; \
	var tint: float = clamp(tex_brick_noise((int(row) << 16) + (int(col) & 65535)) + bias, 0.0, 1.0); \
	var min_dist: float = min(min(x, y), min(bw - x, row_height - y)); \
	var f: float = 0.0; \
	if (min_dist < mortar_size) { \
		if (mortar_smooth == 0.0) { f = 1.0; } \
		if (mortar_smooth > 0.0) { f = smoothstep(0.0, mortar_smooth, 1.0 - min_dist / mortar_size); } \
	} \
	return lerp3(lerp3(c1, c2, tint), cm, f); \
} \
fun tex_brick_f(co: float3, scale: float, mortar_size: float, mortar_smooth: float, brick_width: float, row_height: float, offset_amount: float, offset_frequency: float, squash_amount: float, squash_frequency: float): float { \
	var p: float3 = co * scale; \
	var row: float = floor(p.y / row_height); \
	var bw: float = brick_width; \
	var offset: float = 0.0; \
	if (offset_frequency != 0.0 && squash_frequency != 0.0) { \
		if ((row % squash_frequency) == 0.0) { bw = bw * squash_amount; } \
		if ((row % offset_frequency) == 0.0) { offset = bw * offset_amount; } \
	} \
	var col: float = floor((p.x + offset) / bw); \
	var x: float = (p.x + offset) - bw * col; \
	var y: float = p.y - row_height * row; \
	var min_dist: float = min(min(x, y), min(bw - x, row_height - y)); \
	var f: float = 0.0; \
	if (min_dist < mortar_size) { \
		if (mortar_smooth == 0.0) { f = 1.0; } \
		if (mortar_smooth > 0.0) { f = smoothstep(0.0, mortar_smooth, 1.0 - min_dist / mortar_size); } \
	} \
	return f; \
} \
";

char *brick_texture_node_vector(ui_node_t *node, ui_node_socket_t *socket) {
	node_shader_add_function(parser_material_kong, str_tex_brick);
	char *co            = parser_material_get_coord(node);
	char *col1          = parser_material_parse_vector_input(node->inputs->buffer[1]);
	char *col2          = parser_material_parse_vector_input(node->inputs->buffer[2]);
	char *col3          = parser_material_parse_vector_input(node->inputs->buffer[3]);
	char *scale         = parser_material_parse_value_input(node->inputs->buffer[4], false);
	char *mortar_size   = parser_material_parse_value_input(node->inputs->buffer[5], false);
	char *mortar_smooth = parser_material_parse_value_input(node->inputs->buffer[6], false);
	char *bias          = parser_material_parse_value_input(node->inputs->buffer[7], false);
	char *brick_width   = parser_material_parse_value_input(node->inputs->buffer[8], false);
	char *row_height    = parser_material_parse_value_input(node->inputs->buffer[9], false);
	char *offset        = parser_material_parse_value_input(node->inputs->buffer[10], false);
	char *offset_freq   = parser_material_parse_value_input(node->inputs->buffer[11], false);
	char *squash        = parser_material_parse_value_input(node->inputs->buffer[12], false);
	char *squash_freq   = parser_material_parse_value_input(node->inputs->buffer[13], false);
	return string_tmp("tex_brick(%s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s)", co, col1, col2, col3, scale, mortar_size, mortar_smooth, bias,
	              brick_width, row_height, offset, offset_freq, squash, squash_freq);
}

char *brick_texture_node_value(ui_node_t *node, ui_node_socket_t *socket) {
	node_shader_add_function(parser_material_kong, str_tex_brick);
	char *co            = parser_material_get_coord(node);
	char *scale         = parser_material_parse_value_input(node->inputs->buffer[4], false);
	char *mortar_size   = parser_material_parse_value_input(node->inputs->buffer[5], false);
	char *mortar_smooth = parser_material_parse_value_input(node->inputs->buffer[6], false);
	char *brick_width   = parser_material_parse_value_input(node->inputs->buffer[8], false);
	char *row_height    = parser_material_parse_value_input(node->inputs->buffer[9], false);
	char *offset        = parser_material_parse_value_input(node->inputs->buffer[10], false);
	char *offset_freq   = parser_material_parse_value_input(node->inputs->buffer[11], false);
	char *squash        = parser_material_parse_value_input(node->inputs->buffer[12], false);
	char *squash_freq   = parser_material_parse_value_input(node->inputs->buffer[13], false);
	return string_tmp("tex_brick_f(%s, %s, %s, %s, %s, %s, %s, %s, %s, %s)", co, scale, mortar_size, mortar_smooth, brick_width, row_height, offset, offset_freq,
	              squash, squash_freq);
}

void brick_texture_node_init() {

	ui_node_t *brick_texture_node_def =
	    ALLOC_INIT(ui_node_t, {.id     = 0,
	                              .name   = _tr("Brick Texture"),
	                              .type   = "TEX_BRICK",
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
	                                                                       .name          = _tr("Mortar"),
	                                                                       .type          = "RGBA",
	                                                                       .color         = 0xffc7c729,
	                                                                       .default_value = f32_array_create_xyz(0.0, 0.0, 0.0),
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
	                                      ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                       .node_id       = 0,
	                                                                       .name          = _tr("Mortar Size"),
	                                                                       .type          = "VALUE",
	                                                                       .color         = 0xffa1a1a1,
	                                                                       .default_value = f32_array_create_x(0.02),
	                                                                       .min           = 0.0,
	                                                                       .max           = 0.125,
	                                                                       .precision     = 1000,
	                                                                       .display       = 0}),
	                                      ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                       .node_id       = 0,
	                                                                       .name          = _tr("Mortar Smooth"),
	                                                                       .type          = "VALUE",
	                                                                       .color         = 0xffa1a1a1,
	                                                                       .default_value = f32_array_create_x(0.1),
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .display       = 0}),
	                                      ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                       .node_id       = 0,
	                                                                       .name          = _tr("Bias"),
	                                                                       .type          = "VALUE",
	                                                                       .color         = 0xffa1a1a1,
	                                                                       .default_value = f32_array_create_x(0.0),
	                                                                       .min           = -1.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .display       = 0}),
	                                      ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                       .node_id       = 0,
	                                                                       .name          = _tr("Brick Width"),
	                                                                       .type          = "VALUE",
	                                                                       .color         = 0xffa1a1a1,
	                                                                       .default_value = f32_array_create_x(0.5),
	                                                                       .min           = 0.01,
	                                                                       .max           = 2.0,
	                                                                       .precision     = 100,
	                                                                       .display       = 0}),
	                                      ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                       .node_id       = 0,
	                                                                       .name          = _tr("Row Height"),
	                                                                       .type          = "VALUE",
	                                                                       .color         = 0xffa1a1a1,
	                                                                       .default_value = f32_array_create_x(0.25),
	                                                                       .min           = 0.01,
	                                                                       .max           = 2.0,
	                                                                       .precision     = 100,
	                                                                       .display       = 0}),
	                                      ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                       .node_id       = 0,
	                                                                       .name          = _tr("Offset"),
	                                                                       .type          = "VALUE",
	                                                                       .color         = 0xffa1a1a1,
	                                                                       .default_value = f32_array_create_x(0.5),
	                                                                       .min           = -1.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .display       = 0}),
	                                      ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                       .node_id       = 0,
	                                                                       .name          = _tr("Frequency"),
	                                                                       .type          = "VALUE",
	                                                                       .color         = 0xffa1a1a1,
	                                                                       .default_value = f32_array_create_x(2.0),
	                                                                       .min           = 1.0,
	                                                                       .max           = 10.0,
	                                                                       .precision     = 1,
	                                                                       .display       = 0}),
	                                      ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                       .node_id       = 0,
	                                                                       .name          = _tr("Squash"),
	                                                                       .type          = "VALUE",
	                                                                       .color         = 0xffa1a1a1,
	                                                                       .default_value = f32_array_create_x(1.0),
	                                                                       .min           = 0.0,
	                                                                       .max           = 2.0,
	                                                                       .precision     = 100,
	                                                                       .display       = 0}),
	                                      ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                       .node_id       = 0,
	                                                                       .name          = _tr("Frequency"),
	                                                                       .type          = "VALUE",
	                                                                       .color         = 0xffa1a1a1,
	                                                                       .default_value = f32_array_create_x(2.0),
	                                                                       .min           = 1.0,
	                                                                       .max           = 10.0,
	                                                                       .precision     = 1,
	                                                                       .display       = 0}),
	                                  },
	                                  14),
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

	any_array_push(nodes_material_texture, brick_texture_node_def);
	any_map_set(parser_material_node_vectors, "TEX_BRICK", brick_texture_node_vector);
	any_map_set(parser_material_node_values, "TEX_BRICK", brick_texture_node_value);
}
