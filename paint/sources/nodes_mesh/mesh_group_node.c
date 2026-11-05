#include "../global.h"

#define MESH_GROUP_MAX_DEPTH 16

typedef struct {
	ui_node_canvas_t *canvas;
	ui_node_t        *node;
	u32               key;
} mesh_group_frame_t;

static mesh_group_frame_t mesh_group_frames[MESH_GROUP_MAX_DEPTH];
static i32                mesh_group_frames_count = 0;

node_group_t *nodes_mesh_get_group(char *name) {
	for (i32 i = 0; g_project->_->mesh_groups != NULL && i < g_project->_->mesh_groups->length; ++i) {
		node_group_t *g = g_project->_->mesh_groups->buffer[i];
		if (string_equals(g->canvas->name, name)) {
			return g;
		}
	}
	return NULL;
}

bool nodes_mesh_group_contains(node_group_t *g, char *name, i32 depth) {
	if (g == NULL || depth > MESH_GROUP_MAX_DEPTH) {
		return false;
	}
	if (string_equals(g->canvas->name, name)) {
		return true;
	}
	for (i32 i = 0; i < g->canvas->nodes->length; ++i) {
		ui_node_t *n = g->canvas->nodes->buffer[i];
		if (string_equals(n->type, "GROUP") && nodes_mesh_group_contains(nodes_mesh_get_group(n->name), name, depth + 1)) {
			return true;
		}
	}
	return false;
}

static ui_node_t *mesh_group_node_find(ui_node_canvas_t *c, char *type) {
	for (i32 i = 0; i < c->nodes->length; ++i) {
		if (string_equals(c->nodes->buffer[i]->type, type)) {
			return c->nodes->buffer[i];
		}
	}
	return NULL;
}

static bool mesh_group_node_enter(ui_node_t *node) {
	node_group_t *g = nodes_mesh_get_group(node->name);
	if (g == NULL || mesh_group_frames_count >= MESH_GROUP_MAX_DEPTH) {
		return false;
	}
	mesh_group_frames[mesh_group_frames_count++] = (mesh_group_frame_t){.canvas = nodes_mesh_eval_canvas, .node = node, .key = nodes_mesh_eval_group_key};
	nodes_mesh_eval_canvas                       = g->canvas;
	nodes_mesh_eval_group_key                    = nodes_mesh_eval_group_key * 31 + node->id + 1;
	return true;
}

static void mesh_group_node_leave() {
	mesh_group_frame_t *f     = &mesh_group_frames[--mesh_group_frames_count];
	nodes_mesh_eval_canvas    = f->canvas;
	nodes_mesh_eval_group_key = f->key;
}

static mesh_geo_t *mesh_group_node_eval(ui_node_t *node) {
	i32 socket = nodes_mesh_eval_socket;
	if (!mesh_group_node_enter(node)) {
		return util_geo_create(0, 0);
	}
	ui_node_t  *out = mesh_group_node_find(nodes_mesh_eval_canvas, "GROUP_OUTPUT");
	mesh_geo_t *g   = out != NULL && socket < out->inputs->length ? nodes_mesh_eval_input(out, socket) : util_geo_create(0, 0);
	mesh_group_node_leave();
	return g;
}

static vec4_t mesh_group_node_vector(ui_node_t *node) {
	i32 socket = nodes_mesh_vector_socket;
	if (!mesh_group_node_enter(node)) {
		return (vec4_t){0.0, 0.0, 0.0, 1.0};
	}
	ui_node_t *out    = mesh_group_node_find(nodes_mesh_eval_canvas, "GROUP_OUTPUT");
	vec4_t     result = out != NULL && socket < out->inputs->length ? nodes_mesh_vector(out, socket) : (vec4_t){0.0, 0.0, 0.0, 1.0};
	mesh_group_node_leave();
	return result;
}

