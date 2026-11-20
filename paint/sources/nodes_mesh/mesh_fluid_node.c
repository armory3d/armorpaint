#include "../global.h"

mesh_geo_t *mesh_fluid_node_eval(ui_node_t *node) {
	mesh_geo_t    *g = nodes_mesh_eval_input(node, 0);
	mesh_object_t *o = nodes_mesh_eval_object;
	if (g->index_count == 0 || o == NULL) {
		return g;
	}
	nodes_mesh_eval_reads_frame = true;

	f32           *pos;
	physics_sim_t *grid;
	i32            count = sim_world_particles(o->base->uid, node->id, &pos, &grid);
	if (count < 0) {
		return g; // Not simulated yet
	}
	util_geo_free(g);

	// Surface of the particles
	i32  smooth  = (i32)math_max(0, math_min(8, math_floor(nodes_mesh_value(node, 3))));
	f32  spacing = grid->cell * 0.5f;
	i32  nx, ny, nz;
	f32  mn[3], voxel;
	f32 *field = physics_sim_particle_field(pos, count, spacing, spacing * 0.5f, 128, &nx, &ny, &nz, mn, &voxel);
	free(pos);
	if (field == NULL) {
		return util_geo_create(0, 0);
	}
	g = geom_polygonize_geo(field, nx, ny, nz, mn, voxel, smooth);
	free(field);
	if (g->index_count == 0) {
		return g;
	}

	// Box projected uvs
	f32 dmn[3] = {grid->min[0], grid->min[1], grid->min[2]};
	f32 dmx[3] = {grid->min[0] + grid->nx * grid->cell, grid->min[1] + grid->ny * grid->cell, grid->min[2] + grid->nz * grid->cell};
	util_geo_uv_box(g, dmn, dmx);
	nodes_mesh_geo_transform(g, mat4_inv(o->base->transform->world));
	return nodes_mesh_geo_modify(g, node->type, 1.0);
}

void mesh_fluid_node_init() {
	ui_node_t *n = nodes_mesh_def(nodes_mesh_modifier, _tr("Fluid"), "MESH_FLUID", 0xff4982a0, "MESH");
	nodes_mesh_def_button(n, _tr("Material"), "ENUM", 0, "Liquid\nJelly\nSand");
	nodes_mesh_def_mesh(n);
	nodes_mesh_def_value(n, _tr("Resolution"), 12, 2, 48, 1);
	nodes_mesh_def_value(n, _tr("Viscosity"), 0.1, 0.0, 1.0, 100);
	nodes_mesh_def_value(n, _tr("Smooth"), 2, 0, 8, 1);
	nodes_mesh_def_value(n, _tr("Stiffness"), 100, 1, 1000, 1);
	any_map_set(nodes_mesh_node_evals, "MESH_FLUID", mesh_fluid_node_eval);
}
