
#include "global.h"

node_shader_t *node_shader_create(node_shader_context_t *ctx) {
	node_shader_t *raw = ALLOC_INIT(node_shader_t, {0});
	raw->context       = ctx;
	raw->ins           = any_array_create_from_raw((void *[]){}, 0);
	raw->outs          = any_array_create_from_raw((void *[]){}, 0);
	raw->frag_out      = "float4";
	raw->consts        = any_array_create_from_raw((void *[]){}, 0);
	raw->textures      = any_array_create_from_raw((void *[]){}, 0);
	raw->functions     = any_map_create();

	string_buffer_init(&raw->vert);
	string_buffer_init(&raw->vert_end);
	string_buffer_init(&raw->vert_normal);
	string_buffer_init(&raw->vert_attribs);
	raw->vert_write_normal = 0;

	string_buffer_init(&raw->frag);
	string_buffer_init(&raw->frag_end);
	string_buffer_init(&raw->frag_normal);
	string_buffer_init(&raw->frag_attribs);
	raw->frag_write_normal = 0;

	return raw;
}

void node_shader_add_in(node_shader_t *raw, char *s) {
	any_array_push(raw->ins, s);
}

void node_shader_add_out(node_shader_t *raw, char *s) {
	any_array_push(raw->outs, s);
}

void node_shader_context_add_constant(node_shader_context_t *raw, char *ctype, char *name, char *link) {
	for (i32 i = 0; i < raw->data->constants->length; ++i) {
		shader_const_t *c = raw->data->constants->buffer[i];
		if (string_equals(c->name, name)) {
			return;
		}
	}

	shader_const_t *c = ALLOC_INIT(shader_const_t, {.name = name, .type = ctype});
	if (link != NULL) {
		c->link = string_copy(link);
	}
	shader_const_t_array_t *consts = raw->data->constants;
	any_array_push(consts, c);
}

void node_shader_add_constant(node_shader_t *raw, char *s, char *link) {
	// inp: float4
	if (string_array_index_of(raw->consts, s) == -1) {
		string_array_t *ar    = string_split(s, ": ");
		char           *uname = ar->buffer[0];
		char           *utype = ar->buffer[1];
		any_array_push(raw->consts, s);
		node_shader_context_add_constant(raw->context, utype, uname, link);
		// The context keeps uname / utype
		array_free(ar);
		free(ar);
	}
}

void node_shader_context_add_texture_unit(node_shader_context_t *raw, char *name, char *link) {
	for (i32 i = 0; i < raw->data->texture_units->length; ++i) {
		tex_unit_t *c = raw->data->texture_units->buffer[i];
		if (string_equals(c->name, name)) {
			return;
		}
	}

	tex_unit_t *c = ALLOC_INIT(tex_unit_t, {.name = string_copy(name), .link = string_copy(link)});
	any_array_push(raw->data->texture_units, c);
}

void node_shader_add_texture(node_shader_t *raw, char *name, char *link) {
	if (string_array_index_of(raw->textures, name) == -1) {
		any_array_push(raw->textures, name);
		node_shader_context_add_texture_unit(raw->context, name, link);
	}
}

void node_shader_add_function(node_shader_t *raw, char *s) {
	string_array_t *ar    = string_split(s, "(");
	char           *fname = ar->buffer[0];
	for (i32 i = 1; i < ar->length; ++i) {
		free(ar->buffer[i]);
	}
	array_free(ar);
	free(ar);
	if (any_map_get(raw->functions, fname) != NULL) {
		free(fname);
		return;
	}
	any_map_set(raw->functions, fname, s);
}

static void node_shader_write_line(buffer_t *sb, char *s) {
	string_buffer_append(sb, s);
	string_buffer_append(sb, "\n");
}

void node_shader_write_vert(node_shader_t *raw, char *s) {
	if (raw->vert_write_normal > 0) {
		node_shader_write_line(&raw->vert_normal, s);
	}
	else {
		node_shader_write_line(&raw->vert, s);
	}
}

void node_shader_write_end_vert(node_shader_t *raw, char *s) {
	node_shader_write_line(&raw->vert_end, s);
}

void node_shader_write_attrib_vert(node_shader_t *raw, char *s) {
	node_shader_write_line(&raw->vert_attribs, s);
}

void node_shader_write_frag(node_shader_t *raw, char *s) {
	if (raw->frag_write_normal > 0) {
		node_shader_write_line(&raw->frag_normal, s);
	}
	else {
		node_shader_write_line(&raw->frag, s);
	}
}

void node_shader_write_attrib_frag(node_shader_t *raw, char *s) {
	node_shader_write_line(&raw->frag_attribs, s);
}

