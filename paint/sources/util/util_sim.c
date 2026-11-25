#include "../global.h"

// Fluid, smoke, cloth, fracture and rigid bodies stepped together per timeline frame

#define SIM_MAX_CELLS (4 * 1024 * 1024)
#define SIM_FLUID     0
#define SIM_SMOKE     1
#define SIM_CLOTH     2
#define SIM_FRACTURE  3
#define SIM_FIRE      4

#define SIM_SPARK_MAX    4096
#define SIM_SPARK_FLOATS 8 // xyz, velocity, age, life
#define SIM_SPARK_SHOWN  7 // xyz, velocity, heat

typedef struct {
	i32              uid;
	i32              node_id;
	i32              type;
	i32              gas;
	i32              emit_frames;
	physics_cloth_t *cloth;
	i32             *particle; // Per vertex
	i32              vertex_count;
	mesh_fracture_t *fracture;
	i32             *cells; // Emitter cells of a fire
	i32              cell_count;
	f32              size;
	f32             *spark; // SIM_SPARK_FLOATS per spark
	i32              spark_count;
	f32              spark_rate; // Per second
	f32              spark_life; // Seconds
	f32              spark_carry;
	u32              seed;
} sim_part_t;

typedef struct {
	f32  *pos;
	u8   *owner;
	i32   count;
	u8  **gas;
	u8  **flame;
	f32 **part;
	f32  *bodies;
} sim_frame_t;

typedef struct {
	i32    uid;
	vec4_t loc;
	quat_t rot;
	vec4_t shown_loc; // Detects moves by hand
	quat_t shown_rot;
} sim_body_t;

static sim_part_t    *sim_parts       = NULL;
static i32            sim_part_count  = 0;
static physics_sim_t *sim_grid        = NULL;
static sim_frame_t   *sim_frames      = NULL;
static i32            sim_frame_count = 0;
static sim_body_t    *sim_bodies      = NULL;
static i32            sim_body_count  = 0;
static u32            sim_key         = 0;
static bool           sim_dirty       = true;
static bool           sim_player_ran  = false;
static bool           sim_hand_moved  = false; // Keyed meshes keep the move
static u32            sim_keys        = 0;     // Mesh keyframes the world was stepped with
static i32            sim_shown       = -1;

void sim_world_invalidate() {
	sim_dirty = true;
}

// A collider was moved by hand
void sim_world_collider_moved() {
	sim_dirty      = true;
	sim_hand_moved = true;
}

static bool sim_node_type(char *type, i32 *out) {
	*out = string_equals(type, "MESH_FLUID")      ? SIM_FLUID
	       : string_equals(type, "MESH_SMOKE")    ? SIM_SMOKE
	       : string_equals(type, "MESH_CLOTH")    ? SIM_CLOTH
	       : string_equals(type, "MESH_FRACTURE") ? SIM_FRACTURE
	       : string_equals(type, "MESH_FIRE")     ? SIM_FIRE
	                                              : -1;
	return *out >= 0;
}

static bool sim_dynamic(object_t *o) {
	i32 shape = util_physics_get_shape(o);
	return o->_->body != NULL && (shape == PHYSICS_SHAPE_BOX || shape == PHYSICS_SHAPE_SPHERE) && util_physics_get_mass(o) > 0.0;
}

// Changes when the world has to start over
static u32 sim_world_key() {
	u32 key = 2166136261u;
	for (i32 i = 0; nodes_mesh_slots != NULL && i < nodes_mesh_slots->length; ++i) {
		mesh_nodes_t  *slot = nodes_mesh_slots->buffer[i];
		mesh_object_t *o    = nodes_mesh_object(slot->uid);
		if (o == NULL || !util_physics_in_stage(o->base)) {
			continue;
		}
		bool any = false;
		for (i32 j = 0; j < slot->canvas->nodes->length && !any; ++j) {
			i32 type;
			any = sim_node_type(((ui_node_t *)slot->canvas->nodes->buffer[j])->type, &type);
		}
		if (any) {
			u32 h = nodes_mesh_hash(slot->canvas, o);
			key   = nodes_mesh_hash_bytes(key, &slot->uid, sizeof(i32));
			key   = nodes_mesh_hash_bytes(key, &h, sizeof(u32));
			key   = nodes_mesh_hash_bytes(key, &o->base->transform->world, sizeof(mat4_t));
		}
	}
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		object_t *o     = ((mesh_object_t *)g_project->_->paint_objects->buffer[i])->base;
		i32       shape = util_physics_get_shape(o);
		f32       mass  = util_physics_get_mass(o);
		bool      body  = o->_->body != NULL;
		key             = nodes_mesh_hash_bytes(key, &shape, sizeof(i32));
		key             = nodes_mesh_hash_bytes(key, &mass, sizeof(f32));
		key             = nodes_mesh_hash_bytes(key, &body, sizeof(bool));
	}
	vec4_t gravity = physics_get_gravity();
	i32    fps     = tab_timeline_get_frame_rate();
	key            = nodes_mesh_hash_bytes(key, &gravity, sizeof(vec4_t));
	key            = nodes_mesh_hash_bytes(key, &fps, sizeof(i32));
	return key;
}

