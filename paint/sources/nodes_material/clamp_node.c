
#include "../global.h"

char *clamp_node_value(ui_node_t *node, ui_node_socket_t *socket) {
	char             *val = parser_material_parse_value_input(node->inputs->buffer[0], false);
	char             *min = parser_material_parse_value_input(node->inputs->buffer[1], false);
	char             *max = parser_material_parse_value_input(node->inputs->buffer[2], false);
	ui_node_button_t *but = node->buttons->buffer[0]; // operation;
	char             *op  = to_upper_case(u8_array_string_at(but->data, but->default_value->buffer[0]));
	op                    = string_copy(string_replace_all(op, " ", "_"));
	if (string_equals(op, "MIN_MAX")) {
		return string_tmp("(clamp(%s, %s, %s))", val, min, max);
	}
	else { // RANGE
		return string_tmp("(clamp(%s, min(%s, %s), max(%s, %s)))", val, min, max, min, max);
	}
}

void clamp_node_init() {

	char      *clamp_operation_data = string_tmp("%s\n%s", _tr("Min Max"), _tr("Range"));
	ui_node_t *clamp_node_def =
	    ALLOC_INIT(ui_node_t, {.id     = 0,
	                           .name   = _tr("Clamp"),
	                           .type   = "CLAMP",
	                           .x      = 0,
	                           .y      = 0,
	                           .color  = 0xff62676d,
	                           .inputs = any_array_create_from_raw(
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
	                                                                 .name          = _tr("Min"),
	                                                                 .type          = "VALUE",
	                                                                 .color         = 0xffa1a1a1,
	                                                                 .default_value = f32_array_create_x(0.0),
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .display       = 0}),
	                                   ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                 .node_id       = 0,
	                                                                 .name          = _tr("Max"),
	                                                                 .type          = "VALUE",
	                                                                 .color         = 0xffa1a1a1,
	                                                                 .default_value = f32_array_create_x(1.0),
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .display       = 0}),
	                               },
	                               3),
	                           .outputs = any_array_create_from_raw(
	                               (void *[]){
	                                   ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                 .node_id       = 0,
	                                                                 .name          = _tr("Value"),
	                                                                 .type          = "VALUE",
	                                                                 .color         = 0xffa1a1a1,
	                                                                 .default_value = f32_array_create_x(0.0),
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .display       = 0}),
	                               },
	                               1),
	                           .buttons = any_array_create_from_raw(
	                               (void *[]){
	                                   ALLOC_INIT(ui_node_button_t, {.name          = _tr("operation"),
	                                                                 .type          = "ENUM",
	                                                                 .output        = 0,
	                                                                 .default_value = f32_array_create_x(0),
	                                                                 .data          = u8_array_create_from_string(clamp_operation_data),
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .height        = 0}),
	                               },
	                               1),
	                           .width = 0,
	                           .flags = 0});

	any_array_push(nodes_material_utilities, clamp_node_def);
	any_map_set(parser_material_node_values, "CLAMP", clamp_node_value);
}