static mesh_geo_t *mesh_group_input_node_eval(ui_node_t *node) {
	i32 socket = nodes_mesh_eval_socket;
	if (mesh_group_frames_count == 0) {
		return util_geo_create(0, 0);
	}
	mesh_group_frame_t f                         = mesh_group_frames[--mesh_group_frames_count];
	ui_node_canvas_t  *inner                     = nodes_mesh_eval_canvas;
	u32                key                       = nodes_mesh_eval_group_key;
	nodes_mesh_eval_canvas                       = f.canvas;
	nodes_mesh_eval_group_key                    = f.key;
	mesh_geo_t *g                                = socket < f.node->inputs->length ? nodes_mesh_eval_input(f.node, socket) : util_geo_create(0, 0);
	nodes_mesh_eval_canvas                       = inner;
	nodes_mesh_eval_group_key                    = key;
	mesh_group_frames[mesh_group_frames_count++] = f;
	return g;
}

static vec4_t mesh_group_input_node_vector(ui_node_t *node) {
	i32 socket = nodes_mesh_vector_socket;
	if (mesh_group_frames_count == 0) {
		return (vec4_t){0.0, 0.0, 0.0, 1.0};
	}
	mesh_group_frame_t f                         = mesh_group_frames[--mesh_group_frames_count];
	ui_node_canvas_t  *inner                     = nodes_mesh_eval_canvas;
	u32                key                       = nodes_mesh_eval_group_key;
	nodes_mesh_eval_canvas                       = f.canvas;
	nodes_mesh_eval_group_key                    = f.key;
	vec4_t result                                = socket < f.node->inputs->length ? nodes_mesh_vector(f.node, socket) : (vec4_t){0.0, 0.0, 0.0, 1.0};
	nodes_mesh_eval_canvas                       = inner;
	nodes_mesh_eval_group_key                    = key;
	mesh_group_frames[mesh_group_frames_count++] = f;
	return result;
}

void nodes_mesh_group_reset_eval() {
	mesh_group_frames_count   = 0;
	nodes_mesh_eval_group_key = 0;
}

static ui_node_t *mesh_group_io_node(i32 id, char *name, char *type, char *button, f32 x) {
	ui_node_t *n = ALLOC_INIT(ui_node_t, {.id      = id,
	                                      .name    = name,
	                                      .type    = type,
	                                      .x       = x,
	                                      .y       = 200,
	                                      .color   = 0xff448c6d,
	                                      .inputs  = any_array_create(0),
	                                      .outputs = any_array_create(0),
	                                      .buttons = any_array_create(0),
	                                      .width   = 0,
	                                      .flags   = 0});
	any_array_push(n->buttons,
	               ALLOC_INIT(ui_node_button_t, {.name = button, .type = "CUSTOM", .height = 1, .output = -1, .default_value = f32_array_create_x(0.0)}));
	return n;
}

static ui_node_socket_t *mesh_group_socket(ui_node_t *node, char *name, char *type, ui_node_canvas_t *canvas) {
	ui_node_socket_t *s = nodes_material_create_socket(NULL, node, name, type, canvas, 0.0, 1.0, NULL);
	if (string_equals(type, "MESH")) {
		s->color = 0xff00d6a3;
	}
	s->display = string_equals(type, "VECTOR") ? 1 : 0;
	return s;
}

static ui_node_canvas_t *mesh_group_canvas(char *name) {
	ui_node_canvas_t *c   = ALLOC_INIT(ui_node_canvas_t, {.name = name, .nodes = any_array_create(0), .links = any_array_create(0)});
	ui_node_t        *in  = mesh_group_io_node(0, _tr("Group Input"), "GROUP_INPUT", "nodes_mesh_group_input_button", 50);
	ui_node_t        *out = mesh_group_io_node(1, _tr("Group Output"), "GROUP_OUTPUT", "nodes_mesh_group_output_button", 450);
	any_array_push(c->nodes, in);
	any_array_push(c->nodes, out);
	any_array_push(in->outputs, mesh_group_socket(in, _tr("Mesh"), "MESH", c));
	any_array_push(out->inputs, mesh_group_socket(out, _tr("Mesh"), "MESH", c));
	any_array_push(c->links, ALLOC_INIT(ui_node_link_t, {.id = 0, .from_id = in->id, .from_socket = 0, .to_id = out->id, .to_socket = 0}));
	return c;
}

