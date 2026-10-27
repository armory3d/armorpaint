
#include "../global.h"

mesh_geo_t *nodes_mesh_geo_transform_euler(mesh_geo_t *g, vec4_t loc, vec4_t rot, vec4_t scale) {
	rot     = vec4_mult(rot, math_pi() / 180.0);
	scale.x = math_abs(scale.x) < 0.0001 ? 0.0001 : scale.x;
	scale.y = math_abs(scale.y) < 0.0001 ? 0.0001 : scale.y;
	scale.z = math_abs(scale.z) < 0.0001 ? 0.0001 : scale.z;
	nodes_mesh_geo_transform(g, mat4_compose(loc, quat_from_euler(rot.x, rot.y, rot.z), scale));
	return g;
}

mesh_geo_t *mesh_transform_node_eval(ui_node_t *node) {
	mesh_geo_t *g = nodes_mesh_eval_input(node, 0);
	return nodes_mesh_geo_transform_euler(g, nodes_mesh_vector(node, 1), nodes_mesh_vector(node, 2), nodes_mesh_vector(node, 3));
}

void mesh_transform_node_init() {
	ui_node_t *n = nodes_mesh_def(nodes_mesh_modifier, _tr("Transform"), "MESH_TRANSFORM", 0xff4982a0, "MESH");
	nodes_mesh_def_mesh(n);
	nodes_mesh_def_vector(n, _tr("Location"), 0.0, 0.0, 0.0, -10.0, 10.0);
	nodes_mesh_def_vector(n, _tr("Rotation"), 0.0, 0.0, 0.0, -180.0, 180.0);
	nodes_mesh_def_vector(n, _tr("Scale"), 1.0, 1.0, 1.0, -10.0, 10.0);
	any_map_set(nodes_mesh_node_evals, "MESH_TRANSFORM", mesh_transform_node_eval);
}