char *node_shader_data_size(node_shader_t *raw, char *data) {
	if (string_equals(data, "float1")) {
		return "1";
	}
	else if (string_equals(data, "float2") || string_equals(data, "short2norm")) {
		return "2";
	}
	else if (string_equals(data, "float3")) {
		return "3";
	}
	else { // float4 || short4norm
		return "4";
	}
}

void node_shader_vstruct_to_vsin(node_shader_t *raw) {
	vertex_element_t_array_t *vs = raw->context->data->vertex_elements;
	for (i32 i = 0; i < vs->length; ++i) {
		vertex_element_t *e = vs->buffer[i];
		node_shader_add_in(raw, string_tmp("%s: float%s", e->name, node_shader_data_size(raw, e->data)));
	}
}

char *node_shader_get(node_shader_t *raw) {
	node_shader_vstruct_to_vsin(raw);

	static buffer_t out;
	static bool     out_first = true;
	if (out_first) {
		string_buffer_init(&out);
		out_first = false;
	}
	string_buffer_reset(&out);
	buffer_t *sb = &out;

	string_buffer_append(sb, "struct vert_in {\n");
	for (i32 i = 0; i < raw->ins->length; ++i) {
		string_buffer_append(sb, "\t");
		string_buffer_append(sb, (char *)raw->ins->buffer[i]);
		string_buffer_append(sb, ";\n");
	}
	string_buffer_append(sb, "}\n\n");

	string_buffer_append(sb, "struct vert_out {\n");
	string_buffer_append(sb, "\tpos: float4;\n");
	for (i32 i = 0; i < raw->outs->length; ++i) {
		string_buffer_append(sb, "\t");
		string_buffer_append(sb, (char *)raw->outs->buffer[i]);
		string_buffer_append(sb, ";\n");
	}
	if (raw->consts->length == 0) {
		string_buffer_append(sb, "\tempty: float4;\n");
	}
	string_buffer_append(sb, "}\n\n");

	string_buffer_append(sb, "#[set(everything)]\n");
	string_buffer_append(sb, "const constants: {\n");
	for (i32 i = 0; i < raw->consts->length; ++i) {
		string_buffer_append(sb, "\t");
		string_buffer_append(sb, (char *)raw->consts->buffer[i]);
		string_buffer_append(sb, ";\n");
	}
	if (raw->consts->length == 0) {
		string_buffer_append(sb, "\tempty: float4;\n");
	}
	string_buffer_append(sb, "};\n\n");

	if (raw->textures->length > 0) {
		string_buffer_append(sb, "#[set(everything)]\n");
		string_buffer_append(sb, "const sampler_linear: sampler;\n\n");
	}

	for (i32 i = 0; i < raw->textures->length; ++i) {
		string_buffer_append(sb, "#[set(everything)]\n");
		string_buffer_append(sb, "const ");
		string_buffer_append(sb, (char *)raw->textures->buffer[i]);
		string_buffer_append(sb, ": tex2d;\n");
	}

	string_array_t *keys = map_keys(raw->functions);
	for (i32 i = 0; i < keys->length; ++i) {
		char *f = any_map_get(raw->functions, keys->buffer[i]);
		string_buffer_append(sb, f);
		string_buffer_append(sb, "\n");
	}
	array_free(keys);
	free(keys);
	string_buffer_append(sb, "\n");

	string_buffer_append(sb, "fun kong_vert(input: vert_in): vert_out {\n");
	string_buffer_append(sb, "\tvar output: vert_out;\n\n");
	string_buffer_append(sb, string_buffer_get(&raw->vert_attribs));
	string_buffer_append(sb, string_buffer_get(&raw->vert_normal));
	string_buffer_append(sb, string_buffer_get(&raw->vert));
	string_buffer_append(sb, string_buffer_get(&raw->vert_end));
	if (raw->consts->length == 0) {
		string_buffer_append(sb, "\toutput.empty = constants.empty;\n");
	}
	string_buffer_append(sb, "\n\treturn output;\n");
	string_buffer_append(sb, "}\n\n");

	string_buffer_append(sb, "fun kong_frag(input: vert_out): ");
	string_buffer_append(sb, raw->frag_out);
	string_buffer_append(sb, " {\n");
	string_buffer_append(sb, "\tvar output: ");
	string_buffer_append(sb, raw->frag_out);
	string_buffer_append(sb, ";\n\n");
	string_buffer_append(sb, string_buffer_get(&raw->frag_attribs));
	string_buffer_append(sb, string_buffer_get(&raw->frag_normal));
	string_buffer_append(sb, string_buffer_get(&raw->frag));
	string_buffer_append(sb, string_buffer_get(&raw->frag_end));
	string_buffer_append(sb, "\n\treturn output;\n");
	string_buffer_append(sb, "}\n\n");

	string_buffer_append(sb, "#[pipe]\n");
	string_buffer_append(sb, "struct pipe {\n");
	string_buffer_append(sb, "\tvertex = kong_vert;\n");
	string_buffer_append(sb, "\tfragment = kong_frag;\n");
	string_buffer_append(sb, "}\n");

	string_buffer_free(&raw->vert);
	string_buffer_free(&raw->vert_end);
	string_buffer_free(&raw->vert_normal);
	string_buffer_free(&raw->vert_attribs);
	string_buffer_free(&raw->frag);
	string_buffer_free(&raw->frag_end);
	string_buffer_free(&raw->frag_normal);
	string_buffer_free(&raw->frag_attribs);

	if (node_shader_dump_to_script) {
		node_shader_dump_to_script = false;
		tab_scripts_set(string_buffer_get(&out));
	}

	return string_buffer_get(&out);
}

