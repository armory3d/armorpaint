
#include "../global.h"

char *str_tex_gabor = "\
float gabor_hash3(float3 k) { \
	return frac(sin(dot(k, float3(127.1, 311.7, 74.7))) * 43758.5453); \
} \
float gabor_hash4(float kx, float ky, float kz, float kw) { \
	return frac(sin(kx * 127.1 + ky * 311.7 + kz * 74.7 + kw * 380.3) * 43758.5453); \
} \
float3 tex_gabor_2d(float3 co, float scale, float frequency, float anisotropy, float orientation) { \
	float pi = 3.14159265; \
	frequency = max(0.001, frequency); \
	float isotropy = 1.0 - clamp(anisotropy, 0.0, 1.0); \
	float cx = co.x * scale; \
	float cy = co.y * scale; \
	float celx = floor(cx); \
	float cely = floor(cy); \
	float lx = cx - celx; \
	float ly = cy - cely; \
	float pr = 0.0; \
	float pim = 0.0; \
	for (int jj = -1; jj <= 1; jj += 1) { \
		for (int ii = -1; ii <= 1; ii += 1) { \
			float ccx = celx + float(ii); \
			float ccy = cely + float(jj); \
			float px = lx - float(ii); \
			float py = ly - float(jj); \
			for (int imp = 0; imp < 8; imp += 1) { \
				float kf = float(imp); \
				float rand_ori = (gabor_hash3(float3(ccx, ccy, kf * 3.0)) - 0.5) * pi; \
				float ori = orientation + rand_ori * isotropy; \
				float kcx = gabor_hash3(float3(ccx, ccy, kf * 3.0 + 1.0)); \
				float kcy = gabor_hash3(float3(ccx + 1.0, ccy, kf * 3.0 + 1.0)); \
				float pkx = px - kcx; \
				float pky = py - kcy; \
				float d2 = pkx * pkx + pky * pky; \
				if (d2 < 1.0) { \
					float wt = 1.0; \
					if (gabor_hash3(float3(ccx, ccy, kf * 3.0 + 2.0)) < 0.5) { wt = -1.0; } \
					float hann = 0.5 + 0.5 * cos(pi * d2); \
					float gauss = exp(-pi * d2) * hann; \
					float angle = 2.0 * pi * (pkx * frequency * cos(ori) + pky * frequency * sin(ori)); \
					pr = pr + wt * gauss * cos(angle); \
					pim = pim + wt * gauss * sin(angle); \
				} \
			} \
		} \
	} \
	float norm = 6.0; \
	float value = (pim / norm) * 0.5 + 0.5; \
	float phase = (atan2(pim, pr) + pi) / (2.0 * pi); \
	float intensity = sqrt(pr * pr + pim * pim) / norm; \
	return float3(value, phase, intensity); \
} \
float3 tex_gabor_3d(float3 co, float scale, float frequency, float anisotropy, float3 orientation) { \
	float pi = 3.14159265; \
	frequency = max(0.001, frequency); \
	float isotropy = 1.0 - clamp(anisotropy, 0.0, 1.0); \
	float3 base_ori = normalize(orientation); \
	float3 p = co * scale; \
	float3 cell = floor3(p); \
	float3 lp = p - cell; \
	float inc_base = acos(clamp(base_ori.z, -1.0, 1.0)); \
	float len_xy = sqrt(base_ori.x * base_ori.x + base_ori.y * base_ori.y); \
	float az_base = 0.0; \
	if (len_xy > 0.0001) { az_base = acos(clamp(base_ori.x / len_xy, -1.0, 1.0)); } \
	if (base_ori.y < 0.0) { az_base = -az_base; } \
	float pr = 0.0; \
	float pim = 0.0; \
	for (int kk = -1; kk <= 1; kk += 1) { \
		for (int jj = -1; jj <= 1; jj += 1) { \
			for (int ii = -1; ii <= 1; ii += 1) { \
				float3 cc = cell + float3(float(ii), float(jj), float(kk)); \
				float3 pos = lp - float3(float(ii), float(jj), float(kk)); \
				for (int imp = 0; imp < 8; imp += 1) { \
					float kf = float(imp); \
					float inc = inc_base + gabor_hash4(cc.x, cc.y, cc.z, kf * 3.0) * pi * isotropy; \
					float az = az_base + gabor_hash4(cc.x + 0.5, cc.y, cc.z, kf * 3.0) * pi * isotropy; \
					float sin_inc = sin(inc); \
					float3 ori = float3(sin_inc * cos(az), sin_inc * sin(az), cos(inc)); \
					float kcx = gabor_hash4(cc.x, cc.y, cc.z, kf * 3.0 + 1.0); \
					float kcy = gabor_hash4(cc.x + 0.5, cc.y, cc.z, kf * 3.0 + 1.0); \
					float kcz = gabor_hash4(cc.x + 1.0, cc.y, cc.z, kf * 3.0 + 1.0); \
					float3 pk = pos - float3(kcx, kcy, kcz); \
					float d2 = dot(pk, pk); \
					if (d2 < 1.0) { \
						float wt = 1.0; \
						if (gabor_hash4(cc.x, cc.y, cc.z, kf * 3.0 + 2.0) < 0.5) { wt = -1.0; } \
						float hann = 0.5 + 0.5 * cos(pi * d2); \
						float gauss = exp(-pi * d2) * hann; \
						float angle = 2.0 * pi * dot(pk, frequency * ori); \
						pr = pr + wt * gauss * cos(angle); \
						pim = pim + wt * gauss * sin(angle); \
					} \
				} \
			} \
		} \
	} \
	float norm = 5.04551; \
	float value = (pim / norm) * 0.5 + 0.5; \
	float phase = (atan2(pim, pr) + pi) / (2.0 * pi); \
	float intensity = sqrt(pr * pr + pim * pim) / norm; \
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
