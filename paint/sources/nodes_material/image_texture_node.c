
#include "../global.h"

char *parser_material_tex_coord = "tex_coord";

bind_tex_t *parser_material_make_texture(ui_node_t *image_node, char *tex_name) {
	i32 i = image_node->buttons->buffer[0]->default_value->buffer[0];
	if (i > 9000) { // 9999 - Texture deleted, use pink now
		return NULL;
	}
	char *filepath = parser_material_enum_data(base_combo_enum_texts(image_node->type)->buffer[i]);
	if (string_equals(filepath, "") || string_index_of(filepath, ".") == -1) {
		return NULL;
	}
	return parser_material_make_bind_tex(tex_name, filepath);
}

char *parser_material_texture_store(ui_node_t *node, bind_tex_t *tex, char *tex_name, i32 color_space) {
	any_array_push(parser_material_matcon->bind_textures, tex);
	node_shader_context_add_elem(parser_material_kong->context, "tex", "short2norm");
	node_shader_add_texture(parser_material_kong, tex_name, NULL);
	char *uv_name = "";
	if (string_equals(node->type, "TEX_IMAGE") && parser_material_get_input_link(node->inputs->buffer[0]) != NULL) {
		uv_name = string_copy(parser_material_parse_vector_input(node->inputs->buffer[0]));
	}
	else {
		uv_name = string_copy(parser_material_tex_coord);
	}
	char *tex_store = parser_material_store_var_name(node);
	if (parser_material_sample_keep_aspect) {
		node_shader_add_constant(parser_material_kong, string_tmp("%s_size: float2", tex_name), string_tmp("_size(%s)", tex_name));
		parser_material_write(parser_material_kong, string_tmp("var %s_size: float2 = constants.%s_size;", tex_store, tex_name));
		parser_material_write(parser_material_kong, string_tmp("var %s_ax: float = %s_size.x / %s_size.y;", tex_store, tex_store, tex_store));
		parser_material_write(parser_material_kong, string_tmp("var %s_ay: float = %s_size.y / %s_size.x;", tex_store, tex_store, tex_store));
		parser_material_write(
		    parser_material_kong,
		    string_tmp("var %s_uv: float2 = ((%s.xy / float(%s) - float2(0.5, 0.5)) * float2(max(%s_ay, 1.0), max(%s_ax, 1.0))) + float2(0.5, 0.5);", tex_store,
		           uv_name, parser_material_sample_uv_scale, tex_store, tex_store));
		parser_material_write(parser_material_kong, string_tmp("if (%s_uv.x < 0.0 || %s_uv.y < 0.0 || %s_uv.x > 1.0 || %s_uv.y > 1.0) { discard; }", tex_store,
		                                                   tex_store, tex_store, tex_store));
		parser_material_write(parser_material_kong, string_tmp("%s_uv = %s_uv * float(%s);", tex_store, tex_store, parser_material_sample_uv_scale));
		uv_name = string_tmp("%s_uv", tex_store);
	}
	if (parser_material_triplanar) {
		if (!string_equals(uv_name, parser_material_tex_coord)) {
			parser_material_write(parser_material_kong, string_tmp("var %s1: float3 = %s;", uv_name, uv_name));
			parser_material_write(parser_material_kong, string_tmp("var %s2: float3 = %s;", uv_name, uv_name));
		}
		parser_material_write(parser_material_kong, string_tmp("var %s: float4 = float4(0.0, 0.0, 0.0, 0.0);", tex_store));
		parser_material_write(parser_material_kong, string_tmp("if (tex_coord_blend.x > 0.0) {%s += sample(%s, sampler_linear, %s.xy) * tex_coord_blend.x; }",
		                                                   tex_store, tex_name, uv_name));
		parser_material_write(parser_material_kong, string_tmp("if (tex_coord_blend.y > 0.0) {%s += sample(%s, sampler_linear, %s1.xy) * tex_coord_blend.y; }",
		                                                   tex_store, tex_name, uv_name));
		parser_material_write(parser_material_kong, string_tmp("if (tex_coord_blend.z > 0.0) {%s += sample(%s, sampler_linear, %s2.xy) * tex_coord_blend.z; }",
		                                                   tex_store, tex_name, uv_name));
	}
	else {
		if (parser_material_is_frag) {
			parser_material_write(parser_material_kong, string_tmp("var %s: float4 = sample(%s, sampler_linear, %s.xy);", tex_store, tex_name, uv_name));
		}
		else {
			parser_material_write(parser_material_kong, string_tmp("var %s: float4 = sample_lod(%s, sampler_linear, %s.xy, 0.0);", tex_store, tex_name, uv_name));
		}
		if (!ends_with(tex->file, ".jpg")) { // Pre-mult alpha
			parser_material_write(parser_material_kong, string_tmp("%s.rgb = %s.rgb * %s.a;", tex_store, tex_store, tex_store));
		}
	}
	if (parser_material_transform_color_space) {
		// Base color socket auto-converts from sRGB to linear
		if (color_space == COLOR_SPACE_LINEAR && parser_material_parsing_basecolor) { // Linear to sRGB
			parser_material_write(parser_material_kong, string_tmp("%s.rgb = pow3(%s.rgb, float3(2.2, 2.2, 2.2));", tex_store, tex_store));
		}
		else if (color_space == COLOR_SPACE_SRGB && !parser_material_parsing_basecolor) { // sRGB to linear
			parser_material_write(parser_material_kong, string_tmp("%s.rgb = pow3(%s.rgb, float3(1.0 / 2.2, 1.0 / 2.2, 1.0 / 2.2));", tex_store, tex_store));
		}
		else if (color_space == COLOR_SPACE_DIRECTX_NORMAL_MAP) { // DirectX normal map to OpenGL normal map
			parser_material_write(parser_material_kong, string_tmp("%s.y = 1.0 - %s.y;", tex_store, tex_store));
		}
	}
	return tex_store;
}

