
#include "../global.h"

mesh_geo_t *mesh_decimate_node_eval(ui_node_t *node) {
	return nodes_mesh_eval_modifier(node);
}

void mesh_decimate_node_init() {
	ui_node_t *n = nodes_mesh_def(nodes_mesh_modifier, _tr("Decimate"), "MESH_DECIMATE", 0xff4982a0, "MESH");
	nodes_mesh_def_mesh(n);
	nodes_mesh_def_value(n, _tr("Ratio"), 0.5, 0.01, 1.0, 100);
	any_map_set(nodes_mesh_node_evals, "MESH_DECIMATE", mesh_decimate_node_eval);
}
