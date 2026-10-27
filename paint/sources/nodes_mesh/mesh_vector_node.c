
#include "../global.h"

vec4_t mesh_vector_node_vector(ui_node_t *node) {
	return (vec4_t){nodes_mesh_value(node, 0), nodes_mesh_value(node, 1), nodes_mesh_value(node, 2), 1.0};
}

void mesh_vector_node_init() {
	ui_node_t *n = nodes_mesh_def(nodes_mesh_utilities, _tr("Vector"), "MESH_VECTOR", 0xff62676d, "VECTOR");
	nodes_mesh_def_value(n, "X", 0.0, -10.0, 10.0, 100);
	nodes_mesh_def_value(n, "Y", 0.0, -10.0, 10.0, 100);
	nodes_mesh_def_value(n, "Z", 0.0, -10.0, 10.0, 100);
	any_map_set(nodes_mesh_node_vectors, "MESH_VECTOR", mesh_vector_node_vector);
}
