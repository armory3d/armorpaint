#include "global.h"

void *plugin;
char *category_name = "My Nodes";
char *node_name = "Hello World";
char *node_type = "HELLO_WORLD";

float custom_node(logic_node_t *node, int from) {
	// float f = node->inputs->buffer[0]->get(0);
	float f = 1.0;
	return sinf(sys_time() * f);
}

void on_delete() {
	plugin_brush_custom_nodes_remove(node_type);
	plugin_brush_category_remove(category_name);
}

void main() {
	plugin = plugin_create();

	// Create new node category
	any_array_t *node_list = any_array_create(0);
	ui_node_t *n = calloc(1, sizeof(ui_node_t));
	any_array_push(node_list, n);

	n->id = 0;
	n->name = node_name;
	n->type = node_type;
	n->x = 0;
	n->y = 0;
	n->color = 0xffb34f5a;

	n->inputs = any_array_create(0);
	ui_node_socket_t *s = calloc(1, sizeof(ui_node_socket_t));
	any_array_push(n->inputs, s);
	s->id = 0;
	s->node_id = 0;
	s->name = "Scale";
	s->type = "VALUE";
	s->color = 0xffa1a1a1;
	s->default_value = f32_array_create_x(1.0);
	s->min = 0.0;
	s->max = 5.0;
	s->precision = 100.0;
	s->display = 0;

	n->outputs = any_array_create(0);
	s = calloc(1, sizeof(ui_node_socket_t));
	any_array_push(n->outputs, s);
	s->id = 0;
	s->node_id = 0;
	s->name = "Value";
	s->type = "VALUE";
	s->color = 0xffc7c729;
	s->default_value = f32_array_create_x(1.0);
	s->min = 0.0;
	s->max = 5.0;
	s->precision = 100.0;
	s->display = 0;

	n->buttons = any_array_create(0);
	n->width = 0;
	n->flags = 0;

	plugin_brush_category_add(category_name, node_list);

	// Brush node
	plugin_brush_custom_nodes_set(node_type, custom_node);

	// Cleanup
	plugin_notify_on_delete(plugin, on_delete);
}
