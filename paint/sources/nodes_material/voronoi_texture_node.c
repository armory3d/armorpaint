
#include "../global.h"

char *str_tex_voronoi = "\
float3 voronoi_hash3(float3 p) { \
    float3 h; \
    h.x = frac(sin(dot(p, float3(127.1, 311.7, 74.7))) * 43758.5453); \
    h.y = frac(sin(dot(p, float3(269.5, 183.3, 246.1))) * 43758.5453); \
    h.z = frac(sin(dot(p, float3(113.5, 271.9, 124.6))) * 43758.5453); \
    return h; \
} \
float4 voronoi_2d_f1(float px, float py, float r) { \
    float cx0 = floor(px); float cy0 = floor(py); \
    float lx = px - cx0; float ly = py - cy0; \
    float min_dist = 8.0; \
    int bi = 0; int bj = 0; \
    for (int j = -1; j <= 1; j += 1) \
    for (int i = -1; i <= 1; i += 1) { \
        float3 h3 = voronoi_hash3(float3(cx0 + float(i), cy0 + float(j), 0.0)); \
        float ptx = float(i) + h3.x * r; float pty = float(j) + h3.y * r; \
        float dvx = ptx - lx; float dvy = pty - ly; \
        float d = sqrt(dvx * dvx + dvy * dvy); \
        if (d < min_dist) { min_dist = d; bi = i; bj = j; } \
    } \
    float bcx = cx0 + float(bi); float bcy = cy0 + float(bj); \
    float3 bh = voronoi_hash3(float3(bcx, bcy, 0.0)); \
    float bptx = float(bi) + bh.x * r; float bpty = float(bj) + bh.y * r; \
    float3 col = voronoi_hash3(float3(cx0 + float(bi) + bptx * (1.0 - r), cy0 + float(bj) + bpty * (1.0 - r), 0.0)); \
    return float4(min_dist, col.x, col.y, col.z); \
} \
float3 voronoi_2d_f1_pos(float px, float py, float r) { \
    float cx0 = floor(px); float cy0 = floor(py); \
    float lx = px - cx0; float ly = py - cy0; \
    float min_dist = 8.0; \
    int bi = 0; int bj = 0; \
    for (int j = -1; j <= 1; j += 1) \
    for (int i = -1; i <= 1; i += 1) { \
        float3 h3 = voronoi_hash3(float3(cx0 + float(i), cy0 + float(j), 0.0)); \
        float ptx = float(i) + h3.x * r; float pty = float(j) + h3.y * r; \
        float dvx = ptx - lx; float dvy = pty - ly; \
        float d = sqrt(dvx * dvx + dvy * dvy); \
        if (d < min_dist) { min_dist = d; bi = i; bj = j; } \
    } \
    float bcx = cx0 + float(bi); float bcy = cy0 + float(bj); \
    float3 bh = voronoi_hash3(float3(bcx, bcy, 0.0)); \
    return float3(bcx + float(bi) + bh.x * r, bcy + float(bj) + bh.y * r, 0.0); \
} \
float4 voronoi_2d_f2(float px, float py, float r) { \
    float cx0 = floor(px); float cy0 = floor(py); \
    float lx = px - cx0; float ly = py - cy0; \
    float dist1 = 8.0; float dist2 = 8.0; \
    int b1i = 0; int b1j = 0; int b2i = 0; int b2j = 0; \
    for (int j = -1; j <= 1; j += 1) \
    for (int i = -1; i <= 1; i += 1) { \
        float3 h3 = voronoi_hash3(float3(cx0 + float(i), cy0 + float(j), 0.0)); \
        float ptx = float(i) + h3.x * r; float pty = float(j) + h3.y * r; \
        float dvx = ptx - lx; float dvy = pty - ly; \
        float d = sqrt(dvx * dvx + dvy * dvy); \
        if (d < dist1) { dist2 = dist1; b2i = b1i; b2j = b1j; dist1 = d; b1i = i; b1j = j; } \
        else if (d < dist2) { dist2 = d; b2i = i; b2j = j; } \
    } \
    float bcx = cx0 + float(b2i); float bcy = cy0 + float(b2j); \
    float3 bh = voronoi_hash3(float3(bcx, bcy, 0.0)); \
    float bptx = float(b2i) + bh.x * r; float bpty = float(b2j) + bh.y * r; \
    float3 col = voronoi_hash3(float3(cx0 + float(b2i) + bptx * (1.0 - r), cy0 + float(b2j) + bpty * (1.0 - r), 0.0)); \
    return float4(dist2, col.x, col.y, col.z); \
} \
float3 voronoi_2d_f2_pos(float px, float py, float r) { \
    float cx0 = floor(px); float cy0 = floor(py); \
    float lx = px - cx0; float ly = py - cy0; \
    float dist1 = 8.0; float dist2 = 8.0; \
    int b1i = 0; int b1j = 0; int b2i = 0; int b2j = 0; \
    for (int j = -1; j <= 1; j += 1) \
    for (int i = -1; i <= 1; i += 1) { \
        float3 h3 = voronoi_hash3(float3(cx0 + float(i), cy0 + float(j), 0.0)); \
        float ptx = float(i) + h3.x * r; float pty = float(j) + h3.y * r; \
        float dvx = ptx - lx; float dvy = pty - ly; \
        float d = sqrt(dvx * dvx + dvy * dvy); \
        if (d < dist1) { dist2 = dist1; b2i = b1i; b2j = b1j; dist1 = d; b1i = i; b1j = j; } \
        else if (d < dist2) { dist2 = d; b2i = i; b2j = j; } \
    } \
    float bcx = cx0 + float(b2i); float bcy = cy0 + float(b2j); \
    float3 bh = voronoi_hash3(float3(bcx, bcy, 0.0)); \
    return float3(bcx + float(b2i) + bh.x * r, bcy + float(b2j) + bh.y * r, 0.0); \
} \
float voronoi_2d_f1_fbm(float px, float py, float detail, float roughness, float lacunarity, float r) { \
    float sum = 0.0; float max_amp = 0.0; \
    float amp = 1.0; float freq = 1.0; \
    int n = int(clamp(detail, 0.0, 15.0)); \
    for (int i = 0; i <= n; i += 1) { \
        sum = sum + amp * voronoi_2d_f1(px * freq, py * freq, r).x; \
        max_amp = max_amp + amp; amp = amp * roughness; freq = freq * lacunarity; \
    } \
    float rmd = detail - floor(detail); \
    if (rmd > 0.001) { \
        sum = sum + rmd * amp * voronoi_2d_f1(px * freq, py * freq, r).x; \
        max_amp = max_amp + rmd * amp; \
    } \
    return sum / max_amp; \
} \
float voronoi_2d_f2_fbm(float px, float py, float detail, float roughness, float lacunarity, float r) { \
    float sum = 0.0; float max_amp = 0.0; \
    float amp = 1.0; float freq = 1.0; \
    int n = int(clamp(detail, 0.0, 15.0)); \
    for (int i = 0; i <= n; i += 1) { \
        sum = sum + amp * voronoi_2d_f2(px * freq, py * freq, r).x; \
        max_amp = max_amp + amp; amp = amp * roughness; freq = freq * lacunarity; \
    } \
    float rmd = detail - floor(detail); \
    if (rmd > 0.001) { \
        sum = sum + rmd * amp * voronoi_2d_f2(px * freq, py * freq, r).x; \
        max_amp = max_amp + rmd * amp; \
    } \
    return sum / max_amp; \
} \
float4 voronoi_3d_f1(float3 p, float r) { \
    float3 cell = floor(p); \
    float3 lp = p - cell; \
    float min_dist = 8.0; \
    int bi = 0; int bj = 0; int bk = 0; \
    for (int k = -1; k <= 1; k += 1) \
    for (int j = -1; j <= 1; j += 1) \
    for (int i = -1; i <= 1; i += 1) { \
        float3 offset = float3(float(i), float(j), float(k)); \
        float3 pt = offset + voronoi_hash3(cell + offset) * r; \
        float3 dv = pt - lp; \
        float d = sqrt(dot(dv, dv)); \
        if (d < min_dist) { min_dist = d; bi = i; bj = j; bk = k; } \
    } \
    float3 off = float3(float(bi), float(bj), float(bk)); \
    float3 bpt = off + voronoi_hash3(cell + off) * r; \
    float3 col = voronoi_hash3(cell + off + bpt * (1.0 - r)); \
    return float4(min_dist, col.x, col.y, col.z); \
} \
float3 voronoi_3d_f1_pos(float3 p, float r) { \
    float3 cell = floor(p); \
    float3 lp = p - cell; \
    float min_dist = 8.0; \
    int bi = 0; int bj = 0; int bk = 0; \
    for (int k = -1; k <= 1; k += 1) \
    for (int j = -1; j <= 1; j += 1) \
    for (int i = -1; i <= 1; i += 1) { \
        float3 offset = float3(float(i), float(j), float(k)); \
        float3 pt = offset + voronoi_hash3(cell + offset) * r; \
        float3 dv = pt - lp; \
        float d = sqrt(dot(dv, dv)); \
        if (d < min_dist) { min_dist = d; bi = i; bj = j; bk = k; } \
    } \
    float3 off = float3(float(bi), float(bj), float(bk)); \
    float3 bpt = off + voronoi_hash3(cell + off) * r; \
    return cell + off + bpt; \
} \
float4 voronoi_3d_f2(float3 p, float r) { \
    float3 cell = floor(p); \
    float3 lp = p - cell; \
    float dist1 = 8.0; float dist2 = 8.0; \
    int b1i = 0; int b1j = 0; int b1k = 0; \
    int b2i = 0; int b2j = 0; int b2k = 0; \
    for (int k = -1; k <= 1; k += 1) \
    for (int j = -1; j <= 1; j += 1) \
    for (int i = -1; i <= 1; i += 1) { \
        float3 offset = float3(float(i), float(j), float(k)); \
        float3 pt = offset + voronoi_hash3(cell + offset) * r; \
        float3 dv = pt - lp; \
        float d = sqrt(dot(dv, dv)); \
        if (d < dist1) { \
            dist2 = dist1; b2i = b1i; b2j = b1j; b2k = b1k; \
            dist1 = d; b1i = i; b1j = j; b1k = k; \
        } else if (d < dist2) { \
            dist2 = d; b2i = i; b2j = j; b2k = k; \
        } \
    } \
    float3 off2 = float3(float(b2i), float(b2j), float(b2k)); \
    float3 bpt2 = off2 + voronoi_hash3(cell + off2) * r; \
    float3 col = voronoi_hash3(cell + off2 + bpt2 * (1.0 - r)); \
    return float4(dist2, col.x, col.y, col.z); \
} \
float3 voronoi_3d_f2_pos(float3 p, float r) { \
    float3 cell = floor(p); \
    float3 lp = p - cell; \
    float dist1 = 8.0; float dist2 = 8.0; \
    int b1i = 0; int b1j = 0; int b1k = 0; \
    int b2i = 0; int b2j = 0; int b2k = 0; \
    for (int k = -1; k <= 1; k += 1) \
    for (int j = -1; j <= 1; j += 1) \
    for (int i = -1; i <= 1; i += 1) { \
        float3 offset = float3(float(i), float(j), float(k)); \
        float3 pt = offset + voronoi_hash3(cell + offset) * r; \
        float3 dv = pt - lp; \
        float d = sqrt(dot(dv, dv)); \
        if (d < dist1) { \
            dist2 = dist1; b2i = b1i; b2j = b1j; b2k = b1k; \
            dist1 = d; b1i = i; b1j = j; b1k = k; \
        } else if (d < dist2) { \
            dist2 = d; b2i = i; b2j = j; b2k = k; \
        } \
    } \
    float3 off2 = float3(float(b2i), float(b2j), float(b2k)); \
    float3 bpt2 = off2 + voronoi_hash3(cell + off2) * r; \
    return cell + off2 + bpt2; \
} \
float voronoi_3d_f1_fbm(float3 p, float detail, float roughness, float lacunarity, float r) { \
    float sum = 0.0; float max_amp = 0.0; \
    float amp = 1.0; float freq = 1.0; \
    int n = int(clamp(detail, 0.0, 15.0)); \
    for (int i = 0; i <= n; i += 1) { \
        sum = sum + amp * voronoi_3d_f1(p * freq, r).x; \
        max_amp = max_amp + amp; amp = amp * roughness; freq = freq * lacunarity; \
    } \
    float rmd = detail - floor(detail); \
    if (rmd > 0.001) { \
        sum = sum + rmd * amp * voronoi_3d_f1(p * freq, r).x; \
        max_amp = max_amp + rmd * amp; \
    } \
    return sum / max_amp; \
} \
float voronoi_3d_f2_fbm(float3 p, float detail, float roughness, float lacunarity, float r) { \
    float sum = 0.0; float max_amp = 0.0; \
    float amp = 1.0; float freq = 1.0; \
    int n = int(clamp(detail, 0.0, 15.0)); \
    for (int i = 0; i <= n; i += 1) { \
        sum = sum + amp * voronoi_3d_f2(p * freq, r).x; \
        max_amp = max_amp + amp; amp = amp * roughness; freq = freq * lacunarity; \
    } \
    float rmd = detail - floor(detail); \
    if (rmd > 0.001) { \
        sum = sum + rmd * amp * voronoi_3d_f2(p * freq, r).x; \
        max_amp = max_amp + rmd * amp; \
    } \
    return sum / max_amp; \
} \
";

