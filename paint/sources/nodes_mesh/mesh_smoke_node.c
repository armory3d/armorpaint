#include "../global.h"

#define MESH_VOLUME_BLOCK 4 // Cells per side of the blocks rays skip when empty, VOLUME_BLOCK in raytrace_brute

static mesh_volume_t **mesh_volumes      = NULL;
static i32             mesh_volume_count = 0;

mesh_volume_t *mesh_volume_get(i32 uid) {
	for (i32 i = 0; i < mesh_volume_count; ++i) {
		if (mesh_volumes[i]->uid == uid) {
			return mesh_volumes[i];
		}
	}
	return NULL;
}

// Slices side by side, about as wide as tall
void mesh_volume_set(i32 uid, physics_sim_t *grid, u8 *density, u8 *flame, f32 emission) {
	mesh_volume_t *v = mesh_volume_get(uid);
	if (v == NULL) {
		mesh_volumes                      = realloc(mesh_volumes, sizeof(mesh_volume_t *) * (mesh_volume_count + 1));
		v                                 = calloc(1, sizeof(mesh_volume_t));
		v->uid                            = uid;
		mesh_volumes[mesh_volume_count++] = v;
	}
	i32 nx = grid->nx, ny = grid->ny, nz = grid->nz;
	i32 tiles = (i32)ceilf(sqrtf((f32)nz * ny / nx));
	i32 rows  = (nz + tiles - 1) / tiles;
	i32 w = nx * tiles, h = ny * rows;
	if (v->atlas == NULL || v->width != w || v->height != h) {
		free(v->atlas);
		v->atlas = malloc(w * h * 4);
	}
	memset(v->atlas, 0, w * h * 4);
	// Blue marks the blocks of MESH_VOLUME_BLOCK cells without gas, also a cell around them, rays skip those
	i32 n[3] = {nx, ny, nz}, lo[3] = {nx, ny, nz}, hi[3] = {-1, -1, -1};
	i32 bx = (nx + MESH_VOLUME_BLOCK - 1) / MESH_VOLUME_BLOCK, by = (ny + MESH_VOLUME_BLOCK - 1) / MESH_VOLUME_BLOCK;
	i32 bz   = (nz + MESH_VOLUME_BLOCK - 1) / MESH_VOLUME_BLOCK;
	u8 *used = calloc(bx * by * bz, 1);
	for (i32 k = 0; k < nz; ++k) {
		i32 ox = (k % tiles) * nx, oy = (k / tiles) * ny;
		for (i32 j = 0; j < ny; ++j) {
			u8 *row = &v->atlas[((oy + j) * w + ox) * 4];
			for (i32 i = 0; i < nx; ++i) {
				i32 c          = i + nx * (j + ny * k);
				u8  f          = flame != NULL ? flame[c] : 0;
				u8  d          = density != NULL ? density[c] : 0;
				row[i * 4]     = f;
				row[i * 4 + 1] = d;
				if (f == 0 && d == 0) {
					continue;
				}
				// Blocks this cell or its neighbors fall in
				i32 p[3] = {i, j, k}, b0[3], b1[3];
				for (i32 a = 0; a < 3; ++a) {
					lo[a] = p[a] < lo[a] ? p[a] : lo[a];
					hi[a] = p[a] > hi[a] ? p[a] : hi[a];
					b0[a] = (p[a] > 0 ? p[a] - 1 : 0) / MESH_VOLUME_BLOCK;
					b1[a] = (p[a] + 1 < n[a] ? p[a] + 1 : p[a]) / MESH_VOLUME_BLOCK;
				}
				for (i32 bk = b0[2]; bk <= b1[2]; ++bk) {
					for (i32 bj = b0[1]; bj <= b1[1]; ++bj) {
						for (i32 bi = b0[0]; bi <= b1[0]; ++bi) {
							used[bi + bx * (bj + by * bk)] = 1;
						}
					}
				}
			}
		}
	}
	for (i32 k = 0; k < nz; ++k) {
		i32 ox = (k % tiles) * nx, oy = (k / tiles) * ny;
		for (i32 j = 0; j < ny; ++j) {
			for (i32 i = 0; i < nx; ++i) {
				bool empty                                = !used[i / MESH_VOLUME_BLOCK + bx * (j / MESH_VOLUME_BLOCK + by * (k / MESH_VOLUME_BLOCK))];
				v->atlas[((oy + j) * w + ox + i) * 4 + 2] = empty ? 255 : 0;
			}
		}
	}
	free(used);

	// A cell of margin, filtering reaches half a cell past the last one holding gas
	for (i32 a = 0; a < 3; ++a) {
		v->n[a]       = n[a];
		v->box_min[a] = grid->min[a] + (lo[a] > 0 ? lo[a] - 1 : 0) * grid->cell;
		v->box_max[a] = grid->min[a] + (hi[a] + 1 < n[a] ? hi[a] + 2 : n[a]) * grid->cell;
		if (hi[a] < 0) {
			v->box_min[a] = 1.0f;
			v->box_max[a] = -1.0f;
		}
	}
	memcpy(v->min, grid->min, sizeof(f32) * 3);
	v->cell     = grid->cell;
	v->emission = emission;
	v->width    = w;
	v->height   = h;
	v->version++;
}

// Closed surface where the field reaches the level, 0 - 1
mesh_geo_t *mesh_smoke_shell(physics_sim_t *sm, u8 *density, f32 level, i32 smooth) {
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

// Outer surface of the smoke and flame, world space, box projected tex coords
mesh_geo_t *mesh_volume_surface(physics_sim_t *grid, u8 *density, u8 *flame) {
	i32 n   = grid->nx * grid->ny * grid->nz;
	u8 *gas = malloc(n);
	for (i32 c = 0; c < n; ++c) {
		u8 d   = density != NULL ? density[c] : 0;
		u8 f   = flame != NULL ? flame[c] : 0;
		gas[c] = d > f ? d : f;
	}
	mesh_geo_t *g = mesh_smoke_shell(grid, gas, 0.05f, 2);
	free(gas);
	if (g->index_count > 0) {
		f32 dmn[3] = {grid->min[0], grid->min[1], grid->min[2]};
		f32 dmx[3] = {grid->min[0] + grid->nx * grid->cell, grid->min[1] + grid->ny * grid->cell, grid->min[2] + grid->nz * grid->cell};
		util_geo_uv_box(g, dmn, dmx);
	}
	return g;
}

mesh_geo_t *mesh_smoke_node_eval(ui_node_t *node) {
	mesh_geo_t    *g = nodes_mesh_eval_input(node, 0);
	mesh_object_t *o = nodes_mesh_eval_object;
	if (g->index_count == 0 || o == NULL) {
		return g;
	}
	nodes_mesh_eval_reads_frame = true;

	physics_sim_t *grid;
	u8            *density = sim_world_gas(o->base->uid, node->id, &grid);
	if (density == NULL) {
		return g; // Not simulated yet
	}
	util_geo_free(g);

	mesh_volume_set(o->base->uid, grid, density, NULL, 0.0f);
	g = mesh_volume_surface(grid, density, NULL);
	if (g->index_count == 0) {
		return g;
	}
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
	nodes_mesh_def_value(n, _tr("Seed"), 1, 0, 100, 1);
	any_map_set(nodes_mesh_node_evals, "MESH_SMOKE", mesh_smoke_node_eval);
}
