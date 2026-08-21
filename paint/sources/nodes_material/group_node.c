
#include "../global.h"

ui_nodes_t               *_nodes_material_nodes;
ui_node_t                *_nodes_material_node;
ui_node_socket_t_array_t *_nodes_material_sockets;

char *group_node_vector(ui_node_t *node, ui_node_socket_t *socket) {
	return parser_material_parse_group(node, socket);
}

char *group_node_value(ui_node_t *node, ui_node_socket_t *socket) {
	return parser_material_parse_group(node, socket);
}

void nodes_material_new_group_button(i32 node_id) {
	ui_node_t *node = ui_get_node(ui_nodes_get_canvas(true)->nodes, node_id);
	if (string_equals(node->name, "New Group")) {
		for (i32 i = 1; i < 999; ++i) {
			node->name = string("%s %s", tr("Group"), i32_to_string(i));
			bool found = false;
			for (i32 i = 0; i < g_project->_->material_groups->length; ++i) {
				node_group_t *g     = g_project->_->material_groups->buffer[i];
				char         *cname = g->canvas->name;
				if (string_equals(cname, node->name)) {
					found = true;
					break;
				}
			}
			if (!found) {
				break;
			}
		}
		ui_node_canvas_t *canvas =
		    ALLOC_INIT(ui_node_canvas_t,
		                  {.name  = node->name,
		                   .nodes = any_array_create_from_raw(
		                       (void *[]){
		                           ALLOC_INIT(ui_node_t, {.id      = 0,
		                                                     .name    = _tr("Group Input"),
		                                                     .type    = "GROUP_INPUT",
		                                                     .x       = 50,
		                                                     .y       = 200,
		                                                     .color   = 0xff448c6d,
		                                                     .inputs  = any_array_create_from_raw((void *[]){}, 0),
		                                                     .outputs = any_array_create_from_raw((void *[]){}, 0),
		                                                     .buttons = any_array_create_from_raw(
		                                                         (void *[]){
		                                                             ALLOC_INIT(ui_node_button_t, {.name          = "nodes_material_group_input_button",
		                                                                                              .type          = "CUSTOM",
		                                                                                              .height        = 1,
		                                                                                              .output        = -1,
		                                                                                              .default_value = f32_array_create_x(0.0)}),
		                                                         },
		                                                         1),
		                                                     .width = 0,
		                                                     .flags = 0}),
		                           ALLOC_INIT(ui_node_t, {.id      = 1,
		                                                     .name    = _tr("Group Output"),
		                                                     .type    = "GROUP_OUTPUT",
		                                                     .x       = 450,
		                                                     .y       = 200,
		                                                     .color   = 0xff448c6d,
		                                                     .inputs  = any_array_create_from_raw((void *[]){}, 0),
		                                                     .outputs = any_array_create_from_raw((void *[]){}, 0),
		                                                     .buttons = any_array_create_from_raw(
		                                                         (void *[]){
		                                                             ALLOC_INIT(ui_node_button_t, {.name          = "nodes_material_group_output_button",
		                                                                                              .type          = "CUSTOM",
		                                                                                              .height        = 1,
		                                                                                              .output        = -1,
		                                                                                              .default_value = f32_array_create_x(0.0)}),
		                                                         },
		                                                         1),
		                                                     .width = 0,
		                                                     .flags = 0}),
		                       },
		                       2),
		                   .links = any_array_create_from_raw((void *[]){}, 0)});
		node_group_t *ng = ALLOC_INIT(node_group_t, {.canvas = canvas, .nodes = ui_nodes_create()});
		any_array_push(g_project->_->material_groups, ng);
	}
	node_group_t *group = NULL;
	for (i32 i = 0; i < g_project->_->material_groups->length; ++i) {
		node_group_t *g     = g_project->_->material_groups->buffer[i];
		char         *cname = g->canvas->name;
		if (string_equals(cname, node->name)) {
			group = g;
			break;
		}
	}
	if (ui_icon_button(tr("Nodes"), ICON_NODES, UI_ALIGN_CENTER)) {
		any_array_push(ui_nodes_group_stack, group);
	}
}

