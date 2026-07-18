#include "asim.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GRAVITY       -9.81f
#define MAX_BVH_DEPTH 20
#define MAX_SPHERES   32

typedef struct {
	vec4_t min;
	vec4_t max;
} aabb_t;

typedef struct {
	vec4_t position;
	vec4_t velocity;
	float  radius;
	float  mass;
	int    active;
} sphere_t;

typedef struct {
	vec4_t v0;
	vec4_t v1;
	vec4_t v2;
	vec4_t normal;
	aabb_t bounds;
} triangle_t;

typedef struct bvh_node {
	aabb_t           bounds;
	struct bvh_node *left;
	struct bvh_node *right;
	triangle_t      *triangles;
	int              num_tris;
	int              is_leaf;
} bvh_node_t;

typedef struct {
	bvh_node_t *root;
} mesh_t;

static sphere_t       spheres[MAX_SPHERES];
static physics_pair_t ppairs[MAX_SPHERES];
static float          ppair_best_dist;
static mesh_t         mesh;
static aabb_t         root_bounds     = {{-10, -10, -10}, {10, 10, 10}};
static float          asim_bounciness = 0.0f;
static float          asim_friction   = 0.01f;
static float          asim_gravity_x  = 0.0f;
static float          asim_gravity_y  = 0.0f;
static float          asim_gravity_z  = -9.81f;

static inline aabb_t merge_aabbs(aabb_t a, aabb_t b) {
	return (aabb_t){.min = {fminf(a.min.x, b.min.x), fminf(a.min.y, b.min.y), fminf(a.min.z, b.min.z)},
	                .max = {fmaxf(a.max.x, b.max.x), fmaxf(a.max.y, b.max.y), fmaxf(a.max.z, b.max.z)}};
}

static inline int sphere_aabb_intersect(sphere_t *s, aabb_t *a) {
	float x  = fmaxf(a->min.x, fminf(s->position.x, a->max.x));
	float y  = fmaxf(a->min.y, fminf(s->position.y, a->max.y));
	float z  = fmaxf(a->min.z, fminf(s->position.z, a->max.z));
	float dx = x - s->position.x, dy = y - s->position.y, dz = z - s->position.z;
	return dx * dx + dy * dy + dz * dz <= s->radius * s->radius;
}

static int compare_triangles(const void *a, const void *b) {
	triangle_t *ta = (triangle_t *)a;
	triangle_t *tb = (triangle_t *)b;
	vec4_t      ca = vec4_mult(vec4_add(vec4_add(ta->v0, ta->v1), ta->v2), 1.0f / 3.0f);
	vec4_t      cb = vec4_mult(vec4_add(vec4_add(tb->v0, tb->v1), tb->v2), 1.0f / 3.0f);
	return (ca.x > cb.x) - (ca.x < cb.x);
}

static bvh_node_t *create_bvh_node(triangle_t *tris, int num_tris, int depth) {
	bvh_node_t *node = (bvh_node_t *)malloc(sizeof(bvh_node_t));

	*node = (bvh_node_t){.left = NULL, .right = NULL, .triangles = NULL, .num_tris = 0, .is_leaf = 1};

	if (num_tris <= 1 || depth >= MAX_BVH_DEPTH) {
		node->num_tris = num_tris;
		if (num_tris) {
			node->triangles = (triangle_t *)malloc(num_tris * sizeof(triangle_t));
			memcpy(node->triangles, tris, num_tris * sizeof(triangle_t));
			node->bounds = tris[0].bounds;
		}
		else {
			node->bounds = root_bounds;
		}
		return node;
	}

	qsort(tris, num_tris, sizeof(triangle_t), compare_triangles);

	int mid       = num_tris / 2;
	node->is_leaf = 0;
	node->left    = create_bvh_node(tris, mid, depth + 1);
	node->right   = create_bvh_node(tris + mid, num_tris - mid, depth + 1);
	node->bounds  = merge_aabbs(node->left->bounds, node->right->bounds);
	return node;
}

static void collide_sphere_triangle(sphere_t *s, int si, triangle_t *t) {
	if (!sphere_aabb_intersect(s, &t->bounds)) {
		return;
	}

	vec4_t to_sphere = vec4_sub(s->position, t->v0);
	float  dist      = vec4_dot(to_sphere, t->normal);
	if (dist < 0.0f || dist > s->radius) {
		return;
	}

	vec4_t p  = vec4_sub(s->position, vec4_mult(t->normal, dist));
	vec4_t e0 = vec4_sub(t->v1, t->v0), e1 = vec4_sub(t->v2, t->v1), e2 = vec4_sub(t->v0, t->v2);
	vec4_t c0 = vec4_sub(p, t->v0), c1 = vec4_sub(p, t->v1), c2 = vec4_sub(p, t->v2);

	if (vec4_dot(t->normal, vec4_cross(e0, c0)) >= 0 && vec4_dot(t->normal, vec4_cross(e1, c1)) >= 0 && vec4_dot(t->normal, vec4_cross(e2, c2)) >= 0) {

		float orig_dist = dist;

		s->position = vec4_add(s->position, vec4_mult(t->normal, s->radius - dist));

		float v_dot_n = vec4_dot(s->velocity, t->normal);
		if (v_dot_n < 0.0f) {
			vec4_t n_vel = vec4_mult(t->normal, v_dot_n);
			vec4_t t_vel = vec4_sub(s->velocity, n_vel);
			s->velocity  = vec4_add(vec4_mult(n_vel, -asim_bounciness), vec4_mult(t_vel, 1.0f - asim_friction));
		}

		vec4_t contact_point = vec4_sub(s->position, vec4_mult(t->normal, s->radius));
		if (orig_dist < ppair_best_dist) {
			ppair_best_dist    = orig_dist;
			ppairs[si].pos_a_x = contact_point.x;
			ppairs[si].pos_a_y = contact_point.y;
			ppairs[si].pos_a_z = contact_point.z;
			ppairs[si].nor_x   = t->normal.x;
			ppairs[si].nor_y   = t->normal.y;
			ppairs[si].nor_z   = t->normal.z;
		}
	}
}

