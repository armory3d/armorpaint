
#include "../global.h"

char *str_tex_wave = "\
fun tex_wave_hash(n: float): float { return frac(sin(n) * 10000.0); } \
fun tex_wave_noise_f(x: float3): float { \
	var step: float3 = float3(110.0, 241.0, 171.0); \
	var i: float3 = floor3(x); \
	var f: float3 = frac3(x); \
	var n: float = dot(i, step); \
	var u: float3 = f * f * (3.0 - 2.0 * f); \
	return lerp(lerp(lerp(tex_wave_hash(n + dot(step, float3(0.0, 0.0, 0.0))), tex_wave_hash(n + dot(step, float3(1.0, 0.0, 0.0))), u.x), \
	                     lerp(tex_wave_hash(n + dot(step, float3(0.0, 1.0, 0.0))), tex_wave_hash(n + dot(step, float3(1.0, 1.0, 0.0))), u.x), u.y), \
	                lerp(lerp(tex_wave_hash(n + dot(step, float3(0.0, 0.0, 1.0))), tex_wave_hash(n + dot(step, float3(1.0, 0.0, 1.0))), u.x), \
	                     lerp(tex_wave_hash(n + dot(step, float3(0.0, 1.0, 1.0))), tex_wave_hash(n + dot(step, float3(1.0, 1.0, 1.0))), u.x), u.y), u.z); \
} \
fun tex_wave_fbm(p: float3, roughness: float): float { \
	var amp: float = 1.0; \
	var s: float = 0.0; \
	var ma: float = 0.0; \
	var pp: float3 = p; \
	s = s + amp * tex_wave_noise_f(pp); ma = ma + amp; amp = amp * roughness; pp = pp * 2.0; \
	s = s + amp * tex_wave_noise_f(pp); ma = ma + amp; amp = amp * roughness; pp = pp * 2.0; \
	s = s + amp * tex_wave_noise_f(pp); ma = ma + amp; amp = amp * roughness; pp = pp * 2.0; \
	s = s + amp * tex_wave_noise_f(pp); ma = ma + amp; \
	return s / ma; \
} \
fun tex_wave(co: float3, scale: float, distortion: float, detail_scale: float, detail_roughness: float, phase: float, wave_type: int, direction: int, profile: int): float { \
	var pi: float = 3.14159265; \
	var p: float3 = co * scale; \
	var n: float = 0.0; \
	if (wave_type == 0) { \
		if (direction == 0) { n = p.x; } \
		else if (direction == 1) { n = p.y; } \
		else if (direction == 2) { n = p.z; } \
		else { n = (p.x + p.y + p.z) * 0.57735027; } \
	} \
	else { \
		var cx: float = p.x; \
		var cy: float = p.y; \
		var cz: float = p.z; \
		if (direction == 0) { cx = 0.0; } \
		else if (direction == 1) { cy = 0.0; } \
		else if (direction == 2) { cz = 0.0; } \
		n = sqrt(cx * cx + cy * cy + cz * cz); \
	} \
	n = n + distortion * (tex_wave_fbm(p * detail_scale, detail_roughness) * 2.0 - 1.0); \
	n = n * 2.0 * pi + phase; \
	if (profile == 0) { return 0.5 + 0.5 * sin(n); } \
	else if (profile == 1) { var t: float = n / (2.0 * pi); return t - floor(t); } \
	/*else {*/ var t: float = n / (2.0 * pi); return abs(t - floor(t + 0.5)) * 2.0; /*}*/ \
} \
";

static char *wave_texture_node_result(ui_node_t *node) {
	node_shader_add_function(parser_material_kong, str_tex_wave);
	char             *co           = parser_material_get_coord(node);
	char             *scale        = parser_material_parse_value_input(node->inputs->buffer[1], false);
	char             *distortion   = parser_material_parse_value_input(node->inputs->buffer[2], false);
	char             *detail_scale = parser_material_parse_value_input(node->inputs->buffer[3], false);
	char             *detail_rough = parser_material_parse_value_input(node->inputs->buffer[4], false);
	char             *phase        = parser_material_parse_value_input(node->inputs->buffer[5], false);
	ui_node_button_t *but_type     = node->buttons->buffer[0];
	ui_node_button_t *but_dir      = node->buttons->buffer[1];
	ui_node_button_t *but_prof     = node->buttons->buffer[2];
	i32               wave_type    = (i32)but_type->default_value->buffer[0];
	i32               direction    = (i32)but_dir->default_value->buffer[0];
	i32               profile      = (i32)but_prof->default_value->buffer[0];
	return string_tmp("tex_wave(%s, %s, %s, %s, %s, %s, %d, %d, %d)", co, scale, distortion, detail_scale, detail_rough, phase, wave_type, direction, profile);
}

