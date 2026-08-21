
#include "../global.h"

char *material_node_vector(ui_node_t *node, ui_node_socket_t *socket) {
	char *result = "float3(0.0, 0.0, 0.0)";
	i32   mi     = node->buttons->buffer[0]->default_value->buffer[0];
	if (mi >= g_project->_->materials->length) {
		return result;
	}
	slot_material_t        *m      = g_project->_->materials->buffer[mi];
	ui_node_t_array_t      *_nodes = parser_material_nodes;
	ui_node_link_t_array_t *_links = parser_material_links;
	parser_material_nodes = m->canvas->nodes;
	parser_material_links = m->canvas->links;
	any_array_push(parser_material_parents, node);
	ui_node_t *output_node = parser_material_node_by_type(parser_material_nodes, "OUTPUT_MATERIAL_PBR");
	if (socket == node->outputs->buffer[0]) { // Base
		result = string_copy(parser_material_parse_vector_input(output_node->inputs->buffer[0]));
	}
	else if (socket == node->outputs->buffer[5]) { // Normal
		result = string_copy(parser_material_parse_vector_input(output_node->inputs->buffer[5]));
	}
	parser_material_nodes = _nodes;
	parser_material_links = _links;
	array_pop(parser_material_parents);
	return result;
}

char *material_node_value(ui_node_t *node, ui_node_socket_t *socket) {
	char *result = "0.0";
	i32   mi     = node->buttons->buffer[0]->default_value->buffer[0];
	if (mi >= g_project->_->materials->length)
		return result;
	slot_material_t        *m      = g_project->_->materials->buffer[mi];
	ui_node_t_array_t      *_nodes = parser_material_nodes;
	ui_node_link_t_array_t *_links = parser_material_links;
	parser_material_nodes = m->canvas->nodes;
	parser_material_links = m->canvas->links;
	any_array_push(parser_material_parents, node);
	ui_node_t *output_node = parser_material_node_by_type(parser_material_nodes, "OUTPUT_MATERIAL_PBR");
	if (socket == node->outputs->buffer[1]) { // Opac
		result = string_copy(parser_material_parse_value_input(output_node->inputs->buffer[1], false));
	}
	else if (socket == node->outputs->buffer[2]) { // Occ
		result = string_copy(parser_material_parse_value_input(output_node->inputs->buffer[2], false));
	}
	else if (socket == node->outputs->buffer[3]) { // Rough
		result = string_copy(parser_material_parse_value_input(output_node->inputs->buffer[3], false));
	}
	else if (socket == node->outputs->buffer[4]) { // Metal
		result = string_copy(parser_material_parse_value_input(output_node->inputs->buffer[4], false));
	}
	else if (socket == node->outputs->buffer[7]) { // Height
		result = string_copy(parser_material_parse_value_input(output_node->inputs->buffer[7], false));
	}
	parser_material_nodes = _nodes;
	parser_material_links = _links;
	array_pop(parser_material_parents);
	return result;
}

void material_node_init() {

	ui_node_t *material_node_def =
	    ALLOC_INIT(ui_node_t, {.id      = 0,
	                              .name    = _tr("Material"),
	                              .type    = "MATERIAL", // extension
	                              .x       = 0,
	                              .y       = 0,
	                              .color   = 0xff4982a0,
	                              .inputs  = any_array_create_from_raw((void *[]){}, 0),
	                              .outputs = any_array_create_from_raw(
	                                  (void *[]){
	                                      ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                       .node_id       = 0,
	                                                                       .name          = _tr("Base Color"),
	                                                                       .type          = "RGBA",
	                                                                       .color         = 0xffc7c729,
	                                                                       .default_value = f32_array_create_xyzw(0.0, 0.0, 0.0, 1.0),
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .display       = 0}),
	                                      ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                       .node_id       = 0,
	                                                                       .name          = _tr("Opacity"),
	                                                                       .type          = "VALUE",
	                                                                       .color         = 0xffa1a1a1,
	                                                                       .default_value = f32_array_create_x(1.0),
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .display       = 0}),
	                                      ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                       .node_id       = 0,
	                                                                       .name          = _tr("Occlusion"),
	                                                                       .type          = "VALUE",
	                                                                       .color         = 0xffa1a1a1,
	                                                                       .default_value = f32_array_create_x(1.0),
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .display       = 0}),
	                                      ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                       .node_id       = 0,
	                                                                       .name          = _tr("Roughness"),
	                                                                       .type          = "VALUE",
	                                                                       .color         = 0xffa1a1a1,
	                                                                       .default_value = f32_array_create_x(1.0),
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .display       = 0}),
	                                      ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                       .node_id       = 0,
	                                                                       .name          = _tr("Metallic"),
	                                                                       .type          = "VALUE",
	                                                                       .color         = 0xffa1a1a1,
	                                                                       .default_value = f32_array_create_x(1.0),
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .display       = 0}),
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
	                                      ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                       .node_id       = 0,
	                                                                       .name          = _tr("Emission"),
	                                                                       .type          = "VALUE",
	                                                                       .color         = 0xffa1a1a1,
	                                                                       .default_value = f32_array_create_x(1.0),
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .display       = 0}),
	                                      ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                       .node_id       = 0,
	                                                                       .name          = _tr("Height"),
	                                                                       .type          = "VALUE",
	                                                                       .color         = 0xffa1a1a1,
	                                                                       .default_value = f32_array_create_x(1.0),
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .display       = 0}),
	                                      ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                       .node_id       = 0,
	                                                                       .name          = _tr("Subsurface"),
	                                                                       .type          = "VALUE",
	                                                                       .color         = 0xffa1a1a1,
	                                                                       .default_value = f32_array_create_x(1.0),
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .display       = 0}),
	                                  },
	                                  9),
	                              .buttons = any_array_create_from_raw(
	                                  (void *[]){
	                                      ALLOC_INIT(ui_node_button_t, {.name          = _tr("Material"),
	                                                                       .type          = "ENUM",
	                                                                       .output        = -1,
	                                                                       .default_value = f32_array_create_x(0),
	                                                                       .data          = u8_array_create_from_string(""),
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .height        = 0}),
	                                  },
	                                  1),
	                              .width = 0,
	                              .flags = 0});

	any_array_push(nodes_material_input, material_node_def);
	any_map_set(parser_material_node_vectors, "MATERIAL", material_node_vector);
	any_map_set(parser_material_node_values, "MATERIAL", material_node_value);
}
