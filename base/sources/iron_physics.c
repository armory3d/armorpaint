#ifdef WITH_PHYSICS

#include "iron_physics.h"
#include "engine.h"
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GRAVITY         -9.81f
#define MAX_BVH_DEPTH   20
#define MIN_BODIES      16   // Body arrays start this big and double when full
#define GRID_CELL       2.0f // Broadphase cell size on xy, a few times a typical moving body
#define GRID_SPAN       8    // Bodies wider than this many cells go into one bucket that every query visits
#define MAX_TERRAIN_RES 1024

#define SPHERE_TAG  0x10000
#define BOX_TAG     0x20000
#define TERRAIN_TAG 0x40000
#define SLOT_MASK   0xffff

typedef struct {
	vec4_t min;
	vec4_t max;
} aabb_t;

typedef struct {
	vec4_t         position;
	vec4_t         velocity;
	float          radius;
	float          mass;
	vec4_t         sleep_pos;
	float          sleep_timer;
	int            sleeping;
	int            active;
	physics_pair_t contact; // Deepest contact this frame
	float          contact_depth;
} sphere_t;

typedef struct {
	vec4_t         position;
	vec4_t         velocity;
	quat_t         rotation;
	vec4_t         angular;
	vec4_t         half;
	float          mass;
	float          inv_inertia;
	vec4_t         sleep_pos;
	quat_t         sleep_rot;
	float          sleep_timer;
	int            sleeping;
	int            active;
	physics_pair_t contact; // Deepest contact this frame
	float          contact_depth;
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
	float  min_h;
	float  max_h;
	int    active;
} terrain_t;

static sphere_t      *spheres;
static box_t         *boxes;
static int            sphere_count;
static int            box_count;
static int            sphere_cap;
static int            box_cap;
static physics_pair_t null_pair;
static mesh_t         mesh;
static terrain_t      terrain;
static aabb_t         root_bounds        = {{-10, -10, -10}, {10, 10, 10}};
static float          physics_bounciness = 0.0f;
static float          physics_friction   = 0.01f;
static vec4_t         physics_gravity    = {0.0f, 0.0f, GRAVITY};

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

static void report_contact(physics_pair_t *contact, float *best, float depth, vec4_t point, vec4_t normal) {
	if (depth <= *best) {
		return;
	}
	*best    = depth;
	*contact = (physics_pair_t){point.x, point.y, point.z, normal.x, normal.y, normal.z};
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
				s->velocity  = vec4_add(vec4_mult(n_vel, -physics_bounciness), vec4_mult(t_vel, 1.0f - physics_friction));
			}
		}

		vec4_t contact_point = vec4_sub(s->position, vec4_mult(t->normal, s->radius));
		report_contact(&s->contact, &s->contact_depth, s->radius - orig_dist, contact_point, t->normal);
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
	float  impulse = -(1.0f + physics_bounciness) * closing / denom;
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

