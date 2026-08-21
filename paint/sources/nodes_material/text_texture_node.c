
#include "../global.h"

void _parser_material_cache_tex_text_node_on_next_frame(char *text) {
	char          *_text_tool_text  = g_context->text_tool_text;
	gpu_texture_t *_text_tool_image = g_context->text_tool_image;
	g_context->text_tool_text       = string_copy(text);
	g_context->text_tool_image      = NULL;
	util_render_make_text_preview();
	char *file = string_tmp("tex_text_%s", text);
	// TODO: remove old cache
	any_map_set(data_cached_textures, file, g_context->text_tool_image);
	g_context->text_tool_text  = string_copy(_text_tool_text);
	g_context->text_tool_image = _text_tool_image;
}

void _parser_material_cache_tex_text_node(char *file, char *text) {
	if (any_map_get(data_cached_textures, file) == NULL) {
		sys_notify_on_next_frame(&_parser_material_cache_tex_text_node_on_next_frame, text);
	}
}

char *text_texture_node_vector(ui_node_t *node, ui_node_socket_t *socket) {
	char     *tex_name    = parser_material_node_name(node, NULL);
	buffer_t *text_buffer = node->buttons->buffer[0]->default_value;
	char     *text        = sys_buffer_to_string(text_buffer);
	char     *file        = string_tmp("tex_text_%s", text);
	_parser_material_cache_tex_text_node(file, text);
	bind_tex_t *tex      = parser_material_make_bind_tex(tex_name, file);
	char       *texstore = parser_material_texture_store(node, tex, tex_name, COLOR_SPACE_AUTO);
	return string_tmp("%s.rrr", texstore);
}

char *text_texture_node_value(ui_node_t *node, ui_node_socket_t *socket) {
	char     *tex_name    = parser_material_node_name(node, NULL);
	buffer_t *text_buffer = node->buttons->buffer[0]->default_value;
	char     *text        = sys_buffer_to_string(text_buffer);
	char     *file        = string_tmp("tex_text_%s", text);
	_parser_material_cache_tex_text_node(file, text);
	bind_tex_t *tex      = parser_material_make_bind_tex(tex_name, file);
	char       *texstore = parser_material_texture_store(node, tex, tex_name, COLOR_SPACE_AUTO);
	return string_tmp("%s.r", texstore);
}

void text_texture_node_init() {

	ui_node_t *text_texture_node_def = ALLOC_INIT(ui_node_t, {.id     = 0,
	                                                          .name   = _tr("Text Texture"),
	                                                          .type   = "TEX_TEXT", // extension
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
	                                                              },
	                                                              1),
	                                                          .outputs = any_array_create_from_raw(
	                                                              (void *[]){
	                                                                  ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                                                .node_id       = 0,
	                                                                                                .name          = _tr("Color"),
	                                                                                                .type          = "RGBA",
	                                                                                                .color         = 0xffc7c729,
	                                                                                                .default_value = f32_array_create_xyzw(0.0, 0.0, 0.0, 1.0),
	                                                                                                .min           = 0.0,
	                                                                                                .max           = 1.0,
	                                                                                                .precision     = 100,
	                                                                                                .display       = 0}),
	                                                                  ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                                                .node_id       = 0,
	                                                                                                .name          = _tr("Alpha"),
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
	                                                                  ALLOC_INIT(ui_node_button_t, {.name          = "text",
	                                                                                                .type          = "STRING",
	                                                                                                .output        = -1,
	                                                                                                .default_value = f32_array_create_x(0), // "",
	                                                                                                .data          = NULL,
	                                                                                                .min           = 0.0,
	                                                                                                .max           = 1.0,
	                                                                                                .precision     = 100,
	                                                                                                .height        = 0}),
	                                                              },
	                                                              1),
	                                                          .width = 0,
	                                                          .flags = 0});

	any_array_push(nodes_material_texture, text_texture_node_def);
	any_map_set(parser_material_node_vectors, "TEX_TEXT", text_texture_node_vector);
	any_map_set(parser_material_node_values, "TEX_TEXT", text_texture_node_value);
}