static void node_shader_array_delete(void *a) {
	array_free(a);
	free(a);
}

void node_shader_context_free(node_shader_context_t *raw) {
	node_shader_t *kong = raw->kong;
	if (kong != NULL) {
		node_shader_array_delete(kong->ins);
		node_shader_array_delete(kong->outs);
		node_shader_array_delete(kong->consts);
		node_shader_array_delete(kong->textures);
		string_array_t *keys = kong->functions->keys;
		for (i32 i = 0; i < keys->capacity; ++i) {
			free(keys->buffer[i]);
		}
		node_shader_array_delete(kong->functions->keys);
		node_shader_array_delete(kong->functions->values);
		free(kong->functions);
		free(kong);
	}
	free(raw);
}

node_shader_context_t *node_shader_context_create(material_t *material, shader_context_t *props) {
	node_shader_context_t *raw = ALLOC_INIT(node_shader_context_t, {0});
	raw->material              = material;

	vertex_element_t_array_t *vertex_elements = props->vertex_elements;
	if (vertex_elements == NULL) {
		vertex_elements = any_array_create_from_raw(
		    (void *[]){
		        ALLOC_INIT(vertex_element_t, {.name = "pos", .data = "short4norm"}),
		        ALLOC_INIT(vertex_element_t, {.name = "nor", .data = "short2norm"}),
		    },
		    2);
	}

	raw->data = ALLOC_INIT(shader_context_t, {.name                    = props->name,
	                                             .depth_write             = props->depth_write,
	                                             .compare_mode            = props->compare_mode,
	                                             .cull_mode               = props->cull_mode,
	                                             .blend_source            = props->blend_source,
	                                             .blend_destination       = props->blend_destination,
	                                             .alpha_blend_source      = props->alpha_blend_source,
	                                             .alpha_blend_destination = props->alpha_blend_destination,
	                                             .fragment_shader         = "",
	                                             .vertex_shader           = "",
	                                             .vertex_elements         = vertex_elements,
	                                             .color_attachments       = props->color_attachments,
	                                             .depth_attachment        = props->depth_attachment});

	shader_context_t *rw = raw->data;
	rw->_                = ALLOC_INIT(shader_context_runtime_t, {0});

	if (props->color_writes_red != NULL) {
		raw->data->color_writes_red = props->color_writes_red;
	}
	if (props->color_writes_green != NULL) {
		raw->data->color_writes_green = props->color_writes_green;
	}
	if (props->color_writes_blue != NULL) {
		raw->data->color_writes_blue = props->color_writes_blue;
	}
	if (props->color_writes_alpha != NULL) {
		raw->data->color_writes_alpha = props->color_writes_alpha;
	}

	raw->data->texture_units = any_array_create_from_raw((void *[]){}, 0);
	raw->data->constants     = i32_array_create_from_raw((i32[]){}, 0);

	free(props);
	return raw;
}

void node_shader_context_add_elem(node_shader_context_t *raw, char *name, char *data_type) {
	for (i32 i = 0; i < raw->data->vertex_elements->length; ++i) {
		vertex_element_t *e = raw->data->vertex_elements->buffer[i];
		if (string_equals(e->name, name)) {
			return;
		}
	}
	vertex_element_t *elem = ALLOC_INIT(vertex_element_t, {.name = name, .data = data_type});
	any_array_push(raw->data->vertex_elements, elem);
}

bool node_shader_context_is_elem(node_shader_context_t *raw, char *name) {
	for (i32 i = 0; i < raw->data->vertex_elements->length; ++i) {
		vertex_element_t *elem = raw->data->vertex_elements->buffer[i];
		if (string_equals(elem->name, name)) {
			return true;
		}
	}
	return false;
}

node_shader_t *node_shader_context_make_kong(node_shader_context_t *raw) {
	raw->data->vertex_shader   = string_tmp("%s_%s.vert", raw->material->name, raw->data->name);
	raw->data->fragment_shader = string_tmp("%s_%s.frag", raw->material->name, raw->data->name);
	raw->kong                  = node_shader_create(raw);
	return raw->kong;
}