void nodes_material_add_socket_menu_draw() {
	ui_nodes_t               *nodes       = _nodes_material_nodes;
	ui_node_t                *node        = _nodes_material_node;
	ui_node_socket_t_array_t *sockets     = _nodes_material_sockets;
	node_group_t_array_t     *group_stack = ui_nodes_group_stack;
	ui_node_canvas_t         *c           = group_stack->buffer[group_stack->length - 1]->canvas;
	if (ui_menu_button(tr("RGBA"), "", ICON_NONE)) {
		any_array_push(sockets, nodes_material_create_socket(nodes, node, NULL, "RGBA", c, 0.0, 1.0, NULL));
		nodes_material_sync_sockets(node);
	}
	if (ui_menu_button(tr("Vector"), "", ICON_NONE)) {
		any_array_push(sockets, nodes_material_create_socket(nodes, node, NULL, "VECTOR", c, 0.0, 1.0, NULL));
		nodes_material_sync_sockets(node);
	}
	if (ui_menu_button(tr("Value"), "", ICON_NONE)) {
		any_array_push(sockets, nodes_material_create_socket(nodes, node, NULL, "VALUE", c, 0.0, 1.0, NULL));
		nodes_material_sync_sockets(node);
	}
}

void nodes_material_add_socket_button(ui_nodes_t *nodes, ui_node_t *node, ui_node_socket_t_array_t *sockets) {
	if (ui_icon_button(tr("Add"), ICON_PLUS, UI_ALIGN_CENTER)) {
		_nodes_material_nodes = nodes;
		_nodes_material_node = node;
		_nodes_material_sockets = sockets;
		ui_menu_draw(&nodes_material_add_socket_menu_draw, -1, -1);
	}
}

void nodes_material_group_input_button(i32 node_id) {
	ui_nodes_t *nodes = ui_nodes_get_nodes();
	ui_node_t  *node  = ui_get_node(ui_nodes_get_canvas(true)->nodes, node_id);
	nodes_material_add_socket_button(nodes, node, node->outputs);
}

void nodes_material_group_output_button(i32 node_id) {
	ui_nodes_t *nodes = ui_nodes_get_nodes();
	ui_node_t  *node  = ui_get_node(ui_nodes_get_canvas(true)->nodes, node_id);
	nodes_material_add_socket_button(nodes, node, node->inputs);
}

void nodes_material_sync_group_sockets(ui_node_canvas_t *canvas, char *group_name, ui_node_t *node) {
	for (i32 i = 0; i < canvas->nodes->length; ++i) {
		ui_node_t *n = canvas->nodes->buffer[i];
		if (string_equals(n->type, "GROUP") && string_equals(n->name, group_name)) {
			bool                      is_inputs   = string_equals(node->name, "Group Input");
			ui_node_socket_t_array_t *old_sockets = is_inputs ? n->inputs : n->outputs;
			ui_node_socket_t_array_t *sockets     = util_clone_canvas_sockets(is_inputs ? node->outputs : node->inputs);
			if (is_inputs) {
				n->inputs = sockets;
			}
			else {
				n->outputs = sockets;
			}
			for (i32 i = 0; i < sockets->length; ++i) {
				ui_node_socket_t *s = sockets->buffer[i];
				s->node_id          = n->id;
			}
			i32 num_sockets = sockets->length < old_sockets->length ? sockets->length : old_sockets->length;
			for (i32 i = 0; i < num_sockets; ++i) {
				if (sockets->buffer[i]->type == old_sockets->buffer[i]->type) {
					sockets->buffer[i]->default_value = old_sockets->buffer[i]->default_value;
				}
			}
		}
	}
}

