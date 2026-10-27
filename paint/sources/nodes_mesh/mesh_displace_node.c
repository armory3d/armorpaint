
#include "../global.h"

// Height painted on the picked layer, the alpha of its pack texture, NULL for masks and groups
gpu_texture_t *mesh_displace_node_texture(ui_node_t *node) {
	i32 i = nodes_mesh_button(node, 0);
	return i >= 0 && i < g_project->_->layers->length ? g_project->_->layers->buffer[i]->texpaint_pack : NULL; // 9999 - Layer deleted
}

// Moves vertices inward along their normals, a height of 1 keeps the surface
static void mesh_displace_geo(mesh_geo_t *g, mesh_geo_image_t *height, f32 strength) {
	for (i32 i = 0; i < g->vertex_count; ++i) {
		f32 h = (1.0 - mesh_image_sample_bilinear(height, g->tex[i * 2], g->tex[i * 2 + 1]).w) * strength;
		g->pos[i * 3] -= g->nor[i * 3] * h;
		g->pos[i * 3 + 1] -= g->nor[i * 3 + 1] * h;
		g->pos[i * 3 + 2] -= g->nor[i * 3 + 2] * h;
	}
}

mesh_geo_t *mesh_displace_node_eval(ui_node_t *node) {
	mesh_geo_t    *g        = nodes_mesh_eval_input(node, 0);
	gpu_texture_t *texture  = mesh_displace_node_texture(node);
	f32            strength = nodes_mesh_value(node, 1);
	if (g->index_count == 0 || texture == NULL || texture->width <= 0 || texture->height <= 0 || strength == 0.0) {
		return g;
	}
	mesh_geo_image_t *height = mesh_image_texture_get(texture, true, 3);
	if (height == NULL) {
		return g;
	}
	mesh_displace_geo(g, height, strength);
	return nodes_mesh_geo_modify(g, node->type, 0.0); // Flat normals
}

void mesh_displace_node_init() {
	ui_node_t *n = nodes_mesh_def(nodes_mesh_modifier, _tr("Displace"), "MESH_DISPLACE", 0xff4982a0, "MESH");
	nodes_mesh_def_mesh(n);
	nodes_mesh_def_button(n, _tr("Layer"), "ENUM", 0, "");
	n->buttons->buffer[0]->output = -1;
	nodes_mesh_def_value(n, _tr("Strength"), 0.1, -1.0, 1.0, 100);
	any_map_set(nodes_mesh_node_evals, "MESH_DISPLACE", mesh_displace_node_eval);
}
