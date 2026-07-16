
#include "global.h"

ui_node_t *parser_material_get_node(i32 id) {
	for (i32 i = 0; i < parser_material_nodes->length; ++i) {
		ui_node_t *n = parser_material_nodes->buffer[i];
		if (n->id == id) {
			return n;
		}
	}
	return NULL;
}

ui_node_link_t *parser_material_get_input_link(ui_node_socket_t *inp) {
	for (i32 i = 0; i < parser_material_links->length; ++i) {
		ui_node_link_t *l = parser_material_links->buffer[i];
		if (l->to_id == inp->node_id) {
			ui_node_t *node = parser_material_get_node(inp->node_id);
			if (node->inputs->length <= l->to_socket) {
				return NULL;
			}
			if (node->inputs->buffer[l->to_socket] == inp) {
				return l;
			}
		}
	}
	return NULL;
}

void parser_material_init() {
	gc_unroot(parser_material_parsed);
	parser_material_parsed = any_array_create_from_raw((void *[]){}, 0);
	gc_root(parser_material_parsed);

	gc_unroot(parser_material_parents);
	parser_material_parents = any_array_create_from_raw((void *[]){}, 0);
	gc_root(parser_material_parents);

	gc_unroot(parser_material_out_normaltan);
	parser_material_out_normaltan = "float3(0.5, 0.5, 1.0)";
	gc_root(parser_material_out_normaltan);

	gc_unroot(parser_material_script_links);
	parser_material_script_links      = NULL;
	parser_material_parsing_basecolor = false;
}

void parse_normal_map_color_input(ui_node_socket_t *inp) {
	parser_material_kong->frag_write_normal++;
	gc_unroot(parser_material_out_normaltan);
	parser_material_out_normaltan = string_copy(parser_material_parse_vector_input(inp));
	gc_root(parser_material_out_normaltan);
	bool _parser_material_is_frag = parser_material_is_frag;
	parser_material_is_frag       = true;
	parser_material_is_frag       = _parser_material_is_frag;
	parser_material_kong->frag_write_normal--;
}

shader_out_t *parser_material_parse_shader(ui_node_t *node, ui_node_socket_t *socket) {
	shader_out_t *sout = GC_ALLOC_INIT(shader_out_t, {.out_basecol    = "float3(0.8, 0.8, 0.8)",
	                                                  .out_roughness  = "0.0",
	                                                  .out_metallic   = "0.0",
	                                                  .out_occlusion  = "1.0",
	                                                  .out_opacity    = "1.0",
	                                                  .out_height     = "0.0",
	                                                  .out_emission   = "0.0",
	                                                  .out_subsurface = "0.0"});
	if (string_equals(node->type, "OUTPUT_MATERIAL_PBR")) {

		// Normal - parsed first to retrieve uv coords
		parse_normal_map_color_input(node->inputs->buffer[5]);
		// Base color
		parser_material_parsing_basecolor = true;
		sout->out_basecol                 = string_copy(parser_material_parse_vector_input(node->inputs->buffer[0]));
		parser_material_parsing_basecolor = false;
		// Occlusion
		sout->out_occlusion = string_copy(parser_material_parse_value_input(node->inputs->buffer[2], false));
		// Roughness
		sout->out_roughness = string_copy(parser_material_parse_value_input(node->inputs->buffer[3], false));
		// Metallic
		sout->out_metallic = string_copy(parser_material_parse_value_input(node->inputs->buffer[4], false));
		// Emission
		if (parser_material_parse_emission) {
			sout->out_emission = string_copy(parser_material_parse_value_input(node->inputs->buffer[6], false));
		}
		// Subsurface
		if (parser_material_parse_subsurface) {
			sout->out_subsurface = string_copy(parser_material_parse_value_input(node->inputs->buffer[8], false));
		}
		// Opacity
		sout->out_opacity = string_copy(parser_material_parse_value_input(node->inputs->buffer[1], false));
		// Height
		if (parser_material_parse_height) {
			sout->out_height = string_copy(parser_material_parse_value_input(node->inputs->buffer[7], false));
		}
	}
	return sout;
}