static void query_bvh(sphere_t *s, int si, bvh_node_t *n) {
	if (!n || !sphere_aabb_intersect(s, &n->bounds)) {
		return;
	}

	if (n->is_leaf) {
		for (int i = 0; i < n->num_tris; i++) {
			collide_sphere_triangle(s, si, &n->triangles[i]);
		}
	}
	else {
		query_bvh(s, si, n->left);
		query_bvh(s, si, n->right);
	}
}

static void free_bvh(bvh_node_t *n) {
	if (!n) {
		return;
	}
	if (n->is_leaf) {
		free(n->triangles);
	}
	else {
		free_bvh(n->left);
		free_bvh(n->right);
	}
	free(n);
}

void asim_world_create() {
	memset(spheres, 0, sizeof(spheres));
	memset(ppairs, 0, sizeof(ppairs));
}

void asim_world_destroy() {
	free_bvh(mesh.root);
	mesh.root = NULL;
}

void asim_world_update(float time_step) {
	const int sub_steps = 8;
	float     dt        = time_step / sub_steps;

	for (int s = 0; s < MAX_SPHERES; s++) {
		if (!spheres[s].active) {
			continue;
		}
		ppairs[s].pos_a_x = 0;
		ppairs[s].pos_a_y = 0;
		ppairs[s].pos_a_z = 0;
		ppairs[s].nor_x   = 0;
		ppairs[s].nor_y   = 0;
		ppairs[s].nor_z   = 0;
	}

	for (int step = 0; step < sub_steps; step++) {
		// Sphere-mesh collision
		for (int s = 0; s < MAX_SPHERES; s++) {
			if (!spheres[s].active || spheres[s].mass == 0.0f) {
				continue;
			}
			ppair_best_dist = spheres[s].radius;
			spheres[s].velocity.x += asim_gravity_x * dt;
			spheres[s].velocity.y += asim_gravity_y * dt;
			spheres[s].velocity.z += asim_gravity_z * dt;
			spheres[s].position = vec4_add(spheres[s].position, vec4_mult(spheres[s].velocity, dt));
			query_bvh(&spheres[s], s, mesh.root);
		}

		// Sphere-sphere collision
		for (int i = 0; i < MAX_SPHERES; i++) {
			if (!spheres[i].active) {
				continue;
			}
			for (int j = i + 1; j < MAX_SPHERES; j++) {
				if (!spheres[j].active) {
					continue;
				}
				vec4_t delta    = vec4_sub(spheres[i].position, spheres[j].position);
				float  dist     = vec4_len(delta);
				float  min_dist = spheres[i].radius + spheres[j].radius;
				if (dist >= min_dist || dist < 0.0001f) {
					continue;
				}
				float inv_i   = spheres[i].mass > 0.0f ? 1.0f / spheres[i].mass : 0.0f;
				float inv_j   = spheres[j].mass > 0.0f ? 1.0f / spheres[j].mass : 0.0f;
				float inv_sum = inv_i + inv_j;
				if (inv_sum <= 0.0f) { // Both static
					continue;
				}
				vec4_t n            = vec4_mult(delta, 1.0f / dist);
				float  overlap      = min_dist - dist;
				spheres[i].position = vec4_add(spheres[i].position, vec4_mult(n, overlap * inv_i / inv_sum));
				spheres[j].position = vec4_sub(spheres[j].position, vec4_mult(n, overlap * inv_j / inv_sum));
				float vi_n          = vec4_dot(spheres[i].velocity, n);
				float vj_n          = vec4_dot(spheres[j].velocity, n);
				if (vi_n - vj_n < 0.0f) {
					float restitution   = 0.3f;
					float impulse       = -(1.0f + restitution) * (vi_n - vj_n) / inv_sum;
					spheres[i].velocity = vec4_add(spheres[i].velocity, vec4_mult(n, impulse * inv_i));
					spheres[j].velocity = vec4_sub(spheres[j].velocity, vec4_mult(n, impulse * inv_j));
				}
			}
		}
	}
}

physics_pair_t *asim_world_get_contact(void *body) {
	int slot = (int)(uintptr_t)body;
	return &ppairs[slot];
}

