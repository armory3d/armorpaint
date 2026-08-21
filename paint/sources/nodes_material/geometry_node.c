
#include "../global.h"

char *geometry_node_vector(ui_node_t *node, ui_node_socket_t *socket) {
	if (socket == node->outputs->buffer[0]) { // Position
		parser_material_kong->frag_wposition = true;
		return "input.wposition";
	}
	else if (socket == node->outputs->buffer[1]) { // Normal
		parser_material_kong->frag_n = true;
		return "n";
	}
	else if (socket == node->outputs->buffer[2]) { // Tangent
		parser_material_kong->frag_wtangent = true;
		return "input.wtangent";
	}
	else if (socket == node->outputs->buffer[3]) { // Incoming
		parser_material_kong->frag_vvec = true;
		return "vvec";
	}
	else { // Parametric
		parser_material_kong->frag_mposition = true;
		return "input.mposition";
	}
}

char *geometry_node_value(ui_node_t *node, ui_node_socket_t *socket) {
	if (socket == node->outputs->buffer[5]) { // Pointiness
		f32   strength               = 1.0;
		f32   radius                 = 1.0;
		f32   offset                 = 0.0;
		char *store                  = parser_material_store_var_name(node);
		parser_material_kong->frag_n = true;
		parser_material_write(parser_material_kong, string_tmp("var %s_dx: float3 = ddx3(n);", store));
		parser_material_write(parser_material_kong, string_tmp("var %s_dy: float3 = ddy3(n);", store));
		parser_material_write(parser_material_kong,
		                      string_tmp("var %s_curvature: float = max(dot(%s_dx, %s_dx), dot(%s_dy, %s_dy));", store, store, store, store, store));
		parser_material_write(parser_material_kong, string_tmp("%s_curvature = clamp(pow(%s_curvature, (1.0 / %s) * 0.25) * %s * 2.0 + %s / 10.0, 0.0, 1.0);",
		                                                   store, store, f32_to_string(radius), f32_to_string(strength), f32_to_string(offset)));
		return string_tmp("%s_curvature", store);
	}
	return "";
}

void geometry_node_init() {

	ui_node_t *geometry_node_def = ALLOC_INIT(ui_node_t, {.id      = 0,
	                                                         .name    = _tr("Geometry"),
	                                                         .type    = "NEW_GEOMETRY",
	                                                         .x       = 0,
	                                                         .y       = 0,
	                                                         .color   = 0xffb34f5a,
	                                                         .inputs  = any_array_create_from_raw((void *[]){}, 0),
	                                                         .outputs = any_array_create_from_raw(
	                                                             (void *[]){
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
	                                                                 ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                                                  .node_id       = 0,
	                                                                                                  .name          = _tr("Normal"),
	                                                                                                  .type          = "VECTOR",
	                                                                                                  .color         = 0xff6363c7,
	                                                                                                  .default_value = f32_array_create_xyz(0.0, 0.0, 0.0),
	                                                                                                  .min           = 0.0,
	                                                                                                  .max           = 1.0,
	                                                                                                  .precision     = 100,
	                                                                                                  .display       = 0}),
	                                                                 ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                                                  .node_id       = 0,
	                                                                                                  .name          = _tr("Tangent"),
	                                                                                                  .type          = "VECTOR",
	                                                                                                  .color         = 0xff6363c7,
	                                                                                                  .default_value = f32_array_create_xyz(0.0, 0.0, 0.0),
	                                                                                                  .min           = 0.0,
	                                                                                                  .max           = 1.0,
	                                                                                                  .precision     = 100,
	                                                                                                  .display       = 0}),
	                                                                 ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                                                  .node_id       = 0,
	                                                                                                  .name          = _tr("Incoming"),
	                                                                                                  .type          = "VECTOR",
	                                                                                                  .color         = 0xff6363c7,
	                                                                                                  .default_value = f32_array_create_xyz(0.0, 0.0, 0.0),
	                                                                                                  .min           = 0.0,
	                                                                                                  .max           = 1.0,
	                                                                                                  .precision     = 100,
	                                                                                                  .display       = 0}),
	                                                                 ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                                                  .node_id       = 0,
	                                                                                                  .name          = _tr("Parametric"),
	                                                                                                  .type          = "VECTOR",
	                                                                                                  .color         = 0xff6363c7,
	                                                                                                  .default_value = f32_array_create_xyz(0.0, 0.0, 0.0),
	                                                                                                  .min           = 0.0,
	                                                                                                  .max           = 1.0,
	                                                                                                  .precision     = 100,
	                                                                                                  .display       = 0}),
	                                                                 ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                                                  .node_id       = 0,
	                                                                                                  .name          = _tr("Pointiness"),
	                                                                                                  .type          = "VALUE",
	                                                                                                  .color         = 0xffa1a1a1,
	                                                                                                  .default_value = f32_array_create_x(0.0),
	                                                                                                  .min           = 0.0,
	                                                                                                  .max           = 1.0,
	                                                                                                  .precision     = 100,
	                                                                                                  .display       = 0}),
	                                                             },
	                                                             6),
	                                                         .buttons = any_array_create_from_raw((void *[]){}, 0),
	                                                         .width   = 0,
	                                                         .flags   = 0});

	any_array_push(nodes_material_input, geometry_node_def);
	any_map_set(parser_material_node_vectors, "NEW_GEOMETRY", geometry_node_vector);
	any_map_set(parser_material_node_values, "NEW_GEOMETRY", geometry_node_value);
}