shader_out_t *parser_material_parse_shader_input(ui_node_socket_t *inp) {
	ui_node_link_t *l         = parser_material_get_input_link(inp);
	ui_node_t      *from_node = l != NULL ? parser_material_get_node(l->from_id) : NULL;
	if (from_node != NULL) {
		return parser_material_parse_shader(from_node, from_node->outputs->buffer[l->from_socket]);
	}
	else {
		shader_out_t *sout = GC_ALLOC_INIT(shader_out_t, {.out_basecol    = "float3(0.8, 0.8, 0.8)",
		                                                  .out_roughness  = "0.0",
		                                                  .out_metallic   = "0.0",
		                                                  .out_occlusion  = "1.0",
		                                                  .out_opacity    = "1.0",
		                                                  .out_height     = "0.0",
		                                                  .out_emission   = "0.0",
		                                                  .out_subsurface = "0.0"});
		return sout;
	}
}

shader_out_t *parser_material_parse_output_pbr(ui_node_t *node) {
	return parser_material_parse_shader(node, NULL);
}

shader_out_t *parser_material_parse(ui_node_canvas_t *canvas, node_shader_context_t *_con, node_shader_t *_kong, material_context_t *_matcon) {
	parser_material_init();
	gc_unroot(parser_material_canvases);
	parser_material_canvases = any_array_create_from_raw(
	    (void *[]){
	        canvas,
	    },
	    1);
	gc_root(parser_material_canvases);
	gc_unroot(parser_material_nodes);
	parser_material_nodes = canvas->nodes;
	gc_root(parser_material_nodes);
	gc_unroot(parser_material_links);
	parser_material_links = canvas->links;
	gc_root(parser_material_links);
	gc_unroot(parser_material_con);
	parser_material_con = _con;
	gc_root(parser_material_con);
	gc_unroot(parser_material_kong);
	parser_material_kong = _kong;
	gc_root(parser_material_kong);
	gc_unroot(parser_material_matcon);
	parser_material_matcon = _matcon;
	gc_root(parser_material_matcon);

	if (parser_material_start_group != NULL) {
		parser_material_push_group(parser_material_start_group);
		gc_unroot(parser_material_parents);
		parser_material_parents = parser_material_start_parents;
		gc_root(parser_material_parents);
	}

	if (parser_material_start_node != NULL) {
		ui_node_link_t *link =
		    GC_ALLOC_INIT(ui_node_link_t, {.id = 99999, .from_id = parser_material_start_node->id, .from_socket = 0, .to_id = -1, .to_socket = -1});
		parser_material_write_result(link);
		shader_out_t *sout = GC_ALLOC_INIT(shader_out_t, {.out_basecol    = "float3(0.0, 0.0, 0.0)",
		                                                  .out_roughness  = "0.0",
		                                                  .out_metallic   = "0.0",
		                                                  .out_occlusion  = "1.0",
		                                                  .out_opacity    = "1.0",
		                                                  .out_height     = "0.0",
		                                                  .out_emission   = "0.0",
		                                                  .out_subsurface = "0.0"});
		return sout;
	}

	ui_node_t *output_node = parser_material_node_by_type(parser_material_nodes, "OUTPUT_MATERIAL_PBR");
	if (output_node != NULL) {
		return parser_material_parse_output_pbr(output_node);
	}
	return NULL;
}