// Keys of the static colliders and participants, the ones moving the world
static u32 sim_keys_hash() {
	u32 key = 2166136261u;
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_object_t *mo     = g_project->_->paint_objects->buffer[i];
		bool           drives = mo->base->_->body != NULL && !sim_dynamic(mo->base);
		mesh_nodes_t  *slot   = drives ? NULL : nodes_mesh_get(mo, false);
		for (i32 j = 0; slot != NULL && j < slot->canvas->nodes->length && !drives; ++j) {
			i32 type;
			drives = sim_node_type(((ui_node_t *)slot->canvas->nodes->buffer[j])->type, &type);
		}
		if (drives) {
			u32 h = tab_timeline_mesh_keys_hash(i);
			key   = nodes_mesh_hash_bytes(key, &i, sizeof(i32));
			key   = nodes_mesh_hash_bytes(key, &h, sizeof(u32));
		}
	}
	return key;
}

static void sim_frame_free(sim_frame_t *f) {
	free(f->pos);
	free(f->owner);
	for (i32 i = 0; f->gas != NULL && sim_grid != NULL && i < sim_grid->gas_count; ++i) {
		free(f->gas[i]);
		free(f->flame[i]);
	}
	free(f->gas);
	free(f->flame);
	for (i32 i = 0; f->part != NULL && i < sim_part_count; ++i) {
		free(f->part[i]);
	}
	free(f->part);
	free(f->bodies);
}

static void sim_world_clear() {
	for (i32 i = 0; i < sim_frame_count; ++i) {
		sim_frame_free(&sim_frames[i]);
	}
	free(sim_frames);
	for (i32 i = 0; i < sim_part_count; ++i) {
		physics_cloth_destroy(sim_parts[i].cloth);
		free(sim_parts[i].particle);
		mesh_fracture_destroy(sim_parts[i].fracture);
		free(sim_parts[i].cells);
		free(sim_parts[i].spark);
	}
	free(sim_parts);
	physics_sim_destroy(sim_grid);
	sim_frames      = NULL;
	sim_frame_count = 0;
	sim_parts       = NULL;
	sim_part_count  = 0;
	sim_grid        = NULL;
}

static sim_body_t *sim_body(i32 uid, bool create) {
	for (i32 i = 0; i < sim_body_count; ++i) {
		if (sim_bodies[i].uid == uid) {
			return &sim_bodies[i];
		}
	}
	if (!create) {
		return NULL;
	}
	sim_bodies                 = realloc(sim_bodies, sizeof(sim_body_t) * (sim_body_count + 1));
	sim_bodies[sim_body_count] = (sim_body_t){.uid = uid};
	return &sim_bodies[sim_body_count++];
}

static void sim_body_place(object_t *o, vec4_t loc, quat_t rot) {
	o->transform->loc = loc;
	o->transform->rot = rot;
	transform_build_matrix(o->transform);
}

static void sim_cache_frame() {
	sim_frames       = realloc(sim_frames, sizeof(sim_frame_t) * (sim_frame_count + 1));
	sim_frame_t *f   = &sim_frames[sim_frame_count++];
	*f               = (sim_frame_t){0};
	physics_sim_t *s = sim_grid;
	if (s != NULL) {
		f->count = s->count;
		f->pos   = malloc(sizeof(f32) * 3 * s->count + 1);
		f->owner = malloc(s->count + 1);
		memcpy(f->pos, s->pos, sizeof(f32) * 3 * s->count);
		memcpy(f->owner, s->owner, s->count);
		i32 n  = s->nx * s->ny * s->nz;
		f->gas   = malloc(sizeof(u8 *) * (s->gas_count + 1));
		f->flame = calloc(s->gas_count + 1, sizeof(u8 *));
		for (i32 g = 0; g < s->gas_count; ++g) {
			f->gas[g] = malloc(n);
			for (i32 c = 0; c < n; ++c) {
				f->gas[g][c] = (u8)(fminf(fmaxf(s->density[g][c], 0.0f), 1.0f) * 255.0f + 0.5f);
			}
			if (s->flame[g] != NULL) {
				f->flame[g] = malloc(n);
				for (i32 c = 0; c < n; ++c) {
					f->flame[g][c] = (u8)(fminf(fmaxf(s->flame[g][c], 0.0f), 1.0f) * 255.0f + 0.5f);
				}
			}
		}
	}
	f->part = calloc(sim_part_count + 1, sizeof(f32 *));
	for (i32 i = 0; i < sim_part_count; ++i) {
		physics_cloth_t *c  = sim_parts[i].cloth;
		mesh_fracture_t *fr = sim_parts[i].fracture;
		if (c != NULL) {
			f->part[i] = malloc(sizeof(f32) * 3 * c->count + 1);
			memcpy(f->part[i], c->pos, sizeof(f32) * 3 * c->count);
		}
		if (fr != NULL) {
			f->part[i] = malloc(sizeof(f32) * FRACTURE_PS * fr->np);
			mesh_fracture_poses(fr, f->part[i]);
		}
		if (sim_parts[i].type == SIM_FIRE) {
			// Count first, then position, velocity and heat per spark
			sim_part_t *p = &sim_parts[i];
			f->part[i]    = malloc(sizeof(f32) * (1 + SIM_SPARK_SHOWN * p->spark_count));
			f->part[i][0] = (f32)p->spark_count;
			for (i32 k = 0; k < p->spark_count; ++k) {
				f32 *src = &p->spark[k * SIM_SPARK_FLOATS];
				f32 *dst = &f->part[i][1 + k * SIM_SPARK_SHOWN];
				memcpy(dst, src, sizeof(f32) * 6);
				dst[6] = 1.0f - src[6] / src[7];
			}
		}
	}
	f->bodies = malloc(sizeof(f32) * 7 * sim_body_count + 1);
	for (i32 i = 0; i < sim_body_count; ++i) {
		mesh_object_t *mo = nodes_mesh_object(sim_bodies[i].uid);
		vec4_t         p  = sim_bodies[i].loc;
		quat_t         q  = sim_bodies[i].rot;
		if (mo != NULL && mo->base->_->body != NULL) {
			physics_body_t *b = mo->base->_->body;
			physics_body_get_pos(b->_body, &p);
			physics_body_get_rot(b->_body, &q);
			vec4_t off = vec4_apply_quat(b->offset, q);
			p          = vec4_sub(p, off);
		}
		f32 v[7] = {p.x, p.y, p.z, q.x, q.y, q.z, q.w};
		memcpy(&f->bodies[i * 7], v, sizeof(v));
	}
}

