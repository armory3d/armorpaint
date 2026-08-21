
#include "../global.h"

char *script_node_value(ui_node_t *node, ui_node_socket_t *socket) {
	if (parser_material_script_links == NULL) {
		parser_material_script_links = any_map_create();
	}
	buffer_t *script = node->buttons->buffer[0]->default_value;
	char     *str    = sys_buffer_to_string(script);
	char     *link   = parser_material_node_name(node, NULL);
	any_map_set(parser_material_script_links, string_copy(link), str);
	node_shader_add_constant(parser_material_kong, string_tmp("%s: float", link), string_tmp("_%s", link));
	return string_tmp("constants.%s", link);
}

ui_node_t *script_node_draw_snippets_node = NULL;

void script_node_draw_snippets() {
	if (ui_menu_button("sys_time()", "", ICON_DRAFT)) {
		ui_node_t *node                                 = script_node_draw_snippets_node;
		node->buttons->buffer[0]->default_value->buffer = "sys_time()";
	}
}

void script_node_button(i32 node_id) {
	ui_node_t   *node      = ui_get_node(ui_nodes_get_canvas(true)->nodes, node_id);
	char        *node_name = parser_material_node_name(node, NULL);
	ui_handle_t *h         = ui_handle(node_name);

	if (ui_button(tr("Snippets"), UI_ALIGN_CENTER, "")) {
		script_node_draw_snippets_node = node;
		ui_menu_draw(&script_node_draw_snippets, -1, -1);
	}
}

void script_node_init() {

	ui_node_t *script_node_def = ALLOC_INIT(ui_node_t, {.id      = 0,
	                                                       .name    = _tr("Script"),
	                                                       .type    = "SCRIPT_CPU", // extension
	                                                       .x       = 0,
	                                                       .y       = 0,
	                                                       .color   = 0xffb34f5a,
	                                                       .inputs  = any_array_create_from_raw((void *[]){}, 0),
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
	                                                           },
	                                                           1),
	                                                       .buttons = any_array_create_from_raw(
	                                                           (void *[]){
	                                                               ALLOC_INIT(ui_node_button_t, {.name          = " ",
	                                                                                                .type          = "STRING",
	                                                                                                .output        = -1,
	                                                                                                .default_value = f32_array_create_x(0), // "",
	                                                                                                .data          = NULL,
	                                                                                                .min           = 0.0,
	                                                                                                .max           = 1.0,
	                                                                                                .precision     = 100,
	                                                                                                .height        = 0}),
	                                                               ALLOC_INIT(ui_node_button_t, {.name          = "script_node_button",
	                                                                                                .type          = "CUSTOM",
	                                                                                                .output        = -1,
	                                                                                                .default_value = f32_array_create_x(0),
	                                                                                                .data          = NULL,
	                                                                                                .min           = 0.0,
	                                                                                                .max           = 1.0,
	                                                                                                .precision     = 100,
	                                                                                                .height        = 1}),
	                                                           },
	                                                           2),
	                                                       .width = 0,
	                                                       .flags = 0});

	any_array_push(nodes_material_input, script_node_def);
	any_map_set(parser_material_node_values, "SCRIPT_CPU", script_node_value);
	any_map_set(ui_nodes_custom_buttons, "script_node_button", script_node_button);
}