char *wave_texture_node_vector(ui_node_t *node, ui_node_socket_t *socket) {
	return parser_material_to_vec3(wave_texture_node_result(node));
}

char *wave_texture_node_value(ui_node_t *node, ui_node_socket_t *socket) {
	return wave_texture_node_result(node);
}

void wave_texture_node_init() {

	char *wave_type_data      = string_tmp("%s\n%s", _tr("Bands"), _tr("Rings"));
	char *wave_direction_data = string_tmp("%s\n%s\n%s\n%s", _tr("X"), _tr("Y"), _tr("Z"), _tr("Diagonal"));
	char *wave_profile_data   = string_tmp("%s\n%s\n%s", _tr("Sine"), _tr("Saw"), _tr("Triangle"));

	ui_node_t *wave_texture_node_def =
	    ALLOC_INIT(ui_node_t, {.id     = 0,
	                              .name   = _tr("Wave Texture"),
	                              .type   = "TEX_WAVE",
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
	                                                                       .name          = _tr("Distortion"),
	                                                                       .type          = "VALUE",
	                                                                       .color         = 0xffa1a1a1,
	                                                                       .default_value = f32_array_create_x(0.0),
	                                                                       .min           = -10.0,
	                                                                       .max           = 10.0,
	                                                                       .precision     = 100,
	                                                                       .display       = 0}),
	                                      ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                       .node_id       = 0,
	                                                                       .name          = _tr("Detail Scale"),
	                                                                       .type          = "VALUE",
	                                                                       .color         = 0xffa1a1a1,
	                                                                       .default_value = f32_array_create_x(1.0),
	                                                                       .min           = 0.0,
	                                                                       .max           = 10.0,
	                                                                       .precision     = 100,
	                                                                       .display       = 0}),
	                                      ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                       .node_id       = 0,
	                                                                       .name          = _tr("Detail Roughness"),
	                                                                       .type          = "VALUE",
	                                                                       .color         = 0xffa1a1a1,
	                                                                       .default_value = f32_array_create_x(0.5),
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .display       = 0}),
	                                      ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                       .node_id       = 0,
	                                                                       .name          = _tr("Phase Offset"),
	                                                                       .type          = "VALUE",
	                                                                       .color         = 0xffa1a1a1,
	                                                                       .default_value = f32_array_create_x(0.0),
	                                                                       .min           = -10.0,
	                                                                       .max           = 10.0,
	                                                                       .precision     = 100,
	                                                                       .display       = 0}),
	                                  },
	                                  6),
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
	                              .buttons = any_array_create_from_raw(
	                                  (void *[]){
	                                      ALLOC_INIT(ui_node_button_t, {.name          = _tr("Wave Type"),
	                                                                       .type          = "ENUM",
	                                                                       .output        = -1,
	                                                                       .default_value = f32_array_create_x(0),
	                                                                       .data          = u8_array_create_from_string(wave_type_data),
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .height        = 0}),
	                                      ALLOC_INIT(ui_node_button_t, {.name          = _tr("Direction"),
	                                                                       .type          = "ENUM",
	                                                                       .output        = -1,
	                                                                       .default_value = f32_array_create_x(0),
	                                                                       .data          = u8_array_create_from_string(wave_direction_data),
	                                                                       .min           = 0.0,
	                                                                       .max           = 3.0,
	                                                                       .precision     = 100,
	                                                                       .height        = 0}),
	                                      ALLOC_INIT(ui_node_button_t, {.name          = _tr("Wave Profile"),
	                                                                       .type          = "ENUM",
	                                                                       .output        = -1,
	                                                                       .default_value = f32_array_create_x(0),
	                                                                       .data          = u8_array_create_from_string(wave_profile_data),
	                                                                       .min           = 0.0,
	                                                                       .max           = 2.0,
	                                                                       .precision     = 100,
	                                                                       .height        = 0}),
	                                  },
	                                  3),
	                              .width = 0,
	                              .flags = 0});

	any_array_push(nodes_material_texture, wave_texture_node_def);
	any_map_set(parser_material_node_vectors, "TEX_WAVE", wave_texture_node_vector);
	any_map_set(parser_material_node_values, "TEX_WAVE", wave_texture_node_value);
}
