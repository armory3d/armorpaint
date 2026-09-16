
#include "../global.h"

char *str_tex_gabor = "\
fun gabor_hash3(k: float3): float { \
	return frac(sin(dot(k, float3(127.1, 311.7, 74.7))) * 43758.5453); \
} \
fun gabor_hash4(kx: float, ky: float, kz: float, kw: float): float { \
	return frac(sin(kx * 127.1 + ky * 311.7 + kz * 74.7 + kw * 380.3) * 43758.5453); \
} \
fun tex_gabor_2d(co: float3, scale: float, frequency: float, anisotropy: float, orientation: float): float3 { \
	var pi: float = 3.14159265; \
	frequency = max(0.001, frequency); \
	var isotropy: float = 1.0 - clamp(anisotropy, 0.0, 1.0); \
	var cx: float = co.x * scale; \
	var cy: float = co.y * scale; \
	var celx: float = floor(cx); \
	var cely: float = floor(cy); \
	var lx: float = cx - celx; \
	var ly: float = cy - cely; \
	var pr: float = 0.0; \
	var pim: float = 0.0; \
	for (var jj: int = -1; jj <= 1; jj += 1) { \
		for (var ii: int = -1; ii <= 1; ii += 1) { \
			var ccx: float = celx + float(ii); \
			var ccy: float = cely + float(jj); \
			var px: float = lx - float(ii); \
			var py: float = ly - float(jj); \
			for (var imp: int = 0; imp < 8; imp += 1) { \
				var kf: float = float(imp); \
				var rand_ori: float = (gabor_hash3(float3(ccx, ccy, kf * 3.0)) - 0.5) * pi; \
				var ori: float = orientation + rand_ori * isotropy; \
				var kcx: float = gabor_hash3(float3(ccx, ccy, kf * 3.0 + 1.0)); \
				var kcy: float = gabor_hash3(float3(ccx + 1.0, ccy, kf * 3.0 + 1.0)); \
				var pkx: float = px - kcx; \
				var pky: float = py - kcy; \
				var d2: float = pkx * pkx + pky * pky; \
				if (d2 < 1.0) { \
					var wt: float = 1.0; \
					if (gabor_hash3(float3(ccx, ccy, kf * 3.0 + 2.0)) < 0.5) { wt = -1.0; } \
					var hann: float = 0.5 + 0.5 * cos(pi * d2); \
					var gauss: float = exp(-pi * d2) * hann; \
					var angle: float = 2.0 * pi * (pkx * frequency * cos(ori) + pky * frequency * sin(ori)); \
					pr = pr + wt * gauss * cos(angle); \
					pim = pim + wt * gauss * sin(angle); \
				} \
			} \
		} \
	} \
	var norm: float = 6.0; \
	var value: float = (pim / norm) * 0.5 + 0.5; \
	var phase: float = (atan2(pim, pr) + pi) / (2.0 * pi); \
	var intensity: float = sqrt(pr * pr + pim * pim) / norm; \
	return float3(value, phase, intensity); \
} \
fun tex_gabor_3d(co: float3, scale: float, frequency: float, anisotropy: float, orientation: float3): float3 { \
	var pi: float = 3.14159265; \
	frequency = max(0.001, frequency); \
	var isotropy: float = 1.0 - clamp(anisotropy, 0.0, 1.0); \
	var base_ori: float3 = normalize(orientation); \
	var p: float3 = co * scale; \
	var cell: float3 = floor3(p); \
	var lp: float3 = p - cell; \
	var inc_base: float = acos(clamp(base_ori.z, -1.0, 1.0)); \
	var len_xy: float = sqrt(base_ori.x * base_ori.x + base_ori.y * base_ori.y); \
	var az_base: float = 0.0; \
	if (len_xy > 0.0001) { az_base = acos(clamp(base_ori.x / len_xy, -1.0, 1.0)); } \
	if (base_ori.y < 0.0) { az_base = -az_base; } \
	var pr: float = 0.0; \
	var pim: float = 0.0; \
	for (var kk: int = -1; kk <= 1; kk += 1) { \
		for (var jj: int = -1; jj <= 1; jj += 1) { \
			for (var ii: int = -1; ii <= 1; ii += 1) { \
				var cc: float3 = cell + float3(float(ii), float(jj), float(kk)); \
				var pos: float3 = lp - float3(float(ii), float(jj), float(kk)); \
				for (var imp: int = 0; imp < 8; imp += 1) { \
					var kf: float = float(imp); \
					var inc: float = inc_base + gabor_hash4(cc.x, cc.y, cc.z, kf * 3.0) * pi * isotropy; \
					var az: float = az_base + gabor_hash4(cc.x + 0.5, cc.y, cc.z, kf * 3.0) * pi * isotropy; \
					var sin_inc: float = sin(inc); \
					var ori: float3 = float3(sin_inc * cos(az), sin_inc * sin(az), cos(inc)); \
					var kcx: float = gabor_hash4(cc.x, cc.y, cc.z, kf * 3.0 + 1.0); \
					var kcy: float = gabor_hash4(cc.x + 0.5, cc.y, cc.z, kf * 3.0 + 1.0); \
					var kcz: float = gabor_hash4(cc.x + 1.0, cc.y, cc.z, kf * 3.0 + 1.0); \
					var pk: float3 = pos - float3(kcx, kcy, kcz); \
					var d2: float = dot(pk, pk); \
					if (d2 < 1.0) { \
						var wt: float = 1.0; \
						if (gabor_hash4(cc.x, cc.y, cc.z, kf * 3.0 + 2.0) < 0.5) { wt = -1.0; } \
						var hann: float = 0.5 + 0.5 * cos(pi * d2); \
						var gauss: float = exp(-pi * d2) * hann; \
						var angle: float = 2.0 * pi * dot(pk, frequency * ori); \
						pr = pr + wt * gauss * cos(angle); \
						pim = pim + wt * gauss * sin(angle); \
					} \
				} \
			} \
		} \
	} \
	var norm: float = 5.04551; \
	var value: float = (pim / norm) * 0.5 + 0.5; \
	var phase: float = (atan2(pim, pr) + pi) / (2.0 * pi); \
	var intensity: float = sqrt(pr * pr + pim * pim) / norm; \
	return float3(value, phase, intensity); \
} \
";

