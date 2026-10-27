
#include "../global.h"

mesh_geo_t *mesh_smooth_node_eval(ui_node_t *node) {
	return nodes_mesh_eval_modifier(node);
}

void mesh_smooth_node_init() {
	ui_node_t *n = nodes_mesh_def(nodes_mesh_modifier, _tr("Smooth"), "MESH_SMOOTH", 0xff4982a0, "MESH");
	nodes_mesh_def_mesh(n);
	nodes_mesh_def_value(n, _tr("Iterations"), 1, 1, 8, 1);
	any_map_set(nodes_mesh_node_evals, "MESH_SMOOTH", mesh_smooth_node_eval);
}
