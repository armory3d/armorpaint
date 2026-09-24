
#include "../global.h"

char *shader_node_text(ui_node_t *node) {
	ui_node_button_t *but = node->buttons->buffer[0];
	return but->data != NULL ? (char *)but->data->buffer : "";
}

static char *shader_node_constants[][3] = {
    {"time", "float", "_time"},
};

static i32 shader_node_constants_count = sizeof(shader_node_constants) / sizeof(shader_node_constants[0]);

char *shader_node_call(ui_node_t *node, char *type, char *suffix) {
	char *text = shader_node_text(node);
	for (i32 i = 0; i < shader_node_constants_count; ++i) {
		char *name = shader_node_constants[i][0];
		if (string_index_of(text, string("constants.%s", name)) != -1) {
			node_shader_add_constant(parser_material_kong, string("%s %s", shader_node_constants[i][1], name), shader_node_constants[i][2]);
		}
	}
	char *fname = string("shader_node_%s_%s", parser_material_node_name(node, NULL), suffix);
	node_shader_add_function(parser_material_kong, string("%s %s(float2 tex_coord) {\n%s\n}", type, fname, text));
	node_shader_context_add_elem(parser_material_kong->context, "tex", "short2norm");
	return string("%s(tex_coord)", fname);
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
	                          "//     Example:\n"
	                          "//     ui_node_t *sh = script_material_create_node_at(\"SHADER_GPU\", -400.0, 0.0);\n"
	                          "//     script_material_set_text(sh, 0, \"float2 c = frac(tex_coord * 8.0);\\n"
	                          "float d = length(c - float2(0.5, 0.5));\\n"
	                          "return lerp(float3(1.0, 0.8, 0.2), float3(0.1, 0.1, 0.3), smoothstep(0.28, 0.3, d));\");\n"
	                          "//     script_material_connect(sh, 0, script_material_get_node(\"OUTPUT_MATERIAL_PBR\"), 0);\n"
	                          "//     script_material_update();\n");
	char *result = string_copy(string_buffer_get(&sb));
	string_buffer_free(&sb);
	return result;
}

ui_node_button_t *shader_node_dirty = NULL;

void shader_node_button(i32 node_id) {
	ui_node_t        *node = ui_get_node(ui_nodes_get_canvas(true)->nodes, node_id);
	ui_node_button_t *but  = node->buttons->buffer[0];
	char             *text = shader_node_text(node);
	ui_set_next_id((ui_id_t)&but->data);
	bool selected = g_ui->text_selected_id == ui_widget_id(NULL, UI_ID_TEXT);
	bool changed  = g_ui->changed;
	ui_text_area(&text, &ui_nodes_editor_state(node)->line, UI_ALIGN_LEFT, true, "", true);
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
	but->height = string_split(text, "\n")->length + 1;
}

void shader_node_init() {

	ui_node_t *shader_node_def = ALLOC_INIT(ui_node_t, {.id      = 0,
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
	                                                                                          .data          = NULL,
	                                                                                          .min           = 0.0,
	                                                                                          .max           = 1.0,
	                                                                                          .precision     = 100,
	                                                                                          .height        = 2}),
	                                                        },
	                                                        1),
	                                                    .width = 0,
	                                                    .flags = 0});

	any_array_push(nodes_material_input, shader_node_def);
	any_map_set(parser_material_node_vectors, "SHADER_GPU", shader_node_vector);
	any_map_set(parser_material_node_values, "SHADER_GPU", shader_node_value);
	any_map_set(ui_nodes_custom_buttons, "shader_node_button", shader_node_button);
}