static float box_inv_inertia(vec4_t half, float mass) {
	float inertia = mass / 3.0f * (half.x * half.x + half.y * half.y + half.z * half.z) * (2.0f / 3.0f);
	return mass > 0.0f && inertia > 0.0f ? 1.0f / inertia : 0.0f;
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

		report_contact(&boxes[ai].contact, &boxes[ai].contact_depth, depth, corner, n);
		report_contact(&boxes[bi].contact, &boxes[bi].contact_depth, depth, corner, vec4_mult(n, -1.0f));
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
	report_contact(&b->contact, &b->contact_depth, depth, point, vec4_mult(n, -1.0f));
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

static int ray_clip_slab(float origin, float dir, float lo, float hi, float *t0, float *t1) {
	if (fabsf(dir) < 1e-6f) {
		return origin >= lo && origin <= hi;
	}

	float ta = (lo - origin) / dir;
	float tb = (hi - origin) / dir;
	if (ta > tb) {
		float tmp = ta;
		ta        = tb;
		tb        = tmp;
	}
	if (ta > *t0) {
		*t0 = ta;
	}
	if (tb < *t1) {
		*t1 = tb;
	}
	return *t0 <= *t1;
}

// Nearest static box
int physics_box_raycast(vec4_t origin, vec4_t dir, float max_dist, vec4_t *hit) {
	float len = vec4_len(dir);
	if (len == 0.0f) {
		return 0;
	}
	vec4_t d    = vec4_mult(dir, 1.0f / len);
	float  best = max_dist;
	int    got  = 0;
	for (int i = 0; i < box_count; i++) {
		box_t *b = &boxes[i];
		if (!b->active || b->mass > 0.0f) {
			continue;
		}
		quat_t inv = quat_conj(b->rotation);
		vec4_t o   = vec4_apply_quat(vec4_sub(origin, b->position), inv);
		vec4_t ld  = vec4_apply_quat(d, inv);
		float  t0  = 0.0f;
		float  t1  = best;
		if (!ray_clip_slab(o.x, ld.x, -b->half.x, b->half.x, &t0, &t1) || !ray_clip_slab(o.y, ld.y, -b->half.y, b->half.y, &t0, &t1) ||
		    !ray_clip_slab(o.z, ld.z, -b->half.z, b->half.z, &t0, &t1)) {
			continue;
		}
		if (t0 <= 0.0f) {
			continue;
		}
		if (t0 < best) {
			best = t0;
			got  = 1;
		}
	}
	if (got) {
		*hit = vec4_add(origin, vec4_mult(d, best));
	}
	return got;
}

int physics_terrain_raycast(vec4_t origin, vec4_t dir, vec4_t *hit) {
	if (!terrain.active) {
		return 0;
	}

	float len = vec4_len(dir);
	if (len == 0.0f) {
		return 0;
	}
	vec4_t d = vec4_mult(dir, 1.0f / len);

	const float pad = 0.001f;
	float       t0  = 0.0f;
	float       t1  = FLT_MAX;
	if (!ray_clip_slab(origin.x, d.x, terrain.min.x - pad, terrain.min.x + terrain.size_x + pad, &t0, &t1) ||
	    !ray_clip_slab(origin.y, d.y, terrain.min.y - pad, terrain.min.y + terrain.size_y + pad, &t0, &t1) ||
	    !ray_clip_slab(origin.z, d.z, terrain.min.z + terrain.min_h - pad, terrain.min.z + terrain.max_h + pad, &t0, &t1)) {
		return 0;
	}
	if (t1 < 0.0f) {
		return 0;
	}
	if (t0 < 0.0f) {
		t0 = 0.0f;
	}

	float cell_x = terrain.size_x / (terrain.res_x - 1);
	float cell_y = terrain.size_y / (terrain.res_y - 1);
	float step   = cell_x < cell_y ? cell_x : cell_y;
	if (step <= 0.0f) {
		return 0;
	}

	float  ground = 0.0f;
	vec4_t normal;
	float  above       = t0;
	int    above_valid = 0;

	for (float t = t0;; t += step) {
		if (t > t1) {
			t = t1;
		}

		vec4_t p = vec4_add(origin, vec4_mult(d, t));
		if (terrain_sample(p.x, p.y, &ground, &normal)) {
			if (p.z <= ground) {
				if (above_valid) {
					float lo = above;
					float hi = t;
					for (int i = 0; i < 24; ++i) {
						float  mid = (lo + hi) * 0.5f;
						vec4_t q   = vec4_add(origin, vec4_mult(d, mid));
						if (terrain_sample(q.x, q.y, &ground, &normal) && q.z <= ground) {
							hi = mid;
						}
						else {
							lo = mid;
						}
					}
					p = vec4_add(origin, vec4_mult(d, hi));
					if (!terrain_sample(p.x, p.y, &ground, &normal)) {
						return 0;
					}
				}
				*hit = (vec4_t){p.x, p.y, ground, 1.0f};
				return 1;
			}
			above       = t;
			above_valid = 1;
		}
		else {
			above_valid = 0;
		}

		if (t >= t1) {
			break;
		}
	}
	return 0;
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

		report_contact(&b->contact, &b->contact_depth, depth, corner, normal);
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
	report_contact(&s->contact, &s->contact_depth, depth, point, normal);
}

static void terrain_clear() {
	free(terrain.heights);
	memset(&terrain, 0, sizeof(terrain));
}

static void mesh_clear() {
	free_bvh(mesh.root);
	mesh.root = NULL;
}

static void pairs_clear() {
	for (int i = 0; i < sphere_count; i++) {
		spheres[i].contact       = (physics_pair_t){0};
		spheres[i].contact_depth = 0.0f;
	}
	for (int i = 0; i < box_count; i++) {
		boxes[i].contact       = (physics_pair_t){0};
		boxes[i].contact_depth = 0.0f;
	}
}

// Broadphase: a hashed grid on xy, rebuilt every substep. Each body goes into the cells its bounding circle covers
typedef struct {
	int  is_box;
	int  cells;
	int *start; // Bucket h holds items[start[h] .. start[h + 1]), bucket cells holds the large bodies
	int *items;
	int *seen; // Last query that returned the body, so a body in several cells is returned once
	int  start_cap;
	int  items_cap;
	int  seen_cap;
} grid_t;

static grid_t sphere_grid = {.is_box = 0};
static grid_t box_grid    = {.is_box = 1};
static int   *candidates;
static int    candidates_cap;
static int    grid_query;

static int grid_body(grid_t *g, int i, vec4_t *pos, float *radius) {
	if (g->is_box) {
		*pos    = boxes[i].position;
		*radius = vec4_len(boxes[i].half);
		return boxes[i].active;
	}
	*pos    = spheres[i].position;
	*radius = spheres[i].radius;
	return spheres[i].active;
}

// Returns 0 when the circle spans too many cells to insert one by one
static int grid_range(vec4_t pos, float radius, int *x0, int *y0, int *x1, int *y1) {
	*x0 = (int)floorf((pos.x - radius) / GRID_CELL);
	*y0 = (int)floorf((pos.y - radius) / GRID_CELL);
	*x1 = (int)floorf((pos.x + radius) / GRID_CELL);
	*y1 = (int)floorf((pos.y + radius) / GRID_CELL);
	return *x1 - *x0 < GRID_SPAN && *y1 - *y0 < GRID_SPAN;
}

static inline int grid_hash(grid_t *g, int x, int y) {
	return (int)(((unsigned)x * 73856093u ^ (unsigned)y * 19349663u) & (unsigned)(g->cells - 1));
}

static int *grid_reserve(int *array, int *cap, int count) {
	if (count > *cap) {
		*cap  = count * 2;
		array = realloc(array, sizeof(int) * *cap);
	}
	return array;
}

static void grid_insert(grid_t *g, int i, int fill) {
	vec4_t pos;
	float  radius;
	if (!grid_body(g, i, &pos, &radius)) {
		return;
	}
	int x0, y0, x1, y1;
	int large = !grid_range(pos, radius, &x0, &y0, &x1, &y1);
	if (large) {
		x0 = x1 = y0 = y1 = 0;
	}
	for (int y = y0; y <= y1; y++) {
		for (int x = x0; x <= x1; x++) {
			int h = large ? g->cells : grid_hash(g, x, y);
			if (fill) {
				g->items[--g->start[h]] = i;
			}
			else {
				g->start[h]++;
			}
		}
	}
}

static void grid_build(grid_t *g, int count) {
	g->cells = 64;
	while (g->cells < count * 2) {
		g->cells *= 2;
	}
	g->start = grid_reserve(g->start, &g->start_cap, g->cells + 2);
	g->seen  = grid_reserve(g->seen, &g->seen_cap, count);
	memset(g->start, 0, sizeof(int) * (g->cells + 2));
	memset(g->seen, 0, sizeof(int) * count);

	// Count, turn the counts into bucket ends, then fill each bucket backwards so start[h] ends up at its beginning
	for (int i = 0; i < count; i++) {
		grid_insert(g, i, 0);
	}
	int total = 0;
	for (int h = 0; h <= g->cells; h++) {
		total += g->start[h];
		g->start[h] = total;
	}
	g->start[g->cells + 1] = total;
	g->items               = grid_reserve(g->items, &g->items_cap, total);
	for (int i = 0; i < count; i++) {
		grid_insert(g, i, 1);
	}
}

static void grid_take(grid_t *g, int h, int *n) {
	for (int k = g->start[h]; k < g->start[h + 1]; k++) {
		int j = g->items[k];
		if (g->seen[j] != grid_query) {
			g->seen[j]         = grid_query;
			candidates[(*n)++] = j;
		}
	}
}

// Fills candidates with the bodies of g that may touch the circle, returns how many
static int grid_find(grid_t *g, int count, vec4_t pos, float radius) {
	candidates = grid_reserve(candidates, &candidates_cap, count);
	int n      = 0;
	int x0, y0, x1, y1;
	if (!grid_range(pos, radius, &x0, &y0, &x1, &y1)) { // A large body checks everything
		for (int j = 0; j < count; j++) {
			candidates[n++] = j;
		}
		return n;
	}
	grid_query++;
	for (int y = y0; y <= y1; y++) {
		for (int x = x0; x <= x1; x++) {
			grid_take(g, grid_hash(g, x, y), &n);
		}
	}
	grid_take(g, g->cells, &n);
	return n;
}

void physics_world_create() {
	physics_world_destroy();
	memset(spheres, 0, sizeof(sphere_t) * sphere_count);
	memset(boxes, 0, sizeof(box_t) * box_count);
	sphere_count = 0;
	box_count    = 0;
}

void physics_world_destroy() {
	mesh_clear();
	terrain_clear();
}

void physics_world_update() {
	const int sub_steps = 8;
	float     dt        = sys_delta() / sub_steps;

	pairs_clear();

	for (int step = 0; step < sub_steps; step++) {
		// Sphere-mesh collision
		for (int i = 0; i < sphere_count; i++) {
			sphere_t *s = &spheres[i];
			if (!s->active || s->mass == 0.0f) {
				continue;
			}
			if (!s->sleeping) { // A sleeping body still collides, it just no longer moves
				s->velocity = vec4_add(s->velocity, vec4_mult(physics_gravity, dt));
				s->position = vec4_add(s->position, vec4_mult(s->velocity, dt));
			}
			query_bvh(s, i, mesh.root);
			collide_sphere_terrain(i);
		}

		// Sphere-sphere collision
		grid_build(&sphere_grid, sphere_count);
		for (int i = 0; i < sphere_count; i++) {
			sphere_t *s = &spheres[i];
			if (!s->active) {
				continue;
			}
			int n = grid_find(&sphere_grid, sphere_count, s->position, s->radius);
			for (int k = 0; k < n; k++) {
				int j = candidates[k];
				if (j > i && spheres[j].active) {
					collide_sphere_sphere(i, j);
				}
			}
		}

		for (int i = 0; i < box_count; i++) {
			box_t *b = &boxes[i];
			if (!b->active || b->mass == 0.0f) {
				continue;
			}
			if (!b->sleeping) { // A sleeping body still collides, it just no longer moves
				b->velocity = vec4_add(b->velocity, vec4_mult(physics_gravity, dt));
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

		// Box-box collision, static boxes only collide with moving ones
		grid_build(&box_grid, box_count);
		for (int i = 0; i < box_count; i++) {
			box_t *b = &boxes[i];
			if (!b->active || b->mass == 0.0f) {
				continue;
			}
			int n = grid_find(&box_grid, box_count, b->position, vec4_len(b->half));
			for (int k = 0; k < n; k++) {
				int j = candidates[k];
				if (j == i || !boxes[j].active || (boxes[j].mass != 0.0f && j < i)) {
					continue;
				}
				collide_box_box(i, j);
			}
		}

		// Box-sphere collision
		for (int i = 0; i < box_count; i++) {
			box_t *b = &boxes[i];
			if (!b->active) {
				continue;
			}
			int n = grid_find(&sphere_grid, sphere_count, b->position, vec4_len(b->half));
			for (int k = 0; k < n; k++) {
				int j = candidates[k];
				if (spheres[j].active) {
					collide_box_sphere(i, j);
				}
			}
		}
	}

	// Put bodies that have settled on something to sleep, so they stop entirely
	float frame_dt = sys_delta();
	for (int i = 0; i < sphere_count; i++) {
		sphere_t *s = &spheres[i];
		if (s->active && s->mass != 0.0f && !s->sleeping) {
			settling_t settling = {&s->position, NULL, &s->velocity, &rigid_zero, &s->sleep_pos, NULL, &s->sleep_timer, &s->sleeping};
			sleep_update(settling, s->contact_depth > 0.0f, frame_dt);
		}
	}
	for (int i = 0; i < box_count; i++) {
		box_t *b = &boxes[i];
		if (b->active && b->mass != 0.0f && !b->sleeping) {
			settling_t settling = {&b->position, &b->rotation, &b->velocity, &b->angular, &b->sleep_pos, &b->sleep_rot, &b->sleep_timer, &b->sleeping};
			sleep_update(settling, b->contact_depth > 0.0f, frame_dt);
		}
	}

	// Write the simulated state back into the scene
	for (int i = 0; i < scene_meshes->length; i++) {
		mesh_object_t  *mo   = scene_meshes->buffer[i];
		physics_body_t *body = mo->base->_->body;
		if (body != NULL) {
			physics_body_update(body);
		}
	}
}

physics_pair_t *physics_world_get_contact(void *body) {
	int slot = body_slot(body);
	if (body_is_box(body) && slot < box_count) {
		return &boxes[slot].contact;
	}
	if (body_is_sphere(body) && slot < sphere_count) {
		return &spheres[slot].contact;
	}
	return &null_pair;
}

physics_pair_t_array_t *physics_get_contact_pairs(physics_body_t *body) {
	physics_pair_t_array_t *result = any_array_create_from_raw((void *[]){}, 0);
	physics_pair_t         *p      = physics_world_get_contact(body->_body);
	if (p->pos_a_x != 0 || p->pos_a_y != 0 || p->pos_a_z != 0) {
		any_array_push(result, p);
	}
	return result;
}

static inline vec4_t mesh_vertex(i16_array_t *pa, uint32_t index, float scale) {
	return (vec4_t){pa->buffer[index * 4] * scale, pa->buffer[index * 4 + 1] * scale, pa->buffer[index * 4 + 2] * scale};
}

static inline vec4_t mesh_vertex_scaled(i16_array_t *pa, uint32_t index, float sx, float sy, float sz) {
	return (vec4_t){pa->buffer[index * 4] * sx, pa->buffer[index * 4 + 1] * sy, pa->buffer[index * 4 + 2] * sz};
}

static void heightfield_fill_holes(float *heights, int res_x, int res_y) {
	int  count = res_x * res_y;
	int *queue = (int *)malloc(sizeof(int) * count);
	int  head = 0, tail = 0;

	for (int i = 0; i < count; i++) {
		if (heights[i] > -FLT_MAX) {
			queue[tail++] = i;
		}
	}
	if (tail == 0) {
		free(queue);
		for (int i = 0; i < count; i++) {
			heights[i] = 0.0f;
		}
		return;
	}

	while (head < tail) {
		int   i = queue[head++];
		int   x = i % res_x;
		int   y = i / res_x;
		float h = heights[i];

		int neighbors[4] = {x > 0 ? i - 1 : -1, x < res_x - 1 ? i + 1 : -1, y > 0 ? i - res_x : -1, y < res_y - 1 ? i + res_x : -1};
		for (int n = 0; n < 4; n++) {
			int ni = neighbors[n];
			if (ni >= 0 && heights[ni] == -FLT_MAX) {
				heights[ni]   = h;
				queue[tail++] = ni;
			}
		}
	}

	free(queue);
}

static physics_heightfield_t heightfield_from_mesh(i16_array_t *pa, u32_array_t *ia, float scale_x, float scale_y, float scale_z) {
	if (pa == NULL || ia == NULL) {
		return (physics_heightfield_t){0};
	}

	int num_verts = pa->length / 4;
	int num_tris  = ia->length / 3;
	if (num_verts < 3 || num_tris < 1) {
		return (physics_heightfield_t){0};
	}

	float min_x = FLT_MAX, min_y = FLT_MAX, max_x = -FLT_MAX, max_y = -FLT_MAX;
	for (int i = 0; i < num_verts; i++) {
		vec4_t v = mesh_vertex_scaled(pa, i, scale_x, scale_y, scale_z);
		min_x    = fminf(min_x, v.x);
		min_y    = fminf(min_y, v.y);
		max_x    = fmaxf(max_x, v.x);
		max_y    = fmaxf(max_y, v.y);
	}

	float size_x = max_x - min_x;
	float size_y = max_y - min_y;
	if (size_x <= 0.0f || size_y <= 0.0f) {
		return (physics_heightfield_t){0};
	}

	int res = (int)(sqrtf(num_tris / 2.0f) + 0.5f) * 2 + 1;
	if (res > MAX_TERRAIN_RES) {
		res = MAX_TERRAIN_RES;
	}

	float  cell_x  = size_x / (res - 1);
	float  cell_y  = size_y / (res - 1);
	float *heights = (float *)malloc(sizeof(float) * res * res);
	for (int i = 0; i < res * res; i++) {
		heights[i] = -FLT_MAX;
	}

	for (int t = 0; t < num_tris; t++) {
		vec4_t v0 = mesh_vertex_scaled(pa, ia->buffer[t * 3], scale_x, scale_y, scale_z);
		vec4_t v1 = mesh_vertex_scaled(pa, ia->buffer[t * 3 + 1], scale_x, scale_y, scale_z);
		vec4_t v2 = mesh_vertex_scaled(pa, ia->buffer[t * 3 + 2], scale_x, scale_y, scale_z);

		float area = (v1.y - v2.y) * (v0.x - v2.x) + (v2.x - v1.x) * (v0.y - v2.y);
		if (fabsf(area) < 1e-12f) {
			continue;
		}

		int ix0 = (int)ceilf((min3(v0.x, v1.x, v2.x) - min_x) / cell_x);
		int ix1 = (int)floorf((max3(v0.x, v1.x, v2.x) - min_x) / cell_x);
		int iy0 = (int)ceilf((min3(v0.y, v1.y, v2.y) - min_y) / cell_y);
		int iy1 = (int)floorf((max3(v0.y, v1.y, v2.y) - min_y) / cell_y);
		ix0     = ix0 < 0 ? 0 : ix0;
		iy0     = iy0 < 0 ? 0 : iy0;
		ix1     = ix1 > res - 1 ? res - 1 : ix1;
		iy1     = iy1 > res - 1 ? res - 1 : iy1;

		for (int iy = iy0; iy <= iy1; iy++) {
			for (int ix = ix0; ix <= ix1; ix++) {
				float px = min_x + ix * cell_x;
				float py = min_y + iy * cell_y;

				float b0 = ((v1.y - v2.y) * (px - v2.x) + (v2.x - v1.x) * (py - v2.y)) / area;
				float b1 = ((v2.y - v0.y) * (px - v2.x) + (v0.x - v2.x) * (py - v2.y)) / area;
				float b2 = 1.0f - b0 - b1;
				if (b0 < -1e-5f || b1 < -1e-5f || b2 < -1e-5f) {
					continue;
				}

				float h = b0 * v0.z + b1 * v1.z + b2 * v2.z;
				if (h > heights[iy * res + ix]) {
					heights[iy * res + ix] = h;
				}
			}
		}
	}

	heightfield_fill_holes(heights, res, res);

	return (physics_heightfield_t){.heights = heights, .res_x = res, .res_y = res, .min_x = min_x, .min_y = min_y, .size_x = size_x, .size_y = size_y};
}

// Grows by doubling. Handles are slot indices, so bodies stay valid when the array moves
static void *body_array_grow(void *array, int *cap, size_t size) {
	int old = *cap;
	*cap    = old == 0 ? MIN_BODIES : old * 2;
	array   = realloc(array, size * *cap);
	memset((char *)array + size * old, 0, size * (*cap - old));
	return array;
}

static void *body_create(int shape, float mass, float dimx, float dimy, float dimz, float x, float y, float z, void *posa, void *inda, float scale_pos) {

	if (shape == PHYSICS_SHAPE_TERRAIN) {
		physics_heightfield_t *field = posa;
		if (field == NULL || field->heights == NULL || field->res_x < 2 || field->res_y < 2) {
			return NULL;
		}

		int    count   = field->res_x * field->res_y;
		float *heights = (float *)malloc(sizeof(float) * count);
		memcpy(heights, field->heights, sizeof(float) * count);

		float min_h = heights[0];
		float max_h = heights[0];
		for (int i = 1; i < count; ++i) {
			if (heights[i] < min_h) {
				min_h = heights[i];
			}
			if (heights[i] > max_h) {
				max_h = heights[i];
			}
		}

		terrain_clear();
		terrain = (terrain_t){.heights = heights,
		                      .res_x   = field->res_x,
		                      .res_y   = field->res_y,
		                      .min     = {x + field->min_x, y + field->min_y, z},
		                      .size_x  = field->size_x,
		                      .size_y  = field->size_y,
		                      .min_h   = min_h,
		                      .max_h   = max_h,
		                      .active  = 1};

		return (void *)(uintptr_t)TERRAIN_TAG;
	}

	if (shape == PHYSICS_SHAPE_BOX && posa == NULL) {
		int slot = 0;
		while (slot < box_count && boxes[slot].active) {
			slot++;
		}
		if (slot > SLOT_MASK) {
			return NULL;
		}
		if (slot == box_cap) {
			boxes = body_array_grow(boxes, &box_cap, sizeof(box_t));
		}
		if (slot >= box_count) {
			box_count = slot + 1;
		}

		vec4_t half = {dimx / 2.0f, dimy / 2.0f, dimz / 2.0f};

		boxes[slot] = (box_t){
		    .position = {x, y, z}, .rotation = {0.0f, 0.0f, 0.0f, 1.0f}, .half = half, .mass = mass, .inv_inertia = box_inv_inertia(half, mass), .active = 1};

		return (void *)(uintptr_t)(slot | BOX_TAG);
	}

	if (shape == PHYSICS_SHAPE_SPHERE) {
		int slot = 0;
		while (slot < sphere_count && spheres[slot].active) {
			slot++;
		}
		if (slot > SLOT_MASK) {
			return NULL;
		}
		if (slot == sphere_cap) {
			spheres = body_array_grow(spheres, &sphere_cap, sizeof(sphere_t));
		}
		if (slot >= sphere_count) {
			sphere_count = slot + 1;
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
	if (body_is_box(body) && slot < box_count) {
		box_t *b = &boxes[slot];
		return (body_ref_t){&b->position, &b->velocity, &b->mass, &b->active, &b->sleep_timer, &b->sleeping};
	}
	if (body_is_sphere(body) && slot < sphere_count) {
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
	for (int i = 0; i < sphere_count; i++) {
		sphere_wake(&spheres[i]);
	}
	for (int i = 0; i < box_count; i++) {
		boxes[i].sleeping    = 0;
		boxes[i].sleep_timer = 0.0f;
	}
}

static vec4_t object_world_scale(object_t *obj) {
	vec4_t scale = obj->transform->scale;
	if (obj->parent != NULL) {
		scale.x *= obj->parent->transform->scale.x;
		scale.y *= obj->parent->transform->scale.y;
		scale.z *= obj->parent->transform->scale.z;
	}
	return scale;
}

static bool mesh_bounds(object_t *obj, vec4_t *center, vec4_t *extent) {
	// The object origin is not necessarily the middle of the geometry
	if (obj->ext == NULL || !string_equals(obj->ext_type, "mesh_object_t")) {
		return false;
	}

	mesh_object_t *mo        = obj->ext;
	i16_array_t   *pa        = mesh_data_get_vertex_array(mo->data, "pos")->values;
	int            num_verts = pa->length / 4;
	if (num_verts < 1) {
		return false;
	}

	int16_t min[3] = {pa->buffer[0], pa->buffer[1], pa->buffer[2]};
	int16_t max[3] = {pa->buffer[0], pa->buffer[1], pa->buffer[2]};
	for (int i = 1; i < num_verts; i++) {
		for (int c = 0; c < 3; c++) {
			int16_t v = pa->buffer[i * 4 + c];
			min[c]    = v < min[c] ? v : min[c];
			max[c]    = v > max[c] ? v : max[c];
		}
	}

	vec4_t scale  = object_world_scale(obj);
	float  unpack = (1.0f / 32767.0f) * mo->data->scale_pos;
	float  sc[3]  = {unpack * scale.x, unpack * scale.y, unpack * scale.z};

	*center = (vec4_t){(min[0] + max[0]) * 0.5f * sc[0], (min[1] + max[1]) * 0.5f * sc[1], (min[2] + max[2]) * 0.5f * sc[2], 0.0f};
	*extent = (vec4_t){(max[0] - min[0]) * sc[0], (max[1] - min[1]) * sc[1], (max[2] - min[2]) * sc[2], 0.0f};
	return true;
}

physics_body_t *physics_body_create(object_t *obj, physics_shape_t shape, float mass) {
	physics_body_t *body = ALLOC_INIT(physics_body_t, {0});
	body->shape          = shape;
	body->mass           = mass;
	body->obj            = obj;
	obj->_->body         = body;

	// A dim that was set (by a script or the scene file) sizes the shape. A measured one includes the origin,
	// so the exact mesh bounds are used instead
	transform_compute_dim(obj->transform);
	vec4_t dim   = obj->transform->dim;
	vec4_t scale = object_world_scale(obj);
	body->dimx   = dim.x * scale.x;
	body->dimy   = dim.y * scale.y;
	body->dimz   = dim.z * scale.z;

	if (shape == PHYSICS_SHAPE_BOX || shape == PHYSICS_SHAPE_SPHERE) {
		vec4_t center, extent;
		if (mesh_bounds(obj, &center, &extent)) {
			body->offset = center;
			if (obj->transform->measured) {
				body->dimx = extent.x;
				body->dimy = extent.y;
				body->dimz = extent.z;
			}
		}
	}

	float                 scale_pos = 1.0f;
	void                 *posa      = NULL;
	u32_array_t          *inda      = NULL;
	physics_heightfield_t field     = {0};

	if (shape == PHYSICS_SHAPE_MESH || shape == PHYSICS_SHAPE_TERRAIN) {
		mesh_object_t *mo    = obj->ext;
		mesh_data_t   *data  = mo->data;
		vec4_t         scale = object_world_scale(obj);

		i16_array_t *pa = mesh_data_get_vertex_array(data, "pos")->values;
		inda            = data->index_array;
		scale_pos       = scale.x * data->scale_pos;

		if (shape == PHYSICS_SHAPE_TERRAIN) {
			float unpack = (1.0f / 32767.0f) * data->scale_pos;
			field        = heightfield_from_mesh(pa, inda, unpack * scale.x, unpack * scale.y, unpack * scale.z);
			posa         = &field;
		}
		else {
			posa = pa;
		}
	}

	vec4_t loc  = obj->transform->loc;
	vec4_t off  = vec4_apply_quat(body->offset, obj->transform->rot);
	body->_body = body_create(shape, mass, body->dimx, body->dimy, body->dimz, loc.x + off.x, loc.y + off.y, loc.z + off.z, posa, inda, scale_pos);
	free(field.heights);

	if (shape == PHYSICS_SHAPE_BOX) { // Start out at the object rotation
		physics_body_sync_transform(body);
	}
	return body;
}

void physics_body_set_mass(physics_body_t *body, float mass) {
	body_ref_t ref = body_ref(body->_body);
	body->mass     = mass;
	*ref.mass      = mass;
	if (body_is_box(body->_body)) {
		box_t *b       = &boxes[body_slot(body->_body)];
		b->inv_inertia = box_inv_inertia(b->half, mass);
		if (mass == 0.0f) {
			b->angular = (vec4_t){0.0f, 0.0f, 0.0f};
		}
	}
	if (mass == 0.0f) {
		*ref.velocity = (vec4_t){0.0f, 0.0f, 0.0f};
	}
	body_wake(body->_body);
}

void physics_body_set_dim(physics_body_t *body, float dimx, float dimy, float dimz) {
	body->dimx = dimx;
	body->dimy = dimy;
	body->dimz = dimz;
	if (body_is_box(body->_body)) {
		box_t *b       = &boxes[body_slot(body->_body)];
		b->half        = (vec4_t){dimx / 2.0f, dimy / 2.0f, dimz / 2.0f};
		b->inv_inertia = box_inv_inertia(b->half, b->mass);
	}
	else if (body_is_sphere(body->_body)) {
		spheres[body_slot(body->_body)].radius = dimx / 2.0f;
	}
	body_wake(body->_body);
}

void physics_body_apply_impulse(void *body, vec4_t impulse) {
	vec4_t *vel = body_ref(body).velocity;
	vel->x += impulse.x;
	vel->y += impulse.y;
	vel->z += impulse.z;
	body_wake(body);
}

void physics_body_get_pos(void *body, vec4_t *pos) {
	vec4_t *p = body_ref(body).position;
	pos->x    = p->x;
	pos->y    = p->y;
	pos->z    = p->z;
}

void physics_body_get_rot(void *body, quat_t *rot) {
	if (body_is_box(body)) {
		*rot = boxes[body_slot(body)].rotation;
	}
}

void physics_body_get_velocity(void *body, vec4_t *vel) {
	vec4_t *v = body_ref(body).velocity;
	vel->x    = v->x;
	vel->y    = v->y;
	vel->z    = v->z;
}

void physics_body_set_velocity(void *body, float x, float y, float z) {
	vec4_t *vel = body_ref(body).velocity;
	vel->x      = x;
	vel->y      = y;
	vel->z      = z;
	body_wake(body);
}

void physics_body_sync_transform(physics_body_t *body) {
	transform_t *transform = body->obj->transform;
	vec4_t       off       = vec4_apply_quat(body->offset, transform->rot);
	vec4_t      *p         = body_ref(body->_body).position;
	p->x                   = transform->loc.x + off.x;
	p->y                   = transform->loc.y + off.y;
	p->z                   = transform->loc.z + off.z;
	if (body_is_box(body->_body)) {
		boxes[body_slot(body->_body)].rotation = transform->rot;
	}
	body_wake(body->_body);
}

void physics_body_set_rotation(physics_body_t *body, quat_t rot) {
	if (!body_is_box(body->_body)) {
		return;
	}
	box_t *b    = &boxes[body_slot(body->_body)];
	b->rotation = rot;
	b->angular  = (vec4_t){0.0f, 0.0f, 0.0f};
	body_wake(body->_body);
}

void physics_body_update(physics_body_t *body) {
	if (body->shape == PHYSICS_SHAPE_MESH || body->shape == PHYSICS_SHAPE_TERRAIN) {
		return; // Static collider, the object drives the shape
	}

	transform_t *transform = body->obj->transform;
	vec4_t       pos       = transform->loc;
	physics_body_get_pos(body->_body, &pos);
	physics_body_get_rot(body->_body, &transform->rot);

	vec4_t off       = vec4_apply_quat(body->offset, transform->rot);
	transform->loc.x = pos.x - off.x;
	transform->loc.y = pos.y - off.y;
	transform->loc.z = pos.z - off.z;
	transform_build_matrix(transform);
}

void physics_body_remove(physics_body_t *body) {
	if (body == NULL) {
		return;
	}
	body->obj->_->body = NULL;
	wake_all(); // Whatever was resting on this body has to fall now
	if (body->shape == PHYSICS_SHAPE_MESH) {
		mesh_clear();
		return;
	}
	if (body_is_terrain(body->_body)) {
		terrain_clear();
		return;
	}
	*body_ref(body->_body).active = 0;
	while (sphere_count > 0 && !spheres[sphere_count - 1].active) {
		sphere_count--;
	}
	while (box_count > 0 && !boxes[box_count - 1].active) {
		box_count--;
	}
}

float physics_body_get_speed(physics_body_t *body) {
	return vec4_len(*body_ref(body->_body).velocity);
}

void physics_set_friction(float v) {
	physics_friction = v * 0.1f;
}

void physics_set_bounciness(float v) {
	physics_bounciness = v;
}

void physics_set_gravity(float x, float y, float z) {
	physics_gravity = (vec4_t){x, y, z};
	wake_all();
}

#endif