// World space input mesh positions
static mesh_geo_t *sim_input(mesh_object_t *o, ui_node_t *node, f32 *mn, f32 *mx) {
	mesh_nodes_t *slot          = nodes_mesh_get(o, false);
	nodes_mesh_eval_canvas      = slot->canvas;
	nodes_mesh_eval_slot        = slot;
	nodes_mesh_eval_object      = o;
	nodes_mesh_eval_unwrap      = false;
	nodes_mesh_eval_depth       = 0;
	nodes_mesh_eval_reads_frame = false;
	nodes_mesh_group_reset_eval();
	mesh_geo_t *g = nodes_mesh_eval_input(node, 0);
	nodes_mesh_geo_transform(g, o->base->transform->world);
	for (i32 a = 0; a < 3; ++a) {
		mn[a] = 1e9f;
		mx[a] = -1e9f;
	}
	for (i32 i = 0; i < g->vertex_count; ++i) {
		f32 *p = &g->pos[i * 3];
		for (i32 a = 0; a < 3; ++a) {
			mn[a] = fminf(mn[a], p[a]);
			mx[a] = fmaxf(mx[a], p[a]);
		}
	}
	return g;
}

static void sim_input_end() {
	nodes_mesh_eval_object = NULL;
	mesh_image_node_clear();
}

static bool sim_domain(f32 *mn, f32 *mx) {
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_object_t *mo = g_project->_->paint_objects->buffer[i];
		if (util_physics_get_shape(mo->base) != PHYSICS_SHAPE_DOMAIN || !util_physics_in_stage(mo->base)) {
			continue;
		}
		i16_array_t *pa = mesh_data_get_vertex_array(mo->data, "pos")->values;
		transform_build_matrix(mo->base->transform);
		mat4_t m = mo->base->transform->world_unpack;
		for (i32 a = 0; a < 3; ++a) {
			mn[a] = 1e9f;
			mx[a] = -1e9f;
		}
		for (i32 v = 0; v < pa->length / 4; ++v) {
			vec4_t w    = vec4_apply_mat4((vec4_t){pa->buffer[v * 4] / 32767.0f, pa->buffer[v * 4 + 1] / 32767.0f, pa->buffer[v * 4 + 2] / 32767.0f, 1.0f}, m);
			f32    p[3] = {w.x, w.y, w.z};
			for (i32 a = 0; a < 3; ++a) {
				mn[a] = fminf(mn[a], p[a]);
				mx[a] = fmaxf(mx[a], p[a]);
			}
		}
		return true;
	}
	return false;
}

// Jittered lattice, two particles per cell along each axis
static void sim_fill(physics_sim_t *s, mesh_geo_t *g, f32 *mn, f32 *mx, i32 material, i32 owner) {
	f32 sp       = s->cell * 0.5f;
	i32 nx       = (i32)((mx[0] - mn[0]) / sp);
	i32 ny       = (i32)((mx[1] - mn[1]) / sp);
	i32 nz       = (i32)((mx[2] - mn[2]) / sp);
	f32 start[3] = {(mx[0] + mn[0] - (nx - 1) * sp) * 0.5f, (mx[1] + mn[1] - (ny - 1) * sp) * 0.5f, (mx[2] + mn[2] - (nz - 1) * sp) * 0.5f};
	u8 *inside   = malloc(nx * ny * nz + 1);
	u32 seed     = 1;
	nodes_mesh_geo_voxelize(g->pos, g->ind, g->index_count, start, sp, nx, ny, nz, inside);
	for (i32 k = 0; k < nz; ++k) {
		for (i32 j = 0; j < ny; ++j) {
			for (i32 i = 0; i < nx; ++i) {
				if (inside[i + nx * (j + ny * k)]) {
					f32 d[3];
					for (i32 a = 0; a < 3; ++a) {
						seed = seed * 1664525u + 1013904223u;
						d[a] = ((f32)(seed >> 8) / 16777216.0f - 0.5f) * sp * 0.5f;
					}
					physics_sim_add_particle(s, start[0] + i * sp + d[0], start[1] + j * sp + d[1], start[2] + k * sp + d[2], material, owner);
				}
			}
		}
	}
	free(inside);
}

static f32 *sim_vertex_compare_pos;
static int  sim_vertex_compare(const void *pa, const void *pb) {
	f32 *a = &sim_vertex_compare_pos[*(i32 *)pa * 3], *b = &sim_vertex_compare_pos[*(i32 *)pb * 3];
	for (i32 k = 0; k < 3; ++k) {
		if (a[k] != b[k]) {
			return a[k] < b[k] ? -1 : 1;
		}
	}
	return 0;
}

