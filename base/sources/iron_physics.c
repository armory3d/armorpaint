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
#define CONTACT_SKIN    0.01f

#define SPHERE_TAG  0x10000
#define BOX_TAG     0x20000
#define TERRAIN_TAG 0x40000
#define MESH_TAG    0x80000
#define FIELD_TAG   0x100000
#define SLOT_MASK   0xffff

typedef struct {
	vec4_t min;
	vec4_t max;
} aabb_t;

typedef struct {
	vec4_t         position;
	vec4_t         prev_position;
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
	vec4_t         prev_position;
	vec4_t         velocity;
	quat_t         rotation;
	quat_t         prev_rotation;
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
	bvh_node_t *root; // Triangles in world space
	int         active;
	mat4_t      world;
	mat4_t      inv;
	mat4_t      prev; // Previous build, for the collider velocity
	float       dt;
} mesh_t;

typedef struct {
	int    active;
	int    wind; // Else an attractor
	vec4_t position;
	vec4_t dir; // Local z axis
	float  strength;
} field_t;

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
static mesh_t        *meshes;
static field_t       *fields;
static int            field_count;
static int            field_cap;
static int            collider_version;
static int            mesh_count;
static int            mesh_cap;
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
	float r = s->radius * (1.0f + CONTACT_SKIN);
	return dx * dx + dy * dy + dz * dz <= r * r;
}

static int bvh_axis;

static inline float tri_center(triangle_t *t, int axis) {
	return axis == 0 ? t->v0.x + t->v1.x + t->v2.x : axis == 1 ? t->v0.y + t->v1.y + t->v2.y : t->v0.z + t->v1.z + t->v2.z;
}

