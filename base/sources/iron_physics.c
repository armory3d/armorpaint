#ifdef WITH_PHYSICS

#include "iron_physics.h"
#include "engine.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GRAVITY       -9.81f
#define MAX_BVH_DEPTH 20
#define MAX_SPHERES   32
#define MAX_BOXES     32
#define MAX_BODIES    (MAX_SPHERES + MAX_BOXES)

#define SPHERE_TAG  0x10000
#define BOX_TAG     0x20000
#define TERRAIN_TAG 0x40000
#define SLOT_MASK   0xffff

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
	vec4_t position;
	vec4_t velocity;
	quat_t rotation;
	vec4_t angular;
	vec4_t half;
	float  mass;
	float  inv_inertia;
	int    active;
} box_t;

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

typedef struct {
	float *heights;
	int    res_x;
	int    res_y;
	vec4_t min;
	float  size_x;
	float  size_y;
	int    active;
} terrain_t;

static sphere_t    spheres[MAX_SPHERES];
static box_t       boxes[MAX_BOXES];
static asim_pair_t pairs[MAX_BODIES];
static float       pair_best[MAX_BODIES];
static asim_pair_t null_pair;
static mesh_t      mesh;
static terrain_t   terrain;
static aabb_t      root_bounds     = {{-10, -10, -10}, {10, 10, 10}};
static float       asim_bounciness = 0.0f;
static float       asim_friction   = 0.01f;
static vec4_t      asim_gravity    = {0.0f, 0.0f, GRAVITY};

static inline int box_pair(int slot) {
	return MAX_SPHERES + slot;
}

static inline aabb_t merge_aabbs(aabb_t a, aabb_t b) {
	return (aabb_t){.min = {fminf(a.min.x, b.min.x), fminf(a.min.y, b.min.y), fminf(a.min.z, b.min.z)},
	                .max = {fmaxf(a.max.x, b.max.x), fmaxf(a.max.y, b.max.y), fmaxf(a.max.z, b.max.z)}};
}

static inline float min3(float a, float b, float c) {
	return fminf(a, fminf(b, c));
}

