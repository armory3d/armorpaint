
#include "../global.h"

char *mix_normal_map_node_vector(ui_node_t *node, ui_node_socket_t *socket) {
	char             *nm1   = parser_material_parse_vector_input(node->inputs->buffer[0]);
	char             *nm2   = parser_material_parse_vector_input(node->inputs->buffer[1]);
	ui_node_button_t *but   = node->buttons->buffer[0];
	char             *blend = to_upper_case(u8_array_string_at(but->data, but->default_value->buffer[0])); // blend_type
	blend                   = string_copy(string_replace_all(blend, " ", "_"));
	char *store             = parser_material_store_var_name(node);

	// The blending algorithms are based on the paper "Blending in Detail" by Colin Barré-Brisebois and Stephen Hill 2012
	// https://blog.selfshadow.com/publications/blending-in-detail/
	if (string_equals(blend, "PARTIAL_DERIVATIVE")) { // partial derivate blending
		parser_material_write(parser_material_kong, string_tmp("var %s_n1: float3 = %s * 2.0 - 1.0;", store, nm1));
		parser_material_write(parser_material_kong, string_tmp("var %s_n2: float3 = %s * 2.0 - 1.0;", store, nm2));
		return string_tmp("0.5 * normalize(float3(%s_n1.xy * %s_n2.z + %s_n2.xy * %s_n1.z, %s_n1.z * %s_n2.z)) + 0.5", store, store, store, store, store,
		                  store);
	}
	else if (string_equals(blend, "WHITEOUT")) { // whiteout blending
		parser_material_write(parser_material_kong, string_tmp("var %s_n1: float3 = %s * 2.0 - 1.0;", store, nm1));
		parser_material_write(parser_material_kong, string_tmp("var %s_n2: float3 = %s * 2.0 - 1.0;", store, nm2));
		return string_tmp("0.5 * normalize(float3(%s_n1.xy + %s_n2.xy, %s_n1.z * %s_n2.z)) + 0.5", store, store, store, store);
	}
	else { // REORIENTED - reoriented normal mapping
		parser_material_write(parser_material_kong, string_tmp("var %s_n1: float3 = %s * 2.0 - float3(1.0, 1.0, 0.0);", store, nm1));
		parser_material_write(parser_material_kong, string_tmp("var %s_n2: float3 = %s * float3(-2.0, -2.0, 2.0) - float3(-1.0, -1.0, 1.0);", store, nm2));
		return string_tmp("0.5 * normalize(%s_n1 * dot(%s_n1, %s_n2) - %s_n2 * %s_n1.z) + 0.5", store, store, store, store, store);
	}
}

void mix_normal_map_node_init() {

	char      *mix_normal_map_blend_type_data = string_tmp("%s\n%s\n%s", _tr("Partial Derivative"), _tr("Whiteout"), _tr("Reoriented"));
	ui_node_t *mix_normal_map_node_def =
	    ALLOC_INIT(ui_node_t, {.id     = 0,
	                           .name   = _tr("Mix Normal Map"),
	                           .type   = "MIX_NORMAL_MAP", // extension
	                           .x      = 0,
	                           .y      = 0,
	                           .color  = 0xff522c99,
	                           .inputs = any_array_create_from_raw(
	                               (void *[]){
	                                   ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                 .node_id       = 0,
	                                                                 .name          = _tr("Normal Map 1"),
	                                                                 .type          = "VECTOR",
	                                                                 .color         = -10238109,
	                                                                 .default_value = f32_array_create_xyz(0.5, 0.5, 1.0),
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .display       = 0}),
	                                   ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                 .node_id       = 0,
	                                                                 .name          = _tr("Normal Map 2"),
	                                                                 .type          = "VECTOR",
	                                                                 .color         = -10238109,
	                                                                 .default_value = f32_array_create_xyz(0.5, 0.5, 1.0),
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .display       = 0}),
	                               },
	                               2),
	                           .outputs = any_array_create_from_raw(
	                               (void *[]){
	                                   ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                 .node_id       = 0,
	                                                                 .name          = _tr("Normal Map"),
	                                                                 .type          = "VECTOR",
	                                                                 .color         = -10238109,
	                                                                 .default_value = f32_array_create_xyz(0.5, 0.5, 1.0),
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .display       = 0}),
	                               },
	                               1),
	                           .buttons = any_array_create_from_raw(
	                               (void *[]){
	                                   ALLOC_INIT(ui_node_button_t, {.name          = _tr("blend_type"),
	                                                                 .type          = "ENUM",
	                                                                 .output        = 0,
	                                                                 .default_value = f32_array_create_x(0),
	                                                                 .data          = u8_array_create_from_string(mix_normal_map_blend_type_data),
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .height        = 0}),
	                               },
	                               1),
	                           .width = 0,
	                           .flags = 0});

	any_array_push(nodes_material_utilities, mix_normal_map_node_def);
	any_map_set(parser_material_node_vectors, "MIX_NORMAL_MAP", mix_normal_map_node_vector);
}
