#ifdef WITH_PHYSICS

#include "iron_physics.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define CLOTH_SUBSTEPS 20
#define CLOTH_FRICTION 0.5f
#define CLOTH_DRAG     2.0f
#define CLOTH_SOAK     10.0f
#define CLOTH_DENSITY  0.3f

typedef struct {
	int a, b, far;
} cloth_edge_t;

static int cloth_edge_compare(const void *pa, const void *pb) {
	const cloth_edge_t *x = pa, *y = pb;
	return x->a != y->a ? x->a - y->a : x->b - y->b;
}

static void cloth_add_constraint(physics_cloth_t *c, int a, int b) {
	float d[3]          = {c->pos[b * 3] - c->pos[a * 3], c->pos[b * 3 + 1] - c->pos[a * 3 + 1], c->pos[b * 3 + 2] - c->pos[a * 3 + 2]};
	int   i             = c->constraint_count++;
	c->pairs[i * 2]     = a;
	c->pairs[i * 2 + 1] = b;
	c->rest[i]          = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
}

physics_cloth_t *physics_cloth_create(float *pos, int count, unsigned int *ind, int index_count) {
	physics_cloth_t *c = calloc(1, sizeof(physics_cloth_t));
	c->count           = count;
	c->pos             = malloc(sizeof(float) * 3 * count);
	c->origin          = malloc(sizeof(float) * 3 * count);
	c->prev            = malloc(sizeof(float) * 3 * count);
	c->vel             = calloc(3 * count, sizeof(float));
	c->inv_mass        = malloc(sizeof(float) * count);
	memcpy(c->pos, pos, sizeof(float) * 3 * count);
	memcpy(c->origin, pos, sizeof(float) * 3 * count);
	for (int i = 0; i < count; i++) {
		c->inv_mass[i] = 1.0f;
	}
	c->thickness = 0.01f;
	c->tri_count = index_count / 3;
	c->tris      = malloc(sizeof(int) * (index_count + 1));
	for (int i = 0; i < index_count; i++) {
		c->tris[i] = ind[i];
	}

	// Sort so shared edges are adjacent
	int           edge_count = index_count;
	cloth_edge_t *edges      = malloc(sizeof(cloth_edge_t) * (edge_count + 1));
	for (int t = 0; t < index_count / 3; t++) {
		for (int e = 0; e < 3; e++) {
			int a = ind[t * 3 + e], b = ind[t * 3 + (e + 1) % 3];
			edges[t * 3 + e] = (cloth_edge_t){a < b ? a : b, a < b ? b : a, ind[t * 3 + (e + 2) % 3]};
		}
	}
	qsort(edges, edge_count, sizeof(cloth_edge_t), cloth_edge_compare);

	// Stretch, then bend constraints
	c->pairs  = malloc(sizeof(int) * 2 * (edge_count + 1));
	c->rest   = malloc(sizeof(float) * (edge_count + 1));
	c->lambda = malloc(sizeof(float) * (edge_count + 1));
	for (int i = 0; i < edge_count; i++) {
		if (i == 0 || edges[i].a != edges[i - 1].a || edges[i].b != edges[i - 1].b) {
			cloth_add_constraint(c, edges[i].a, edges[i].b);
		}
	}
	c->stretch_count = c->constraint_count;
	for (int i = 1; i < edge_count; i++) {
		if (edges[i].a == edges[i - 1].a && edges[i].b == edges[i - 1].b && edges[i].far != edges[i - 1].far) {
			cloth_add_constraint(c, edges[i - 1].far, edges[i].far);
		}
	}
	free(edges);

	float area = 0.0f;
	for (int t = 0; t < c->tri_count; t++) {
		float *a = &c->pos[c->tris[t * 3] * 3], *b = &c->pos[c->tris[t * 3 + 1] * 3], *d = &c->pos[c->tris[t * 3 + 2] * 3];
		vec4_t n = vec4_cross((vec4_t){b[0] - a[0], b[1] - a[1], b[2] - a[2]}, (vec4_t){d[0] - a[0], d[1] - a[1], d[2] - a[2]});
		area += vec4_len(n) * 0.5f;
	}
	c->mass = count > 0 ? CLOTH_DENSITY * area / count : 0.0f;
	return c;
}

void physics_cloth_destroy(physics_cloth_t *c) {
	if (c == NULL) {
		return;
	}
	free(c->pos);
	free(c->origin);
	free(c->prev);
	free(c->vel);
	free(c->inv_mass);
	free(c->pairs);
	free(c->rest);
	free(c->lambda);
	free(c->tris);
	free(c);
}

