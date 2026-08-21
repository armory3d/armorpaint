
#include "global.h"

ui_node_t_array_t      *parser_logic_nodes;
ui_node_link_t_array_t *parser_logic_links;
string_array_t         *parser_logic_parsed_nodes = NULL;
any_map_t              *parser_logic_node_map;

ui_node_t *parser_logic_get_node(i32 id) {
	for (i32 i = 0; i < parser_logic_nodes->length; ++i) {
		ui_node_t *n = parser_logic_nodes->buffer[i];
		if (n->id == id) {
			return n;
		}
	}
	return NULL;
}

ui_node_link_t *parser_logic_get_input_link(ui_node_socket_t *inp) {
	for (i32 i = 0; i < parser_logic_links->length; ++i) {
		ui_node_link_t *l = parser_logic_links->buffer[i];
		if (l->to_id == inp->node_id) {
			ui_node_t *node = parser_logic_get_node(inp->node_id);
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

ui_node_link_t_array_t *parser_logic_get_output_links(ui_node_socket_t *out) {
	ui_node_link_t_array_t *res = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < parser_logic_links->length; ++i) {
		ui_node_link_t *l = parser_logic_links->buffer[i];
		if (l->from_id == out->node_id) {
			ui_node_t *node = parser_logic_get_node(out->node_id);
			if (node->outputs->length <= l->from_socket) {
				continue;
			}
			if (node->outputs->buffer[l->from_socket] == out) {
				any_array_push(res, l);
			}
		}
	}
	return res;
}

char *parser_logic_safe_src(char *s) {
	return string_replace_all(s, " ", "");
}

char *parser_logic_node_name(ui_node_t *node) {
	char *safe = parser_logic_safe_src(node->name);
	i32   nid  = node->id;
	char *s    = string("%s%s", safe, i32_to_string(nid));
	return s;
}

logic_node_ext_t *parser_logic_create_node_instance(char *node_type, ui_node_t *raw, f32_array_t *args) {
	if (any_map_get(parser_logic_custom_nodes, node_type) != NULL) {
		logic_node_t *node = logic_node_create(NULL);
		node->get          = any_map_get(parser_logic_custom_nodes, node_type);
		node->ext          = ALLOC_INIT(logic_node_ext_t, {.base = node});
		return node->ext;
	}
	if (nodes_brush_creates == NULL) {
		nodes_brush_init();
	}
	logic_node_ext_t *(*create)(ui_node_t *, f32_array_t *) = any_map_get(nodes_brush_creates, node_type);
	if (create == NULL) {
		return NULL;
	}
	return create(raw, args);
}

logic_node_ext_t *parser_logic_build_default_node(ui_node_socket_t *inp) {
	logic_node_ext_t *v = NULL;

	if (string_equals(inp->type, "VECTOR")) {
		if (inp->default_value == NULL) {
			inp->default_value = f32_array_create_xyz(0, 0, 0);
		}
		v = parser_logic_create_node_instance("vector_node", NULL, inp->default_value);
	}
	else if (string_equals(inp->type, "RGBA")) {
		if (inp->default_value == NULL) {
			inp->default_value = f32_array_create_xyzw(0, 0, 0, 0);
		}
		v = parser_logic_create_node_instance("color_node", NULL, inp->default_value);
	}
	else if (string_equals(inp->type, "RGB")) {
		if (inp->default_value == NULL) {
			inp->default_value = f32_array_create_xyzw(0, 0, 0, 0);
		}
		v = parser_logic_create_node_instance("color_node", NULL, inp->default_value);
	}
	else if (string_equals(inp->type, "VALUE")) {
		v = parser_logic_create_node_instance("float_node", NULL, inp->default_value);
	}
	else if (string_equals(inp->type, "INT")) {
		v = parser_logic_create_node_instance("integer_node", NULL, inp->default_value);
	}
	else if (string_equals(inp->type, "BOOLEAN")) {
		v = parser_logic_create_node_instance("boolean_node", NULL, inp->default_value);
	}
	else if (string_equals(inp->type, "STRING")) {
		v = parser_logic_create_node_instance("string_node", NULL, inp->default_value);
	}
	else {
		v = parser_logic_create_node_instance("null_node", NULL, NULL);
	}
	return v;
}

char *parser_logic_build_node(ui_node_t *node) {
	// Get node name
	char *name = parser_logic_node_name(node);

	// Check if node already exists
	if (string_array_index_of(parser_logic_parsed_nodes, name) != -1) {
		return name;
	}

	// Create node
	logic_node_ext_t *v = parser_logic_create_node_instance(node->type, node, NULL);
	if (v == NULL) {
		return NULL;
	}

	any_array_push(parser_logic_parsed_nodes, name);
	any_map_set(parser_logic_node_map, name, v);

	// Create inputs
	logic_node_ext_t *inp_node = NULL;
	i32               inp_from = 0;
	for (i32 i = 0; i < node->inputs->length; ++i) {
		ui_node_socket_t *inp = node->inputs->buffer[i];
		// Is linked - find node
		ui_node_link_t *l = parser_logic_get_input_link(inp);
		if (l != NULL) {
			ui_node_t *n = parser_logic_get_node(l->from_id);
			char      *s = parser_logic_build_node(n);
			if (s != NULL) {
				inp_node = any_map_get(parser_logic_node_map, s);
				inp_from = l->from_socket;
			}
		}
		// Not linked - create node with default values
		else {
			inp_node = parser_logic_build_default_node(inp);
			inp_from = 0;
		}
		// Add input
		logic_node_add_input(v->base, inp_node, inp_from);
	}

	// Create outputss
	for (i32 i = 0; i < node->outputs->length; ++i) {
		ui_node_socket_t       *out       = node->outputs->buffer[i];
		logic_node_t_array_t   *out_nodes = any_array_create_from_raw((void *[]){}, 0);
		ui_node_link_t_array_t *ls        = parser_logic_get_output_links(out);
		if (ls != NULL && ls->length > 0) {
			for (i32 i = 0; i < ls->length; ++i) {
				ui_node_link_t *l        = ls->buffer[i];
				ui_node_t      *n        = parser_logic_get_node(l->to_id);
				char           *out_name = parser_logic_build_node(n);
				if (out_name != NULL) {
					any_array_push(out_nodes, any_map_get(parser_logic_node_map, out_name));
				}
			}
		}
		// Not linked - create node with default values
		else {
			any_array_push(out_nodes, parser_logic_build_default_node(out));
		}
		// Add outputs
		logic_node_add_outputs(v->base, out_nodes);
	}

	return name;
}

ui_node_t_array_t *parser_logic_get_root_nodes(ui_node_canvas_t *node_group) {
	ui_node_t_array_t *roots = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < node_group->nodes->length; ++i) {
		ui_node_t *node   = node_group->nodes->buffer[i];
		bool       linked = false;
		for (i32 i = 0; i < node->outputs->length; ++i) {
			ui_node_socket_t       *out = node->outputs->buffer[i];
			ui_node_link_t_array_t *ls  = parser_logic_get_output_links(out);
			if (ls != NULL && ls->length > 0) {
				linked = true;
				break;
			}
		}
		if (!linked) { // Assume node with no connected outputs as roots
			any_array_push(roots, node);
		}
	}
	return roots;
}

void parser_logic_parse(ui_node_canvas_t *canvas) {
	parser_logic_nodes = canvas->nodes;

	parser_logic_links = canvas->links;

	parser_logic_parsed_nodes = any_array_create_from_raw((void *[]){}, 0);

	parser_logic_node_map = any_map_create();

	ui_node_t_array_t *root_nodes = parser_logic_get_root_nodes(canvas);
	for (i32 i = 0; i < root_nodes->length; ++i) {
		ui_node_t *node = root_nodes->buffer[i];
		parser_logic_build_node(node);
	}
}
