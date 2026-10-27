
#include "../global.h"

// Takes ownership of a and b
mesh_geo_t *nodes_mesh_geo_boolean(mesh_geo_t *a, mesh_geo_t *b, i32 op) {
	if (a->index_count == 0 || b->index_count == 0) {
		// Nothing to cut
		bool keep_a = op == MESH_BOOLEAN_DIFFERENCE || (op == MESH_BOOLEAN_UNION && b->index_count == 0);
		bool keep_b = op == MESH_BOOLEAN_UNION && !keep_a;
		if (!keep_a && !keep_b) {
			util_geo_free(b);
			b = util_geo_create(0, 0);
		}
		util_geo_free(keep_a ? b : a);
		return keep_a ? a : b;
	}
	mesh_geo_t *g = util_csg_boolean(a, b, op);
	util_geo_free(a);
	util_geo_free(b);
	nodes_mesh_eval_unwrap = true;
	return g;
}

mesh_geo_t *mesh_boolean_node_eval(ui_node_t *node) {
	mesh_geo_t *a = nodes_mesh_eval_input(node, 0);
	return nodes_mesh_geo_boolean(a, nodes_mesh_eval_input(node, 1), nodes_mesh_button(node, 0));
}

void mesh_boolean_node_init() {
	ui_node_t *n = nodes_mesh_def(nodes_mesh_combine, _tr("Boolean"), "MESH_BOOLEAN", 0xff448c6d, "MESH");
	nodes_mesh_def_button(n, _tr("Operation"), "ENUM", MESH_BOOLEAN_DIFFERENCE, "Union\nDifference\nIntersect");
	nodes_mesh_def_mesh(n);
	nodes_mesh_def_mesh(n);
	any_map_set(nodes_mesh_node_evals, "MESH_BOOLEAN", mesh_boolean_node_eval);
}