void parser_material_finalize(node_shader_context_t *con) {
	node_shader_t *kong = con->kong;

	if (kong->frag_dotnv) {
		kong->frag_vvec = true;
		kong->frag_n    = true;
	}
	if (kong->frag_vvec) {
		kong->frag_wposition = true;
	}

	if (kong->frag_bposition) {
		if (parser_material_triplanar) {
			node_shader_write_attrib_frag(kong, "var bposition: float3 = float3(\
				tex_coord1.x * tex_coord_blend.y + tex_coord2.x * tex_coord_blend.z,\
				tex_coord.x * tex_coord_blend.x + tex_coord2.y * tex_coord_blend.z,\
				tex_coord.y * tex_coord_blend.x + tex_coord1.y * tex_coord_blend.y);");
		}
		else if (kong->frag_ndcpos) {
			node_shader_add_out(kong, "_bposition: float3");
			node_shader_write_vert(kong, "output._bposition = (output.ndc.xyz / output.ndc.w);");
			node_shader_write_attrib_frag(kong, "var bposition: float3 = input._bposition;");
		}
		else {
			node_shader_add_out(kong, "_bposition: float3");
			node_shader_add_constant(kong, "dim: float3", "_dim");
			node_shader_add_constant(kong, "hdim: float3", "_half_dim");
			node_shader_write_attrib_vert(kong, "output._bposition = (input.pos.xyz + constants.hdim) / constants.dim;");
			node_shader_write_attrib_frag(kong, "var bposition: float3 = input._bposition;");
		}
	}
	if (kong->frag_wposition) {
		node_shader_add_constant(kong, "W: float4x4", "_world_matrix");
		node_shader_add_out(kong, "wposition: float3");
		node_shader_write_attrib_vert(kong, "output.wposition = (constants.W * float4(input.pos.xyz, 1.0)).xyz;");
	}
	if (kong->frag_vposition) {
		node_shader_add_constant(kong, "WV: float4x4", "_world_view_matrix");
		node_shader_add_out(kong, "vposition: float3");
		node_shader_write_attrib_vert(kong, "output.vposition = (constants.WV * float4(input.pos.xyz, 1.0)).xyz;");
	}
	if (kong->frag_mposition) {
		node_shader_add_out(kong, "mposition: float3");
		if (kong->frag_ndcpos) {
			node_shader_write_vert(kong, "output.mposition = (output.ndc.xyz / output.ndc.w);");
		}
		else {
			node_shader_write_attrib_vert(kong, "output.mposition = input.pos.xyz;");
		}
	}
	if (kong->frag_wtangent) {
		node_shader_add_out(kong, "wtangent: float3");
		node_shader_write_attrib_vert(kong, "output.wtangent = float3(0.0, 0.0, 0.0);");
	}
	if (kong->frag_vvec_cam) {
		node_shader_add_constant(kong, "WV: float4x4", "_world_view_matrix");
		node_shader_add_out(kong, "eye_dir_cam: float3");
		node_shader_write_attrib_vert(kong, "output.eye_dir_cam = (constants.WV * float4(input.pos.xyz, 1.0)).xyz;");
		node_shader_write_attrib_vert(kong, "output.eye_dir_cam.z *= -1.0;");
		node_shader_write_attrib_frag(kong, "var vvec_cam: float3 = normalize(input.eye_dir_cam);");
	}
	if (kong->frag_vvec) {
		node_shader_add_constant(kong, "eye: float3", "_camera_pos");
		node_shader_add_out(kong, "eye_dir: float3");
		node_shader_write_attrib_vert(kong, "output.eye_dir = constants.eye - output.wposition;");
		node_shader_write_attrib_frag(kong, "var vvec: float3 = normalize(input.eye_dir);");
	}
	if (kong->frag_n) {
		node_shader_add_constant(kong, "N: float3x3", "_normal_matrix");
		node_shader_add_out(kong, "wnormal: float3");
		node_shader_write_attrib_vert(kong, "output.wnormal = constants.N * float3(input.nor.xy, input.pos.w);");
		node_shader_write_attrib_frag(kong, "var n: float3 = normalize(input.wnormal);");
	}
	if (kong->vert_n) {
		node_shader_add_constant(kong, "N: float3x3", "_normal_matrix");
		node_shader_write_attrib_vert(kong, "var vert_wnormal: float3 = normalize(constants.N * float3(input.nor.xy, input.pos.w));");
	}
	if (kong->frag_nattr) {
		node_shader_add_out(kong, "nattr: float3");
		node_shader_write_attrib_vert(kong, "output.nattr = float3(input.nor.xy, input.pos.w);");
	}
	if (kong->frag_dotnv) {
		node_shader_write_attrib_frag(kong, "var dotnv: float = max(dot(n, vvec), 0.0);");
	}
	if (kong->frag_wvpposition) {
		node_shader_add_out(kong, "wvpposition: float4");
		node_shader_write_end_vert(kong, "output.wvpposition = output.pos;");
	}
	if (node_shader_context_is_elem(con, "col")) {
		node_shader_add_out(kong, "vcolor: float3");
		node_shader_write_attrib_vert(kong, "output.vcolor = input.col.rgb;");
	}
}

