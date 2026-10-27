
#include "../global.h"

// Takes ownership of g
mesh_geo_t *nodes_mesh_geo_mirror(mesh_geo_t *g, i32 axis) {
	axis          = axis < 0 ? 0 : (axis > 2 ? 2 : axis);
	mesh_geo_t *m = util_geo_create(g->vertex_count, g->index_count);
	nodes_mesh_geo_copy_into(m, g, 0, 0);
	for (i32 i = 0; i < m->vertex_count; ++i) {
		m->pos[i * 3 + axis] = -m->pos[i * 3 + axis];
		m->nor[i * 3 + axis] = -m->nor[i * 3 + axis];
	}
	nodes_mesh_geo_flip(m);
	return nodes_mesh_geo_join(g, m);
}

mesh_geo_t *mesh_mirror_node_eval(ui_node_t *node) {
	return nodes_mesh_geo_mirror(nodes_mesh_eval_input(node, 0), nodes_mesh_button(node, 0));
}

void mesh_mirror_node_init() {
	ui_node_t *n = nodes_mesh_def(nodes_mesh_modifier, _tr("Mirror"), "MESH_MIRROR", 0xff4982a0, "MESH");
	nodes_mesh_def_button(n, _tr("Axis"), "ENUM", 0, "X\nY\nZ");
	nodes_mesh_def_mesh(n);
	any_map_set(nodes_mesh_node_evals, "MESH_MIRROR", mesh_mirror_node_eval);
}
