
#include "../global.h"

char *shader_node_text(ui_node_t *node) {
	ui_node_button_t *but = node->buttons->buffer[0];
	return but->data != NULL ? (char *)but->data->buffer : "";
}

static char *shader_node_constants[][3] = {
    {"time", "float", "_time"},      {"frame", "float", "_timeline_frame"},    {"path_a", "float4", "_path_a"},
    {"path_b", "float4", "_path_b"}, {"path_length", "float", "_path_length"},
};

static i32 shader_node_constants_count = sizeof(shader_node_constants) / sizeof(shader_node_constants[0]);

static char *shader_node_live_constants[] = {"constants.time", "constants.frame"};

// Reads a constant that changes every frame
bool shader_node_is_live(ui_node_t *node) {
	char *text = shader_node_text(node);
	for (i32 i = 0; i < sizeof(shader_node_live_constants) / sizeof(shader_node_live_constants[0]); ++i) {
		if (string_index_of(text, shader_node_live_constants[i]) != -1) {
			return true;
		}
	}
	return false;
}

// Reads the timeline frame, its fill layers are refilled when the frame changes
bool shader_node_reads_frame(ui_node_t *node) {
	return string_index_of(shader_node_text(node), "constants.frame") != -1;
}

// Socket name in lower case, other characters replaced by _
// NULL when the name is already taken
static char *shader_node_socket_name(ui_node_socket_array_t *sockets, i32 index) {
	char *name = string_copy(parser_material_safesrc(to_lower_case(sockets->buffer[index]->name)));
	for (i32 i = 0; i < index; ++i) {
		if (string_equals(parser_material_safesrc(to_lower_case(sockets->buffer[i]->name)), name)) {
			return NULL;
		}
	}
	return name;
}

// Parameter name of an input
char *shader_node_input_name(ui_node_t *node, i32 index) {
	char *name = shader_node_socket_name(node->inputs, index);
	return name != NULL && string_equals(name, "tex_coord") ? NULL : name;
}

// Field name of an output in the output struct
char *shader_node_output_name(ui_node_t *node, i32 index) {
	return shader_node_socket_name(node->outputs, index);
}

// Code after a return is invalid spirv, the closing return is added only when the text does not end with one
static bool shader_node_ends_with_return(char *text) {
	i32 end = string_length(text);
	while (end > 0 && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\n' || text[end - 1] == '\r')) {
		end--;
	}
	char *t   = substring(text, 0, end);
	i32   ret = string_last_index_of(t, "return");
	if (ret == -1 || end == 0 || t[end - 1] != ';') {
		return false;
	}
	char *tail = t + ret;
	if (string_index_of(tail, "}") != -1 || string_index_of(tail, ";") != string_length(tail) - 1) {
		return false;
	}
	// A statement of its own, not the body of an if or else
	i32 prev = ret - 1;
	while (prev >= 0 && (t[prev] == ' ' || t[prev] == '\t' || t[prev] == '\n' || t[prev] == '\r')) {
		prev--;
	}
	return prev < 0 || t[prev] == ';' || t[prev] == '}';
}

static char *shader_node_add_constants(ui_node_t *node) {
	char *text = shader_node_text(node);
	for (i32 i = 0; i < shader_node_constants_count; ++i) {
		char *name = shader_node_constants[i][0];
		if (string_index_of(text, string("constants.%s", name)) != -1) {
			node_shader_add_constant(parser_material_kong, string("%s %s", shader_node_constants[i][1], name), shader_node_constants[i][2]);
		}
	}
	return text;
}

// Inputs added to the node are passed in as parameters
static void shader_node_params(ui_node_t *node, char **params, char **args) {
	*params = "float2 tex_coord";
	*args   = "tex_coord";
	for (i32 i = 0; i < node->inputs->length; ++i) {
		ui_node_socket_t *inp  = node->inputs->buffer[i];
		char             *name = shader_node_input_name(node, i);
		if (name == NULL) {
			continue;
		}
		bool is_value = string_equals(inp->type, "VALUE");
		*params       = string("%s, %s %s", *params, is_value ? "float" : "float3", name);
		*args         = string("%s, %s", *args, is_value ? parser_material_parse_value_input(inp, false) : parser_material_parse_vector_input(inp));
	}
}

// All outputs come from one call returning a struct with a field per output, stored in a variable
static char *shader_node_write_outputs(ui_node_t *node) {
	char *store = parser_material_store_var_name(node);
	char *key   = string("%s_shader", store);
	if (string_array_index_of(parser_material_parsed, key) >= 0) {
		return store;
	}
	any_array_push(parser_material_parsed, key);

	char *text  = shader_node_add_constants(node);
	char *fname = string("shader_node_%s", parser_material_node_name(node, NULL));
	char *sname = string("%s_out", fname);
	char *body  = "";
	char *init  = "";
	for (i32 i = 0; i < node->outputs->length; ++i) {
		char *name = shader_node_output_name(node, i);
		if (name == NULL) {
			continue;
		}
		bool is_value = string_equals(node->outputs->buffer[i]->type, "VALUE");
		body          = string("%s\t%s %s;\n", body, is_value ? "float" : "float3", name);
		init          = string("%s\toutput.%s = %s;\n", init, name, is_value ? "0.0" : "float3(0.0, 0.0, 0.0)");
	}
	node_shader_add_function(parser_material_kong, string("struct %s {\n%s};\n", sname, body));

	char *params;
	char *args;
	shader_node_params(node, &params, &args);
	char *ret = shader_node_ends_with_return(text) ? "" : "\n\treturn output;";
	node_shader_add_function(parser_material_kong, string("%s %s(%s) {\n\t%s output;\n%s%s%s\n}", sname, fname, params, sname, init, text, ret));
	node_shader_context_add_elem(parser_material_kong->context, "tex", "short2norm");
	parser_material_write(parser_material_kong, string_tmp("%s %s = %s(%s);", sname, store, fname, args));
	return store;
}