ui_node_canvas_t *parser_material_get_group(char *name) {
	for (i32 i = 0; i < g_project->_->material_groups->length; ++i) {
		node_group_t *g = g_project->_->material_groups->buffer[i];
		if (string_equals(g->canvas->name, name)) {
			return g->canvas;
		}
	}
	return NULL;
}

void parser_material_push_group(ui_node_canvas_t *g) {
	any_array_push(parser_material_canvases, g);
	gc_unroot(parser_material_nodes);
	parser_material_nodes = g->nodes;
	gc_root(parser_material_nodes);
	gc_unroot(parser_material_links);
	parser_material_links = g->links;
	gc_root(parser_material_links);
}

void parser_material_pop_group() {
	array_pop(parser_material_canvases);
	ui_node_canvas_t *g = parser_material_canvases->buffer[parser_material_canvases->length - 1];
	gc_unroot(parser_material_nodes);
	parser_material_nodes = g->nodes;
	gc_root(parser_material_nodes);
	gc_unroot(parser_material_links);
	parser_material_links = g->links;
	gc_root(parser_material_links);
}

char *parser_material_parse_input(ui_node_socket_t *inp) {
	if (string_equals(inp->type, "RGB")) {
		return parser_material_parse_vector_input(inp);
	}
	else if (string_equals(inp->type, "RGBA")) {
		return parser_material_parse_vector_input(inp);
	}
	else if (string_equals(inp->type, "VECTOR")) {
		return parser_material_parse_vector_input(inp);
	}
	else if (string_equals(inp->type, "VALUE")) {
		return parser_material_parse_value_input(inp, false);
	}
	return NULL;
}

i32 parser_material_socket_index(ui_node_t *node, ui_node_socket_t *socket) {
	for (i32 i = 0; i < node->outputs->length; ++i) {
		if (node->outputs->buffer[i] == socket) {
			return i;
		}
	}
	return -1;
}

char *parser_material_parse_group(ui_node_t *node, ui_node_socket_t *socket) {
	any_array_push(parser_material_parents, node); // Entering group
	parser_material_push_group(parser_material_get_group(node->name));
	ui_node_t *output_node = parser_material_node_by_type(parser_material_nodes, "GROUP_OUTPUT");
	if (output_node == NULL) {
		return NULL;
	}
	i32               index     = parser_material_socket_index(node, socket);
	ui_node_socket_t *inp       = output_node->inputs->buffer[index];
	char             *out_group = parser_material_parse_input(inp);
	array_pop(parser_material_parents);
	parser_material_pop_group();
	return out_group;
}

char *parser_material_parse_group_input(ui_node_t *node, ui_node_socket_t *socket) {
	ui_node_t *parent = array_pop(parser_material_parents); // Leaving group
	parser_material_pop_group();
	i32               index = parser_material_socket_index(node, socket);
	ui_node_socket_t *inp   = parent->inputs->buffer[index];
	char             *res   = parser_material_parse_input(inp);
	any_array_push(parser_material_parents, parent); // Return to group
	parser_material_push_group(parser_material_get_group(parent->name));
	return res;
}

void parser_material_write(node_shader_t *raw, char *s) {
	if (parser_material_is_frag) {
		node_shader_write_frag(raw, s);
	}
	else {
		node_shader_write_vert(raw, s);
	}
}