void nodes_material_sync_sockets(ui_node_t *node) {
	node_group_t_array_t *group_stack = ui_nodes_group_stack;
	ui_node_canvas_t     *c           = group_stack->buffer[group_stack->length - 1]->canvas;
	for (i32 i = 0; i < g_project->_->materials->length; ++i) {
		slot_material_t *m = g_project->_->materials->buffer[i];
		nodes_material_sync_group_sockets(m->canvas, c->name, node);
	}
	for (i32 i = 0; i < g_project->_->material_groups->length; ++i) {
		node_group_t *g = g_project->_->material_groups->buffer[i];
		nodes_material_sync_group_sockets(g->canvas, c->name, node);
	}
}

i32 nodes_material_get_socket_color(char *type) {
	return string_equals(type, "RGBA") ? 0xffc7c729 : string_equals(type, "VECTOR") ? 0xff6363c7 : 0xffa1a1a1;
}

f32_array_t *nodes_material_get_socket_default_value(char *type) {
	return string_equals(type, "RGBA")     ? f32_array_create_xyzw(0.8, 0.8, 0.8, 1.0)
	       : string_equals(type, "VECTOR") ? f32_array_create_xyz(0.0, 0.0, 0.0)
	                                       : f32_array_create_x(0.0);
}

char *nodes_material_get_socket_name(char *type) {
	return string_equals(type, "RGBA") ? _tr("Color") : string_equals(type, "VECTOR") ? _tr("Vector") : _tr("Value");
}

ui_node_socket_t *nodes_material_create_socket(ui_nodes_t *nodes, ui_node_t *node, char *name, char *type, ui_node_canvas_t *canvas, f32 min, f32 max,
                                               void *default_value) {
	ui_node_socket_t *soc =
	    ALLOC_INIT(ui_node_socket_t, {.id            = ui_get_socket_id(canvas->nodes),
	                                     .node_id       = node->id,
	                                     .name          = name == NULL ? nodes_material_get_socket_name(type) : name,
	                                     .type          = type,
	                                     .color         = nodes_material_get_socket_color(type),
	                                     .default_value = default_value == NULL ? nodes_material_get_socket_default_value(type) : default_value,
	                                     .min           = min,
	                                     .max           = max,
	                                     .precision     = 100});
	return soc;
}

void group_node_init() {

	ui_node_t *group_node_def = ALLOC_INIT(ui_node_t, {.id      = 0,
	                                                      .name    = _tr("New Group"),
	                                                      .type    = "GROUP",
	                                                      .x       = 0,
	                                                      .y       = 0,
	                                                      .color   = 0xffb34f5a,
	                                                      .inputs  = any_array_create_from_raw((void *[]){}, 0),
	                                                      .outputs = any_array_create_from_raw((void *[]){}, 0),
	                                                      .buttons = any_array_create_from_raw(
	                                                          (void *[]){
	                                                              ALLOC_INIT(ui_node_button_t, {.name          = "nodes_material_new_group_button",
	                                                                                               .type          = "CUSTOM",
	                                                                                               .output        = -1,
	                                                                                               .default_value = f32_array_create_x(0),
	                                                                                               .data          = NULL,
	                                                                                               .min           = 0.0,
	                                                                                               .max           = 1.0,
	                                                                                               .precision     = 100,
	                                                                                               .height        = 1}),
	                                                          },
	                                                          1),
	                                                      .width = 0,
	                                                      .flags = 0});

	any_array_push(nodes_material_group, group_node_def);
	any_map_set(parser_material_node_vectors, "GROUP", group_node_vector);
	any_map_set(parser_material_node_values, "GROUP", group_node_value);
	any_map_set(ui_nodes_custom_buttons, "nodes_material_new_group_button", nodes_material_new_group_button);
	any_map_set(ui_nodes_custom_buttons, "nodes_material_group_input_button", nodes_material_group_input_button);
	any_map_set(ui_nodes_custom_buttons, "nodes_material_group_output_button", nodes_material_group_output_button);
}