static int sim_fracture_collide(vec4_t *pos, float radius, physics_hit_t *hit) {
	int any = 0;
	for (i32 i = 0; i < sim_part_count; ++i) {
		if (sim_parts[i].fracture != NULL && mesh_fracture_collide(sim_parts[i].fracture, pos, radius, hit)) {
			any = 1;
		}
	}
	return any;
}

// Vertices at the same position become one particle
static void sim_add_cloth(sim_part_t *p, mesh_geo_t *g, f32 bending) {
	i32 *order = malloc(sizeof(i32) * g->vertex_count + 1);
	for (i32 i = 0; i < g->vertex_count; ++i) {
		order[i] = i;
	}
	sim_vertex_compare_pos = g->pos;
	qsort(order, g->vertex_count, sizeof(i32), sim_vertex_compare);
	p->particle     = malloc(sizeof(i32) * g->vertex_count + 1);
	p->vertex_count = g->vertex_count;
	f32 *pos        = malloc(sizeof(f32) * 3 * g->vertex_count + 1);
	i32  count      = 0;
	for (i32 i = 0; i < g->vertex_count; ++i) {
		if (i == 0 || sim_vertex_compare(&order[i - 1], &order[i]) != 0) {
			memcpy(&pos[count * 3], &g->pos[order[i] * 3], sizeof(f32) * 3);
			count++;
		}
		p->particle[order[i]] = count - 1;
	}
	u32 *ind = malloc(sizeof(u32) * g->index_count + 1);
	for (i32 i = 0; i < g->index_count; ++i) {
		ind[i] = p->particle[g->ind[i]];
	}
	physics_cloth_t *c    = physics_cloth_create(pos, count, ind, g->index_count);
	f32              edge = 0.0f;
	for (i32 i = 0; i < c->stretch_count; ++i) {
		edge += c->rest[i];
	}
	c->bend_compliance = powf(10.0f, 2.0f - 8.0f * bending);
	c->damping         = 0.5f;
	c->thickness       = c->stretch_count > 0 ? edge / c->stretch_count * 0.5f : 0.01f;
	c->collide         = sim_fracture_collide;
	p->cloth           = c;
	free(order);
	free(pos);
	free(ind);
}