char *parser_material_parse_vector_input(ui_node_socket_t *inp) {
	ui_node_link_t *l         = parser_material_get_input_link(inp);
	ui_node_t      *from_node = l != NULL ? parser_material_get_node(l->from_id) : NULL;
	if (from_node != NULL) {
		char *res_var = parser_material_write_result(l);
		char *st      = from_node->outputs->buffer[l->from_socket]->type;
		if (string_equals(st, "RGB") || string_equals(st, "RGBA") || string_equals(st, "VECTOR")) {
			return res_var;
		}
		else { // VALUE
			return parser_material_to_vec3(res_var);
		}
	}
	else {
		return parser_material_vec3(inp->default_value);
	}
}

char *parser_material_parse_vector(ui_node_t *node, ui_node_socket_t *socket) {
	char *(*node_vector)(ui_node_t *, ui_node_socket_t *) = any_map_get(parser_material_node_vectors, node->type);
	if (node_vector != NULL) {
		return node_vector(node, socket);
	}
	else if (string_equals(node->type, "GROUP_INPUT")) {
		return parser_material_parse_group_input(node, socket);
	}
	else if (any_map_get(parser_material_custom_nodes, node->type) != NULL) {
		void       *cb      = any_map_get(parser_material_custom_nodes, node->type);
		minic_val_t args[2] = {minic_val_ptr(node), minic_val_ptr(socket->name)};
		return minic_call_fn(cb, args, 2).p;
	}
	return "float3(0.0, 0.0, 0.0)";
}

char *parser_material_parse_value_input(ui_node_socket_t *inp, bool vector_as_grayscale) {
	ui_node_link_t *l         = parser_material_get_input_link(inp);
	ui_node_t      *from_node = l != NULL ? parser_material_get_node(l->from_id) : NULL;
	if (from_node != NULL) {
		char *res_var = parser_material_write_result(l);
		char *st      = from_node->outputs->buffer[l->from_socket]->type;
		if (string_equals(st, "RGB") || string_equals(st, "RGBA") || string_equals(st, "VECTOR")) {
			if (vector_as_grayscale) {
				return string("dot(%s.rbg, float3(0.299, 0.587, 0.114))", res_var);
			}
			else {
				return string("%s.x", res_var);
			}
		}
		else { // VALUE
			return res_var;
		}
	}
	else {
		return parser_material_vec1(inp->default_value->buffer[0]);
	}
}

char *parser_material_parse_value(ui_node_t *node, ui_node_socket_t *socket) {
	char *(*node_value)(ui_node_t *, ui_node_socket_t *) = any_map_get(parser_material_node_values, node->type);
	if (node_value != NULL) {
		return node_value(node, socket);
	}
	else if (string_equals(node->type, "GROUP_INPUT")) {
		return parser_material_parse_group_input(node, socket);
	}
	else if (any_map_get(parser_material_custom_nodes, node->type) != NULL) {
		void       *cb      = any_map_get(parser_material_custom_nodes, node->type);
		minic_val_t args[2] = {minic_val_ptr(node), minic_val_ptr(socket->name)};
		return minic_call_fn(cb, args, 2).p;
	}
	return "0.0";
}

char *parser_material_get_coord(ui_node_t *node) {
	if (parser_material_get_input_link(node->inputs->buffer[0]) != NULL) {
		return parser_material_parse_vector_input(node->inputs->buffer[0]);
	}
	else {
		parser_material_kong->frag_bposition = true;
		return "bposition";
	}
}

char *parser_material_safesrc(char *s) {
	for (i32 i = 0; i < string_length(s); ++i) {
		i32  code   = char_code_at(s, i);
		bool letter = (code >= 65 && code <= 90) || (code >= 97 && code <= 122);
		bool digit  = code >= 48 && code <= 57;
		if (!letter && !digit) {
			s = string_copy(string_replace_all(s, char_at(s, i), "_"));
		}
		if (i == 0 && digit) {
			s = string("_%s", s);
		}
	}
	return s;
}

