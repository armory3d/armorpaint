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
	vec4_t sleep_pos;
	float  sleep_timer;
	int    sleeping;
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
	vec4_t sleep_pos;
	quat_t sleep_rot;
	float  sleep_timer;
	int    sleeping;
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

		if (!s->sleeping) {
			s->position = vec4_add(s->position, vec4_mult(t->normal, s->radius - dist));

			float v_dot_n = vec4_dot(s->velocity, t->normal);
			if (v_dot_n < 0.0f) {
				vec4_t n_vel = vec4_mult(t->normal, v_dot_n);
				vec4_t t_vel = vec4_sub(s->velocity, n_vel);
				s->velocity  = vec4_add(vec4_mult(n_vel, -asim_bounciness), vec4_mult(t_vel, 1.0f - asim_friction));
			}
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
#define SLEEP_DISTANCE     0.002f
#define SLEEP_TURN         0.9999f // Quaternion dot, a little under a degree
#define SLEEP_TIME         0.4f
#define REST_DAMPING       6.0f
#define PENETRATION_SLOP   0.002f
#define CONTACT_TOLERANCE  0.0001f

typedef struct {
	vec4_t *position;
	vec4_t *velocity;
	vec4_t *angular;
	float   inv_mass;
	float   inv_inertia;
	float  *sleep_timer;
	int    *sleeping;
} rigid_t;

static vec4_t rigid_zero;
static float  rigid_no_timer = SLEEP_TIME; // The static world is always settled
static int    rigid_never_sleeps;

static rigid_t rigid_static(void) {
	return (rigid_t){&rigid_zero, &rigid_zero, &rigid_zero, 0.0f, 0.0f, &rigid_no_timer, &rigid_never_sleeps};
}

static rigid_t rigid_box(box_t *b) {
	float inv_mass = b->mass > 0.0f && !b->sleeping ? 1.0f / b->mass : 0.0f;
	return (rigid_t){&b->position, &b->velocity, &b->angular, inv_mass, b->sleeping ? 0.0f : b->inv_inertia, &b->sleep_timer, &b->sleeping};
}

static rigid_t rigid_sphere(sphere_t *s) {
	float inv_mass = s->mass > 0.0f && !s->sleeping ? 1.0f / s->mass : 0.0f;
	return (rigid_t){&s->position, &s->velocity, &rigid_zero, inv_mass, 0.0f, &s->sleep_timer, &s->sleeping};
}

static void rigid_wake(rigid_t body) {
	if (!*body.sleeping) {
		return;
	}
	*body.sleeping    = 0;
	*body.sleep_timer = 0.0f;
}

static void sphere_wake(sphere_t *s) {
	if (!s->sleeping) {
		return;
	}
	s->sleeping    = 0;
	s->sleep_timer = 0.0f;
}

static inline int rigid_on_the_move(rigid_t body) {
	return *body.sleep_timer == 0.0f;
}

typedef struct {
	vec4_t *position;
	quat_t *rotation; // NULL for a body whose orientation is not simulated
	vec4_t *velocity;
	vec4_t *angular;
	vec4_t *anchor_pos;
	quat_t *anchor_rot;
	float  *timer;
	int    *sleeping;
} settling_t;

static void sleep_update(settling_t s, int touching, float dt) {
	if (*s.timer == 0.0f) { // Start of a new window, take the pose to compare against
		*s.anchor_pos = *s.position;
		if (s.rotation != NULL) {
			*s.anchor_rot = *s.rotation;
		}
	}

	int still = touching && vec4_len(vec4_sub(*s.position, *s.anchor_pos)) <= SLEEP_DISTANCE;
	if (still && s.rotation != NULL) {
		still = fabsf(quat_dot(*s.rotation, *s.anchor_rot)) >= SLEEP_TURN;
	}
	if (!still) {
		*s.timer = 0.0f;
		return;
	}

	*s.timer += dt;
	if (*s.timer < SLEEP_TIME) {
		return;
	}

	*s.velocity = (vec4_t){0.0f, 0.0f, 0.0f};
	*s.angular  = (vec4_t){0.0f, 0.0f, 0.0f};
	*s.sleeping = 1;
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
	float       push       = depth - PENETRATION_SLOP;
	if (push > 0.0f) {
		if (a.inv_mass > 0.0f) {
			*a.position = vec4_add(*a.position, vec4_mult(n, push * correction * a.inv_mass / inv_sum));
		}
		if (b.inv_mass > 0.0f) {
			*b.position = vec4_sub(*b.position, vec4_mult(n, push * correction * b.inv_mass / inv_sum));
		}
	}

	vec4_t ra  = vec4_sub(point, *a.position);
	vec4_t rb  = vec4_sub(point, *b.position);
	vec4_t rel = vec4_sub(vec4_add(*a.velocity, vec4_cross(*a.angular, ra)), vec4_add(*b.velocity, vec4_cross(*b.angular, rb)));

	if (rigid_on_the_move(a) || rigid_on_the_move(b)) {
		rigid_wake(a);
		rigid_wake(b);
	}

	float closing = vec4_dot(rel, n);
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

static inline vec4_t box_axis(box_t *b, int i) {
	vec4_t unit = {i == 0 ? 1.0f : 0.0f, i == 1 ? 1.0f : 0.0f, i == 2 ? 1.0f : 0.0f};
	return vec4_apply_quat(unit, b->rotation);
}

static float box_extent(box_t *b, vec4_t axis) {
	return fabsf(b->half.x * vec4_dot(box_axis(b, 0), axis)) + fabsf(b->half.y * vec4_dot(box_axis(b, 1), axis)) +
	       fabsf(b->half.z * vec4_dot(box_axis(b, 2), axis));
}

static int box_contains_point(box_t *b, vec4_t point) {
	vec4_t local = vec4_apply_quat(vec4_sub(point, b->position), quat_conj(b->rotation));
	return fabsf(local.x) <= b->half.x + CONTACT_TOLERANCE && fabsf(local.y) <= b->half.y + CONTACT_TOLERANCE &&
	       fabsf(local.z) <= b->half.z + CONTACT_TOLERANCE;
}

static int box_contact_normal(box_t *a, box_t *b, vec4_t *out_normal, float *out_depth) {
	vec4_t delta     = vec4_sub(a->position, b->position);
	vec4_t best_axis = {0.0f, 0.0f, 1.0f};
	float  best      = 0.0f;

	for (int i = 0; i < 6; i++) {
		vec4_t axis    = i < 3 ? box_axis(a, i) : box_axis(b, i - 3);
		float  overlap = box_extent(a, axis) + box_extent(b, axis) - fabsf(vec4_dot(delta, axis));
		if (overlap < 0.0f) {
			return 0; // A gap along this direction, so the boxes are apart
		}
		if (i == 0 || overlap < best) {
			best      = overlap;
			best_axis = axis;
		}
	}

	*out_normal = vec4_dot(delta, best_axis) < 0.0f ? vec4_mult(best_axis, -1.0f) : best_axis;
	*out_depth  = best;
	return 1;
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

typedef struct {
	vec4_t position;
	vec4_t velocity;
	vec4_t angular;
	int    woke;
	int    count;
} contact_sum_t;

static void contact_sum_add(contact_sum_t *sum, box_t *solved, box_t *start) {
	sum->position = vec4_add(sum->position, vec4_sub(solved->position, start->position));
	sum->velocity = vec4_add(sum->velocity, vec4_sub(solved->velocity, start->velocity));
	sum->angular  = vec4_add(sum->angular, vec4_sub(solved->angular, start->angular));
	sum->woke |= start->sleeping && !solved->sleeping;
}

static void contact_sum_apply(contact_sum_t *sum, box_t *b) {
	float share = 1.0f / sum->count;
	b->position = vec4_add(b->position, vec4_mult(sum->position, share));
	b->velocity = vec4_add(b->velocity, vec4_mult(sum->velocity, share));
	b->angular  = vec4_add(b->angular, vec4_mult(sum->angular, share));
	if (sum->woke) {
		b->sleeping    = 0;
		b->sleep_timer = 0.0f;
	}
}

static void collide_box_corners(int ai, int bi) {
	box_t start_a = boxes[ai];
	box_t start_b = boxes[bi];

	vec4_t n;
	float  overlap;
	if (!box_contact_normal(&start_a, &start_b, &n, &overlap)) {
		return;
	}

	// Where the face of b that a has to be pushed back out through sits along n
	float surface = vec4_dot(start_b.position, n) + box_extent(&start_b, n);

	contact_sum_t sum_a = {0};
	contact_sum_t sum_b = {0};

	for (int c = 0; c < 8; c++) {
		vec4_t corner = box_corner(&start_a, c);
		if (!box_contains_point(&start_b, corner)) {
			continue;
		}

		// Never push a corner further than it takes to part the two boxes
		float depth = surface - vec4_dot(corner, n);
		if (depth <= 0.0f) {
			continue;
		}
		if (depth > overlap) {
			depth = overlap;
		}

		box_t solved_a = start_a;
		box_t solved_b = start_b;
		resolve_contact_pair(rigid_box(&solved_a), rigid_box(&solved_b), corner, n, depth);

		contact_sum_add(&sum_a, &solved_a, &start_a);
		contact_sum_add(&sum_b, &solved_b, &start_b);
		sum_a.count++;
		sum_b.count++;

		report_contact(box_pair(ai), depth, corner, n);
		report_contact(box_pair(bi), depth, corner, vec4_mult(n, -1.0f));
	}

	if (sum_a.count == 0) {
		return;
	}
	contact_sum_apply(&sum_a, &boxes[ai]);
	contact_sum_apply(&sum_b, &boxes[bi]);
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

	float inv_a   = a->mass > 0.0f && !a->sleeping ? 1.0f / a->mass : 0.0f;
	float inv_b   = b->mass > 0.0f && !b->sleeping ? 1.0f / b->mass : 0.0f;
	float inv_sum = inv_a + inv_b;
	if (inv_sum <= 0.0f) { // Both static or both resting
		return;
	}

	vec4_t n       = vec4_mult(delta, 1.0f / dist);
	float  overlap = min_dist - dist - PENETRATION_SLOP;
	if (overlap > 0.0f) {
		a->position = vec4_add(a->position, vec4_mult(n, overlap * inv_a / inv_sum));
		b->position = vec4_sub(b->position, vec4_mult(n, overlap * inv_b / inv_sum));
	}

	float closing = vec4_dot(a->velocity, n) - vec4_dot(b->velocity, n);
	if (a->sleep_timer == 0.0f || b->sleep_timer == 0.0f) {
		sphere_wake(a);
		sphere_wake(b);
	}
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

	box_t         start = *b;
	contact_sum_t sum   = {0};

	for (int c = 0; c < 8; c++) {
		vec4_t corner = box_corner(&start, c);
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

		box_t solved = start;
		resolve_contact_pair(rigid_box(&solved), rigid_static(), corner, normal, depth);

		contact_sum_add(&sum, &solved, &start);
		sum.count++;

		report_contact(box_pair(bi), depth, corner, normal);
	}

	if (sum.count > 0) {
		contact_sum_apply(&sum, b);
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

static void mesh_clear() {
	free_bvh(mesh.root);
	mesh.root = NULL;
}

void asim_world_create() {
	asim_world_destroy();
	memset(spheres, 0, sizeof(spheres));
	memset(boxes, 0, sizeof(boxes));
	memset(pairs, 0, sizeof(pairs));
}

void asim_world_destroy() {
	mesh_clear();
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
			if (!s->sleeping) { // A sleeping body still collides, it just no longer moves
				s->velocity = vec4_add(s->velocity, vec4_mult(asim_gravity, dt));
				s->position = vec4_add(s->position, vec4_mult(s->velocity, dt));
			}
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
			if (!b->sleeping) { // A sleeping body still collides, it just no longer moves
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

				if (b->sleep_timer > 0.0f) {
					float drain = 1.0f - fminf(1.0f, REST_DAMPING * dt);
					b->velocity = vec4_mult(b->velocity, drain);
					b->angular  = vec4_mult(b->angular, drain);
				}
			}

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

	// Put bodies that have settled on something to sleep, so they stop entirely
	float frame_dt = sys_delta();
	for (int i = 0; i < MAX_SPHERES; i++) {
		sphere_t *s = &spheres[i];
		if (s->active && s->mass != 0.0f && !s->sleeping) {
			settling_t settling = {&s->position, NULL, &s->velocity, &rigid_zero, &s->sleep_pos, NULL, &s->sleep_timer, &s->sleeping};
			sleep_update(settling, pair_best[i] > 0.0f, frame_dt);
		}
	}
	for (int i = 0; i < MAX_BOXES; i++) {
		box_t *b = &boxes[i];
		if (b->active && b->mass != 0.0f && !b->sleeping) {
			settling_t settling = {&b->position, &b->rotation, &b->velocity, &b->angular, &b->sleep_pos, &b->sleep_rot, &b->sleep_timer, &b->sleeping};
			sleep_update(settling, pair_best[box_pair(i)] > 0.0f, frame_dt);
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

static asim_heightfield_t heightfield_from_mesh(i16_array_t *pa, float scale, float dimz) {
	int num_verts = pa->length / 4;
	int res       = (int)(sqrtf((float)num_verts) + 0.5f);
	if (res < 2) {
		return (asim_heightfield_t){0};
	}

	float *heights = (float *)malloc(sizeof(float) * res * res);
	for (int i = 0; i < res * res; i++) {
		heights[i] = dimz > 0.0f ? (mesh_vertex(pa, i, scale).z + dimz / 2.0f) / dimz : 0.0f;
	}

	return (asim_heightfield_t){.heights = heights, .res_x = res, .res_y = res};
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
		                      .min     = {x - dimx / 2.0f, y - dimy / 2.0f, z - dimz / 2.0f},
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

	mesh_clear();
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
	float  *sleep_timer;
	int    *sleeping;
} body_ref_t;

static body_ref_t body_ref(void *body) {
	int slot = body_slot(body);
	if (body_is_box(body) && slot < MAX_BOXES) {
		box_t *b = &boxes[slot];
		return (body_ref_t){&b->position, &b->velocity, &b->mass, &b->active, &b->sleep_timer, &b->sleeping};
	}
	if (body_is_sphere(body) && slot < MAX_SPHERES) {
		sphere_t *s = &spheres[slot];
		return (body_ref_t){&s->position, &s->velocity, &s->mass, &s->active, &s->sleep_timer, &s->sleeping};
	}
	return (body_ref_t){&null_body.position, &null_body.velocity, &null_body.mass, &null_body.active, &null_body.sleep_timer, &null_body.sleeping};
}

static void body_wake(void *body) {
	body_ref_t ref   = body_ref(body);
	*ref.sleeping    = 0;
	*ref.sleep_timer = 0.0f;
}

static void wake_all() {
	for (int i = 0; i < MAX_SPHERES; i++) {
		sphere_wake(&spheres[i]);
	}
	for (int i = 0; i < MAX_BOXES; i++) {
		boxes[i].sleeping    = 0;
		boxes[i].sleep_timer = 0.0f;
	}
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

	float              scale_pos = 1.0f;
	void              *posa      = NULL;
	u32_array_t       *inda      = NULL;
	asim_heightfield_t field     = {0};

	if (shape == ASIM_SHAPE_MESH || shape == ASIM_SHAPE_TERRAIN) {
		mesh_object_t *mo    = obj->ext;
		mesh_data_t   *data  = mo->data;
		vec4_t         scale = obj->transform->scale;

		if (obj->parent != NULL) {
			scale.x *= obj->parent->transform->scale.x;
			scale.y *= obj->parent->transform->scale.y;
			scale.z *= obj->parent->transform->scale.z;
		}

		i16_array_t *pa = mesh_data_get_vertex_array(data, "pos")->values;
		inda            = data->index_array;
		scale_pos       = scale.x * data->scale_pos;

		if (shape == ASIM_SHAPE_TERRAIN) {
			field = heightfield_from_mesh(pa, (1.0f / 32767.0f) * scale_pos, body->dimz);
			posa  = &field;
		}
		else {
			posa = pa;
		}
	}

	vec4_t loc  = obj->transform->loc;
	body->_body = body_create(shape, mass, body->dimx, body->dimy, body->dimz, loc.x, loc.y, loc.z, posa, inda, scale_pos);
	free(field.heights);

	if (shape == ASIM_SHAPE_BOX) { // Start out at the object rotation
		asim_body_sync_transform(body);
	}
	return body;
}

void asim_body_set_mass(asim_body_t *body, float mass) {
	body->mass                  = mass;
	*body_ref(body->_body).mass = mass;
	body_wake(body->_body);
}

void asim_body_apply_impulse(void *body, vec4_t impulse) {
	vec4_t *vel = body_ref(body).velocity;
	vel->x += impulse.x;
	vel->y += impulse.y;
	vel->z += impulse.z;
	body_wake(body);
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
	body_wake(body);
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
	body_wake(body->_body);
}

void asim_body_update(asim_body_t *body) {
	if (body->shape == ASIM_SHAPE_MESH || body->shape == ASIM_SHAPE_TERRAIN) {
		return; // Static collider, the object drives the shape
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
	wake_all(); // Whatever was resting on this body has to fall now
	if (body->shape == ASIM_SHAPE_MESH) {
		mesh_clear();
		return;
	}
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
	wake_all();
}

#endif