char *gabor_texture_node_value(ui_node_t *node, ui_node_socket_t *socket) {
	node_shader_add_function(parser_material_kong, str_tex_gabor);
	char             *co         = parser_material_get_coord(node);
	char             *scale      = parser_material_parse_value_input(node->inputs->buffer[1], false);
	char             *frequency  = parser_material_parse_value_input(node->inputs->buffer[2], false);
	char             *anisotropy = parser_material_parse_value_input(node->inputs->buffer[3], false);
	ui_node_button_t *but        = node->buttons->buffer[0];
	i32               is_2d      = (i32)but->default_value->buffer[0] == 0;
	char             *res;
	if (is_2d) {
		char *ori2d = parser_material_parse_value_input(node->inputs->buffer[5], false);
		res         = string_tmp("tex_gabor_2d(%s, %s, %s, %s, %s)", co, scale, frequency, anisotropy, ori2d);
	}
	else {
		char *ori3d = parser_material_parse_vector_input(node->inputs->buffer[4]);
		res         = string_tmp("tex_gabor_3d(%s, %s, %s, %s, %s)", co, scale, frequency, anisotropy, ori3d);
	}
	if (socket == node->outputs->buffer[0]) {
		return string_tmp("%s.x", res);
	}
	else if (socket == node->outputs->buffer[1]) {
		return string_tmp("%s.y", res);
	}
	else {
		return string_tmp("%s.z", res);
	}
}

void gabor_texture_node_init() {

	char      *gabor_dimensions_data = string_tmp("%s\n%s", _tr("2D"), _tr("3D"));
	ui_node_t *gabor_texture_node_def =
	    ALLOC_INIT(ui_node_t, {.id     = 0,
	                           .name   = _tr("Gabor Texture"),
	                           .type   = "TEX_GABOR",
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
	                                                                 .name          = _tr("Frequency"),
	                                                                 .type          = "VALUE",
	                                                                 .color         = 0xffa1a1a1,
	                                                                 .default_value = f32_array_create_x(1.0),
	                                                                 .min           = 0.0,
	                                                                 .max           = 10.0,
	                                                                 .precision     = 100,
	                                                                 .display       = 0}),
	                                   ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                 .node_id       = 0,
	                                                                 .name          = _tr("Anisotropy"),
	                                                                 .type          = "VALUE",
	                                                                 .color         = 0xffa1a1a1,
	                                                                 .default_value = f32_array_create_x(0.0),
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .display       = 0}),
	                                   ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                 .node_id       = 0,
	                                                                 .name          = _tr("Orientation 3D"),
	                                                                 .type          = "VECTOR",
	                                                                 .color         = 0xff6363c7,
	                                                                 .default_value = f32_array_create_xyz(1.0, 0.0, 0.0),
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .display       = 0}),
	                                   ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                 .node_id       = 0,
	                                                                 .name          = _tr("Orientation 2D"),
	                                                                 .type          = "VALUE",
	                                                                 .color         = 0xffa1a1a1,
	                                                                 .default_value = f32_array_create_x(0.0),
	                                                                 .min           = -3.14159,
	                                                                 .max           = 3.14159,
	                                                                 .precision     = 100,
	                                                                 .display       = 0}),
	                               },
	                               6),
	                           .outputs = any_array_create_from_raw(
	                               (void *[]){
	                                   ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                 .node_id       = 0,
	                                                                 .name          = _tr("Value"),
	                                                                 .type          = "VALUE",
	                                                                 .color         = 0xffa1a1a1,
	                                                                 .default_value = f32_array_create_x(0.5),
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .display       = 0}),
	                                   ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                 .node_id       = 0,
	                                                                 .name          = _tr("Phase"),
	                                                                 .type          = "VALUE",
	                                                                 .color         = 0xffa1a1a1,
	                                                                 .default_value = f32_array_create_x(0.5),
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .display       = 0}),
	                                   ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                 .node_id       = 0,
	                                                                 .name          = _tr("Intensity"),
	                                                                 .type          = "VALUE",
	                                                                 .color         = 0xffa1a1a1,
	                                                                 .default_value = f32_array_create_x(0.5),
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
	                                                                 .data          = u8_array_create_from_string(gabor_dimensions_data),
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .height        = 0}),
	                               },
	                               1),
	                           .width = 0,
	                           .flags = 0});

	any_array_push(nodes_material_texture, gabor_texture_node_def);
	any_map_set(parser_material_node_values, "TEX_GABOR", gabor_texture_node_value);
}