// Euclidean normalization constants per dimension (sqrt(N)/2, approximate F1 max distance)
static const char *voronoi_norm_const(i32 dim) {
	if (dim == 0) // 2D: sqrt(2)/2
		return "0.7071";
	else // 3D: sqrt(3)/2
		return "0.8660";
}

char *voronoi_texture_node_vector(ui_node_t *node, ui_node_socket_t *socket) {
	node_shader_add_function(parser_material_kong, str_tex_voronoi);
	char             *co         = parser_material_get_coord(node);
	char             *scale      = parser_material_parse_value_input(node->inputs->buffer[1], false);
	char             *randomness = parser_material_parse_value_input(node->inputs->buffer[5], false);
	char             *p3s        = string_tmp("%s * %s", co, scale);
	ui_node_button_t *but_dim    = node->buttons->buffer[0];
	ui_node_button_t *but_feat   = node->buttons->buffer[1];
	i32               dim        = (i32)but_dim->default_value->buffer[0];
	i32               is_f2      = (i32)but_feat->default_value->buffer[0] == 1;
	char             *fn         = is_f2 ? "f2" : "f1";

	if (socket == node->outputs->buffer[1]) { // Color
		if (dim == 0) {                       //  2D
			return string_tmp("voronoi_2d_%s((%s).x, (%s).y, %s).yzw", fn, p3s, p3s, randomness);
		}
		else { // 3D
			return string_tmp("voronoi_3d_%s(%s, %s).yzw", fn, p3s, randomness);
		}
	}
	else {              // Position
		if (dim == 0) { // 2D
			return string_tmp("voronoi_2d_%s_pos((%s).x, (%s).y, %s)", fn, p3s, p3s, randomness);
		}
		else { // 3D
			return string_tmp("voronoi_3d_%s_pos(%s, %s)", fn, p3s, randomness);
		}
	}
}