// Starts over from frame 0
static void sim_world_reset() {
	sim_world_clear();
	tab_timeline_pose_meshes(0.0f);

	// Rigid bodies
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		object_t *o = ((mesh_object_t *)g_project->_->paint_objects->buffer[i])->base;
		if (!sim_dynamic(o)) {
			continue;
		}
		bool        known = sim_body(o->uid, false) != NULL;
		sim_body_t *b     = sim_body(o->uid, true);
		if (!known) {
			b->loc = o->transform->loc;
			b->rot = o->transform->rot;
		}
		sim_body_place(o, b->loc, b->rot);
		b->shown_loc = b->loc;
		b->shown_rot = b->rot;
		physics_body_sync_transform(o->_->body);
		physics_body_set_rotation(o->_->body, b->rot);
		physics_body_set_velocity(o->_->body->_body, 0.0f, 0.0f, 0.0f);
	}
	for (i32 i = 0; i < sim_body_count;) {
		mesh_object_t *mo = nodes_mesh_object(sim_bodies[i].uid);
		if (mo == NULL || !sim_dynamic(mo->base)) {
			sim_bodies[i] = sim_bodies[--sim_body_count];
		}
		else {
			++i;
		}
	}

	// Participants
	f32 cell = 1e9f, dmn[3] = {1e9f, 1e9f, 1e9f}, dmx[3] = {-1e9f, -1e9f, -1e9f};
	f32 viscosity = 0.0f, buoyancy = 1.0f, vorticity = 0.3f, dissipation = 0.2f, burn = 0.8f, soot = 0.5f;
	i32 seed = 1, grid_parts = 0;
	for (i32 i = 0; nodes_mesh_slots != NULL && i < nodes_mesh_slots->length; ++i) {
		mesh_nodes_t  *slot = nodes_mesh_slots->buffer[i];
		mesh_object_t *o    = nodes_mesh_object(slot->uid);
		if (o == NULL || !util_physics_in_stage(o->base)) {
			continue;
		}
		for (i32 j = 0; j < slot->canvas->nodes->length && sim_part_count < 255; ++j) {
			ui_node_t *node = slot->canvas->nodes->buffer[j];
			i32        type;
			if (!sim_node_type(node->type, &type)) {
				continue;
			}
			sim_parts                 = realloc(sim_parts, sizeof(sim_part_t) * (sim_part_count + 1));
			sim_parts[sim_part_count] = (sim_part_t){.uid = slot->uid, .node_id = node->id, .type = type, .gas = -1};
			sim_part_count++;
			if (type == SIM_CLOTH || type == SIM_FRACTURE) {
				continue;
			}
			f32         mn[3], mx[3];
			mesh_geo_t *g    = sim_input(o, node, mn, mx);
			f32         size = fmaxf(mx[0] - mn[0], fmaxf(mx[1] - mn[1], mx[2] - mn[2]));
			if (g->index_count > 0 && size > 0.0f) {
				grid_parts++;
				f32 c[2] = {(mn[0] + mx[0]) * 0.5f, (mn[1] + mx[1]) * 0.5f};
				if (type == SIM_FLUID) {
					cell      = fminf(cell, size / math_max(2, math_min(48, math_floor(nodes_mesh_value(node, 1)))));
					viscosity = fmaxf(viscosity, math_max(0.0, math_min(1.0, nodes_mesh_value(node, 2))));
					f32 lo[3] = {c[0] - size * 3.0f, c[1] - size * 3.0f, mx[2] + size * 0.5f - size * 3.0f};
					f32 hi[3] = {c[0] + size * 3.0f, c[1] + size * 3.0f, mx[2] + size * 0.5f};
					for (i32 a = 0; a < 3; ++a) {
						dmn[a] = fminf(dmn[a], lo[a]);
						dmx[a] = fmaxf(dmx[a], hi[a]);
					}
				}
				else {
					if (type == SIM_FIRE) {
						burn = math_max(0.0, nodes_mesh_value(node, 7));
						soot = math_max(0.0, nodes_mesh_value(node, 8));
					}
					cell        = fminf(cell, size * 3.0f / math_max(8, math_min(128, math_floor(nodes_mesh_value(node, 1)))));
					buoyancy    = nodes_mesh_value(node, 2) * size;
					vorticity   = math_max(0.0, nodes_mesh_value(node, 3));
					dissipation = math_max(0.0, nodes_mesh_value(node, 4));
					seed        = (i32)math_floor(nodes_mesh_value(node, 6)) * 7919 + 1;
					f32 lo[3]   = {c[0] - size * 1.5f, c[1] - size * 1.5f, mn[2] - size * 0.25f};
					f32 hi[3]   = {c[0] + size * 1.5f, c[1] + size * 1.5f, mn[2] + size * 5.75f};
					for (i32 a = 0; a < 3; ++a) {
						dmn[a] = fminf(dmn[a], lo[a]);
						dmx[a] = fmaxf(dmx[a], hi[a]);
					}
				}
			}
			util_geo_free(g);
			sim_input_end();
		}
	}

	// Grid
	if (grid_parts > 0) {
		sim_domain(dmn, dmx);
		i32 n[3];
		for (;;) {
			for (i32 a = 0; a < 3; ++a) {
				n[a] = (i32)math_max(1, ceilf((dmx[a] - dmn[a]) / cell));
			}
			if ((i64)n[0] * n[1] * n[2] <= SIM_MAX_CELLS) {
				break;
			}
			cell *= 1.25f;
		}
		sim_grid              = physics_sim_create(n[0], n[1], n[2], cell, dmn);
		sim_grid->viscosity   = viscosity;
		sim_grid->buoyancy    = buoyancy;
		sim_grid->vorticity   = vorticity;
		sim_grid->dissipation = dissipation;
		sim_grid->burn        = burn;
		sim_grid->soot        = soot;
		sim_grid->seed        = (u32)seed;
	}

	// Fill
	for (i32 i = 0; i < sim_part_count; ++i) {
		sim_part_t    *p    = &sim_parts[i];
		mesh_object_t *o    = nodes_mesh_object(p->uid);
		ui_node_t     *node = ui_get_node(nodes_mesh_get(o, false)->canvas->nodes, p->node_id);
		f32            mn[3], mx[3];
		mesh_geo_t    *g = sim_input(o, node, mn, mx);
		if (g->index_count > 0) {
			if (p->type == SIM_CLOTH) {
				sim_add_cloth(p, g, math_max(0.0, math_min(1.0, nodes_mesh_value(node, 1))));
			}
			else if (p->type == SIM_FRACTURE) {
				p->fracture = mesh_fracture_create(g, node);
			}
			else if (p->type == SIM_FLUID && sim_grid != NULL) {
				sim_grid->stiffness[i] = math_max(1.0, math_min(1000.0, nodes_mesh_value(node, 4)));
				sim_fill(sim_grid, g, mn, mx, nodes_mesh_button(node, 0), i);
			}
			else if ((p->type == SIM_SMOKE || p->type == SIM_FIRE) && sim_grid != NULL) {
				physics_sim_t *s    = sim_grid;
				bool           fire = p->type == SIM_FIRE;
				p->gas              = fire ? physics_sim_add_fire(s) : physics_sim_add_gas(s);
				p->emit_frames      = (i32)math_max(0, math_floor(nodes_mesh_value(node, 5)));
				i32 cells           = s->nx * s->ny * s->nz;
				u8 *inside          = malloc(cells);
				f32 origin[3]       = {s->min[0] + s->cell * 0.5f, s->min[1] + s->cell * 0.5f, s->min[2] + s->cell * 0.5f};
				nodes_mesh_geo_voxelize(g->pos, g->ind, g->index_count, origin, s->cell, s->nx, s->ny, s->nz, inside);
				if (fire) {
					p->cells = malloc(sizeof(i32) * (cells + 1));
				}
				for (i32 c = 0; c < cells; ++c) {
					if (inside[c]) {
						s->emitter[c] = (u8)(p->gas + 1);
						if (fire) {
							s->flame[p->gas][c]       = 1.0f;
							p->cells[p->cell_count++] = c;
						}
						else {
							s->density[p->gas][c] = 1.0f;
						}
					}
				}
				free(inside);
				if (fire) {
					p->size       = fmaxf(mx[0] - mn[0], fmaxf(mx[1] - mn[1], mx[2] - mn[2]));
					p->spark_rate = math_max(0.0, nodes_mesh_value(node, 10));
					p->spark_life = math_max(0.05, nodes_mesh_value(node, 11));
					p->seed       = (u32)(math_floor(nodes_mesh_value(node, 6)) * 7919 + 17 + i);
					p->spark      = malloc(sizeof(f32) * SIM_SPARK_FLOATS * SIM_SPARK_MAX);
				}
			}
		}
		util_geo_free(g);
		sim_input_end();
	}

	sim_cache_frame();
	sim_key   = sim_world_key();
	sim_keys  = sim_keys_hash();
	sim_dirty = false;
	sim_shown = -1;
}

