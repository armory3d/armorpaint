
#include "../global.h"

vec4_t mesh_value_node_vector(ui_node_t *node) {
	return nodes_mesh_vector(node, 0);
}

void mesh_value_node_init() {
	ui_node_t *n = nodes_mesh_def(nodes_mesh_utilities, _tr("Value"), "MESH_VALUE", 0xff62676d, "VALUE");
	nodes_mesh_def_value(n, _tr("Value"), 1.0, -10.0, 10.0, 100);
	any_map_set(nodes_mesh_node_vectors, "MESH_VALUE", mesh_value_node_vector);
}
