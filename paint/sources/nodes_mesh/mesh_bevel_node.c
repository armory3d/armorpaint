
#include "../global.h"

mesh_geo_t *mesh_bevel_node_eval(ui_node_t *node) {
	return nodes_mesh_eval_modifier(node);
}

void mesh_bevel_node_init() {
	ui_node_t *n = nodes_mesh_def(nodes_mesh_modifier, _tr("Bevel"), "MESH_BEVEL", 0xff4982a0, "MESH");
	nodes_mesh_def_mesh(n);
	nodes_mesh_def_value(n, _tr("Amount"), 0.1, 0.0, 1.0, 100);
	any_map_set(nodes_mesh_node_evals, "MESH_BEVEL", mesh_bevel_node_eval);
}
