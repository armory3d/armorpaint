
#include "../global.h"

// Takes ownership of a and b
mesh_geo_t *nodes_mesh_geo_join(mesh_geo_t *a, mesh_geo_t *b) {
	mesh_geo_t *g = util_geo_create(a->vertex_count + b->vertex_count, a->index_count + b->index_count);
	nodes_mesh_geo_copy_into(g, a, 0, 0);
	nodes_mesh_geo_copy_into(g, b, a->vertex_count, a->index_count);
	util_geo_free(a);
	util_geo_free(b);
	return g;
}

mesh_geo_t *mesh_join_node_eval(ui_node_t *node) {
	mesh_geo_t *a = nodes_mesh_eval_input(node, 0);
	return nodes_mesh_geo_join(a, nodes_mesh_eval_input(node, 1));
}

void mesh_join_node_init() {
	ui_node_t *n = nodes_mesh_def(nodes_mesh_combine, _tr("Join"), "MESH_JOIN", 0xff448c6d, "MESH");
	nodes_mesh_def_mesh(n);
	nodes_mesh_def_mesh(n);
	any_map_set(nodes_mesh_node_evals, "MESH_JOIN", mesh_join_node_eval);
}