char *voronoi_texture_node_value(ui_node_t *node, ui_node_socket_t *socket) {
	node_shader_add_function(parser_material_kong, str_tex_voronoi);
	char             *co         = parser_material_get_coord(node);
	char             *scale      = parser_material_parse_value_input(node->inputs->buffer[1], false);
	char             *detail     = parser_material_parse_value_input(node->inputs->buffer[2], false);
	char             *roughness  = parser_material_parse_value_input(node->inputs->buffer[3], false);
	char             *lacunarity = parser_material_parse_value_input(node->inputs->buffer[4], false);
	char             *randomness = parser_material_parse_value_input(node->inputs->buffer[5], false);
	char             *p3s        = string_tmp("%s * %s", co, scale);
	ui_node_button_t *but_dim    = node->buttons->buffer[0];
	ui_node_button_t *but_feat   = node->buttons->buffer[1];
	ui_node_button_t *but_norm   = node->buttons->buffer[2];
	i32               dim        = (i32)but_dim->default_value->buffer[0];
	i32               is_f2      = (i32)but_feat->default_value->buffer[0] == 1;
	i32               normalize  = (i32)but_norm->default_value->buffer[0] == 1;
	char             *fn         = is_f2 ? "f2" : "f1";

	// Distance output
	char *dist;
	if (dim == 0) { // 2D
		dist = string_tmp("voronoi_2d_%s_fbm((%s).x, (%s).y, %s, %s, %s, %s)", fn, p3s, p3s, detail, roughness, lacunarity, randomness);
	}
	else { // 3D
		dist = string_tmp("voronoi_3d_%s_fbm(%s, %s, %s, %s, %s)", fn, p3s, detail, roughness, lacunarity, randomness);
	}

	if (normalize) {
		dist = string_tmp("clamp(%s / (%s * max(%s, 0.0001)), 0.0, 1.0)", dist, voronoi_norm_const(dim), randomness);
	}

	return dist;
}