static f32 sim_spark_random(sim_part_t *p) {
	p->seed = p->seed * 1664525u + 1013904223u;
	return (f32)(p->seed >> 8) / 16777216.0f;
}

// Embers thrown out of the fire, carried by the hot air and slowly falling as they cool
static void sim_sparks_step(sim_part_t *p, f32 dt, bool emit) {
	physics_sim_t *s = sim_grid;
	if (s == NULL || p->spark == NULL) {
		return;
	}
	f32 size = p->size;
	if (emit && p->cell_count > 0) {
		p->spark_carry += p->spark_rate * dt;
		while (p->spark_carry >= 1.0f && p->spark_count < SIM_SPARK_MAX) {
			p->spark_carry -= 1.0f;
			i32 c  = p->cells[(i32)(sim_spark_random(p) * p->cell_count) % p->cell_count];
			i32 ci = c % s->nx, cj = (c / s->nx) % s->ny, ck = c / (s->nx * s->ny);
			f32 *k = &p->spark[p->spark_count++ * SIM_SPARK_FLOATS];
			k[0]   = s->min[0] + (ci + sim_spark_random(p)) * s->cell;
			k[1]   = s->min[1] + (cj + sim_spark_random(p)) * s->cell;
			k[2]   = s->min[2] + (ck + sim_spark_random(p)) * s->cell;
			k[3]   = (sim_spark_random(p) - 0.5f) * size * 2.0f;
			k[4]   = (sim_spark_random(p) - 0.5f) * size * 2.0f;
			k[5]   = (0.5f + sim_spark_random(p)) * size * 2.0f;
			k[6]   = 0.0f;
			k[7]   = p->spark_life * (0.5f + sim_spark_random(p));
		}
		p->spark_carry = fminf(p->spark_carry, 1.0f);
	}
	vec4_t   gravity = physics_get_gravity();
	f32      max[3]  = {s->min[0] + s->nx * s->cell, s->min[1] + s->ny * s->cell, s->min[2] + s->nz * s->cell};
	const int steps  = 4;
	f32       h      = dt / steps;
	f32       drag   = fminf(4.0f * h, 1.0f);
	for (i32 i = 0; i < p->spark_count;) {
		f32 *k   = &p->spark[i * SIM_SPARK_FLOATS];
		bool out = false;
		for (i32 st = 0; st < steps && !out; ++st) {
			vec4_t air = {0};
			f32    liquid;
			physics_sim_sample(s, (vec4_t){k[0], k[1], k[2], 1.0f}, &air, &liquid);
			f32 a[3] = {air.x, air.y, air.z};
			f32 g[3] = {gravity.x, gravity.y, gravity.z};
			for (i32 d = 0; d < 3; ++d) {
				// Light embers: the air drags them along, gravity barely shows until they leave the plume
				k[3 + d] += (a[d] - k[3 + d]) * drag + g[d] * 0.15f * h + (sim_spark_random(p) - 0.5f) * size * 6.0f * h;
				k[d] += k[3 + d] * h;
				out |= k[d] < s->min[d] || k[d] >= max[d];
			}
			k[6] += h;
		}
		if (out || k[6] >= k[7]) {
			memmove(k, &p->spark[--p->spark_count * SIM_SPARK_FLOATS], sizeof(f32) * SIM_SPARK_FLOATS);
			continue;
		}
		i++;
	}
}

static void sim_world_step() {
	f32 dt    = 1.0f / (f32)math_max(1, tab_timeline_get_frame_rate());
	i32 frame = sim_frame_count;
	tab_timeline_pose_meshes((f32)frame);

	physics_world_step(dt);
	for (i32 i = 0; i < sim_part_count; ++i) {
		if (sim_parts[i].fracture != NULL) {
			mesh_fracture_frame(sim_parts[i].fracture, frame, dt, sim_grid);
		}
	}

	// Cloth and chunks are solids for the grid
	if (sim_grid != NULL) {
		i32 points = 0;
		for (i32 i = 0; i < sim_part_count; ++i) {
			points += sim_parts[i].cloth != NULL ? sim_parts[i].cloth->count : 0;
			points += sim_parts[i].fracture != NULL ? sim_parts[i].fracture->gs[sim_parts[i].fracture->np] : 0;
		}
		free(sim_grid->obstacles);
		sim_grid->obstacles      = malloc(sizeof(f32) * 6 * points + 1);
		sim_grid->obstacle_count = 0;
		for (i32 i = 0; i < sim_part_count; ++i) {
			physics_cloth_t *c = sim_parts[i].cloth;
			for (i32 k = 0; c != NULL && k < c->count; ++k) {
				f32 *o = &sim_grid->obstacles[sim_grid->obstacle_count++ * 6];
				memcpy(o, &c->pos[k * 3], sizeof(f32) * 3);
				memcpy(o + 3, &c->vel[k * 3], sizeof(f32) * 3);
			}
			if (sim_parts[i].fracture != NULL) {
				sim_grid->obstacle_count += mesh_fracture_obstacles(sim_parts[i].fracture, &sim_grid->obstacles[sim_grid->obstacle_count * 6]);
			}
		}
		u32 emit = 0;
		for (i32 i = 0; i < sim_part_count; ++i) {
			if (sim_parts[i].gas >= 0 && sim_parts[i].gas < 32 && frame - 1 < sim_parts[i].emit_frames) {
				emit |= 1u << sim_parts[i].gas;
			}
		}
		physics_sim_step(sim_grid, dt, emit);
		for (i32 i = 0; i < sim_part_count; ++i) {
			if (sim_parts[i].type == SIM_FIRE) {
				sim_sparks_step(&sim_parts[i], dt, frame - 1 < sim_parts[i].emit_frames);
			}
		}
	}
	for (i32 i = 0; i < sim_part_count; ++i) {
		if (sim_parts[i].cloth != NULL) {
			physics_cloth_step(sim_parts[i].cloth, dt, sim_grid);
		}
	}
	sim_cache_frame();
}

