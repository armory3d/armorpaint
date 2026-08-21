
#include "../global.h"

char *str_tex_voronoi = "\
fun voronoi_hash3(p: float3): float3 { \
    var h: float3; \
    h.x = frac(sin(dot(p, float3(127.1, 311.7, 74.7))) * 43758.5453); \
    h.y = frac(sin(dot(p, float3(269.5, 183.3, 246.1))) * 43758.5453); \
    h.z = frac(sin(dot(p, float3(113.5, 271.9, 124.6))) * 43758.5453); \
    return h; \
} \
fun voronoi_2d_f1(px: float, py: float, r: float): float4 { \
    var cx0: float = floor(px); var cy0: float = floor(py); \
    var lx: float = px - cx0; var ly: float = py - cy0; \
    var min_dist: float = 8.0; \
    var bi: int = 0; var bj: int = 0; \
    for (var j: int = -1; j <= 1; j += 1) \
    for (var i: int = -1; i <= 1; i += 1) { \
        var h3: float3 = voronoi_hash3(float3(cx0 + float(i), cy0 + float(j), 0.0)); \
        var ptx: float = float(i) + h3.x * r; var pty: float = float(j) + h3.y * r; \
        var dvx: float = ptx - lx; var dvy: float = pty - ly; \
        var d: float = sqrt(dvx * dvx + dvy * dvy); \
        if (d < min_dist) { min_dist = d; bi = i; bj = j; } \
    } \
    var bcx: float = cx0 + float(bi); var bcy: float = cy0 + float(bj); \
    var bh: float3 = voronoi_hash3(float3(bcx, bcy, 0.0)); \
    var bptx: float = float(bi) + bh.x * r; var bpty: float = float(bj) + bh.y * r; \
    var col: float3 = voronoi_hash3(float3(cx0 + float(bi) + bptx * (1.0 - r), cy0 + float(bj) + bpty * (1.0 - r), 0.0)); \
    return float4(min_dist, col.x, col.y, col.z); \
} \
fun voronoi_2d_f1_pos(px: float, py: float, r: float): float3 { \
    var cx0: float = floor(px); var cy0: float = floor(py); \
    var lx: float = px - cx0; var ly: float = py - cy0; \
    var min_dist: float = 8.0; \
    var bi: int = 0; var bj: int = 0; \
    for (var j: int = -1; j <= 1; j += 1) \
    for (var i: int = -1; i <= 1; i += 1) { \
        var h3: float3 = voronoi_hash3(float3(cx0 + float(i), cy0 + float(j), 0.0)); \
        var ptx: float = float(i) + h3.x * r; var pty: float = float(j) + h3.y * r; \
        var dvx: float = ptx - lx; var dvy: float = pty - ly; \
        var d: float = sqrt(dvx * dvx + dvy * dvy); \
        if (d < min_dist) { min_dist = d; bi = i; bj = j; } \
    } \
    var bcx: float = cx0 + float(bi); var bcy: float = cy0 + float(bj); \
    var bh: float3 = voronoi_hash3(float3(bcx, bcy, 0.0)); \
    return float3(bcx + float(bi) + bh.x * r, bcy + float(bj) + bh.y * r, 0.0); \
} \
fun voronoi_2d_f2(px: float, py: float, r: float): float4 { \
    var cx0: float = floor(px); var cy0: float = floor(py); \
    var lx: float = px - cx0; var ly: float = py - cy0; \
    var dist1: float = 8.0; var dist2: float = 8.0; \
    var b1i: int = 0; var b1j: int = 0; var b2i: int = 0; var b2j: int = 0; \
    for (var j: int = -1; j <= 1; j += 1) \
    for (var i: int = -1; i <= 1; i += 1) { \
        var h3: float3 = voronoi_hash3(float3(cx0 + float(i), cy0 + float(j), 0.0)); \
        var ptx: float = float(i) + h3.x * r; var pty: float = float(j) + h3.y * r; \
        var dvx: float = ptx - lx; var dvy: float = pty - ly; \
        var d: float = sqrt(dvx * dvx + dvy * dvy); \
        if (d < dist1) { dist2 = dist1; b2i = b1i; b2j = b1j; dist1 = d; b1i = i; b1j = j; } \
        else if (d < dist2) { dist2 = d; b2i = i; b2j = j; } \
    } \
    var bcx: float = cx0 + float(b2i); var bcy: float = cy0 + float(b2j); \
    var bh: float3 = voronoi_hash3(float3(bcx, bcy, 0.0)); \
    var bptx: float = float(b2i) + bh.x * r; var bpty: float = float(b2j) + bh.y * r; \
    var col: float3 = voronoi_hash3(float3(cx0 + float(b2i) + bptx * (1.0 - r), cy0 + float(b2j) + bpty * (1.0 - r), 0.0)); \
    return float4(dist2, col.x, col.y, col.z); \
} \
fun voronoi_2d_f2_pos(px: float, py: float, r: float): float3 { \
    var cx0: float = floor(px); var cy0: float = floor(py); \
    var lx: float = px - cx0; var ly: float = py - cy0; \
    var dist1: float = 8.0; var dist2: float = 8.0; \
    var b1i: int = 0; var b1j: int = 0; var b2i: int = 0; var b2j: int = 0; \
    for (var j: int = -1; j <= 1; j += 1) \
    for (var i: int = -1; i <= 1; i += 1) { \
        var h3: float3 = voronoi_hash3(float3(cx0 + float(i), cy0 + float(j), 0.0)); \
        var ptx: float = float(i) + h3.x * r; var pty: float = float(j) + h3.y * r; \
        var dvx: float = ptx - lx; var dvy: float = pty - ly; \
        var d: float = sqrt(dvx * dvx + dvy * dvy); \
        if (d < dist1) { dist2 = dist1; b2i = b1i; b2j = b1j; dist1 = d; b1i = i; b1j = j; } \
        else if (d < dist2) { dist2 = d; b2i = i; b2j = j; } \
    } \
    var bcx: float = cx0 + float(b2i); var bcy: float = cy0 + float(b2j); \
    var bh: float3 = voronoi_hash3(float3(bcx, bcy, 0.0)); \
    return float3(bcx + float(b2i) + bh.x * r, bcy + float(b2j) + bh.y * r, 0.0); \
} \
fun voronoi_2d_f1_fbm(px: float, py: float, detail: float, roughness: float, lacunarity: float, r: float): float { \
    var sum: float = 0.0; var max_amp: float = 0.0; \
    var amp: float = 1.0; var freq: float = 1.0; \
    var n: int = int(clamp(detail, 0.0, 15.0)); \
    for (var i: int = 0; i <= n; i += 1) { \
        sum = sum + amp * voronoi_2d_f1(px * freq, py * freq, r).x; \
        max_amp = max_amp + amp; amp = amp * roughness; freq = freq * lacunarity; \
    } \
    var rmd: float = detail - floor(detail); \
    if (rmd > 0.001) { \
        sum = sum + rmd * amp * voronoi_2d_f1(px * freq, py * freq, r).x; \
        max_amp = max_amp + rmd * amp; \
    } \
    return sum / max_amp; \
} \
fun voronoi_2d_f2_fbm(px: float, py: float, detail: float, roughness: float, lacunarity: float, r: float): float { \
    var sum: float = 0.0; var max_amp: float = 0.0; \
    var amp: float = 1.0; var freq: float = 1.0; \
    var n: int = int(clamp(detail, 0.0, 15.0)); \
    for (var i: int = 0; i <= n; i += 1) { \
        sum = sum + amp * voronoi_2d_f2(px * freq, py * freq, r).x; \
        max_amp = max_amp + amp; amp = amp * roughness; freq = freq * lacunarity; \
    } \
    var rmd: float = detail - floor(detail); \
    if (rmd > 0.001) { \
        sum = sum + rmd * amp * voronoi_2d_f2(px * freq, py * freq, r).x; \
        max_amp = max_amp + rmd * amp; \
    } \
    return sum / max_amp; \
} \
fun voronoi_3d_f1(p: float3, r: float): float4 { \
    var cell: float3 = floor3(p); \
    var lp: float3 = p - cell; \
    var min_dist: float = 8.0; \
    var bi: int = 0; var bj: int = 0; var bk: int = 0; \
    for (var k: int = -1; k <= 1; k += 1) \
    for (var j: int = -1; j <= 1; j += 1) \
    for (var i: int = -1; i <= 1; i += 1) { \
        var offset: float3 = float3(float(i), float(j), float(k)); \
        var pt: float3 = offset + voronoi_hash3(cell + offset) * r; \
        var dv: float3 = pt - lp; \
        var d: float = sqrt(dot(dv, dv)); \
        if (d < min_dist) { min_dist = d; bi = i; bj = j; bk = k; } \
    } \
    var off: float3 = float3(float(bi), float(bj), float(bk)); \
    var bpt: float3 = off + voronoi_hash3(cell + off) * r; \
    var col: float3 = voronoi_hash3(cell + off + bpt * (1.0 - r)); \
    return float4(min_dist, col.x, col.y, col.z); \
} \
fun voronoi_3d_f1_pos(p: float3, r: float): float3 { \
    var cell: float3 = floor3(p); \
    var lp: float3 = p - cell; \
    var min_dist: float = 8.0; \
    var bi: int = 0; var bj: int = 0; var bk: int = 0; \
    for (var k: int = -1; k <= 1; k += 1) \
    for (var j: int = -1; j <= 1; j += 1) \
    for (var i: int = -1; i <= 1; i += 1) { \
        var offset: float3 = float3(float(i), float(j), float(k)); \
        var pt: float3 = offset + voronoi_hash3(cell + offset) * r; \
        var dv: float3 = pt - lp; \
        var d: float = sqrt(dot(dv, dv)); \
        if (d < min_dist) { min_dist = d; bi = i; bj = j; bk = k; } \
    } \
    var off: float3 = float3(float(bi), float(bj), float(bk)); \
    var bpt: float3 = off + voronoi_hash3(cell + off) * r; \
    return cell + off + bpt; \
} \
fun voronoi_3d_f2(p: float3, r: float): float4 { \
    var cell: float3 = floor3(p); \
    var lp: float3 = p - cell; \
    var dist1: float = 8.0; var dist2: float = 8.0; \
    var b1i: int = 0; var b1j: int = 0; var b1k: int = 0; \
    var b2i: int = 0; var b2j: int = 0; var b2k: int = 0; \
    for (var k: int = -1; k <= 1; k += 1) \
    for (var j: int = -1; j <= 1; j += 1) \
    for (var i: int = -1; i <= 1; i += 1) { \
        var offset: float3 = float3(float(i), float(j), float(k)); \
        var pt: float3 = offset + voronoi_hash3(cell + offset) * r; \
        var dv: float3 = pt - lp; \
        var d: float = sqrt(dot(dv, dv)); \
        if (d < dist1) { \
            dist2 = dist1; b2i = b1i; b2j = b1j; b2k = b1k; \
            dist1 = d; b1i = i; b1j = j; b1k = k; \
        } else if (d < dist2) { \
            dist2 = d; b2i = i; b2j = j; b2k = k; \
        } \
    } \
    var off2: float3 = float3(float(b2i), float(b2j), float(b2k)); \
    var bpt2: float3 = off2 + voronoi_hash3(cell + off2) * r; \
    var col: float3 = voronoi_hash3(cell + off2 + bpt2 * (1.0 - r)); \
    return float4(dist2, col.x, col.y, col.z); \
} \
fun voronoi_3d_f2_pos(p: float3, r: float): float3 { \
    var cell: float3 = floor3(p); \
    var lp: float3 = p - cell; \
    var dist1: float = 8.0; var dist2: float = 8.0; \
    var b1i: int = 0; var b1j: int = 0; var b1k: int = 0; \
    var b2i: int = 0; var b2j: int = 0; var b2k: int = 0; \
    for (var k: int = -1; k <= 1; k += 1) \
    for (var j: int = -1; j <= 1; j += 1) \
    for (var i: int = -1; i <= 1; i += 1) { \
        var offset: float3 = float3(float(i), float(j), float(k)); \
        var pt: float3 = offset + voronoi_hash3(cell + offset) * r; \
        var dv: float3 = pt - lp; \
        var d: float = sqrt(dot(dv, dv)); \
        if (d < dist1) { \
            dist2 = dist1; b2i = b1i; b2j = b1j; b2k = b1k; \
            dist1 = d; b1i = i; b1j = j; b1k = k; \
        } else if (d < dist2) { \
            dist2 = d; b2i = i; b2j = j; b2k = k; \
        } \
    } \
    var off2: float3 = float3(float(b2i), float(b2j), float(b2k)); \
    var bpt2: float3 = off2 + voronoi_hash3(cell + off2) * r; \
    return cell + off2 + bpt2; \
} \
fun voronoi_3d_f1_fbm(p: float3, detail: float, roughness: float, lacunarity: float, r: float): float { \
    var sum: float = 0.0; var max_amp: float = 0.0; \
    var amp: float = 1.0; var freq: float = 1.0; \
    var n: int = int(clamp(detail, 0.0, 15.0)); \
    for (var i: int = 0; i <= n; i += 1) { \
        sum = sum + amp * voronoi_3d_f1(p * freq, r).x; \
        max_amp = max_amp + amp; amp = amp * roughness; freq = freq * lacunarity; \
    } \
    var rmd: float = detail - floor(detail); \
    if (rmd > 0.001) { \
        sum = sum + rmd * amp * voronoi_3d_f1(p * freq, r).x; \
        max_amp = max_amp + rmd * amp; \
    } \
    return sum / max_amp; \
} \
fun voronoi_3d_f2_fbm(p: float3, detail: float, roughness: float, lacunarity: float, r: float): float { \
    var sum: float = 0.0; var max_amp: float = 0.0; \
    var amp: float = 1.0; var freq: float = 1.0; \
    var n: int = int(clamp(detail, 0.0, 15.0)); \
    for (var i: int = 0; i <= n; i += 1) { \
        sum = sum + amp * voronoi_3d_f2(p * freq, r).x; \
        max_amp = max_amp + amp; amp = amp * roughness; freq = freq * lacunarity; \
    } \
    var rmd: float = detail - floor(detail); \
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
