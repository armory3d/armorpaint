
#include "../global.h"

char *str_tex_magic = "\
fun tex_magic(co: float3, distortion: float, depth: float): float3 { \
	var p: float3 = co % float3(6.28318530718, 6.28318530718, 6.28318530718); \
	var x: float = sin((p.x + p.y + p.z) * 5.0); \
	var y: float = cos((-p.x + p.y - p.z) * 5.0); \
	var z: float = -cos((-p.x - p.y + p.z) * 5.0); \
	if (depth > 0.0) { \
		x = x * distortion; \
		y = y * distortion; \
		z = z * distortion; \
		y = -cos(x - y + z); \
		y = y * distortion; \
		if (depth > 1.0) { \
			x = cos(x - y - z); \
			x = x * distortion; \
			if (depth > 2.0) { \
				z = sin(-x - y - z); \
				z = z * distortion; \
				if (depth > 3.0) { \
					x = -cos(-x + y - z); \
					x = x * distortion; \
					if (depth > 4.0) { \
						y = -sin(-x + y + z); \
						y = y * distortion; \
						if (depth > 5.0) { \
							y = -cos(-x + y + z); \
							y = y * distortion; \
							if (depth > 6.0) { \
								x = cos(x + y + z); \
								x = x * distortion; \
								if (depth > 7.0) { \
									z = sin(x + y - z); \
									z = z * distortion; \
									if (depth > 8.0) { \
										x = -cos(-x - y + z); \
										x = x * distortion; \
										if (depth > 9.0) { \
											y = -sin(x - y + z); \
											y = y * distortion; \
										} \
									} \
								} \
							} \
						} \
					} \
				} \
			} \
		} \
	} \
	if (abs(distortion) > 0.0) { \
		var d2: float = distortion * 2.0; \
		x = x / d2; \
		y = y / d2; \
		z = z / d2; \
	} \
	return float3(0.5 - x, 0.5 - y, 0.5 - z); \
} \
fun tex_magic_f(co: float3, distortion: float, depth: float): float { \
	var c: float3 = tex_magic(co, distortion, depth); \
	return (c.x + c.y + c.z) / 3.0; \
} \
";

char *magic_texture_node_vector(ui_node_t *node, ui_node_socket_t *socket) {
	node_shader_add_function(parser_material_kong, str_tex_magic);
	char *co         = parser_material_get_coord(node);
	char *scale      = parser_material_parse_value_input(node->inputs->buffer[1], false);
	char *distortion = parser_material_parse_value_input(node->inputs->buffer[2], false);
	i32   depth      = (i32)node->outputs->buffer[0]->default_value->buffer[0];
	return string_tmp("tex_magic(%s * %s, %s, %d.0)", co, scale, distortion, depth);
}

char *magic_texture_node_value(ui_node_t *node, ui_node_socket_t *socket) {
	node_shader_add_function(parser_material_kong, str_tex_magic);
	char *co         = parser_material_get_coord(node);
	char *scale      = parser_material_parse_value_input(node->inputs->buffer[1], false);
	char *distortion = parser_material_parse_value_input(node->inputs->buffer[2], false);
	i32   depth      = (i32)node->outputs->buffer[0]->default_value->buffer[0];
	return string_tmp("tex_magic_f(%s * %s, %s, %d.0)", co, scale, distortion, depth);
}

void magic_texture_node_init() {

	ui_node_t *magic_texture_node_def =
	    ALLOC_INIT(ui_node_t, {.id     = 0,
	                           .name   = _tr("Magic Texture"),
	                           .type   = "TEX_MAGIC",
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
	                                                                 .default_value = f32_array_create_x(1.0),
	                                                                 .min           = 0.0,
	                                                                 .max           = 10.0,
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
	                                   ALLOC_INIT(ui_node_button_t, {.name          = _tr("Depth"),
	                                                                 .type          = "VALUE",
	                                                                 .output        = -1,
	                                                                 .default_value = f32_array_create_x(2),
	                                                                 .data          = NULL,
	                                                                 .min           = 0.0,
	                                                                 .max           = 10.0,
	                                                                 .precision     = 1,
	                                                                 .height        = 0}),
	                               },
	                               1),
	                           .width = 0,
	                           .flags = 0});

	any_array_push(nodes_material_texture, magic_texture_node_def);
	any_map_set(parser_material_node_vectors, "TEX_MAGIC", magic_texture_node_vector);
	any_map_set(parser_material_node_values, "TEX_MAGIC", magic_texture_node_value);
}
