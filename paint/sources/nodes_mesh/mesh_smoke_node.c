#include "../global.h"

static mesh_geo_t *mesh_smoke_shell(physics_sim_t *sm, u8 *density, f32 level, i32 smooth) {
	i32  nx = sm->nx + 2, ny = sm->ny + 2, nz = sm->nz + 2; // Padded to close the surface at the walls
	f32 *f = malloc(sizeof(f32) * nx * ny * nz);
	for (i32 k = 0; k < nz; ++k) {
		for (i32 j = 0; j < ny; ++j) {
			for (i32 i = 0; i < nx; ++i) {
				f32 d = 0.0f;
				if (i > 0 && j > 0 && k > 0 && i < nx - 1 && j < ny - 1 && k < nz - 1) {
					d = density[(i - 1) + sm->nx * ((j - 1) + sm->ny * (k - 1))] / 255.0f;
				}
				f[i + nx * (j + ny * k)] = level - d;
			}
		}
	}
	f32         origin[3] = {sm->min[0] - sm->cell * 0.5f, sm->min[1] - sm->cell * 0.5f, sm->min[2] - sm->cell * 0.5f};
	mesh_geo_t *g         = geom_polygonize_geo(f, nx, ny, nz, origin, sm->cell, smooth);
	free(f);
	return g;
}

mesh_geo_t *mesh_smoke_node_eval(ui_node_t *node) {
	mesh_geo_t    *g = nodes_mesh_eval_input(node, 0);
	mesh_object_t *o = nodes_mesh_eval_object;
	if (g->index_count == 0 || o == NULL) {
		return g;
	}
	nodes_mesh_eval_reads_frame = true;

	i32            layers = (i32)math_max(1, math_min(8, math_floor(nodes_mesh_value(node, 6))));
	physics_sim_t *grid;
	u8            *density = sim_world_gas(o->base->uid, node->id, &grid);
	if (density == NULL) {
		return g; // Not simulated yet
	}
	util_geo_free(g);

	// Nested shells
	mesh_geo_t **shells = malloc(sizeof(mesh_geo_t *) * layers);
	i32          vc = 0, ic = 0;
	for (i32 l = 0; l < layers; ++l) {
		f32 level = (l + 0.5f) / layers * 0.6f;
		shells[l] = mesh_smoke_shell(grid, density, level, 2);
		vc += shells[l]->vertex_count;
		ic += shells[l]->index_count;
	}
	g  = util_geo_create(vc, ic);
	vc = ic = 0;
	for (i32 l = 0; l < layers; ++l) {
		nodes_mesh_geo_copy_into(g, shells[l], vc, ic);
		vc += shells[l]->vertex_count;
		ic += shells[l]->index_count;
		util_geo_free(shells[l]);
	}
	free(shells);
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

void mesh_smoke_node_init() {
	ui_node_t *n = nodes_mesh_def(nodes_mesh_modifier, _tr("Smoke"), "MESH_SMOKE", 0xff4982a0, "MESH");
	nodes_mesh_def_mesh(n);
	nodes_mesh_def_value(n, _tr("Resolution"), 32, 8, 128, 1);
	nodes_mesh_def_value(n, _tr("Buoyancy"), 4.0, 0.0, 20.0, 100);
	nodes_mesh_def_value(n, _tr("Vorticity"), 1.0, 0.0, 4.0, 100);
	nodes_mesh_def_value(n, _tr("Dissipation"), 0.2, 0.0, 2.0, 100);
	nodes_mesh_def_value(n, _tr("Emit Frames"), 60, 0, 250, 1);
	nodes_mesh_def_value(n, _tr("Layers"), 3, 1, 8, 1);
	nodes_mesh_def_value(n, _tr("Seed"), 1, 0, 100, 1);
	any_map_set(nodes_mesh_node_evals, "MESH_SMOKE", mesh_smoke_node_eval);
}
