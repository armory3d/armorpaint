#include "../global.h"

#define FIRE_EMISSION_SCALE 1000.0f

// Elongated octahedron along the velocity, as if blurred over a part of the frame
static mesh_geo_t *mesh_fire_sparks(f32 *sparks, i32 count, f32 radius, f32 shutter, f32 radiance) {
	mesh_geo_t *g = util_geo_create(count * 6, count * 24);
	for (i32 s = 0; s < count; ++s) {
		f32   *k      = &sparks[s * 7];
		vec4_t v      = {k[3], k[4], k[5], 0.0f};
		f32    len    = vec4_len(v);
		vec4_t ax     = len > 1e-6f ? vec4_mult(v, 1.0f / len) : (vec4_t){0.0f, 0.0f, 1.0f, 0.0f};
		f32    hl     = fmaxf(radius * 2.0f, len * shutter * 0.5f);
		vec4_t t      = fabsf(ax.z) < 0.9f ? (vec4_t){0.0f, 0.0f, 1.0f, 0.0f} : (vec4_t){1.0f, 0.0f, 0.0f, 0.0f};
		vec4_t b0     = vec4_norm(vec4_cross(ax, t));
		vec4_t b1     = vec4_cross(ax, b0);
		vec4_t dir[6] = {vec4_mult(ax, hl), vec4_mult(ax, -hl), vec4_mult(b0, radius), vec4_mult(b1, radius), vec4_mult(b0, -radius), vec4_mult(b1, -radius)};
		for (i32 i = 0; i < 6; ++i) {
			i32    vi          = s * 6 + i;
			vec4_t n           = vec4_norm(dir[i]);
			g->pos[vi * 3]     = k[0] + dir[i].x;
			g->pos[vi * 3 + 1] = k[1] + dir[i].y;
			g->pos[vi * 3 + 2] = k[2] + dir[i].z;
			g->nor[vi * 3]     = n.x;
			g->nor[vi * 3 + 1] = n.y;
			g->nor[vi * 3 + 2] = n.z;
			g->tex[vi * 2]     = fminf(fmaxf(k[6], 0.0f), 1.0f);
			g->tex[vi * 2 + 1] = -fminf(fmaxf(radiance / FIRE_EMISSION_SCALE, 2.0f / 32767.0f), 1.0f);
		}
		// Tips 0 and 1, the ring 2 3 4 5 counter clockwise around the axis
		i32 tris[24] = {0, 2, 3, 0, 3, 4, 0, 4, 5, 0, 5, 2, 1, 3, 2, 1, 4, 3, 1, 5, 4, 1, 2, 5};
		for (i32 i = 0; i < 24; ++i) {
			g->ind[s * 24 + i] = s * 6 + tris[i];
		}
	}
	return g;
}

mesh_geo_t *mesh_fire_node_eval(ui_node_t *node) {
	mesh_geo_t    *g = nodes_mesh_eval_input(node, 0);
	mesh_object_t *o = nodes_mesh_eval_object;
	if (g->index_count == 0 || o == NULL) {
		return g;
	}
	nodes_mesh_eval_reads_frame = true;

	physics_sim_t *grid;
	u8            *soot  = sim_world_gas(o->base->uid, node->id, &grid);
	u8            *flame = sim_world_flame(o->base->uid, node->id);
	if (soot == NULL || flame == NULL) {
		return g; // Not simulated yet
	}

	// Size of the fire, from the emitter in world space
	nodes_mesh_geo_transform(g, o->base->transform->world);
	f32 mn[3] = {1e9f, 1e9f, 1e9f}, mx[3] = {-1e9f, -1e9f, -1e9f};
	for (i32 i = 0; i < g->vertex_count; ++i) {
		for (i32 a = 0; a < 3; ++a) {
			mn[a] = fminf(mn[a], g->pos[i * 3 + a]);
			mx[a] = fmaxf(mx[a], g->pos[i * 3 + a]);
		}
	}
	f32 size = fmaxf(fmaxf(mx[0] - mn[0], fmaxf(mx[1] - mn[1], mx[2] - mn[2])), 1e-4f);
	util_geo_free(g);

	// Glow of the flame per unit of distance at full heat, a core about the size of the emitter
	f32 intensity = math_max(0.0, nodes_mesh_value(node, 9));
	mesh_volume_set(o->base->uid, grid, soot, flame, intensity * 3.2f / size);

	g                = mesh_volume_surface(grid, soot, flame);
	i32  spark_count = 0;
	f32 *sparks      = sim_world_sparks(o->base->uid, node->id, &spark_count);
	if (sparks != NULL && spark_count > 0) {
		f32 shutter = 0.5f / (f32)math_max(1, tab_timeline_get_frame_rate());
		g           = nodes_mesh_geo_join(g, mesh_fire_sparks(sparks, spark_count, size * 0.004f, shutter, intensity * 8.0f));
	}
	if (g->index_count == 0) {
		return g;
	}
	nodes_mesh_geo_transform(g, mat4_inv(o->base->transform->world));
	return nodes_mesh_geo_modify(g, node->type, 1.0);
}

void mesh_fire_node_init() {
	ui_node_t *n = nodes_mesh_def(nodes_mesh_modifier, _tr("Fire"), "MESH_FIRE", 0xff4982a0, "MESH");
	nodes_mesh_def_mesh(n);
	nodes_mesh_def_value(n, _tr("Resolution"), 48, 8, 128, 1);
	nodes_mesh_def_value(n, _tr("Buoyancy"), 8.0, 0.0, 20.0, 100);
	nodes_mesh_def_value(n, _tr("Vorticity"), 2.0, 0.0, 4.0, 100);
	nodes_mesh_def_value(n, _tr("Dissipation"), 0.5, 0.0, 2.0, 100);
	nodes_mesh_def_value(n, _tr("Emit Frames"), 250, 0, 250, 1);
	nodes_mesh_def_value(n, _tr("Seed"), 1, 0, 100, 1);
	nodes_mesh_def_value(n, _tr("Burn Rate"), 0.8, 0.1, 20.0, 100);
	nodes_mesh_def_value(n, _tr("Soot"), 0.5, 0.0, 2.0, 100);
	nodes_mesh_def_value(n, _tr("Intensity"), 1.0, 0.0, 10.0, 100);
	nodes_mesh_def_value(n, _tr("Sparks"), 20, 0, 200, 1);
	nodes_mesh_def_value(n, _tr("Spark Life"), 1.0, 0.1, 5.0, 100);
	any_map_set(nodes_mesh_node_evals, "MESH_FIRE", mesh_fire_node_eval);
}