// Shows a cached frame
static void sim_world_show(i32 frame) {
	sim_frame_t *f = &sim_frames[frame];
	for (i32 i = 0; i < sim_body_count; ++i) {
		mesh_object_t *mo = nodes_mesh_object(sim_bodies[i].uid);
		if (mo == NULL) {
			continue;
		}
		f32   *v = &f->bodies[i * 7];
		vec4_t p = {v[0], v[1], v[2], 1.0f};
		quat_t q = {v[3], v[4], v[5], v[6]};
		sim_body_place(mo->base, p, q);
		sim_bodies[i].shown_loc = mo->base->transform->loc;
		sim_bodies[i].shown_rot = mo->base->transform->rot;
	}
	if (sim_body_count > 0) {
		if (tab_timeline_playing) {
			render_path_raytrace_ready = false;
		}
		else {
			util_mesh_transform_changed();
		}
		g_context->ddirty = 2;
	}
	for (i32 i = 0; i < sim_part_count; ++i) {
		mesh_object_t *mo   = nodes_mesh_object(sim_parts[i].uid);
		mesh_nodes_t  *slot = mo != NULL ? nodes_mesh_get(mo, false) : NULL;
		if (slot != NULL) {
			slot->dirty = true;
		}
	}
	nodes_mesh_request_refresh();
	sim_shown = frame;
}

typedef struct {
	vec4_t loc;
	quat_t rot;
	vec4_t scale;
} sim_held_t;

// Static meshes moved by hand stay put while the world is posed and stepped again, like they do without a collider
static sim_held_t *sim_hold_meshes() {
	tab_timeline_keep_mesh_edits();
	i32         n    = g_project->_->paint_objects->length;
	sim_held_t *held = malloc(sizeof(sim_held_t) * n + 1);
	for (i32 i = 0; i < n; ++i) {
		transform_t *t = ((mesh_object_t *)g_project->_->paint_objects->buffer[i])->base->transform;
		held[i]        = (sim_held_t){t->loc, t->rot, t->scale};
	}
	return held;
}

static void sim_release_meshes(sim_held_t *held) {
	bool moved = false;
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		object_t    *o = ((mesh_object_t *)g_project->_->paint_objects->buffer[i])->base;
		transform_t *t = o->transform;
		if (sim_dynamic(o) || memcmp(&held[i], &(sim_held_t){t->loc, t->rot, t->scale}, sizeof(sim_held_t)) == 0) {
			continue;
		}
		t->loc   = held[i].loc;
		t->rot   = held[i].rot;
		t->scale = held[i].scale;
		transform_build_matrix(t);
		if (o->_->body != NULL) {
			physics_body_sync_transform(o->_->body);
		}
		moved = true;
	}
	free(held);
	if (moved) {
		util_mesh_transform_changed();
		g_context->ddirty = 2;
	}
}

static bool sim_bodies_moved() {
	bool moved = false;
	for (i32 i = 0; i < sim_body_count; ++i) {
		mesh_object_t *mo = nodes_mesh_object(sim_bodies[i].uid);
		if (mo == NULL) {
			continue;
		}
		transform_t *t = mo->base->transform;
		if (vec4_len(vec4_sub(t->loc, sim_bodies[i].shown_loc)) > 1e-5f || fabsf(quat_dot(t->rot, sim_bodies[i].shown_rot)) < 0.99999f) {
			sim_bodies[i].loc = t->loc;
			sim_bodies[i].rot = t->rot;
			moved             = true;
		}
	}
	return moved;
}

void sim_world_update() {
	if (base_player) {
		sim_player_ran = true;
		return;
	}
	if (sim_player_ran) {
		sim_player_ran = false;
		sim_dirty      = true;
		for (i32 i = 0; i < sim_body_count; ++i) {
			mesh_object_t *mo = nodes_mesh_object(sim_bodies[i].uid);
			if (mo != NULL) {
				sim_body_place(mo->base, sim_bodies[i].shown_loc, sim_bodies[i].shown_rot);
			}
		}
	}
	bool held     = mouse_down("left"); // Restart once the edit is done
	bool any_body = false;
	for (i32 i = 0; i < g_project->_->paint_objects->length && !any_body; ++i) {
		any_body = sim_dynamic(((mesh_object_t *)g_project->_->paint_objects->buffer[i])->base);
	}
	if (!held && sim_bodies_moved()) {
		sim_dirty = true;
	}
	if (!held && !sim_dirty && sim_world_key() != sim_key) {
		sim_dirty = true;
	}
	if (!held && !sim_dirty && !tab_timeline_playing && sim_keys_hash() != sim_keys) {
		sim_dirty = true; // Keys moved a collider, played on after a pause
	}
	sim_held_t *hand = NULL;
	if (sim_dirty) {
		if (held) {
			return;
		}
		if (sim_hand_moved) {
			sim_hand_moved = false;
			hand           = sim_hold_meshes();
		}
		sim_world_reset();
		if (sim_part_count == 0 && !any_body) {
			tab_timeline_pose_meshes((f32)tab_timeline_selected_frame);
			if (hand != NULL) {
				sim_release_meshes(hand);
			}
			return;
		}
	}
	if (sim_part_count > 0 || sim_body_count > 0) {
		i32 frame = (i32)math_max(0, tab_timeline_selected_frame);
		while (sim_frame_count <= frame) {
			sim_world_step();
		}
		if (frame != sim_shown) {
			sim_world_show(frame);
		}
	}
	if (hand != NULL) {
		sim_release_meshes(hand);
	}
}

