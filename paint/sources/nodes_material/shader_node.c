
#include "../global.h"

char *shader_node_text(ui_node_t *node) {
	ui_node_button_t *but = node->buttons->buffer[0];
	return but->data != NULL ? (char *)but->data->buffer : "";
}

static char *shader_node_constants[][3] = {
    {"time", "float", "_time"},
    {"frame", "float", "_timeline_frame"},
};

static i32 shader_node_constants_count = sizeof(shader_node_constants) / sizeof(shader_node_constants[0]);

// Parameter name of an input: socket name in lower case, other characters replaced by _
// NULL when the name is already taken
char *shader_node_input_name(ui_node_t *node, i32 index) {
	char *name = string_copy(parser_material_safesrc(to_lower_case(node->inputs->buffer[index]->name)));
	if (string_equals(name, "tex_coord")) {
		return NULL;
	}
	for (i32 i = 0; i < index; ++i) {
		if (string_equals(parser_material_safesrc(to_lower_case(node->inputs->buffer[i]->name)), name)) {
			return NULL;
		}
	}
	return name;
}

char *shader_node_call(ui_node_t *node, char *type, char *suffix) {
	char *text = shader_node_text(node);
	for (i32 i = 0; i < shader_node_constants_count; ++i) {
		char *name = shader_node_constants[i][0];
		if (string_index_of(text, string("constants.%s", name)) != -1) {
			node_shader_add_constant(parser_material_kong, string("%s %s", shader_node_constants[i][1], name), shader_node_constants[i][2]);
		}
	}
	// Inputs added to the node are passed in as parameters
	char *params = "float2 tex_coord";
	char *args   = "tex_coord";
	for (i32 i = 0; i < node->inputs->length; ++i) {
		ui_node_socket_t *inp  = node->inputs->buffer[i];
		char             *name = shader_node_input_name(node, i);
		if (name == NULL) {
			continue;
		}
		bool is_value = string_equals(inp->type, "VALUE");
		params        = string("%s, %s %s", params, is_value ? "float" : "float3", name);
		args          = string("%s, %s", args, is_value ? parser_material_parse_value_input(inp, false) : parser_material_parse_vector_input(inp));
	}
	char *fname = string("shader_node_%s_%s", parser_material_node_name(node, NULL), suffix);
	node_shader_add_function(parser_material_kong, string("%s %s(%s) {\n%s\n}", type, fname, params, text));
	node_shader_context_add_elem(parser_material_kong->context, "tex", "short2norm");
	return string("%s(%s)", fname, args);
}

char *shader_node_vector(ui_node_t *node, ui_node_socket_t *socket) {
	char *str = shader_node_text(node);
	return string_equals(str, "") ? "float3(0.0, 0.0, 0.0)" : shader_node_call(node, "float3", "color");
}

char *shader_node_value(ui_node_t *node, ui_node_socket_t *socket) {
	char *str = shader_node_text(node);
	return string_equals(str, "") ? "0.0" : shader_node_call(node, "float", "value");
}

char *shader_node_reference() {
	buffer_t sb;
	string_buffer_init(&sb);
	string_buffer_append(&sb, "//     Constants:");
	for (i32 i = 0; i < shader_node_constants_count; ++i) {
		string_buffer_append(&sb, string("%s %s constants.%s", i > 0 ? "," : "", shader_node_constants[i][1], shader_node_constants[i][0]));
	}
	string_buffer_append(&sb, "\n"
	                          "//     Inputs: script_material_add_input(sh, \"VALUE\", \"Bark Scale\") adds a VALUE, VECTOR or RGBA input socket.\n"
	                          "//     The shader text reads it as a variable named after the socket, in lower case with other characters\n"
	                          "//     replaced by _: float bark_scale. VECTOR and RGBA are float3.\n"
	                          "//     Example:\n"
	                          "//     ui_node_t *sh = script_material_create_node_at(\"SHADER_GPU\", -400.0, 0.0);\n"
	                          "//     script_material_set_text(sh, 0, \"float2 c = frac(tex_coord * 8.0);\\n"
	                          "float d = length(c - float2(0.5, 0.5));\\n"
	                          "return lerp(float3(1.0, 0.8, 0.2), float3(0.1, 0.1, 0.3), smoothstep(dot_size - 0.02, dot_size, d));\");\n"
	                          "//     script_material_add_input(sh, \"VALUE\", \"Dot Size\");\n"
	                          "//     script_material_set_float(sh, 1, 0, 0.3);\n"
	                          "//     script_material_connect(sh, 0, script_material_get_node(\"OUTPUT_MATERIAL_PBR\"), 0);\n"
	                          "//     script_material_update();\n");
	char *result = string_copy(string_buffer_get(&sb));
	string_buffer_free(&sb);
	return result;
}