char *image_texture_node_vector(ui_node_t *node, ui_node_socket_t *socket) {
	// Already fetched
	if (string_array_index_of(parser_material_parsed, parser_material_res_var_name(node, node->outputs->buffer[1])) >= 0) { // TODO: node.outputs[0]
		char *varname = parser_material_store_var_name(node);
		return string_tmp("%s.rgb", varname);
	}
	char       *tex_name = parser_material_node_name(node, NULL);
	bind_tex_t *tex      = parser_material_make_texture(node, tex_name);
	if (tex != NULL) {
		i32   color_space = node->buttons->buffer[1]->default_value->buffer[0];
		char *texstore    = parser_material_texture_store(node, tex, tex_name, color_space);
		return string_tmp("%s.rgb", texstore);
	}
	else {
		char *tex_store = parser_material_store_var_name(node); // Pink color for missing texture
		parser_material_write(parser_material_kong, string_tmp("var %s: float4 = float4(1.0, 0.0, 1.0, 1.0);", tex_store));
		return string_tmp("%s.rgb", tex_store);
	}
}

char *image_texture_node_value(ui_node_t *node, ui_node_socket_t *socket) {
	// Already fetched
	if (string_array_index_of(parser_material_parsed, parser_material_res_var_name(node, node->outputs->buffer[0])) >= 0) { // TODO: node.outputs[1]
		char *varname = parser_material_store_var_name(node);
		return string_tmp("%s.a", varname);
	}
	char       *tex_name = parser_material_node_name(node, NULL);
	bind_tex_t *tex      = parser_material_make_texture(node, tex_name);
	if (tex != NULL) {
		i32   color_space = node->buttons->buffer[1]->default_value->buffer[0];
		char *texstore    = parser_material_texture_store(node, tex, tex_name, color_space);
		return string_tmp("%s.a", texstore);
	}
	return "0.0";
}

void image_texture_node_init() {

	char      *image_texture_color_space_data = string_tmp("%s\n%s\n%s\n%s", _tr("Auto"), _tr("Linear"), _tr("sRGB"), _tr("DirectX Normal Map"));
	ui_node_t *image_texture_node_def =
	    ALLOC_INIT(ui_node_t, {.id     = 0,
	                              .name   = _tr("Image Texture"),
	                              .type   = "TEX_IMAGE",
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
	                                      ALLOC_INIT(ui_node_button_t, {.name          = _tr("File"),
	                                                                       .type          = "ENUM",
	                                                                       .output        = -1,
	                                                                       .default_value = f32_array_create_x(0),
	                                                                       .data          = u8_array_create_from_string(""),
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .height        = 0}),
	                                      ALLOC_INIT(ui_node_button_t, {.name          = _tr("Color Space"),
	                                                                       .type          = "ENUM",
	                                                                       .output        = -1,
	                                                                       .default_value = f32_array_create_x(0),
	                                                                       .data          = u8_array_create_from_string(image_texture_color_space_data),
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .height        = 0}),
	                                  },
	                                  2),
	                              .width = 0,
	                              .flags = 0});

	any_array_push(nodes_material_texture, image_texture_node_def);
	any_map_set(parser_material_node_vectors, "TEX_IMAGE", image_texture_node_vector);
	any_map_set(parser_material_node_values, "TEX_IMAGE", image_texture_node_value);
}