void voronoi_texture_node_init() {

	char      *voronoi_dimensions_data = string_tmp("%s\n%s", _tr("2D"), _tr("3D"));
	char      *voronoi_feature_data    = string_tmp("%s\n%s", _tr("F1"), _tr("F2"));
	ui_node_t *voronoi_texture_node_def =
	    ALLOC_INIT(ui_node_t, {.id     = 0,
	                           .name   = _tr("Voronoi Texture"),
	                           .type   = "TEX_VORONOI",
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
	                                                                 .default_value = f32_array_create_x(0.0),
	                                                                 .min           = 0.0,
	                                                                 .max           = 15.0,
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
	                                                                 .min           = 0.1,
	                                                                 .max           = 10.0,
	                                                                 .precision     = 100,
	                                                                 .display       = 0}),
	                                   ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                 .node_id       = 0,
	                                                                 .name          = _tr("Randomness"),
	                                                                 .type          = "VALUE",
	                                                                 .color         = 0xffa1a1a1,
	                                                                 .default_value = f32_array_create_x(1.0),
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .display       = 0}),
	                               },
	                               6),
	                           .outputs = any_array_create_from_raw(
	                               (void *[]){
	                                   ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                 .node_id       = 0,
	                                                                 .name          = _tr("Distance"),
	                                                                 .type          = "VALUE",
	                                                                 .color         = 0xffa1a1a1,
	                                                                 .default_value = f32_array_create_x(0.0),
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .display       = 0}),
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
	                                                                 .name          = _tr("Position"),
	                                                                 .type          = "VECTOR",
	                                                                 .color         = 0xff6363c7,
	                                                                 .default_value = f32_array_create_xyz(0.0, 0.0, 0.0),
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .display       = 0}),
	                               },
	                               3),
	                           .buttons = any_array_create_from_raw(
	                               (void *[]){
	                                   ALLOC_INIT(ui_node_button_t, {.name          = _tr("Dimensions"),
	                                                                 .type          = "ENUM",
	                                                                 .output        = -1,
	                                                                 .default_value = f32_array_create_x(1),
	                                                                 .data          = u8_array_create_from_string(voronoi_dimensions_data),
	                                                                 .min           = 0.0,
	                                                                 .max           = 3.0,
	                                                                 .precision     = 100,
	                                                                 .height        = 0}),
	                                   ALLOC_INIT(ui_node_button_t, {.name          = _tr("Feature Output"),
	                                                                 .type          = "ENUM",
	                                                                 .output        = -1,
	                                                                 .default_value = f32_array_create_x(0),
	                                                                 .data          = u8_array_create_from_string(voronoi_feature_data),
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

	any_array_push(nodes_material_texture, voronoi_texture_node_def);
	any_map_set(parser_material_node_vectors, "TEX_VORONOI", voronoi_texture_node_vector);
	any_map_set(parser_material_node_values, "TEX_VORONOI", voronoi_texture_node_value);
}
