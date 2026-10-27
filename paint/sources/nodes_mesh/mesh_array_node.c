
#include "../global.h"
#include <float.h>

// Takes ownership of g
mesh_geo_t *nodes_mesh_geo_array(mesh_geo_t *g, i32 count, vec4_t offset, bool relative) {
	count = count < 1 ? 1 : (count > 256 ? 256 : count);
	if (count == 1 || g->vertex_count == 0) {
		return g;
	}

	if (relative) {
		f32 mn[3] = {FLT_MAX, FLT_MAX, FLT_MAX};
		f32 mx[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
		for (i32 i = 0; i < g->vertex_count * 3; ++i) {
			mn[i % 3] = math_min(mn[i % 3], g->pos[i]);
			mx[i % 3] = math_max(mx[i % 3], g->pos[i]);
		}
		offset.x *= mx[0] - mn[0];
		offset.y *= mx[1] - mn[1];
		offset.z *= mx[2] - mn[2];
	}

	mesh_geo_t *out = util_geo_create(g->vertex_count * count, g->index_count * count);
	for (i32 c = 0; c < count; ++c) {
		i32 vo = g->vertex_count * c;
		nodes_mesh_geo_copy_into(out, g, vo, g->index_count * c);
		for (i32 i = 0; i < g->vertex_count; ++i) {
			out->pos[(vo + i) * 3] += offset.x * c;
			out->pos[(vo + i) * 3 + 1] += offset.y * c;
			out->pos[(vo + i) * 3 + 2] += offset.z * c;
		}
	}
	util_geo_free(g);
	return out;
}

mesh_geo_t *mesh_array_node_eval(ui_node_t *node) {
	mesh_geo_t *g = nodes_mesh_eval_input(node, 0);
	return nodes_mesh_geo_array(g, math_floor(nodes_mesh_value(node, 1)), nodes_mesh_vector(node, 2), nodes_mesh_button(node, 0) > 0);
}

void mesh_array_node_init() {
	ui_node_t *n = nodes_mesh_def(nodes_mesh_modifier, _tr("Array"), "MESH_ARRAY", 0xff4982a0, "MESH");
	nodes_mesh_def_button(n, _tr("Relative Offset"), "BOOL", 1, NULL);
	nodes_mesh_def_mesh(n);
	nodes_mesh_def_value(n, _tr("Count"), 2, 1, 32, 1);
	nodes_mesh_def_vector(n, _tr("Offset"), 1.0, 0.0, 0.0, -10.0, 10.0);
	any_map_set(nodes_mesh_node_evals, "MESH_ARRAY", mesh_array_node_eval);
}
