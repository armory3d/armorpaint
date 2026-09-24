
#include "../global.h"

char *str_tex_noise = "\
float hash(float n) { return frac(sin(n) * 10000.0); } \
float tex_noise_f(float3 x) { \
	float3 step = float3(110.0, 241.0, 171.0); \
	float3 i = floor3(x); \
	float3 f = frac3(x); \
	float n = dot(i, step); \
	float3 u = f * f * (3.0 - 2.0 * f); \
	return lerp(lerp(lerp(hash(n + dot(step, float3(0.0, 0.0, 0.0))), hash(n + dot(step, float3(1.0, 0.0, 0.0))), u.x), \
	                     lerp(hash(n + dot(step, float3(0.0, 1.0, 0.0))), hash(n + dot(step, float3(1.0, 1.0, 0.0))), u.x), u.y), \
	                lerp(lerp(hash(n + dot(step, float3(0.0, 0.0, 1.0))), hash(n + dot(step, float3(1.0, 0.0, 1.0))), u.x), \
	                     lerp(hash(n + dot(step, float3(0.0, 1.0, 1.0))), hash(n + dot(step, float3(1.0, 1.0, 1.0))), u.x), u.y), u.z); \
} \
float tex_noise_fbm(float3 p, float detail, float roughness, float lacunarity) { \
	float fscale = 1.0; \
	float amp = 1.0; \
	float maxamp = 0.0; \
	float sum = 0.0; \
	int n = int(clamp(detail, 0.0, 8.0)); \
	for (int ii = 0; ii <= n; ii += 1) { \
		sum = sum + amp * tex_noise_f(p * fscale); \
		maxamp = maxamp + amp; \
		amp = amp * roughness; \
		fscale = fscale * lacunarity; \
	} \
	float rmd = detail - floor(detail); \
	if (rmd > 0.0) { \
		float t = tex_noise_f(p * fscale); \
		float sum2 = sum + t * amp; \
		float maxamp2 = maxamp + amp; \
		return lerp(sum / maxamp, sum2 / maxamp2, rmd); \
	} \
	return sum / maxamp; \
} \
float3 tex_noise(float3 p, float scale, float detail, float roughness, float lacunarity, float distortion) { \
	float3 pp = p * scale; \
	pp = pp + distortion * (float3(tex_noise_f(pp), tex_noise_f(pp + float3(0.5, 0.0, 0.0)), tex_noise_f(pp + float3(0.0, 0.5, 0.0))) * 2.0 - 1.0); \
	float f = tex_noise_fbm(pp, detail, roughness, lacunarity); \
	float r = tex_noise_fbm(pp + float3(0.33, 0.0, 0.0), detail, roughness, lacunarity); \
	float g = tex_noise_fbm(pp + float3(0.0, 0.33, 0.0), detail, roughness, lacunarity); \
	return float3(f, r, g); \
} \
";

static char *noise_texture_node_result(ui_node_t *node) {
	node_shader_add_function(parser_material_kong, str_tex_noise);
	char *co         = parser_material_get_coord(node);
	char *scale      = parser_material_parse_value_input(node->inputs->buffer[1], false);
	char *detail     = parser_material_parse_value_input(node->inputs->buffer[2], false);
	char *roughness  = parser_material_parse_value_input(node->inputs->buffer[3], false);
	char *lacunarity = parser_material_parse_value_input(node->inputs->buffer[4], false);
	char *distortion = parser_material_parse_value_input(node->inputs->buffer[5], false);
	return string_tmp("tex_noise(%s, %s, %s, %s, %s, %s)", co, scale, detail, roughness, lacunarity, distortion);
}

char *noise_texture_node_vector(ui_node_t *node, ui_node_socket_t *socket) {
	return noise_texture_node_result(node);
}

char *noise_texture_node_value(ui_node_t *node, ui_node_socket_t *socket) {
	return string_tmp("%s.x", noise_texture_node_result(node));
}

void noise_texture_node_init() {

	char      *noise_dimensions_data = string_tmp("%s", _tr("3D"));
	char      *noise_feature_data    = string_tmp("%s", _tr("fBM"));
	ui_node_t *noise_texture_node_def =
	    ALLOC_INIT(ui_node_t, {.id     = 0,
	                           .name   = _tr("Noise Texture"),
	                           .type   = "TEX_NOISE",
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
	                                                                 .name          = _tr("Detail"),
	                                                                 .type          = "VALUE",
	                                                                 .color         = 0xffa1a1a1,
	                                                                 .default_value = f32_array_create_x(2.0),
	                                                                 .min           = 0.0,
	                                                                 .max           = 8.0,
	                                                                 .precision     = 100,
	                                                                 .display       = 0}),
	                                   ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                 .node_id       = 0,
	                                                                 .name          = _tr("Roughness"),
	                                                                 .type          = "VALUE",
	                                                                 .color         = 0xffa1a1a1,
	                                                                 .default_value = f32_array_create_x(0.5),
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .display       = 0}),
	                                   ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                 .node_id       = 0,
	                                                                 .name          = _tr("Lacunarity"),
	                                                                 .type          = "VALUE",
	                                                                 .color         = 0xffa1a1a1,
	                                                                 .default_value = f32_array_create_x(2.0),
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
	                               },
	                               6),
	                           .outputs = any_array_create_from_raw(
	                               (void *[]){
	                                   ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                 .node_id       = 0,
	                                                                 .name          = _tr("Factor"),
	                                                                 .type          = "VALUE",
	                                                                 .color         = 0xffa1a1a1,
	                                                                 .default_value = f32_array_create_x(0.5),
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .display       = 0}),
	                                   ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                 .node_id       = 0,
	                                                                 .name          = _tr("Color"),
	                                                                 .type          = "RGBA",
	                                                                 .color         = 0xffc7c729,
	                                                                 .default_value = f32_array_create_xyzw(0.5, 0.5, 0.5, 1.0),
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .display       = 0}),
	                               },
	                               2),
	                           .buttons = any_array_create_from_raw(
	                               (void *[]){
	                                   ALLOC_INIT(ui_node_button_t, {.name          = _tr("Dimensions"),
	                                                                 .type          = "ENUM",
	                                                                 .output        = -1,
	                                                                 .default_value = f32_array_create_x(0),
	                                                                 .data          = u8_array_create_from_string(noise_dimensions_data),
	                                                                 .min           = 0.0,
	                                                                 .max           = 3.0,
	                                                                 .precision     = 100,
	                                                                 .height        = 0}),
	                                   ALLOC_INIT(ui_node_button_t, {.name          = _tr("Type"),
	                                                                 .type          = "ENUM",
	                                                                 .output        = -1,
	                                                                 .default_value = f32_array_create_x(0),
	                                                                 .data          = u8_array_create_from_string(noise_feature_data),
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .height        = 0}),
	                                   ALLOC_INIT(ui_node_button_t, {.name          = _tr("Normalize"),
	                                                                 .type          = "BOOL",
	                                                                 .output        = -1,
	                                                                 .default_value = f32_array_create_x(0),
	                                                                 .data          = NULL,
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .height        = 0}),
	                               },
	                               3),
	                           .width = 0,
	                           .flags = 0});

	any_array_push(nodes_material_texture, noise_texture_node_def);
	any_map_set(parser_material_node_vectors, "TEX_NOISE", noise_texture_node_vector);
	any_map_set(parser_material_node_values, "TEX_NOISE", noise_texture_node_value);
}