static sim_part_t *sim_part(i32 uid, i32 node_id) {
	for (i32 i = 0; i < sim_part_count; ++i) {
		if (sim_parts[i].uid == uid && sim_parts[i].node_id == node_id) {
			return &sim_parts[i];
		}
	}
	return NULL;
}

static sim_frame_t *sim_shown_frame() {
	i32 frame = (i32)math_max(0, tab_timeline_selected_frame);
	return frame < sim_frame_count ? &sim_frames[frame] : NULL;
}

// -1 when not simulated yet, free the positions
i32 sim_world_particles(i32 uid, i32 node_id, f32 **pos, physics_sim_t **grid) {
	sim_part_t  *p = sim_part(uid, node_id);
	sim_frame_t *f = sim_shown_frame();
	if (p == NULL || f == NULL || sim_grid == NULL) {
		return -1;
	}
	i32 owner = (i32)(p - sim_parts), count = 0;
	*pos = malloc(sizeof(f32) * 3 * f->count + 1);
	for (i32 i = 0; i < f->count; ++i) {
		if (f->owner[i] == owner) {
			memcpy(&(*pos)[count++ * 3], &f->pos[i * 3], sizeof(f32) * 3);
		}
	}
	*grid = sim_grid;
	return count;
}

// 0 - 255 per cell, NULL when not simulated yet
u8 *sim_world_gas(i32 uid, i32 node_id, physics_sim_t **grid) {
	sim_part_t  *p = sim_part(uid, node_id);
	sim_frame_t *f = sim_shown_frame();
	if (p == NULL || f == NULL || p->gas < 0 || f->gas == NULL) {
		return NULL;
	}
	*grid = sim_grid;
	return f->gas[p->gas];
}

// 0 - 255 per cell, NULL when not simulated yet
u8 *sim_world_flame(i32 uid, i32 node_id) {
	sim_part_t  *p = sim_part(uid, node_id);
	sim_frame_t *f = sim_shown_frame();
	if (p == NULL || f == NULL || p->gas < 0 || f->flame == NULL) {
		return NULL;
	}
	return f->flame[p->gas];
}

// Position, velocity and heat per spark, NULL when not simulated yet
f32 *sim_world_sparks(i32 uid, i32 node_id, i32 *count) {
	sim_part_t  *p = sim_part(uid, node_id);
	sim_frame_t *f = sim_shown_frame();
	if (p == NULL || f == NULL || p->type != SIM_FIRE || f->part == NULL || f->part[p - sim_parts] == NULL) {
		return NULL;
	}
	f32 *k = f->part[p - sim_parts];
	*count = (i32)k[0];
	return k + 1;
}

// NULL when not simulated yet
f32 *sim_world_cloth(i32 uid, i32 node_id, i32 **particle, i32 *vertex_count) {
	sim_part_t  *p = sim_part(uid, node_id);
	sim_frame_t *f = sim_shown_frame();
	if (p == NULL || f == NULL || p->cloth == NULL) {
		return NULL;
	}
	*particle     = p->particle;
	*vertex_count = p->vertex_count;
	return f->part[p - sim_parts];
}

// NULL when not simulated yet
mesh_fracture_t *sim_world_fracture(i32 uid, i32 node_id, f32 **poses) {
	sim_part_t  *p = sim_part(uid, node_id);
	sim_frame_t *f = sim_shown_frame();
	if (p == NULL || f == NULL || p->fracture == NULL) {
		return NULL;
	}
	*poses = f->part[p - sim_parts];
	return p->fracture;
}

static vec4_t *sim_export_loc = NULL;
static quat_t *sim_export_rot = NULL;

void sim_world_export_begin() {
	sim_export_loc = malloc(sizeof(vec4_t) * (sim_body_count + 1));
	sim_export_rot = malloc(sizeof(quat_t) * (sim_body_count + 1));
	for (i32 i = 0; i < sim_body_count; ++i) {
		mesh_object_t *mo = nodes_mesh_object(sim_bodies[i].uid);
		if (mo != NULL) {
			sim_export_loc[i] = mo->base->transform->loc;
			sim_export_rot[i] = mo->base->transform->rot;
			sim_body_place(mo->base, sim_bodies[i].loc, sim_bodies[i].rot);
		}
	}
}

void sim_world_export_end() {
	for (i32 i = 0; i < sim_body_count; ++i) {
		mesh_object_t *mo = nodes_mesh_object(sim_bodies[i].uid);
		if (mo != NULL) {
			sim_body_place(mo->base, sim_export_loc[i], sim_export_rot[i]);
		}
	}
	free(sim_export_loc);
	free(sim_export_rot);
	sim_export_loc = NULL;
	sim_export_rot = NULL;
}
