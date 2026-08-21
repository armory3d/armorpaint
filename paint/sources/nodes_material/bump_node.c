
#include "../global.h"

char *bump_node_vector(ui_node_t *node, ui_node_socket_t *socket) {
	char *strength = parser_material_parse_value_input(node->inputs->buffer[0], false);
	// char *distance = parser_material_parse_value_input(node->inputs->buffer[1], false);
	char *height          = parser_material_parse_value_input(node->inputs->buffer[2], false);
	char *nor             = parser_material_parse_vector_input(node->inputs->buffer[3]);
	bool  invert          = node->buttons->buffer[0]->default_value->buffer[0] > 0;
	char *sign            = invert ? "-" : "";
	char *sample_bump_res = string_tmp("%s_bump", parser_material_store_var_name(node));
	parser_material_write(parser_material_kong, string_tmp("var %s_x: float = %sddx(float(%s)) * (%s) * 16.0;", sample_bump_res, sign, height, strength));
	parser_material_write(parser_material_kong, string_tmp("var %s_y: float = %sddy(float(%s)) * (%s) * 16.0;", sample_bump_res, sign, height, strength));
	return string_tmp("(normalize(float3(%s_x, %s_y, 1.0) + %s) * float3(0.5, 0.5, 0.5) + float3(0.5, 0.5, 0.5))", sample_bump_res, sample_bump_res, nor);
}

void bump_node_init() {

	ui_node_t *bump_node_def = ALLOC_INIT(
	    ui_node_t,
	    {.id     = 0,
	     .name   = _tr("Bump"),
	     .type   = "BUMP",
	     .x      = 0,
	     .y      = 0,
	     .color  = 0xff522c99,
	     .inputs = any_array_create_from_raw(
	         (void *[]){
	             ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                              .node_id       = 0,
	                                              .name          = _tr("Strength"),
	                                              .type          = "VALUE",
	                                              .color         = 0xffa1a1a1,
	                                              .default_value = f32_array_create_x(1.0),
	                                              .min           = 0.0,
	                                              .max           = 1.0,
	                                              .precision     = 100,
	                                              .display       = 0}),
	             ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                              .node_id       = 0,
	                                              .name          = _tr("Distance"),
	                                              .type          = "VALUE",
	                                              .color         = 0xffa1a1a1,
	                                              .default_value = f32_array_create_x(0.001),
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
	                                              .name          = _tr("Normal"),
	                                              .type          = "VECTOR",
	                                              .color         = 0xff6363c7,
	                                              .default_value = f32_array_create_xyz(0.0, 0.0, 0.0),
	                                              .min           = 0.0,
	                                              .max           = 1.0,
	                                              .precision     = 100,
	                                              .display       = 0}),
	         },
	         4),
	     .outputs = any_array_create_from_raw(
	         (void *[]){
	             ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                              .node_id       = 0,
	                                              .name          = _tr("Normal Map"),
	                                              .type          = "VECTOR",
	                                              .color         = -10238109,
	                                              .default_value = f32_array_create_xyz(0.0, 0.0, 0.0),
	                                              .min           = 0.0,
	                                              .max           = 1.0,
	                                              .precision     = 100,
	                                              .display       = 0}),
	         },
	         1),
	     .buttons = any_array_create_from_raw(
	         (void *[]){
	             ALLOC_INIT(ui_node_button_t, {.name = _tr("Invert"), .type = "BOOL", .output = 0, .default_value = f32_array_create_x(0), .height = 0}),
	         },
	         1),
	     .width = 0,
	     .flags = 0});

	any_array_push(nodes_material_utilities, bump_node_def);
	any_map_set(parser_material_node_vectors, "BUMP", bump_node_vector);
}