static int compare_triangles(const void *a, const void *b) {
	float ca = tri_center((triangle_t *)a, bvh_axis);
	float cb = tri_center((triangle_t *)b, bvh_axis);
	return (ca > cb) - (ca < cb);
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

	// Split along the longest axis of the centers
	float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
	for (int i = 0; i < num_tris; i++) {
		for (int a = 0; a < 3; a++) {
			float c = tri_center(&tris[i], a);
			lo[a]   = fminf(lo[a], c);
			hi[a]   = fmaxf(hi[a], c);
		}
	}
	bvh_axis = hi[1] - lo[1] > hi[0] - lo[0] ? 1 : 0;
	bvh_axis = hi[2] - lo[2] > hi[bvh_axis] - lo[bvh_axis] ? 2 : bvh_axis;
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

static void sphere_mesh_contact(sphere_t *s, vec4_t point, vec4_t surface, vec4_t n);

static void collide_sphere_triangle(sphere_t *s, int si, triangle_t *t) {
	if (!sphere_aabb_intersect(s, &t->bounds)) {
		return;
	}

	vec4_t to_sphere = vec4_sub(s->position, t->v0);
	float  dist      = vec4_dot(to_sphere, t->normal);
	float  skin      = s->radius * CONTACT_SKIN;
	if (dist < 0.0f || dist > s->radius + skin) {
		return;
	}

	vec4_t p  = vec4_sub(s->position, vec4_mult(t->normal, dist));
	vec4_t e0 = vec4_sub(t->v1, t->v0), e1 = vec4_sub(t->v2, t->v1), e2 = vec4_sub(t->v0, t->v2);
	vec4_t c0 = vec4_sub(p, t->v0), c1 = vec4_sub(p, t->v1), c2 = vec4_sub(p, t->v2);

	if (vec4_dot(t->normal, vec4_cross(e0, c0)) >= 0 && vec4_dot(t->normal, vec4_cross(e1, c1)) >= 0 && vec4_dot(t->normal, vec4_cross(e2, c2)) >= 0) {

		float orig_dist = dist;

		if (si >= 0 && dist < s->radius) {
			sphere_mesh_contact(s, vec4_sub(s->position, vec4_mult(t->normal, s->radius)), p, t->normal);
		}
		else if (si < 0 && dist < s->radius) {
			s->position = vec4_add(s->position, vec4_mult(t->normal, s->radius - dist));
		}

		vec4_t contact_point = vec4_sub(s->position, vec4_mult(t->normal, s->radius));
		report_contact(&s->contact, &s->contact_depth, s->radius + skin - orig_dist, contact_point, t->normal);
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
#define SUBSTEPS           16
#define CONTACT_MAX_PUSH   2.0f // m/s

typedef struct {
	vec4_t *position;
	quat_t *rotation;
	vec4_t *velocity;
	vec4_t *angular;
	float   inv_mass;
	float   inv_inertia;
	float  *sleep_timer;
	int    *sleeping;
} rigid_t;

static vec4_t rigid_zero;
static vec4_t rigid_zero_pos;
static float  rigid_no_timer = SLEEP_TIME; // The static world is always settled
static int    rigid_never_sleeps;

static rigid_t rigid_static(void) {
	return (rigid_t){&rigid_zero_pos, NULL, &rigid_zero, &rigid_zero, 0.0f, 0.0f, &rigid_no_timer, &rigid_never_sleeps};
}

static rigid_t rigid_box(box_t *b) {
	float inv_mass = b->mass > 0.0f && !b->sleeping ? 1.0f / b->mass : 0.0f;
	return (rigid_t){&b->position, &b->rotation, &b->velocity, &b->angular, inv_mass, b->sleeping ? 0.0f : b->inv_inertia, &b->sleep_timer, &b->sleeping};
}

static rigid_t rigid_sphere(sphere_t *s) {
	float inv_mass = s->mass > 0.0f && !s->sleeping ? 1.0f / s->mass : 0.0f;
	return (rigid_t){&s->position, NULL, &s->velocity, &rigid_zero, inv_mass, 0.0f, &s->sleep_timer, &s->sleeping};
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

static inline quat_t quat_conj(quat_t q) {
	return (quat_t){-q.x, -q.y, -q.z, q.w};
}

static vec4_t rigid_to_local(rigid_t r, vec4_t p) {
	vec4_t d = vec4_sub(p, *r.position);
	return r.rotation != NULL ? vec4_apply_quat(d, quat_conj(*r.rotation)) : d;
}

static vec4_t rigid_to_world(rigid_t r, vec4_t l) {
	return vec4_add(*r.position, r.rotation != NULL ? vec4_apply_quat(l, *r.rotation) : l);
}

static vec4_t rigid_velocity_at(rigid_t r, vec4_t p) {
	return vec4_add(*r.velocity, vec4_cross(*r.angular, vec4_sub(p, *r.position)));
}

// Inverse effective mass along n at p
static float rigid_give(rigid_t r, vec4_t p, vec4_t n) {
	vec4_t rn = vec4_cross(vec4_sub(p, *r.position), n);
	return r.inv_mass + r.inv_inertia * vec4_dot(rn, rn);
}

// Positional impulse at p
static void rigid_shift(rigid_t r, vec4_t p, vec4_t impulse) {
	vec4_t arm  = vec4_sub(p, *r.position);
	*r.position = vec4_add(*r.position, vec4_mult(impulse, r.inv_mass));
	if (r.rotation != NULL && r.inv_inertia > 0.0f) {
		vec4_t w  = vec4_mult(vec4_cross(arm, impulse), r.inv_inertia);
		quat_t dq = quat_mult((quat_t){w.x, w.y, w.z, 0.0f}, *r.rotation);
		r.rotation->x += 0.5f * dq.x;
		r.rotation->y += 0.5f * dq.y;
		r.rotation->z += 0.5f * dq.z;
		r.rotation->w += 0.5f * dq.w;
		*r.rotation = quat_norm(*r.rotation);
	}
}

static void apply_impulse(rigid_t body, vec4_t r, vec4_t impulse) {
	if (body.inv_mass > 0.0f) {
		*body.velocity = vec4_add(*body.velocity, vec4_mult(impulse, body.inv_mass));
	}
	if (body.inv_inertia > 0.0f) {
		*body.angular = vec4_add(*body.angular, vec4_mult(vec4_cross(r, impulse), body.inv_inertia));
	}
}

typedef struct {
	rigid_t a;
	rigid_t b;
	vec4_t  ra; // Local
	vec4_t  rb;
	vec4_t  n; // From b towards a
	float   lambda;
	float   vn;
	float   bounce;
	float   h;
} contact_t;

static contact_t *contacts;
static int        contact_count;
static int        contact_cap;
static float      contact_h;

// point_a inside b, point_b on the surface of b
static void contact_add(rigid_t a, rigid_t b, vec4_t point_a, vec4_t point_b, vec4_t n, float bounce) {
	if (a.inv_mass + b.inv_mass <= 0.0f) {
		return;
	}
	if (rigid_on_the_move(a) || rigid_on_the_move(b)) {
		rigid_wake(a);
		rigid_wake(b);
	}
	if (contact_count == contact_cap) {
		contact_cap = contact_cap == 0 ? 64 : contact_cap * 2;
		contacts    = realloc(contacts, sizeof(contact_t) * contact_cap);
	}
	vec4_t rel                = vec4_sub(rigid_velocity_at(a, point_a), rigid_velocity_at(b, point_b));
	contacts[contact_count++] = (contact_t){a, b, rigid_to_local(a, point_a), rigid_to_local(b, point_b), n, 0.0f, vec4_dot(rel, n), bounce, contact_h};
}

static void sphere_mesh_contact(sphere_t *s, vec4_t point, vec4_t surface, vec4_t n) {
	contact_add(rigid_sphere(s), rigid_static(), point, surface, n, physics_bounciness);
}

static void contact_solve(contact_t *c) {
	vec4_t pa    = rigid_to_world(c->a, c->ra);
	vec4_t pb    = rigid_to_world(c->b, c->rb);
	float  depth = vec4_dot(vec4_sub(pb, pa), c->n);
	float  w     = rigid_give(c->a, pa, c->n) + rigid_give(c->b, pb, c->n);
	if (depth <= 0.0f || w <= 0.0f) {
		return;
	}
	depth    = fminf(depth, (fmaxf(-c->vn, 0.0f) + CONTACT_MAX_PUSH) * c->h);
	float dl = depth / w;
	c->lambda += dl;
	rigid_shift(c->a, pa, vec4_mult(c->n, dl));
	rigid_shift(c->b, pb, vec4_mult(c->n, -dl));
}

// Friction and restitution
static void contact_velocity(contact_t *c, float h) {
	if (c->lambda <= 0.0f) {
		return;
	}
	vec4_t pa  = rigid_to_world(c->a, c->ra);
	vec4_t pb  = rigid_to_world(c->b, c->rb);
	float  w   = rigid_give(c->a, pa, c->n) + rigid_give(c->b, pb, c->n);
	vec4_t rel = vec4_sub(rigid_velocity_at(c->a, pa), rigid_velocity_at(c->b, pb));
	float  vn  = vec4_dot(rel, c->n);
	vec4_t vt  = vec4_sub(rel, vec4_mult(c->n, vn));
	float  lt  = vec4_len(vt);
	vec4_t dv  = {0.0f, 0.0f, 0.0f, 0.0f};
	if (lt > 1e-6f) {
		float stop = fminf(CONTACT_FRICTION * c->lambda * w / h, lt);
		dv         = vec4_mult(vt, -stop / lt);
	}
	float bounce = fabsf(c->vn) > 2.0f * vec4_len(physics_gravity) * h ? c->bounce : 0.0f;
	dv           = vec4_add(dv, vec4_mult(c->n, -vn + fmaxf(-bounce * c->vn, 0.0f)));
	vec4_t p     = vec4_mult(dv, 1.0f / w);
	apply_impulse(c->a, vec4_sub(pa, *c->a.position), p);
	apply_impulse(c->b, vec4_sub(pb, *c->b.position), vec4_mult(p, -1.0f));
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

static inline float box_half(box_t *b, int i) {
	return i == 0 ? b->half.x : i == 1 ? b->half.y : b->half.z;
}

// Axis of least overlap, a (0 - 2) or b (3 - 5), normal from b towards a
static int box_contact_normal(box_t *a, box_t *b, float margin, vec4_t *out_normal, float *out_depth, int *out_axis) {
	vec4_t delta     = vec4_sub(a->position, b->position);
	vec4_t best_axis = {0.0f, 0.0f, 1.0f};
	float  best      = 0.0f;

	for (int i = 0; i < 6; i++) {
		vec4_t axis    = i < 3 ? box_axis(a, i) : box_axis(b, i - 3);
		float  overlap = box_extent(a, axis) + box_extent(b, axis) - fabsf(vec4_dot(delta, axis));
		if (overlap < -margin) {
			return 0; // A gap along this direction, so the boxes are apart
		}
		if (i == 0 || overlap < best) {
			best      = overlap;
			best_axis = axis;
			*out_axis = i;
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

// Incident face clipped by the sides of the reference face
static void collide_box_box(int i, int j) {
	box_t *a = &boxes[i];
	box_t *b = &boxes[j];
	if (a->mass == 0.0f && b->mass == 0.0f) {
		return;
	}
	if (vec4_len(vec4_sub(b->position, a->position)) > vec4_len(a->half) + vec4_len(b->half)) {
		return;
	}
	float  skin = fminf(fminf(fminf(a->half.x, a->half.y), a->half.z), fminf(fminf(b->half.x, b->half.y), b->half.z)) * CONTACT_SKIN;
	vec4_t n;
	float  overlap;
	int    axis;
	if (!box_contact_normal(a, b, skin, &n, &overlap, &axis)) {
		return;
	}
	box_t *ref = axis < 3 ? a : b;
	box_t *inc = axis < 3 ? b : a;
	vec4_t rn  = axis < 3 ? vec4_mult(n, -1.0f) : n;
	int    ra  = axis % 3;
	vec4_t rc  = vec4_add(ref->position, vec4_mult(rn, box_half(ref, ra)));

	// Incident face
	int   ia   = 0;
	float best = 0.0f;
	for (int k = 0; k < 3; k++) {
		float d = vec4_dot(box_axis(inc, k), rn);
		if (fabsf(d) > fabsf(best)) {
			best = d;
			ia   = k;
		}
	}
	vec4_t e1      = vec4_mult(box_axis(inc, (ia + 1) % 3), box_half(inc, (ia + 1) % 3));
	vec4_t e2      = vec4_mult(box_axis(inc, (ia + 2) % 3), box_half(inc, (ia + 2) % 3));
	vec4_t ic      = vec4_add(inc->position, vec4_mult(box_axis(inc, ia), best > 0.0f ? -box_half(inc, ia) : box_half(inc, ia)));
	vec4_t poly[8] = {vec4_add(ic, vec4_add(e1, e2)), vec4_sub(vec4_add(ic, e1), e2), vec4_sub(vec4_sub(ic, e1), e2), vec4_add(vec4_sub(ic, e1), e2)};
	int    count   = 4;

	for (int side = 0; side < 4 && count > 0; side++) {
		int    k     = (ra + 1 + side / 2) % 3;
		vec4_t plane = vec4_mult(box_axis(ref, k), side % 2 ? -1.0f : 1.0f);
		float  limit = vec4_dot(ref->position, plane) + box_half(ref, k);
		vec4_t out[8];
		int    kept = 0;
		for (int v = 0; v < count; v++) {
			vec4_t p  = poly[v];
			vec4_t q  = poly[(v + 1) % count];
			float  dp = vec4_dot(p, plane) - limit;
			float  dq = vec4_dot(q, plane) - limit;
			if (dp <= 0.0f) {
				out[kept++] = p;
			}
			if ((dp < 0.0f) != (dq < 0.0f) && kept < 8) {
				out[kept++] = vec4_add(p, vec4_mult(vec4_sub(q, p), dp / (dp - dq)));
			}
		}
		memcpy(poly, out, sizeof(vec4_t) * kept);
		count = kept;
	}

	for (int v = 0; v < count; v++) {
		float depth = fminf(vec4_dot(vec4_sub(rc, poly[v]), rn), overlap);
		if (depth <= -skin) {
			continue;
		}
		vec4_t on = vec4_add(poly[v], vec4_mult(rn, depth));
		vec4_t pa = inc == a ? poly[v] : on;
		vec4_t pb = inc == a ? on : poly[v];
		contact_add(rigid_box(a), rigid_box(b), pa, pb, n, physics_bounciness);
		report_contact(&a->contact, &a->contact_depth, depth + skin, pa, n);
		report_contact(&b->contact, &b->contact_depth, depth + skin, pb, vec4_mult(n, -1.0f));
	}
}

static void collide_box_sphere(int bi, int si) {
	box_t    *b = &boxes[bi];
	sphere_t *s = &spheres[si];

	vec4_t closest = box_closest_point(b, s->position);
	vec4_t delta   = vec4_sub(s->position, closest);
	float  dist    = vec4_len(delta);

	vec4_t n; // Points from the box towards the sphere
	float  depth;
	float  skin = s->radius * CONTACT_SKIN;
	if (dist > 0.0001f) {
		if (dist >= s->radius + skin) {
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
	contact_add(rigid_sphere(s), rigid_box(b), point, vec4_add(point, vec4_mult(n, depth)), n, physics_bounciness);
	report_contact(&b->contact, &b->contact_depth, depth + skin, point, vec4_mult(n, -1.0f));
}

static void collide_sphere_sphere(int i, int j) {
	sphere_t *a = &spheres[i];
	sphere_t *b = &spheres[j];

	vec4_t delta    = vec4_sub(a->position, b->position);
	float  dist     = vec4_len(delta);
	float  min_dist = a->radius + b->radius;
	if (dist >= min_dist * (1.0f + CONTACT_SKIN) || dist < 0.0001f) {
		return;
	}
	vec4_t n = vec4_mult(delta, 1.0f / dist);
	contact_add(rigid_sphere(a), rigid_sphere(b), vec4_sub(a->position, vec4_mult(n, a->radius)), vec4_add(b->position, vec4_mult(n, b->radius)), n,
	            SPHERE_RESTITUTION);
}

// Deepest mesh triangle a box corner sank into
static void collide_corner_mesh(bvh_node_t *node, vec4_t p, float max_depth, vec4_t *n, float *depth) {
	if (node == NULL || p.x < node->bounds.min.x - max_depth || p.y < node->bounds.min.y - max_depth || p.z < node->bounds.min.z - max_depth ||
	    p.x > node->bounds.max.x + max_depth || p.y > node->bounds.max.y + max_depth || p.z > node->bounds.max.z + max_depth) {
		return;
	}
	if (!node->is_leaf) {
		collide_corner_mesh(node->left, p, max_depth, n, depth);
		collide_corner_mesh(node->right, p, max_depth, n, depth);
		return;
	}
	for (int i = 0; i < node->num_tris; i++) {
		triangle_t *t = &node->triangles[i];
		float       d = -vec4_dot(vec4_sub(p, t->v0), t->normal);
		if (d <= 0.0f || d > max_depth || d <= *depth) {
			continue;
		}
		vec4_t q  = vec4_add(p, vec4_mult(t->normal, d));
		vec4_t e0 = vec4_sub(t->v1, t->v0), e1 = vec4_sub(t->v2, t->v1), e2 = vec4_sub(t->v0, t->v2);
		if (vec4_dot(t->normal, vec4_cross(e0, vec4_sub(q, t->v0))) >= 0 && vec4_dot(t->normal, vec4_cross(e1, vec4_sub(q, t->v1))) >= 0 &&
		    vec4_dot(t->normal, vec4_cross(e2, vec4_sub(q, t->v2))) >= 0) {
			*depth = d;
			*n     = t->normal;
		}
	}
}

static void collide_box_mesh(int bi) {
	box_t *b         = &boxes[bi];
	float  max_depth = fminf(b->half.x, fminf(b->half.y, b->half.z));
	for (int c = 0; c < 8; c++) {
		vec4_t corner = box_corner(b, c);
		vec4_t n;
		float  depth = 0.0f;
		for (int m = 0; m < mesh_count; m++) {
			collide_corner_mesh(meshes[m].root, corner, max_depth, &n, &depth);
		}
		if (depth > 0.0f) {
			contact_add(rigid_box(b), rigid_static(), corner, vec4_add(corner, vec4_mult(n, depth)), n, physics_bounciness);
			report_contact(&b->contact, &b->contact_depth, depth, corner, n);
		}
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
	for (int c = 0; c < 8; c++) {
		vec4_t corner = box_corner(b, c);
		float  ground;
		vec4_t normal;
		if (!terrain_sample(corner.x, corner.y, &ground, &normal) || ground <= corner.z) {
			continue;
		}
		float depth = depth_along_normal(ground - corner.z, normal);
		contact_add(rigid_box(b), rigid_static(), corner, vec4_add(corner, vec4_mult(normal, depth)), normal, physics_bounciness);
		report_contact(&b->contact, &b->contact_depth, depth, corner, normal);
	}
}

static void collide_sphere_terrain(int si) {
	sphere_t *s = &spheres[si];
	float     ground;
	vec4_t    normal;
	if (!terrain_sample(s->position.x, s->position.y, &ground, &normal)) {
		return;
	}
	float drop = ground - (s->position.z - s->radius);
	if (drop <= 0.0f) {
		return;
	}
	float  depth = depth_along_normal(drop, normal);
	vec4_t point = vec4_sub(s->position, vec4_mult(normal, s->radius));
	contact_add(rigid_sphere(s), rigid_static(), point, vec4_add(point, vec4_mult(normal, depth)), normal, physics_bounciness);
	report_contact(&s->contact, &s->contact_depth, depth, point, normal);
}

static void terrain_clear() {
	free(terrain.heights);
	memset(&terrain, 0, sizeof(terrain));
}

static void mesh_clear() {
	for (int i = 0; i < mesh_count; i++) {
		free_bvh(meshes[i].root);
	}
	mesh_count = 0;
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
	field_count  = 0;
	collider_version++;
}

void physics_world_destroy() {
	mesh_clear();
	terrain_clear();
}

// Contacts of the predicted poses
static void world_contacts() {
	contact_count = 0;
	grid_build(&sphere_grid, sphere_count);
	grid_build(&box_grid, box_count);
	for (int i = 0; i < sphere_count; i++) {
		sphere_t *s = &spheres[i];
		if (!s->active) {
			continue;
		}
		if (s->mass != 0.0f) {
			for (int m = 0; m < mesh_count; m++) {
				query_bvh(s, i, meshes[m].root);
			}
			collide_sphere_terrain(i);
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
		if (!b->active) {
			continue;
		}
		if (b->mass != 0.0f) {
			collide_box_terrain(i);
			collide_box_mesh(i);
			int n = grid_find(&box_grid, box_count, b->position, vec4_len(b->half));
			for (int k = 0; k < n; k++) {
				int j = candidates[k];
				if (j != i && boxes[j].active && (boxes[j].mass == 0.0f || j > i)) {
					collide_box_box(i, j);
				}
			}
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

void physics_world_step(float dt) {
	float h   = dt / SUBSTEPS;
	contact_h = h;
	pairs_clear();
	for (int step = 0; step < SUBSTEPS; step++) {
		// Predict
		for (int i = 0; i < sphere_count; i++) {
			sphere_t *s = &spheres[i];
			if (!s->active || s->mass == 0.0f || s->sleeping) {
				continue;
			}
			s->prev_position = s->position;
			s->velocity      = vec4_add(s->velocity, vec4_mult(vec4_add(physics_gravity, physics_field_force(s->position)), h));
			s->position      = vec4_add(s->position, vec4_mult(s->velocity, h));
		}
		for (int i = 0; i < box_count; i++) {
			box_t *b = &boxes[i];
			if (!b->active || b->mass == 0.0f || b->sleeping) {
				continue;
			}
			b->prev_position = b->position;
			b->prev_rotation = b->rotation;
			b->velocity      = vec4_add(b->velocity, vec4_mult(vec4_add(physics_gravity, physics_field_force(b->position)), h));
			b->position      = vec4_add(b->position, vec4_mult(b->velocity, h));
			b->angular       = vec4_mult(b->angular, 1.0f - fminf(1.0f, ANGULAR_DAMPING * h));
			quat_t dq        = quat_mult((quat_t){b->angular.x, b->angular.y, b->angular.z, 0.0f}, b->rotation);
			b->rotation.x += dq.x * 0.5f * h;
			b->rotation.y += dq.y * 0.5f * h;
			b->rotation.z += dq.z * 0.5f * h;
			b->rotation.w += dq.w * 0.5f * h;
			b->rotation = quat_norm(b->rotation);
		}

		// Solve positions, then derive velocities
		world_contacts();
		for (int c = 0; c < contact_count; c++) {
			contact_solve(&contacts[c]);
		}
		for (int i = 0; i < sphere_count; i++) {
			sphere_t *s = &spheres[i];
			if (s->active && s->mass != 0.0f && !s->sleeping) {
				s->velocity = vec4_mult(vec4_sub(s->position, s->prev_position), 1.0f / h);
			}
		}
		for (int i = 0; i < box_count; i++) {
			box_t *b = &boxes[i];
			if (!b->active || b->mass == 0.0f || b->sleeping) {
				continue;
			}
			b->velocity = vec4_mult(vec4_sub(b->position, b->prev_position), 1.0f / h);
			quat_t dq   = quat_mult(b->rotation, quat_conj(b->prev_rotation));
			float  sign = dq.w < 0.0f ? -1.0f : 1.0f;
			b->angular  = (vec4_t){dq.x * 2.0f / h * sign, dq.y * 2.0f / h * sign, dq.z * 2.0f / h * sign, 0.0f};
		}
		for (int c = 0; c < contact_count; c++) {
			contact_velocity(&contacts[c], h);
		}
	}

	// Moving bodies change the colliders
	for (int i = 0; i < sphere_count; i++) {
		collider_version += spheres[i].active && spheres[i].mass != 0.0f && !spheres[i].sleeping;
	}
	for (int i = 0; i < box_count; i++) {
		collider_version += boxes[i].active && boxes[i].mass != 0.0f && !boxes[i].sleeping;
	}

	// Put bodies that have settled on something to sleep, so they stop entirely
	for (int i = 0; i < sphere_count; i++) {
		sphere_t *s = &spheres[i];
		if (s->active && s->mass != 0.0f && !s->sleeping) {
			settling_t settling = {&s->position, NULL, &s->velocity, &rigid_zero, &s->sleep_pos, NULL, &s->sleep_timer, &s->sleeping};
			sleep_update(settling, s->contact_depth > 0.0f, dt);
		}
	}
	for (int i = 0; i < box_count; i++) {
		box_t *b = &boxes[i];
		if (b->active && b->mass != 0.0f && !b->sleeping) {
			settling_t settling = {&b->position, &b->rotation, &b->velocity, &b->angular, &b->sleep_pos, &b->sleep_rot, &b->sleep_timer, &b->sleeping};
			sleep_update(settling, b->contact_depth > 0.0f, dt);
		}
	}
}

void physics_world_update() {
	physics_world_step(sys_delta());

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

// Mesh collider triangles in world space
static void mesh_build(int slot, object_t *obj, float dt) {
	mesh_object_t *mo       = obj->ext;
	i16_array_t   *pa       = mesh_data_get_vertex_array(mo->data, "pos")->values;
	u32_array_t   *ia       = mo->data->index_array;
	int            num_tris = 0;
	triangle_t    *tris     = malloc(sizeof(triangle_t) * (ia->length / 3 + 1));
	transform_build_matrix(obj->transform);
	mat4_t  m  = obj->transform->world_unpack;
	mesh_t *mt = &meshes[slot];
	mt->prev   = mt->root != NULL ? mt->world : m;
	mt->world  = m;
	mt->inv    = mat4_inv(m);
	mt->dt     = dt;
	collider_version++;
	for (int i = 0; i < ia->length / 3; i++) {
		vec4_t v[3];
		for (int k = 0; k < 3; k++) {
			int16_t *p = &pa->buffer[ia->buffer[i * 3 + k] * 4];
			v[k]       = vec4_apply_mat4((vec4_t){p[0] / 32767.0f, p[1] / 32767.0f, p[2] / 32767.0f, 1.0f}, m);
		}
		vec4_t cross = vec4_cross(vec4_sub(v[1], v[0]), vec4_sub(v[2], v[0]));
		float  len   = vec4_len(cross);
		if (len < 1e-12f) {
			continue; // Degenerate
		}
		tris[num_tris++] = (triangle_t){.v0     = v[0],
		                                .v1     = v[1],
		                                .v2     = v[2],
		                                .normal = vec4_mult(cross, 1.0f / len),
		                                .bounds = {.min = {min3(v[0].x, v[1].x, v[2].x), min3(v[0].y, v[1].y, v[2].y), min3(v[0].z, v[1].z, v[2].z)},
		                                           .max = {max3(v[0].x, v[1].x, v[2].x), max3(v[0].y, v[1].y, v[2].y), max3(v[0].z, v[1].z, v[2].z)}}};
	}
	free_bvh(meshes[slot].root);
	meshes[slot].root = num_tris > 0 ? create_bvh_node(tris, num_tris, 0) : NULL;
	free(tris);
}

static void *mesh_add(object_t *obj) {
	int slot = 0;
	while (slot < mesh_count && meshes[slot].active) {
		slot++;
	}
	if (slot > SLOT_MASK) {
		return NULL;
	}
	if (slot == mesh_cap) {
		meshes = body_array_grow(meshes, &mesh_cap, sizeof(mesh_t));
	}
	if (slot >= mesh_count) {
		mesh_count = slot + 1;
	}
	meshes[slot] = (mesh_t){.active = 1};
	mesh_build(slot, obj, 0.0f);
	return (void *)(uintptr_t)(slot | MESH_TAG);
}

static void field_place(int slot, object_t *obj) {
	transform_build_matrix(obj->transform);
	mat4_t m              = obj->transform->world;
	fields[slot].position = vec4_apply_mat4((vec4_t){0.0f, 0.0f, 0.0f, 1.0f}, m);
	fields[slot].dir      = vec4_norm(vec4_apply_mat4((vec4_t){0.0f, 0.0f, 1.0f, 0.0f}, m));
}

static void *field_add(object_t *obj, int wind, float strength) {
	int slot = 0;
	while (slot < field_count && fields[slot].active) {
		slot++;
	}
	if (slot > SLOT_MASK) {
		return NULL;
	}
	if (slot == field_cap) {
		fields = body_array_grow(fields, &field_cap, sizeof(field_t));
	}
	if (slot >= field_count) {
		field_count = slot + 1;
	}
	fields[slot] = (field_t){.active = 1, .wind = wind, .strength = strength};
	field_place(slot, obj);
	return (void *)(uintptr_t)(slot | FIELD_TAG);
}

// Inverse square pull of the attractors
vec4_t physics_field_force(vec4_t p) {
	vec4_t f = {0.0f, 0.0f, 0.0f, 0.0f};
	for (int i = 0; i < field_count; i++) {
		if (!fields[i].active || fields[i].wind) {
			continue;
		}
		vec4_t d  = vec4_sub(fields[i].position, p);
		float  r2 = fmaxf(vec4_dot(d, d), 0.25f);
		f         = vec4_add(f, vec4_mult(d, fields[i].strength / (r2 * sqrtf(vec4_dot(d, d) + 1e-12f))));
	}
	return f;
}

vec4_t physics_field_wind(vec4_t p) {
	vec4_t w = {0.0f, 0.0f, 0.0f, 0.0f};
	for (int i = 0; i < field_count; i++) {
		if (fields[i].active && fields[i].wind) {
			w = vec4_add(w, vec4_mult(fields[i].dir, fields[i].strength));
		}
	}
	return w;
}

int physics_field_any(int wind) {
	for (int i = 0; i < field_count; i++) {
		if (fields[i].active && fields[i].wind == wind && fields[i].strength != 0.0f) {
			return 1;
		}
	}
	return 0;
}

int physics_collider_version() {
	return collider_version;
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

	if (shape == PHYSICS_SHAPE_MESH) {
		body->_body = mesh_add(obj);
		return body;
	}
	if (shape == PHYSICS_SHAPE_WIND || shape == PHYSICS_SHAPE_ATTRACTOR) {
		body->_body = field_add(obj, shape == PHYSICS_SHAPE_WIND, mass);
		return body;
	}
	if (shape == PHYSICS_SHAPE_DOMAIN) {
		return body;
	}
	collider_version++;

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
	if (body->shape >= PHYSICS_SHAPE_WIND) { // Fields and the domain
		body->mass = mass;
		if (body->_body != NULL) {
			fields[body_slot(body->_body)].strength = mass;
		}
		return;
	}
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

void physics_body_apply_impulse_at(void *body, vec4_t impulse, vec4_t point) {
	rigid_t r = body_is_box(body) ? rigid_box(&boxes[body_slot(body)]) : body_is_sphere(body) ? rigid_sphere(&spheres[body_slot(body)]) : rigid_static();
	if (r.inv_mass <= 0.0f && (body_is_box(body) || body_is_sphere(body)) && *body_ref(body).mass > 0.0f) {
		body_wake(body);
		r = body_is_box(body) ? rigid_box(&boxes[body_slot(body)]) : rigid_sphere(&spheres[body_slot(body)]);
	}
	apply_impulse(r, vec4_sub(point, *r.position), impulse);
}

vec4_t physics_body_velocity_at(void *body, vec4_t point) {
	rigid_t r = body_is_box(body) ? rigid_box(&boxes[body_slot(body)]) : body_is_sphere(body) ? rigid_sphere(&spheres[body_slot(body)]) : rigid_static();
	return rigid_velocity_at(r, point);
}

float physics_body_give(void *body, vec4_t point, vec4_t n) {
	if (body_is_box(body)) {
		box_t *b = &boxes[body_slot(body)];
		if (b->mass <= 0.0f) {
			return 0.0f;
		}
		vec4_t rn = vec4_cross(vec4_sub(point, b->position), n);
		return 1.0f / b->mass + b->inv_inertia * vec4_dot(rn, rn);
	}
	if (body_is_sphere(body)) {
		sphere_t *s = &spheres[body_slot(body)];
		return s->mass > 0.0f ? 1.0f / s->mass : 0.0f;
	}
	return 0.0f;
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
	physics_body_move(body, 0.0f);
}

void physics_body_move(physics_body_t *body, float dt) {
	if (body->shape == PHYSICS_SHAPE_MESH) {
		if (body->_body != NULL) {
			mesh_build(body_slot(body->_body), body->obj, dt);
		}
		wake_all();
		return;
	}
	if (body->shape >= PHYSICS_SHAPE_WIND) { // Fields and the domain
		if (body->_body != NULL) {
			field_place(body_slot(body->_body), body->obj);
		}
		return;
	}
	transform_t *transform = body->obj->transform;
	vec4_t       off       = vec4_apply_quat(body->offset, transform->rot);
	body_ref_t   ref       = body_ref(body->_body);
	vec4_t       old       = *ref.position;
	ref.position->x        = transform->loc.x + off.x;
	ref.position->y        = transform->loc.y + off.y;
	ref.position->z        = transform->loc.z + off.z;
	if (*ref.mass == 0.0f) { // Animated static body
		*ref.velocity = dt > 0.0f ? vec4_mult(vec4_sub(*ref.position, old), 1.0f / dt) : (vec4_t){0.0f, 0.0f, 0.0f, 0.0f};
	}
	if (body_is_box(body->_body)) {
		boxes[body_slot(body->_body)].rotation = transform->rot;
	}
	collider_version++;
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
	if (body->shape != PHYSICS_SHAPE_BOX && body->shape != PHYSICS_SHAPE_SPHERE) {
		return; // Static collider or field, the object drives the shape
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
	collider_version++;
	if (body->shape >= PHYSICS_SHAPE_WIND) { // Fields and the domain
		if (body->_body != NULL) {
			fields[body_slot(body->_body)].active = 0;
		}
		return;
	}
	if (body->shape == PHYSICS_SHAPE_MESH) {
		if (body->_body != NULL) {
			int slot = body_slot(body->_body);
			free_bvh(meshes[slot].root);
			meshes[slot] = (mesh_t){0};
			while (mesh_count > 0 && !meshes[mesh_count - 1].active) {
				mesh_count--;
			}
		}
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

vec4_t physics_get_gravity() {
	return physics_gravity;
}

// Pushes a small sphere out of the colliders, returns 1 on contact
int physics_collide_point(vec4_t *pos, float radius, physics_hit_t *out) {
	int    hit  = 0;
	vec4_t v    = {0.0f, 0.0f, 0.0f, 0.0f};
	void  *body = NULL;

	for (int m = 0; m < mesh_count; m++) {
		sphere_t s = {.position = *pos, .radius = radius, .mass = 1.0f, .active = 1};
		query_bvh(&s, -1, meshes[m].root);
		if (s.contact_depth > 0.0f) {
			*pos          = s.position;
			hit           = 1;
			mesh_t *mt    = &meshes[m];
			vec4_t  local = vec4_apply_mat4((vec4_t){pos->x, pos->y, pos->z, 1.0f}, mt->inv);
			vec4_t  was   = vec4_apply_mat4(local, mt->prev);
			v             = mt->dt > 0.0f ? vec4_mult(vec4_sub(*pos, was), 1.0f / mt->dt) : (vec4_t){0.0f, 0.0f, 0.0f, 0.0f};
			v.w           = 0.0f;
		}
	}

	for (int i = 0; i < sphere_count; i++) {
		sphere_t *b    = &spheres[i];
		vec4_t    d    = vec4_sub(*pos, b->position);
		float     dist = vec4_len(d);
		float     min  = b->radius + radius;
		if (!b->active || dist >= min || dist < 1e-6f) {
			continue;
		}
		*pos = vec4_add(b->position, vec4_mult(d, min / dist));
		hit  = 1;
		v    = b->velocity;
		body = b->mass > 0.0f ? (void *)(uintptr_t)(i | SPHERE_TAG) : NULL;
	}

	float  ground;
	vec4_t normal;
	if (terrain_sample(pos->x, pos->y, &ground, &normal)) {
		float drop = ground - (pos->z - radius);
		if (drop > 0.0f) {
			*pos = vec4_add(*pos, vec4_mult(normal, depth_along_normal(drop, normal)));
			hit  = 1;
		}
	}

	for (int i = 0; i < box_count; i++) {
		box_t *b     = &boxes[i];
		vec4_t to    = vec4_sub(*pos, b->position);
		float  reach = (radius + vec4_len(b->half)) * 1.001f + 1e-4f;
		if (!b->active || vec4_dot(to, to) > reach * reach) {
			continue;
		}
		vec4_t closest = box_closest_point(b, *pos);
		vec4_t delta   = vec4_sub(*pos, closest);
		float  dist    = vec4_len(delta);
		vec4_t n;
		float  depth;
		if (dist > 0.0001f) {
			if (dist >= radius) {
				continue;
			}
			n     = vec4_mult(delta, 1.0f / dist);
			depth = radius - dist;
		}
		else { // Center is inside the box
			if (!box_point_depth(b, *pos, &n, &depth)) {
				continue;
			}
			depth += radius;
		}
		*pos = vec4_add(*pos, vec4_mult(n, depth));
		hit  = 1;
		v    = vec4_add(b->velocity, vec4_cross(b->angular, vec4_sub(*pos, b->position)));
		body = b->mass > 0.0f ? (void *)(uintptr_t)(i | BOX_TAG) : NULL;
	}

	if (out != NULL) {
		*out = (physics_hit_t){v, body};
	}
	return hit;
}

#endif