static void mesh_group_node_button(i32 node_id) {
	ui_node_t *node = ui_get_node(ui_nodes_get_canvas(true)->nodes, node_id);
	if (string_equals(node->name, "New Group")) {
		for (i32 i = 1; i < 999; ++i) {
			node->name = string("%s %s", tr("Group"), i32_to_string(i));
			if (nodes_mesh_get_group(node->name) == NULL) {
				break;
			}
		}
		ui_node_canvas_t *canvas = mesh_group_canvas(node->name);
		any_array_push(g_project->_->mesh_groups, ALLOC_INIT(node_group_t, {.canvas = canvas, .nodes = ui_nodes_create()}));
		ui_node_t *in  = canvas->nodes->buffer[0];
		ui_node_t *out = canvas->nodes->buffer[1];
		nodes_material_sync_group_sockets(ui_nodes_get_canvas(true), node->name, in);
		nodes_material_sync_group_sockets(ui_nodes_get_canvas(true), node->name, out);
	}
	node_group_t *group = nodes_mesh_get_group(node->name);
	if (group != NULL && ui_icon_button(tr("Nodes"), ICON_NODES, UI_ALIGN_CENTER)) {
		any_array_push(ui_nodes_group_stack, group);
	}
}

static ui_node_t                *_mesh_group_node;
static ui_node_socket_t_array_t *_mesh_group_sockets;

static void mesh_group_add_socket(char *type, char *name) {
	ui_node_canvas_t *c = ui_nodes_get_canvas(true);
	any_array_push(_mesh_group_sockets, mesh_group_socket(_mesh_group_node, name, type, c));
	nodes_material_sync_sockets(_mesh_group_node);
}

static void mesh_group_add_socket_menu_draw() {
	if (ui_menu_button(tr("Mesh"), "", ICON_NONE)) {
		mesh_group_add_socket("MESH", _tr("Mesh"));
	}
	if (ui_menu_button(tr("Vector"), "", ICON_NONE)) {
		mesh_group_add_socket("VECTOR", _tr("Vector"));
	}
	if (ui_menu_button(tr("Value"), "", ICON_NONE)) {
		mesh_group_add_socket("VALUE", _tr("Value"));
	}
}

static void mesh_group_add_socket_button(ui_node_t *node, ui_node_socket_t_array_t *sockets) {
	if (ui_icon_button(tr("Add"), ICON_PLUS, UI_ALIGN_CENTER)) {
		_mesh_group_node    = node;
		_mesh_group_sockets = sockets;
		ui_menu_draw(&mesh_group_add_socket_menu_draw, -1, -1);
	}
}

static void mesh_group_input_button(i32 node_id) {
	ui_node_t *node = ui_get_node(ui_nodes_get_canvas(true)->nodes, node_id);
	mesh_group_add_socket_button(node, node->outputs);
}

static void mesh_group_output_button(i32 node_id) {
	ui_node_t *node = ui_get_node(ui_nodes_get_canvas(true)->nodes, node_id);
	mesh_group_add_socket_button(node, node->inputs);
}

void mesh_group_node_init() {
	nodes_mesh_group_def = nodes_mesh_def(nodes_mesh_group, _tr("New Group"), "GROUP", 0xffb34f5a, NULL);
	nodes_mesh_def_button(nodes_mesh_group_def, "nodes_mesh_new_group_button", "CUSTOM", 0.0, NULL);
	nodes_mesh_group_def->buttons->buffer[0]->output = -1;
	nodes_mesh_group_def->buttons->buffer[0]->height = 1;
	any_map_set(nodes_mesh_node_evals, "GROUP", mesh_group_node_eval);
	any_map_set(nodes_mesh_node_vectors, "GROUP", mesh_group_node_vector);
	any_map_set(nodes_mesh_node_evals, "GROUP_INPUT", mesh_group_input_node_eval);
	any_map_set(nodes_mesh_node_vectors, "GROUP_INPUT", mesh_group_input_node_vector);
	any_map_set(ui_nodes_custom_buttons, "nodes_mesh_new_group_button", mesh_group_node_button);
	any_map_set(ui_nodes_custom_buttons, "nodes_mesh_group_input_button", mesh_group_input_button);
	any_map_set(ui_nodes_custom_buttons, "nodes_mesh_group_output_button", mesh_group_output_button);
}
