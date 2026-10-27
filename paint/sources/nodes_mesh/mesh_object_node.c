
#include "../global.h"

mesh_geo_t *mesh_object_node_eval(ui_node_t *node) {
	mesh_object_t *self = nodes_mesh_eval_object;
	mesh_object_t *ref  = nodes_mesh_node_object(node);
	if (ref == NULL) {
		return util_geo_create(0, 0);
	}
	mesh_geo_t *g = nodes_mesh_geo_from_data(nodes_mesh_object_data(self, ref));
	if (ref != self) {
		nodes_mesh_geo_transform(g, nodes_mesh_object_matrix(self, ref));
	}
	return g;
}

void mesh_object_node_init() {
	// Another mesh of the scene, default value holds the object index, uid and the last synced index
	ui_node_t *n = nodes_mesh_def(nodes_mesh_input, _tr("Mesh"), "MESH_OBJECT", 0xffb34f5a, "MESH");
	nodes_mesh_def_button(n, _tr("Mesh"), "ENUM", 0, NULL);
	n->buttons->buffer[0]->default_value = f32_array_create_xyz(0, -1, 0);
	any_map_set(nodes_mesh_node_evals, "MESH_OBJECT", mesh_object_node_eval);
}
