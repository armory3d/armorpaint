#include "../global.h"

mesh_geo_t *mesh_cloth_node_eval(ui_node_t *node) {
	mesh_geo_t    *g = nodes_mesh_eval_input(node, 0);
	mesh_object_t *o = nodes_mesh_eval_object;
	if (g->index_count == 0 || o == NULL) {
		return g;
	}
	nodes_mesh_eval_reads_frame = true;

	i32 *particle;
	i32  vertex_count;
	f32 *pos = sim_world_cloth(o->base->uid, node->id, &particle, &vertex_count);
	if (pos == NULL || vertex_count != g->vertex_count) {
		return g; // Not simulated yet
	}

	// Particles back into the vertices
	mat4_t inv = mat4_inv(o->base->transform->world);
	for (i32 i = 0; i < g->vertex_count; ++i) {
		f32   *p          = &pos[particle[i] * 3];
		vec4_t l          = vec4_apply_mat4((vec4_t){p[0], p[1], p[2], 1.0}, inv);
		g->pos[i * 3]     = l.x;
		g->pos[i * 3 + 1] = l.y;
		g->pos[i * 3 + 2] = l.z;
	}
	return nodes_mesh_geo_modify(g, node->type, 1.0);
}

void mesh_cloth_node_init() {
	ui_node_t *n = nodes_mesh_def(nodes_mesh_modifier, _tr("Cloth"), "MESH_CLOTH", 0xff4982a0, "MESH");
	nodes_mesh_def_mesh(n);
	nodes_mesh_def_value(n, _tr("Bending"), 0.5, 0.0, 1.0, 100);
	any_map_set(nodes_mesh_node_evals, "MESH_CLOTH", mesh_cloth_node_eval);
}
