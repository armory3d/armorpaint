
#include "../global.h"

mesh_geo_t *mesh_input_node_eval(ui_node_t *node) {
	return nodes_mesh_geo_from_data(nodes_mesh_eval_slot->input);
}

void mesh_input_node_init() {
	nodes_mesh_input_def = nodes_mesh_def(nodes_mesh_input, _tr("Mesh Input"), "MESH_INPUT", 0xffb34f5a, "MESH");
	any_map_set(nodes_mesh_node_evals, "MESH_INPUT", mesh_input_node_eval);
}
