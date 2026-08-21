#include "global.h"

void *plugin;
char *category_name = "My Nodes";
char *node_name = "Hello World";
char *node_type = "HELLO_WORLD";

char *custom_node(ui_node_t *node, char *socket_name) {
	void *kong = plugin_material_kong_get();
	char *scale = parser_material_parse_value_input(node->inputs->buffer[0], false);
	char *my_out = "my_out";

	node_shader_write_frag(kong,
		string("var %s: float = cos(sin(tex_coord.x * 200.0 * %s) + cos(tex_coord.y * 200.0 * %s));", my_out, scale, scale)
	);

	if (string_equals(socket_name, "Color")) {
		return string("float3(%s, %s, %s)", my_out, my_out, my_out);
	}
	else if (string_equals(socket_name, "Factor")) {
		return my_out;
	}
}

void on_delete() {
	plugin_material_custom_nodes_remove(node_type);
	plugin_material_category_remove(category_name);
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
	s->name = "Color";
	s->type = "RGBA";
	s->color = 0xffc7c729;
	s->default_value = f32_array_create_xyzw(0.8, 0.8, 0.8, 1.0);
	s->min = 0.0;
	s->max = 1.0;
	s->precision = 100.0;
	s->display = 0;

	s = calloc(1, sizeof(ui_node_socket_t));
	any_array_push(n->outputs, s);
	s->id = 1;
	s->node_id = 0;
	s->name = "Factor";
	s->type = "VALUE";
	s->color = 0xffa1a1a1;
	s->default_value = f32_array_create_x(1.0);
	s->min = 0.0;
	s->max = 1.0;
	s->precision = 100.0;
	s->display = 0;

	n->buttons = any_array_create(0);
	n->width = 0;
	n->flags = 0;

	plugin_material_category_add(category_name, node_list);

	// Node shader
	plugin_material_custom_nodes_set(node_type, custom_node);

	// Cleanup
	plugin_notify_on_delete(plugin, on_delete);
}