static void cloth_wind(physics_cloth_t *c, float sdt) {
	for (int t = 0; t < c->tri_count; t++) {
		int   *ti = &c->tris[t * 3];
		float *a = &c->pos[ti[0] * 3], *b = &c->pos[ti[1] * 3], *d = &c->pos[ti[2] * 3];
		float  e0[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, e1[3] = {d[0] - a[0], d[1] - a[1], d[2] - a[2]};
		float  n[3] = {e0[1] * e1[2] - e0[2] * e1[1], e0[2] * e1[0] - e0[0] * e1[2], e0[0] * e1[1] - e0[1] * e1[0]};
		float  len  = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
		if (len < 1e-12f) {
			continue;
		}
		vec4_t w      = physics_field_wind((vec4_t){(a[0] + b[0] + d[0]) / 3.0f, (a[1] + b[1] + d[1]) / 3.0f, (a[2] + b[2] + d[2]) / 3.0f, 1.0f});
		float  rel[3] = {w.x, w.y, w.z};
		for (int k = 0; k < 3; k++) {
			for (int v = 0; v < 3; v++) {
				rel[k] -= c->vel[ti[v] * 3 + k] / 3.0f;
			}
		}
		float push = (rel[0] * n[0] + rel[1] * n[1] + rel[2] * n[2]) / (len * len) * CLOTH_DRAG * sdt / 3.0f;
		for (int v = 0; v < 3; v++) {
			for (int k = 0; k < 3; k++) {
				c->vel[ti[v] * 3 + k] += c->inv_mass[ti[v]] * push * n[k];
			}
		}
	}
}

static void cloth_project(physics_cloth_t *c, int i, float sdt) {
	int   a = c->pairs[i * 2], b = c->pairs[i * 2 + 1];
	float w = c->inv_mass[a] + c->inv_mass[b];
	if (w == 0.0f) {
		return;
	}
	float *pa = &c->pos[a * 3], *pb = &c->pos[b * 3];
	float  d[3] = {pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2]};
	float  len  = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
	if (len < 1e-9f) {
		return;
	}
	float alpha = (i < c->stretch_count ? c->stretch_compliance : c->bend_compliance) / (sdt * sdt);
	float dl    = (-(len - c->rest[i]) - alpha * c->lambda[i]) / (w + alpha);
	c->lambda[i] += dl;
	for (int k = 0; k < 3; k++) {
		pa[k] -= c->inv_mass[a] * dl * d[k] / len;
		pb[k] += c->inv_mass[b] * dl * d[k] / len;
	}
}

static inline int cloth_hash(int *cell, int dx, int dy, int dz, int mask) {
	return (int)((((unsigned)(cell[0] + dx) * 92837111u) ^ ((unsigned)(cell[1] + dy) * 689287499u) ^ ((unsigned)(cell[2] + dz) * 283923481u)) & mask);
}

// Pairs already close in the rest shape are skipped
static void cloth_self_collide(physics_cloth_t *c, int mask, int *start, int *entries, int *cells) {
	float d = c->thickness * 2.0f, inv = 1.0f / d;
	memset(start, 0, sizeof(int) * (mask + 2));
	for (int i = 0; i < c->count; i++) {
		for (int k = 0; k < 3; k++) {
			cells[i * 3 + k] = (int)floorf(c->pos[i * 3 + k] * inv);
		}
		start[cloth_hash(&cells[i * 3], 0, 0, 0, mask)]++;
	}
	for (int h = 1; h <= mask + 1; h++) {
		start[h] += start[h - 1];
	}
	for (int i = 0; i < c->count; i++) {
		entries[--start[cloth_hash(&cells[i * 3], 0, 0, 0, mask)]] = i;
	}
	for (int i = 0; i < c->count; i++) {
		for (int n = 13; n < 27; n++) { // Half of the neighbors, each pair once
			int h = cloth_hash(&cells[i * 3], n % 3 - 1, n / 3 % 3 - 1, n / 9 - 1, mask);
			for (int e = start[h]; e < start[h + 1]; e++) {
				int    j  = entries[e];
				float *pi = &c->pos[i * 3], *pj = &c->pos[j * 3];
				float  v[3] = {pj[0] - pi[0], pj[1] - pi[1], pj[2] - pi[2]};
				float  len2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
				if ((n == 13 && j <= i) || len2 >= d * d || len2 < 1e-18f) {
					continue;
				}
				float *oi = &c->origin[i * 3], *oj = &c->origin[j * 3], o[3] = {oj[0] - oi[0], oj[1] - oi[1], oj[2] - oi[2]};
				float  w = c->inv_mass[i] + c->inv_mass[j];
				if (o[0] * o[0] + o[1] * o[1] + o[2] * o[2] < d * d || w == 0.0f) {
					continue;
				}
				float len = sqrtf(len2), push = (d - len) / (len * w);
				for (int k = 0; k < 3; k++) {
					pi[k] -= v[k] * push * c->inv_mass[i];
					pj[k] += v[k] * push * c->inv_mass[j];
				}
			}
		}
	}
}