static inline float max3(float a, float b, float c) {
	return fmaxf(a, fmaxf(b, c));
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
	*node            = (bvh_node_t){.is_leaf = 1};

	if (num_tris <= 1 || depth >= MAX_BVH_DEPTH) {
		node->num_tris = num_tris;
		node->bounds   = root_bounds;
		if (num_tris) {
			node->triangles = (triangle_t *)malloc(num_tris * sizeof(triangle_t));
			memcpy(node->triangles, tris, num_tris * sizeof(triangle_t));

			// A leaf at the depth limit can hold many triangles, so cover them all
			node->bounds = tris[0].bounds;
			for (int i = 1; i < num_tris; i++) {
				node->bounds = merge_aabbs(node->bounds, tris[i].bounds);
			}
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

static void report_contact(int index, float depth, vec4_t point, vec4_t normal) {
	if (depth <= pair_best[index]) {
		return;
	}
	pair_best[index] = depth;
	pairs[index]     = (asim_pair_t){point.x, point.y, point.z, normal.x, normal.y, normal.z};
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
		report_contact(si, s->radius - orig_dist, contact_point, t->normal);
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

static inline int body_is_sphere(void *body) {
	return ((uintptr_t)body & SPHERE_TAG) != 0;
}

static inline int body_is_box(void *body) {
	return ((uintptr_t)body & BOX_TAG) != 0;
}

static inline int body_is_terrain(void *body) {
	return ((uintptr_t)body & TERRAIN_TAG) != 0;
}

static inline int body_slot(void *body) {
	return (int)((uintptr_t)body & SLOT_MASK);
}

#define CONTACT_FRICTION   0.6f
#define ANGULAR_DAMPING    0.4f
#define SPHERE_RESTITUTION 0.3f

typedef struct {
	vec4_t *position;
	vec4_t *velocity;
	vec4_t *angular;
	float   inv_mass;
	float   inv_inertia;
} rigid_t;

static vec4_t rigid_zero;

static rigid_t rigid_static(void) {
	return (rigid_t){&rigid_zero, &rigid_zero, &rigid_zero, 0.0f, 0.0f};
}

static rigid_t rigid_box(box_t *b) {
	return (rigid_t){&b->position, &b->velocity, &b->angular, b->mass > 0.0f ? 1.0f / b->mass : 0.0f, b->inv_inertia};
}

static rigid_t rigid_sphere(sphere_t *s) {
	return (rigid_t){&s->position, &s->velocity, &rigid_zero, s->mass > 0.0f ? 1.0f / s->mass : 0.0f, 0.0f};
}

static void apply_impulse(rigid_t body, vec4_t r, vec4_t impulse) {
	if (body.inv_mass > 0.0f) {
		*body.velocity = vec4_add(*body.velocity, vec4_mult(impulse, body.inv_mass));
	}
	if (body.inv_inertia > 0.0f) {
		*body.angular = vec4_add(*body.angular, vec4_mult(vec4_cross(r, impulse), body.inv_inertia));
	}
}

static void resolve_contact_pair(rigid_t a, rigid_t b, vec4_t point, vec4_t n, float depth) {
	float inv_sum = a.inv_mass + b.inv_mass;
	if (inv_sum <= 0.0f) { // Both static
		return;
	}

	const float correction = 0.6f;
	if (a.inv_mass > 0.0f) {
		*a.position = vec4_add(*a.position, vec4_mult(n, depth * correction * a.inv_mass / inv_sum));
	}
	if (b.inv_mass > 0.0f) {
		*b.position = vec4_sub(*b.position, vec4_mult(n, depth * correction * b.inv_mass / inv_sum));
	}

	vec4_t ra      = vec4_sub(point, *a.position);
	vec4_t rb      = vec4_sub(point, *b.position);
	vec4_t rel     = vec4_sub(vec4_add(*a.velocity, vec4_cross(*a.angular, ra)), vec4_add(*b.velocity, vec4_cross(*b.angular, rb)));
	float  closing = vec4_dot(rel, n);
	if (closing >= 0.0f) { // Already moving apart
		return;
	}

	vec4_t ran     = vec4_cross(ra, n);
	vec4_t rbn     = vec4_cross(rb, n);
	float  denom   = inv_sum + a.inv_inertia * vec4_dot(ran, ran) + b.inv_inertia * vec4_dot(rbn, rbn);
	float  impulse = -(1.0f + asim_bounciness) * closing / denom;
	apply_impulse(a, ra, vec4_mult(n, impulse));
	apply_impulse(b, rb, vec4_mult(n, -impulse));

	vec4_t tangent = vec4_sub(rel, vec4_mult(n, closing));
	float  sliding = vec4_len(tangent);
	if (sliding < 0.0001f) {
		return;
	}
	tangent = vec4_mult(tangent, 1.0f / sliding);

	vec4_t rat     = vec4_cross(ra, tangent);
	vec4_t rbt     = vec4_cross(rb, tangent);
	float  denom_t = inv_sum + a.inv_inertia * vec4_dot(rat, rat) + b.inv_inertia * vec4_dot(rbt, rbt);
	float  stop    = sliding / denom_t;
	float  limit   = CONTACT_FRICTION * impulse;
	if (stop > limit) {
		stop = limit;
	}
	apply_impulse(a, ra, vec4_mult(tangent, -stop));
	apply_impulse(b, rb, vec4_mult(tangent, stop));
}

static inline quat_t quat_conj(quat_t q) {
	return (quat_t){-q.x, -q.y, -q.z, q.w};
}

static vec4_t box_corner(box_t *b, int c) {
	vec4_t local = {(c & 1) ? b->half.x : -b->half.x, (c & 2) ? b->half.y : -b->half.y, (c & 4) ? b->half.z : -b->half.z};
	return vec4_add(b->position, vec4_apply_quat(local, b->rotation));
}

static int box_point_depth(box_t *b, vec4_t point, vec4_t *out_normal, float *out_depth) {
	vec4_t local = vec4_apply_quat(vec4_sub(point, b->position), quat_conj(b->rotation));

	float dx = b->half.x - fabsf(local.x);
	float dy = b->half.y - fabsf(local.y);
	float dz = b->half.z - fabsf(local.z);
	if (dx <= 0.0f || dy <= 0.0f || dz <= 0.0f) {
		return 0;
	}

	vec4_t axis = {0.0f, 0.0f, 0.0f};
	if (dx <= dy && dx <= dz) {
		axis.x     = local.x < 0.0f ? -1.0f : 1.0f;
		*out_depth = dx;
	}
	else if (dy <= dz) {
		axis.y     = local.y < 0.0f ? -1.0f : 1.0f;
		*out_depth = dy;
	}
	else {
		axis.z     = local.z < 0.0f ? -1.0f : 1.0f;
		*out_depth = dz;
	}
	*out_normal = vec4_apply_quat(axis, b->rotation);
	return 1;
}

static vec4_t box_closest_point(box_t *b, vec4_t point) {
	vec4_t local = vec4_apply_quat(vec4_sub(point, b->position), quat_conj(b->rotation));
	local.x      = fmaxf(-b->half.x, fminf(local.x, b->half.x));
	local.y      = fmaxf(-b->half.y, fminf(local.y, b->half.y));
	local.z      = fmaxf(-b->half.z, fminf(local.z, b->half.z));
	return vec4_add(b->position, vec4_apply_quat(local, b->rotation));
}

static void collide_box_corners(int ai, int bi) {
	box_t *a = &boxes[ai];
	box_t *b = &boxes[bi];

	for (int c = 0; c < 8; c++) {
		vec4_t corner = box_corner(a, c);
		vec4_t n;
		float  depth;
		if (box_point_depth(b, corner, &n, &depth)) {
			resolve_contact_pair(rigid_box(a), rigid_box(b), corner, n, depth);
			report_contact(box_pair(ai), depth, corner, n);
			report_contact(box_pair(bi), depth, corner, vec4_mult(n, -1.0f));
		}
	}
}

static void collide_box_box(int i, int j) {
	box_t *a = &boxes[i];
	box_t *b = &boxes[j];
	if (a->mass == 0.0f && b->mass == 0.0f) {
		return;
	}
	if (vec4_len(vec4_sub(b->position, a->position)) > vec4_len(a->half) + vec4_len(b->half)) {
		return;
	}

	collide_box_corners(i, j);
	collide_box_corners(j, i);
}

static void collide_box_sphere(int bi, int si) {
	box_t    *b = &boxes[bi];
	sphere_t *s = &spheres[si];

	vec4_t closest = box_closest_point(b, s->position);
	vec4_t delta   = vec4_sub(s->position, closest);
	float  dist    = vec4_len(delta);

	vec4_t n; // Points from the box towards the sphere
	float  depth;
	if (dist > 0.0001f) {
		if (dist >= s->radius) {
			return;
		}
		n     = vec4_mult(delta, 1.0f / dist);
		depth = s->radius - dist;
	}
	else { // Center is inside the box
		if (!box_point_depth(b, s->position, &n, &depth)) {
			return;
		}
		depth += s->radius;
	}

	vec4_t point = vec4_sub(s->position, vec4_mult(n, s->radius));
	resolve_contact_pair(rigid_sphere(s), rigid_box(b), point, n, depth);
	report_contact(box_pair(bi), depth, point, vec4_mult(n, -1.0f));
}

static void collide_sphere_sphere(int i, int j) {
	sphere_t *a = &spheres[i];
	sphere_t *b = &spheres[j];

	vec4_t delta    = vec4_sub(a->position, b->position);
	float  dist     = vec4_len(delta);
	float  min_dist = a->radius + b->radius;
	if (dist >= min_dist || dist < 0.0001f) {
		return;
	}

	float inv_a   = a->mass > 0.0f ? 1.0f / a->mass : 0.0f;
	float inv_b   = b->mass > 0.0f ? 1.0f / b->mass : 0.0f;
	float inv_sum = inv_a + inv_b;
	if (inv_sum <= 0.0f) { // Both static
		return;
	}

	vec4_t n       = vec4_mult(delta, 1.0f / dist);
	float  overlap = min_dist - dist;
	a->position    = vec4_add(a->position, vec4_mult(n, overlap * inv_a / inv_sum));
	b->position    = vec4_sub(b->position, vec4_mult(n, overlap * inv_b / inv_sum));

	float closing = vec4_dot(a->velocity, n) - vec4_dot(b->velocity, n);
	if (closing < 0.0f) {
		float impulse = -(1.0f + SPHERE_RESTITUTION) * closing / inv_sum;
		a->velocity   = vec4_add(a->velocity, vec4_mult(n, impulse * inv_a));
		b->velocity   = vec4_sub(b->velocity, vec4_mult(n, impulse * inv_b));
	}
}

static int terrain_sample(float x, float y, float *out_height, vec4_t *out_normal) {
	if (!terrain.active) {
		return 0;
	}

	float cell_x = terrain.size_x / (terrain.res_x - 1);
	float cell_y = terrain.size_y / (terrain.res_y - 1);
	float fx     = (x - terrain.min.x) / cell_x;
	float fy     = (y - terrain.min.y) / cell_y;
	if (fx < 0.0f || fy < 0.0f || fx > terrain.res_x - 1 || fy > terrain.res_y - 1) {
		return 0;
	}

	int ix = (int)fx;
	int iy = (int)fy;
	if (ix > terrain.res_x - 2) {
		ix = terrain.res_x - 2;
	}
	if (iy > terrain.res_y - 2) {
		iy = terrain.res_y - 2;
	}
	float tx = fx - ix;
	float ty = fy - iy;

	float h00 = terrain.heights[iy * terrain.res_x + ix];
	float h10 = terrain.heights[iy * terrain.res_x + ix + 1];
	float h01 = terrain.heights[(iy + 1) * terrain.res_x + ix];
	float h11 = terrain.heights[(iy + 1) * terrain.res_x + ix + 1];

	float h0    = h00 + (h10 - h00) * tx;
	float h1    = h01 + (h11 - h01) * tx;
	*out_height = terrain.min.z + h0 + (h1 - h0) * ty;

	// Slope of the cell
	float  dx   = ((h10 - h00) + (h11 - h01)) * 0.5f / cell_x;
	float  dy   = ((h01 - h00) + (h11 - h10)) * 0.5f / cell_y;
	vec4_t n    = {-dx, -dy, 1.0f};
	*out_normal = vec4_mult(n, 1.0f / vec4_len(n));
	return 1;
}

static inline float depth_along_normal(float drop, vec4_t normal) {
	return drop * normal.z;
}

static void collide_box_terrain(int bi) {
	box_t *b = &boxes[bi];
	if (b->mass == 0.0f) {
		return;
	}

	for (int c = 0; c < 8; c++) {
		vec4_t corner = box_corner(b, c);
		float  ground;
		vec4_t normal;
		if (!terrain_sample(corner.x, corner.y, &ground, &normal)) {
			continue;
		}
		float drop = ground - corner.z;
		if (drop <= 0.0f) {
			continue;
		}

		float depth = depth_along_normal(drop, normal);
		resolve_contact_pair(rigid_box(b), rigid_static(), corner, normal, depth);
		report_contact(box_pair(bi), depth, corner, normal);
	}
}

static void collide_sphere_terrain(int si) {
	sphere_t *s = &spheres[si];
	if (s->mass == 0.0f) {
		return;
	}

	float  ground;
	vec4_t normal;
	if (!terrain_sample(s->position.x, s->position.y, &ground, &normal)) {
		return;
	}

	float drop = ground - (s->position.z - s->radius);
	if (drop <= 0.0f) {
		return;
	}

	float  depth = depth_along_normal(drop, normal);
	vec4_t point = {s->position.x, s->position.y, ground};
	resolve_contact_pair(rigid_sphere(s), rigid_static(), point, normal, depth);
	report_contact(si, depth, point, normal);
}

static void terrain_clear() {
	free(terrain.heights);
	memset(&terrain, 0, sizeof(terrain));
}

void asim_world_create() {
	asim_world_destroy();
	memset(spheres, 0, sizeof(spheres));
	memset(boxes, 0, sizeof(boxes));
	memset(pairs, 0, sizeof(pairs));
}

void asim_world_destroy() {
	free_bvh(mesh.root);
	mesh.root = NULL;
	terrain_clear();
}

void asim_world_update() {
	const int sub_steps = 8;
	float     dt        = sys_delta() / sub_steps;

	memset(pairs, 0, sizeof(pairs));
	memset(pair_best, 0, sizeof(pair_best));

	for (int step = 0; step < sub_steps; step++) {
		// Sphere-mesh collision
		for (int i = 0; i < MAX_SPHERES; i++) {
			sphere_t *s = &spheres[i];
			if (!s->active || s->mass == 0.0f) {
				continue;
			}
			s->velocity = vec4_add(s->velocity, vec4_mult(asim_gravity, dt));
			s->position = vec4_add(s->position, vec4_mult(s->velocity, dt));
			query_bvh(s, i, mesh.root);
			collide_sphere_terrain(i);
		}

		// Sphere-sphere collision
		for (int i = 0; i < MAX_SPHERES; i++) {
			if (!spheres[i].active) {
				continue;
			}
			for (int j = i + 1; j < MAX_SPHERES; j++) {
				if (spheres[j].active) {
					collide_sphere_sphere(i, j);
				}
			}
		}

		for (int i = 0; i < MAX_BOXES; i++) {
			box_t *b = &boxes[i];
			if (!b->active || b->mass == 0.0f) {
				continue;
			}
			b->velocity = vec4_add(b->velocity, vec4_mult(asim_gravity, dt));
			b->position = vec4_add(b->position, vec4_mult(b->velocity, dt));

			// Turn the orientation by the angular velocity
			quat_t spin = {b->angular.x, b->angular.y, b->angular.z, 0.0f};
			quat_t dq   = quat_mult(spin, b->rotation);
			b->rotation.x += dq.x * 0.5f * dt;
			b->rotation.y += dq.y * 0.5f * dt;
			b->rotation.z += dq.z * 0.5f * dt;
			b->rotation.w += dq.w * 0.5f * dt;
			b->rotation = quat_norm(b->rotation);

			// Bleed off spin, so a box that has come to rest stops twitching
			b->angular = vec4_mult(b->angular, 1.0f - fminf(1.0f, ANGULAR_DAMPING * dt));

			collide_box_terrain(i);
		}

		// Box-box collision
		for (int i = 0; i < MAX_BOXES; i++) {
			if (!boxes[i].active) {
				continue;
			}
			for (int j = i + 1; j < MAX_BOXES; j++) {
				if (boxes[j].active) {
					collide_box_box(i, j);
				}
			}
		}

		// Box-sphere collision
		for (int i = 0; i < MAX_BOXES; i++) {
			if (!boxes[i].active) {
				continue;
			}
			for (int j = 0; j < MAX_SPHERES; j++) {
				if (spheres[j].active) {
					collide_box_sphere(i, j);
				}
			}
		}
	}

	// Write the simulated state back into the scene
	for (int i = 0; i < scene_meshes->length; i++) {
		mesh_object_t *mo   = scene_meshes->buffer[i];
		asim_body_t   *body = mo->base->_->body;
		if (body != NULL) {
			asim_body_update(body);
		}
	}
}

asim_pair_t *asim_world_get_contact(void *body) {
	int slot = body_slot(body);
	if (body_is_box(body)) {
		return &pairs[box_pair(slot)];
	}
	if (body_is_sphere(body)) {
		return &pairs[slot];
	}
	return &null_pair;
}

asim_pair_t_array_t *asim_get_contact_pairs(asim_body_t *body) {
	asim_pair_t_array_t *result = any_array_create_from_raw((void *[]){}, 0);
	asim_pair_t         *p      = asim_world_get_contact(body->_body);
	if (p->pos_a_x != 0 || p->pos_a_y != 0 || p->pos_a_z != 0) {
		any_array_push(result, p);
	}
	return result;
}

static inline vec4_t mesh_vertex(i16_array_t *pa, uint32_t index, float scale) {
	return (vec4_t){pa->buffer[index * 4] * scale, pa->buffer[index * 4 + 1] * scale, pa->buffer[index * 4 + 2] * scale};
}

static void *body_create(int shape, float mass, float dimx, float dimy, float dimz, float x, float y, float z, void *posa, void *inda, float scale_pos) {

	if (shape == ASIM_SHAPE_TERRAIN) {
		asim_heightfield_t *field = posa;
		if (field == NULL || field->heights == NULL || field->res_x < 2 || field->res_y < 2) {
			return NULL;
		}

		int    count   = field->res_x * field->res_y;
		float *heights = (float *)malloc(sizeof(float) * count);
		for (int i = 0; i < count; i++) {
			heights[i] = field->heights[i] * dimz;
		}

		terrain_clear();
		terrain = (terrain_t){.heights = heights,
		                      .res_x   = field->res_x,
		                      .res_y   = field->res_y,
		                      .min     = {x - dimx / 2.0f, y - dimy / 2.0f, z},
		                      .size_x  = dimx,
		                      .size_y  = dimy,
		                      .active  = 1};

		return (void *)(uintptr_t)TERRAIN_TAG;
	}

	if (shape == ASIM_SHAPE_BOX && posa == NULL) {
		int slot = 0;
		while (slot < MAX_BOXES && boxes[slot].active) {
			slot++;
		}
		if (slot == MAX_BOXES) {
			return NULL;
		}

		vec4_t half    = {dimx / 2.0f, dimy / 2.0f, dimz / 2.0f};
		float  inertia = mass / 3.0f * (half.x * half.x + half.y * half.y + half.z * half.z) * (2.0f / 3.0f);

		boxes[slot] = (box_t){.position    = {x, y, z},
		                      .rotation    = {0.0f, 0.0f, 0.0f, 1.0f},
		                      .half        = half,
		                      .mass        = mass,
		                      .inv_inertia = mass > 0.0f && inertia > 0.0f ? 1.0f / inertia : 0.0f,
		                      .active      = 1};

		return (void *)(uintptr_t)(slot | BOX_TAG);
	}

	if (shape == ASIM_SHAPE_SPHERE) {
		int slot = 0;
		while (slot < MAX_SPHERES && spheres[slot].active) {
			slot++;
		}
		if (slot == MAX_SPHERES) {
			return NULL;
		}

		spheres[slot] = (sphere_t){.position = {x, y, z}, .radius = dimx / 2.0f, .mass = mass, .active = 1};

		return (void *)(uintptr_t)(slot | SPHERE_TAG);
	}

	i16_array_t *pa       = posa;
	u32_array_t *ia       = inda;
	int          num_tris = ia->length / 3;
	triangle_t  *tris     = (triangle_t *)malloc(num_tris * sizeof(triangle_t));
	float        scale    = (1.0 / 32767.0) * scale_pos;

	for (int i = 0; i < num_tris; i++) {
		vec4_t v0 = mesh_vertex(pa, ia->buffer[i * 3], scale);
		vec4_t v1 = mesh_vertex(pa, ia->buffer[i * 3 + 1], scale);
		vec4_t v2 = mesh_vertex(pa, ia->buffer[i * 3 + 2], scale);

		vec4_t cross = vec4_cross(vec4_sub(v1, v0), vec4_sub(v2, v0));

		tris[i].v0     = v0;
		tris[i].v1     = v1;
		tris[i].v2     = v2;
		tris[i].normal = vec4_mult(cross, 1.0f / vec4_len(cross));
		tris[i].bounds = (aabb_t){.min = {min3(v0.x, v1.x, v2.x), min3(v0.y, v1.y, v2.y), min3(v0.z, v1.z, v2.z)},
		                          .max = {max3(v0.x, v1.x, v2.x), max3(v0.y, v1.y, v2.y), max3(v0.z, v1.z, v2.z)}};
	}

	mesh.root = create_bvh_node(tris, num_tris, 0);
	free(tris);

	return NULL;
}

static box_t null_body;

typedef struct {
	vec4_t *position;
	vec4_t *velocity;
	float  *mass;
	int    *active;
} body_ref_t;

static body_ref_t body_ref(void *body) {
	int slot = body_slot(body);
	if (body_is_box(body) && slot < MAX_BOXES) {
		box_t *b = &boxes[slot];
		return (body_ref_t){&b->position, &b->velocity, &b->mass, &b->active};
	}
	if (body_is_sphere(body) && slot < MAX_SPHERES) {
		sphere_t *s = &spheres[slot];
		return (body_ref_t){&s->position, &s->velocity, &s->mass, &s->active};
	}
	return (body_ref_t){&null_body.position, &null_body.velocity, &null_body.mass, &null_body.active};
}

asim_body_t *asim_body_create(object_t *obj, asim_shape_t shape, float mass) {
	asim_body_t *body = GC_ALLOC_INIT(asim_body_t, {0});
	body->shape       = shape;
	body->mass        = mass;
	body->obj         = obj;
	obj->_->body      = body;

	transform_compute_dim(obj->transform);
	body->dimx = obj->transform->dim.x;
	body->dimy = obj->transform->dim.y;
	body->dimz = obj->transform->dim.z;

	float        scale_pos = 1.0f;
	i16_array_t *posa      = NULL;
	u32_array_t *inda      = NULL;

	if (shape == ASIM_SHAPE_MESH || shape == ASIM_SHAPE_TERRAIN) {
		mesh_object_t *mo    = obj->ext;
		mesh_data_t   *data  = mo->data;
		vec4_t         scale = obj->transform->scale;

		if (obj->parent != NULL) {
			scale.x *= obj->parent->transform->scale.x;
			scale.y *= obj->parent->transform->scale.y;
			scale.z *= obj->parent->transform->scale.z;
		}

		posa      = mesh_data_get_vertex_array(data, "pos")->values;
		inda      = data->index_array;
		scale_pos = scale.x * data->scale_pos;
	}

	vec4_t loc  = obj->transform->loc;
	body->_body = body_create(shape, mass, body->dimx, body->dimy, body->dimz, loc.x, loc.y, loc.z, posa, inda, scale_pos);
	return body;
}

void asim_body_set_mass(asim_body_t *body, float mass) {
	body->mass                  = mass;
	*body_ref(body->_body).mass = mass;
}

void asim_body_apply_impulse(void *body, vec4_t impulse) {
	vec4_t *vel = body_ref(body).velocity;
	vel->x += impulse.x;
	vel->y += impulse.y;
	vel->z += impulse.z;
}

void asim_body_get_pos(void *body, vec4_t *pos) {
	vec4_t *p = body_ref(body).position;
	pos->x    = p->x;
	pos->y    = p->y;
	pos->z    = p->z;
}

void asim_body_get_rot(void *body, quat_t *rot) {
	if (body_is_box(body)) {
		*rot = boxes[body_slot(body)].rotation;
	}
}

void asim_body_get_velocity(void *body, vec4_t *vel) {
	vec4_t *v = body_ref(body).velocity;
	vel->x    = v->x;
	vel->y    = v->y;
	vel->z    = v->z;
}

void asim_body_set_velocity(void *body, float x, float y, float z) {
	vec4_t *vel = body_ref(body).velocity;
	vel->x      = x;
	vel->y      = y;
	vel->z      = z;
}

void asim_body_sync_transform(asim_body_t *body) {
	transform_t *transform = body->obj->transform;
	vec4_t      *p         = body_ref(body->_body).position;
	p->x                   = transform->loc.x;
	p->y                   = transform->loc.y;
	p->z                   = transform->loc.z;
	if (body_is_box(body->_body)) {
		boxes[body_slot(body->_body)].rotation = transform->rot;
	}
}

void asim_body_update(asim_body_t *body) {
	if (body->shape == ASIM_SHAPE_MESH) {
		return; ////
	}

	transform_t *transform = body->obj->transform;
	asim_body_get_pos(body->_body, &transform->loc);
	asim_body_get_rot(body->_body, &transform->rot);
	transform_build_matrix(transform);
}

void asim_body_remove(asim_body_t *body) {
	if (body == NULL) {
		return;
	}
	body->obj->_->body = NULL;
	if (body_is_terrain(body->_body)) {
		terrain_clear();
		return;
	}
	*body_ref(body->_body).active = 0;
}

float asim_body_get_speed(asim_body_t *body) {
	return vec4_len(*body_ref(body->_body).velocity);
}

void asim_set_friction(float v) {
	asim_friction = v * 0.1f;
}

void asim_set_bounciness(float v) {
	asim_bounciness = v;
}

void asim_set_gravity(float x, float y, float z) {
	asim_gravity = (vec4_t){x, y, z};
}

#endif