void *asim_body_create(int shape, float mass, float dimx, float dimy, float dimz, float x, float y, float z, void *posa, void *inda, float scale_pos) {

	if (shape == 1) { // SPHERE
		int slot = -1;
		for (int i = 0; i < MAX_SPHERES; i++) {
			if (!spheres[i].active) {
				slot = i;
				break;
			}
		}
		if (slot < 0) {
			return NULL;
		}

		spheres[slot].position.x = x;
		spheres[slot].position.y = y;
		spheres[slot].position.z = z;
		spheres[slot].velocity.x = 0;
		spheres[slot].velocity.y = 0;
		spheres[slot].velocity.z = 0;
		spheres[slot].radius     = dimx / 2.0f;
		spheres[slot].mass       = mass;
		spheres[slot].active     = 1;

		return (void *)(uintptr_t)slot;
	}

	i16_array_t *pa       = posa;
	u32_array_t *ia       = inda;
	int          num_tris = ia->length / 3;
	triangle_t  *tris     = (triangle_t *)malloc(num_tris * sizeof(triangle_t));
	float        scale    = (1.0 / 32767.0) * scale_pos;

	for (int i = 0; i < num_tris; i++) {
		tris[i].v0 =
		    (vec4_t){pa->buffer[ia->buffer[i * 3] * 4] * scale, pa->buffer[ia->buffer[i * 3] * 4 + 1] * scale, pa->buffer[ia->buffer[i * 3] * 4 + 2] * scale};
		tris[i].v1 = (vec4_t){pa->buffer[ia->buffer[i * 3 + 1] * 4] * scale, pa->buffer[ia->buffer[i * 3 + 1] * 4 + 1] * scale,
		                      pa->buffer[ia->buffer[i * 3 + 1] * 4 + 2] * scale};
		tris[i].v2 = (vec4_t){pa->buffer[ia->buffer[i * 3 + 2] * 4] * scale, pa->buffer[ia->buffer[i * 3 + 2] * 4 + 1] * scale,
		                      pa->buffer[ia->buffer[i * 3 + 2] * 4 + 2] * scale};

		vec4_t edge1   = vec4_sub(tris[i].v1, tris[i].v0);
		vec4_t edge2   = vec4_sub(tris[i].v2, tris[i].v0);
		tris[i].normal = vec4_mult(vec4_cross(edge1, edge2), 1.0f / vec4_len(vec4_cross(edge1, edge2)));

		tris[i].bounds = (aabb_t){.min = {fminf(fminf(tris[i].v0.x, tris[i].v1.x), tris[i].v2.x), fminf(fminf(tris[i].v0.y, tris[i].v1.y), tris[i].v2.y),
		                                  fminf(fminf(tris[i].v0.z, tris[i].v1.z), tris[i].v2.z)},
		                          .max = {fmaxf(fmaxf(tris[i].v0.x, tris[i].v1.x), tris[i].v2.x), fmaxf(fmaxf(tris[i].v0.y, tris[i].v1.y), tris[i].v2.y),
		                                  fmaxf(fmaxf(tris[i].v0.z, tris[i].v1.z), tris[i].v2.z)}};
	}

	mesh.root = create_bvh_node(tris, num_tris, 0);
	free(tris);

	return NULL;
}

void asim_body_set_mass(void *body, float mass) {
	int slot           = (int)(uintptr_t)body;
	spheres[slot].mass = mass;
}

void asim_body_apply_impulse(void *body, float x, float y, float z) {
	int slot = (int)(uintptr_t)body;
	spheres[slot].velocity.x += x;
	spheres[slot].velocity.y += y;
	spheres[slot].velocity.z += z;
}

void asim_body_get_pos(void *body, vec4_t *pos) {
	int slot = (int)(uintptr_t)body;
	pos->x   = spheres[slot].position.x;
	pos->y   = spheres[slot].position.y;
	pos->z   = spheres[slot].position.z;
}

void asim_body_get_rot(void *body, quat_t *rot) {}

void asim_body_sync_transform(void *body, vec4_t pos, quat_t rot) {
	int slot                 = (int)(uintptr_t)body;
	spheres[slot].position.x = pos.x;
	spheres[slot].position.y = pos.y;
	spheres[slot].position.z = pos.z;
}

void asim_body_remove(void *body) {
	int slot = (int)(uintptr_t)body;
	if (slot >= 0 && slot < MAX_SPHERES) {
		spheres[slot].active = 0;
	}
}

float asim_body_get_speed(void *body) {
	int slot = (int)(uintptr_t)body;
	return sqrtf(spheres[slot].velocity.x * spheres[slot].velocity.x + spheres[slot].velocity.y * spheres[slot].velocity.y +
	             spheres[slot].velocity.z * spheres[slot].velocity.z);
}

void asim_set_friction(float v) {
	asim_friction = v * 0.1f;
}

void asim_set_bounciness(float v) {
	asim_bounciness = v;
}

void asim_set_gravity(float x, float y, float z) {
	asim_gravity_x = x;
	asim_gravity_y = y;
	asim_gravity_z = z;
}