void physics_cloth_step(physics_cloth_t *c, float dt, physics_sim_t *grid) {
	int mask = 1;
	while (mask < c->count * 2) {
		mask *= 2;
	}
	mask -= 1;
	int   *start   = malloc(sizeof(int) * (mask + 2));
	int   *entries = malloc(sizeof(int) * (c->count + 1));
	int   *cells   = malloc(sizeof(int) * 3 * (c->count + 1));
	vec4_t g       = physics_get_gravity();
	float  sdt     = dt / CLOTH_SUBSTEPS;
	int    pull = physics_field_any(0), wind = physics_field_any(1);
	for (int step = 0; step < CLOTH_SUBSTEPS; step++) {
		if (wind) {
			cloth_wind(c, sdt);
		}

		// Predict
		for (int i = 0; i < c->count; i++) {
			float *p = &c->pos[i * 3], *v = &c->vel[i * 3];
			memcpy(&c->prev[i * 3], p, sizeof(float) * 3);
			if (c->inv_mass[i] == 0.0f) {
				continue;
			}
			vec4_t f = pull ? physics_field_force((vec4_t){p[0], p[1], p[2], 1.0f}) : (vec4_t){0};
			v[0] += (g.x + f.x) * sdt;
			v[1] += (g.y + f.y) * sdt;
			v[2] += (g.z + f.z) * sdt;

			vec4_t u;
			float  wet;
			if (grid != NULL && physics_sim_sample(grid, (vec4_t){p[0], p[1], p[2], 1.0f}, &u, &wet) && wet > 0.0f) {
				float k = fminf(CLOTH_SOAK * sdt, 1.0f) * wet;
				v[0] += (u.x - v[0]) * k;
				v[1] += (u.y - v[1]) * k;
				v[2] += (u.z - v[2]) * k;
			}
			for (int a = 0; a < 3; a++) {
				p[a] += v[a] * sdt;
			}
		}

		// Forward and backward, a single sweep blows up closed shapes
		memset(c->lambda, 0, sizeof(float) * c->constraint_count);
		for (int i = 0; i < c->constraint_count; i++) {
			cloth_project(c, i, sdt);
		}
		for (int i = c->constraint_count - 1; i >= 0; i--) {
			cloth_project(c, i, sdt);
		}
		if (step % 2 == 0) {
			cloth_self_collide(c, mask, start, entries, cells);
		}

		// Collisions
		for (int i = 0; i < c->count; i++) {
			float *p = &c->pos[i * 3], *q = &c->prev[i * 3], *v = &c->vel[i * 3];
			if (c->inv_mass[i] == 0.0f) {
				continue;
			}
			vec4_t        hit = {p[0], p[1], p[2], 1.0f};
			physics_hit_t contact;
			float         n[3] = {0.0f, 0.0f, 0.0f}, nn = 0.0f, push = 0.0f, cvn = 0.0f;
			int           touch = physics_collide_point(&hit, c->thickness, &contact);
			if (c->collide != NULL && c->collide(&hit, c->thickness, &contact)) {
				touch = 1;
			}
			if (touch) {
				vec4_t cv = contact.vel;
				n[0]      = hit.x - p[0];
				n[1]      = hit.y - p[1];
				n[2]      = hit.z - p[2];
				nn        = n[0] * n[0] + n[1] * n[1] + n[2] * n[2];
				p[0]      = hit.x;
				p[1]      = hit.y;
				p[2]      = hit.z;
				if (nn > 1e-18f) {
					push = sqrtf(nn);
					cvn  = (cv.x * n[0] + cv.y * n[1] + cv.z * n[2]) / push;

					// Friction
					float d[3] = {p[0] - q[0] - cv.x * sdt, p[1] - q[1] - cv.y * sdt, p[2] - q[2] - cv.z * sdt};
					float dn   = (d[0] * n[0] + d[1] * n[1] + d[2] * n[2]) / nn;
					for (int k = 0; k < 3; k++) {
						p[k] -= (d[k] - dn * n[k]) * CLOTH_FRICTION;
					}
				}
			}
			float keep = 1.0f - fminf(c->damping * sdt, 1.0f);
			for (int k = 0; k < 3; k++) {
				v[k] = (p[k] - q[k]) / sdt * keep;
			}

			// The push out adds no velocity
			if (push > 0.0f) {
				float vn     = (v[0] * n[0] + v[1] * n[1] + v[2] * n[2]) / push;
				float before = vn - push / sdt * keep;
				float target = fmaxf(before, cvn);
				for (int k = 0; k < 3; k++) {
					v[k] += (target - vn) * n[k] / push;
				}
				if (contact.body != NULL) {
					physics_body_apply_impulse_at(contact.body, vec4_mult((vec4_t){n[0], n[1], n[2], 0.0f}, -c->mass * (target - before) / push), hit);
				}
			}
		}
	}
	free(start);
	free(entries);
	free(cells);
}

#endif