static char *shader_node_output(ui_node_t *node, ui_node_socket_t *socket, char *default_value) {
	char *name = shader_node_output_name(node, array_index_of(node->outputs, socket));
	if (name == NULL || string_equals(shader_node_text(node), "")) {
		return default_value;
	}
	return string("%s.%s", shader_node_write_outputs(node), name);
}

char *shader_node_vector(ui_node_t *node, ui_node_socket_t *socket) {
	return shader_node_output(node, socket, "float3(0.0, 0.0, 0.0)");
}

char *shader_node_value(ui_node_t *node, ui_node_socket_t *socket) {
	return shader_node_output(node, socket, "0.0");
}

char *shader_node_reference() {
	buffer_t sb;
	string_buffer_init(&sb);
	string_buffer_append(&sb, "//     Constants:");
	for (i32 i = 0; i < shader_node_constants_count; ++i) {
		string_buffer_append(&sb, string("%s %s constants.%s", i > 0 ? "," : "", shader_node_constants[i][1], shader_node_constants[i][0]));
	}
	string_buffer_append(&sb, "\n"
	                          "//     Path layers paint the material one segment at a time: path_a.xyz and path_b.xyz are the segment ends in world\n"
	                          "//     space like, .w their arc length along the path, path_length the arc length of the path.\n"
	                          "//     Inputs: script_material_add_input(sh, \"VALUE\", \"Bark Scale\") adds a VALUE, VECTOR or RGBA input socket.\n"
	                          "//     The shader text reads it as a variable named after the socket, in lower case with other characters\n"
	                          "//     replaced by _: float bark_scale. VECTOR and RGBA are float3.\n"
	                          "//     Outputs: the text writes each output socket as output.<name>, named like inputs: output.color is float3,\n"
	                          "//     a VALUE output is float. Outputs not written are 0. The text runs once for all outputs, return output; exits\n"
	                          "//     early. The node starts with a Color output, script_material_add_output(sh, \"VALUE\", \"Height\") adds a VALUE,\n"
	                          "//     VECTOR or RGBA output socket.\n"
	                          "//     Example:\n"
	                          "//     ui_node_t *sh = script_material_create_node_at(\"SHADER_GPU\", -400.0, 0.0);\n"
	                          "//     script_material_set_text(sh, 0, \"float2 c = frac(tex_coord * 8.0);\\n"
	                          "float d = length(c - float2(0.5, 0.5));\\n"
	                          "output.color = lerp(float3(1.0, 0.8, 0.2), float3(0.1, 0.1, 0.3), smoothstep(dot_size - 0.02, dot_size, d));\\n"
	                          "output.height = 1.0 - smoothstep(0.0, dot_size, d);\");\n"
	                          "//     script_material_add_input(sh, \"VALUE\", \"Dot Size\");\n"
	                          "//     script_material_set_float(sh, 1, 0, 0.3);\n"
	                          "//     script_material_add_output(sh, \"VALUE\", \"Height\");\n"
	                          "//     ui_node_t *out = script_material_get_node(\"OUTPUT_MATERIAL_PBR\");\n"
	                          "//     script_material_connect(sh, 0, out, 0); // Base Color\n"
	                          "//     script_material_connect(sh, 1, out, 7); // Height\n"
	                          "//     script_material_update();\n");
	char *result = string_copy(string_buffer_get(&sb));
	string_buffer_free(&sb);
	return result;
}

ui_node_button_t *shader_node_dirty         = NULL;
ui_node_t        *shader_node_socket_node   = NULL;
bool              shader_node_socket_output = false;

void shader_node_add_menu_draw() {
	ui_node_t *node    = shader_node_socket_node;
	char      *names[] = {tr("RGBA"), tr("Vector"), tr("Value")};
	char      *types[] = {"RGBA", "VECTOR", "VALUE"};
	for (i32 i = 0; i < 3; ++i) {
		if (ui_menu_button(names[i], "", ICON_NONE)) {
			ui_nodes_push_undo(NULL);
			nodes_mesh_script_add_socket_to(ui_nodes_get_canvas(true), node, types[i], NULL, shader_node_socket_output);
			ui_nodes_canvas_changed();
		}
	}
}

void shader_node_add_button(i32 node_id) {
	ui_row2();
	bool input  = ui_icon_button(tr("Input"), ICON_PLUS, UI_ALIGN_CENTER);
	bool output = ui_icon_button(tr("Output"), ICON_PLUS, UI_ALIGN_CENTER);
	if (input || output) {
		shader_node_socket_node   = ui_get_node(ui_nodes_get_canvas(true)->nodes, node_id);
		shader_node_socket_output = output;
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
	                               },
	                               1),
	                           .buttons = any_array_create_from_raw(
	                               (void *[]){
	                                   ALLOC_INIT(ui_node_button_t, {.name          = "shader_node_button",
	                                                                 .type          = "CUSTOM",
	                                                                 .output        = -1,
	                                                                 .default_value = f32_array_create_x(0),
	                                                                 .data          = u8_array_create_from_string("output.color = float3(0.0, 0.0, 0.0);"),
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