char *parser_material_res_var_name(ui_node_t *node, ui_node_socket_t *socket) {
	return string("%s_%s_res", parser_material_node_name(node, NULL), parser_material_safesrc(socket->name));
}

char *parser_material_write_result(ui_node_link_t *l) {
	ui_node_t        *from_node   = parser_material_get_node(l->from_id);
	ui_node_socket_t *from_socket = from_node->outputs->buffer[l->from_socket];
	char             *res_var     = parser_material_res_var_name(from_node, from_socket);
	char             *st          = from_socket->type;
	if (string_array_index_of(parser_material_parsed, res_var) < 0) {
		any_array_push(parser_material_parsed, res_var);
		if (string_equals(st, "RGB") || string_equals(st, "RGBA") || string_equals(st, "VECTOR")) {
			char *res = parser_material_parse_vector(from_node, from_socket);
			if (res == NULL) {
				return NULL;
			}
			any_map_set(parser_material_parsed_map, res_var, res);
			parser_material_write(parser_material_kong, string("var %s: float3 = %s;", res_var, res));
		}
		else if (string_equals(st, "VALUE")) {
			char *res = parser_material_parse_value(from_node, from_socket);
			if (res == NULL) {
				return NULL;
			}
			any_map_set(parser_material_parsed_map, res_var, res);
			parser_material_write(parser_material_kong, string("var %s: float = %s;", res_var, res));
		}
	}
	return res_var;
}

char *parser_material_store_var_name(ui_node_t *node) {
	return string("%s_store", parser_material_node_name(node, NULL));
}

char *parser_material_vec1(f32 v) {
	return f32_to_string_with_zeros(v);
	// return "float(" + v + ")";
	// return v + "";
}

char *parser_material_vec3(f32_array_t *v) {
	// let v0: f32 = v[0];
	// let v1: f32 = v[1];
	// let v2: f32 = v[2];
	char *v0 = f32_to_string_with_zeros(v->buffer[0]);
	char *v1 = f32_to_string_with_zeros(v->buffer[1]);
	char *v2 = f32_to_string_with_zeros(v->buffer[2]);
	return string("float3(%s, %s, %s)", v0, v1, v2);
}

char *parser_material_to_vec3(char *s) {
	// return "float3(" + s + ")";
	return string("float3(%s, %s, %s)", s, s, s);
}

ui_node_t *parser_material_node_by_type(ui_node_t_array_t *nodes, char *ntype) {
	for (i32 i = 0; i < nodes->length; ++i) {
		ui_node_t *n = nodes->buffer[i];
		if (string_equals(n->type, ntype)) {
			return n;
		}
	}
	return NULL;
}

char *parser_material_node_name(ui_node_t *node, ui_node_t_array_t *_parents) {
	if (_parents == NULL) {
		_parents = parser_material_parents;
	}
	char *s = node->name;
	for (i32 i = 0; i < _parents->length; ++i) {
		ui_node_t *p = _parents->buffer[i];
		s            = string("%s%s_%s", p->name, i32_to_string(p->id), s);
	}
	s       = string_copy(parser_material_safesrc(s));
	i32 nid = node->id;
	s       = string("%s%s", s, i32_to_string(nid));
	return s;
}

char *parser_material_enum_data(char *s) {
	for (i32 i = 0; i < g_project->_->assets->length; ++i) {
		asset_t *a = g_project->_->assets->buffer[i];
		if (string_equals(a->name, s)) {
			return a->file;
		}
	}
	return "";
}

bind_tex_t *parser_material_make_bind_tex(char *tex_name, char *file) {
	bind_tex_t *tex = GC_ALLOC_INIT(bind_tex_t, {.name = tex_name, .file = file});
	return tex;
}

char *u8_array_string_at(u8_array_t *a, i32 i) {
	char           *s  = u8_array_to_string(a);
	string_array_t *ss = string_split(s, "\n");
	return ss->buffer[i];
}