ui_node_button_t *shader_node_dirty       = NULL;
ui_node_t        *shader_node_socket_node = NULL;

void shader_node_add_menu_draw() {
	ui_node_t *node    = shader_node_socket_node;
	char      *names[] = {tr("RGBA"), tr("Vector"), tr("Value")};
	char      *types[] = {"RGBA", "VECTOR", "VALUE"};
	for (i32 i = 0; i < 3; ++i) {
		if (ui_menu_button(names[i], "", ICON_NONE)) {
			ui_nodes_push_undo(NULL);
			nodes_mesh_script_add_socket(ui_nodes_get_canvas(true), node, types[i], NULL);
			ui_nodes_canvas_changed();
		}
	}
}

void shader_node_add_button(i32 node_id) {
	if (ui_icon_button(tr("Add"), ICON_PLUS, UI_ALIGN_CENTER)) {
		shader_node_socket_node = ui_get_node(ui_nodes_get_canvas(true)->nodes, node_id);
		ui_menu_draw(&shader_node_add_menu_draw, -1, -1);
	}
}

void shader_node_button(i32 node_id) {
	ui_node_t        *node = ui_get_node(ui_nodes_get_canvas(true)->nodes, node_id);
	ui_node_button_t *but  = node->buttons->buffer[0];
	char             *text = shader_node_text(node);
	if (!ui_nodes_code_panel(node)) {
		// Apply the edit of a text field hidden while selected
		if (shader_node_dirty == but) {
			shader_node_dirty = NULL;
			g_ui->changed     = true;
		}
		but->height = 1;
		return;
	}
	ui_set_next_id((ui_id_t)&but->data);
	bool selected = g_ui->text_selected_id == ui_widget_id(NULL, UI_ID_TEXT);
	bool changed  = g_ui->changed;
	ui_nodes_code_begin();
	ui_text_area(&text, &ui_nodes_editor_state(node)->line, UI_ALIGN_LEFT, true, "", false); // Word wrap would reflow the code
	ui_nodes_code_end();
	if (ui_item_changed()) {
		but->data = u8_array_create_from_string(text);
		if (selected) {
			shader_node_dirty = but;
		}
	}
	if (selected) {
		g_ui->changed = changed;
	}
	else if (shader_node_dirty == but) {
		shader_node_dirty = NULL;
		g_ui->changed     = true;
	}
	but->height = ui_line_count(text) * UI_TEXT_AREA_LINE_H() / UI_LINE_H() + 2;
}

void shader_node_init() {

	ui_node_t *shader_node_def =
	    ALLOC_INIT(ui_node_t, {.id      = 0,
	                           .name    = _tr("Shader"),
	                           .type    = "SHADER_GPU", // extension
	                           .x       = 0,
	                           .y       = 0,
	                           .color   = 0xffb34f5a,
	                           .inputs  = any_array_create_from_raw((void *[]){}, 0),
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
	                                                                 .name          = _tr("Value"),
	                                                                 .type          = "VALUE",
	                                                                 .color         = 0xffa1a1a1,
	                                                                 .default_value = f32_array_create_x(0.5),
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .display       = 0}),
	                               },
	                               2),
	                           .buttons = any_array_create_from_raw(
	                               (void *[]){
	                                   ALLOC_INIT(ui_node_button_t, {.name          = "shader_node_button",
	                                                                 .type          = "CUSTOM",
	                                                                 .output        = -1,
	                                                                 .default_value = f32_array_create_x(0),
	                                                                 .data          = u8_array_create_from_string("return float3(0.0, 0.0, 0.0);"),
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .height        = 3}),
	                                   ALLOC_INIT(ui_node_button_t, {.name          = "shader_node_add_button",
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
	                           .width = 300,
	                           .flags = 0});

	any_array_push(nodes_material_input, shader_node_def);
	any_map_set(parser_material_node_vectors, "SHADER_GPU", shader_node_vector);
	any_map_set(parser_material_node_values, "SHADER_GPU", shader_node_value);
	any_map_set(ui_nodes_custom_buttons, "shader_node_button", shader_node_button);
	any_map_set(ui_nodes_custom_buttons, "shader_node_add_button", shader_node_add_button);
}
