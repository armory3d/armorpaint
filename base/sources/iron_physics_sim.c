#ifdef WITH_PHYSICS

#include "iron_physics.h"
#include "iron_system.h"
#include "iron_thread.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SIM_AIR_DENSITY       0.01f  // Relative to liquid
#define SIM_PARTICLE_MASS     0.125f // 8 per cell
#define SIM_MAX_SUBSTEPS      16
#define SIM_CFL               2.0f // Cells moved per substep
#define SIM_PCG_ITERS         200
#define SIM_PCG_TOLERANCE     1e-4f
#define SIM_PRESSURE_EPS      1e-4f
#define SIM_MIC_TAU           0.97f
#define SIM_MIC_SIGMA         0.25f
#define SIM_SLABS             8 // Fixed for determinism
#define SIM_THREAD_MIN_CELLS  16384
#define SIM_MAX_THREADS       16
#define SIM_MIN_PARTICLES     1024 // Per thread
#define SIM_CHUNKS            32   // Per thread
#define SIM_POISSON           0.3f
#define SIM_SAND_FRICTION     30.0f // Degrees
#define SIM_ELASTIC_CFL       0.4f
#define SIM_ELASTIC_SUBSTEPS  128
#define SIM_JELLY_MIN_STRETCH 0.3f
#define SIM_JELLY_MAX_STRETCH 3.0f
#define SIM_WIND_RATE         2.0f
#define SIM_DENSITY           1000.0f
#define SIM_BODY_DRAG         4.0f

typedef struct {
	float *data;
	int    n[3];
	float  off[3];
} sim_field_t;

#define FIX(f, i, j, k) ((i) + (f)->n[0] * ((j) + (f)->n[1] * (k)))
#define CIX(s, i, j, k) ((i) + (s)->nx * ((j) + (s)->ny * (k)))

static sim_field_t sim_face(physics_sim_t *s, int a, float *data) {
	sim_field_t f = {.data = data, .n = {s->nx, s->ny, s->nz}, .off = {0.5f, 0.5f, 0.5f}};
	f.n[a] += 1;
	f.off[a] = 0.0f;
	return f;
}

static sim_field_t sim_cells(physics_sim_t *s, float *data) {
	return (sim_field_t){.data = data, .n = {s->nx, s->ny, s->nz}, .off = {0.5f, 0.5f, 0.5f}};
}

static int sim_field_size(sim_field_t *f) {
	return f->n[0] * f->n[1] * f->n[2];
}

physics_sim_t *physics_sim_create(int nx, int ny, int nz, float cell, float *min) {
	physics_sim_t *s = calloc(1, sizeof(physics_sim_t));
	s->nx            = nx;
	s->ny            = ny;
	s->nz            = nz;
	s->cell          = cell;
	memcpy(s->min, min, sizeof(float) * 3);
	s->buoyancy    = 1.0f;
	s->dissipation = 0.2f;
	s->cooling     = 1.0f;
	s->vorticity   = 0.3f;
	s->seed        = 1;
	for (int i = 0; i < 256; i++) {
		s->stiffness[i] = 50.0f;
	}
	int n = nx * ny * nz;
	for (int a = 0; a < 3; a++) {
		sim_field_t f = sim_face(s, a, NULL);
		int         m = sim_field_size(&f);
		s->vel[a]     = calloc(m, sizeof(float));
		s->mass[a]    = calloc(m, sizeof(float));
		s->tmp[a]     = calloc(m, sizeof(float));
		s->closed[a]  = calloc(m, 1);
	}
	s->temperature = calloc(n, sizeof(float));
	s->emitter     = calloc(n, 1);
	s->solid       = calloc(n, 1);
	s->pressure    = calloc(n, sizeof(float));
	for (int i = 0; i < 6; i++) {
		s->cell_tmp[i] = calloc(n, sizeof(float));
	}

	s->solid_vel        = calloc(n * 3, sizeof(float));
	s->solid_body       = calloc(n, sizeof(void *));
	s->solid_base       = calloc(n, 1);
	s->solid_base_vel   = calloc(n * 3, sizeof(float));
	s->solid_base_body  = calloc(n, sizeof(void *));
	s->collider_version = physics_collider_version() - 1;
	return s;
}

void physics_sim_destroy(physics_sim_t *s) {
	if (s == NULL) {
		return;
	}
	for (int a = 0; a < 3; a++) {
		free(s->vel[a]);
		free(s->mass[a]);
		free(s->tmp[a]);
		free(s->closed[a]);
	}
	free(s->body_cells);
	free(s->closed_list);
	for (int g = 0; g < s->gas_count; g++) {
		free(s->density[g]);
	}
	free(s->density);
	free(s->temperature);
	free(s->emitter);
	free(s->solid);
	free(s->solid_vel);
	free(s->solid_body);
	free(s->solid_base);
	free(s->solid_base_vel);
	free(s->solid_base_body);
	free(s->pressure);
	for (int i = 0; i < 6; i++) {
		free(s->cell_tmp[i]);
	}
	free(s->pos);
	free(s->pvel);
	free(s->affine);
	free(s->deform);
	free(s->stress);
	free(s->material);
	free(s->owner);
	free(s);
}

int physics_sim_add_gas(physics_sim_t *s) {
	s->density               = realloc(s->density, sizeof(float *) * (s->gas_count + 1));
	s->density[s->gas_count] = calloc(s->nx * s->ny * s->nz, sizeof(float));
	return s->gas_count++;
}

void physics_sim_add_particle(physics_sim_t *s, float x, float y, float z, int material, int owner) {
	if (s->count == s->cap) {
		s->cap      = s->cap == 0 ? 1024 : s->cap * 2;
		s->pos      = realloc(s->pos, sizeof(float) * 3 * s->cap);
		s->pvel     = realloc(s->pvel, sizeof(float) * 3 * s->cap);
		s->affine   = realloc(s->affine, sizeof(float) * 9 * s->cap);
		s->deform   = realloc(s->deform, sizeof(float) * 9 * s->cap);
		s->stress   = realloc(s->stress, sizeof(float) * 9 * s->cap);
		s->material = realloc(s->material, s->cap);
		s->owner    = realloc(s->owner, s->cap);
	}
	int i             = s->count++;
	s->pos[i * 3]     = x;
	s->pos[i * 3 + 1] = y;
	s->pos[i * 3 + 2] = z;
	s->material[i]    = (unsigned char)material;
	s->owner[i]       = (unsigned char)owner;
	memset(&s->pvel[i * 3], 0, sizeof(float) * 3);
	memset(&s->affine[i * 9], 0, sizeof(float) * 9);
	memset(&s->stress[i * 9], 0, sizeof(float) * 9);
	float *f = &s->deform[i * 9];
	memset(f, 0, sizeof(float) * 9);
	f[0] = f[4] = f[8] = 1.0f;
}

static void sim_remove_particle(physics_sim_t *s, int i) {
	int last = --s->count;
	memcpy(&s->pos[i * 3], &s->pos[last * 3], sizeof(float) * 3);
	memcpy(&s->pvel[i * 3], &s->pvel[last * 3], sizeof(float) * 3);
	memcpy(&s->affine[i * 9], &s->affine[last * 9], sizeof(float) * 9);
	memcpy(&s->deform[i * 9], &s->deform[last * 9], sizeof(float) * 9);
	memcpy(&s->stress[i * 9], &s->stress[last * 9], sizeof(float) * 9);
	s->material[i] = s->material[last];
	s->owner[i]    = s->owner[last];
}

static float sim_random(physics_sim_t *s) {
	s->seed = s->seed * 1664525u + 1013904223u;
	return (float)(s->seed >> 8) / 16777216.0f;
}

// Trilinear, in cell units
static float sim_sample(sim_field_t *f, float x, float y, float z) {
	float g[3] = {x - f->off[0], y - f->off[1], z - f->off[2]};
	int   c[3];
	float t[3];
	for (int a = 0; a < 3; a++) {
		g[a] = fminf(fmaxf(g[a], 0.0f), f->n[a] - 1.001f);
		if (f->n[a] == 1) {
			g[a] = 0.0f;
		}
		c[a] = (int)g[a];
		t[a] = g[a] - c[a];
	}
	int    di = f->n[0] > 1 ? 1 : 0, dj = f->n[1] > 1 ? f->n[0] : 0, dk = f->n[2] > 1 ? f->n[0] * f->n[1] : 0;
	float *d   = &f->data[FIX(f, c[0], c[1], c[2])];
	float  c00 = d[0] + (d[di] - d[0]) * t[0];
	float  c10 = d[dj] + (d[dj + di] - d[dj]) * t[0];
	float  c01 = d[dk] + (d[dk + di] - d[dk]) * t[0];
	float  c11 = d[dk + dj] + (d[dk + dj + di] - d[dk + dj]) * t[0];
	float  c0  = c00 + (c10 - c00) * t[1];
	float  c1  = c01 + (c11 - c01) * t[1];
	return c0 + (c1 - c0) * t[2];
}

static void sim_velocity_at(physics_sim_t *s, float x, float y, float z, float *v) {
	for (int a = 0; a < 3; a++) {
		sim_field_t f = sim_face(s, a, s->vel[a]);
		v[a]          = sim_sample(&f, x, y, z);
	}
}

// Quadratic B-spline weights
static int sim_weights(float x, float *w) {
	int   base = (int)floorf(x - 0.5f);
	float fx   = x - base;
	w[0]       = 0.5f * (1.5f - fx) * (1.5f - fx);
	w[1]       = 0.75f - (fx - 1.0f) * (fx - 1.0f);
	w[2]       = 0.5f * (fx - 0.5f) * (fx - 0.5f);
	return base;
}

typedef struct {
	physics_sim_t *s;
	float          dt;
	float         *field;
	float         *src;
	int            a;
	void         **bodies;
	int           *layers;
	int           *box;
	int           *cells;
	int            gas;
	void          *solve;
} sim_job_t;

// Workers spin between loops, starting threads per loop is too slow
static struct {
	void (*fn)(sim_job_t *job, int begin, int end);
	sim_job_t       *job;
	int              count;
	int              chunks;
	volatile int32_t next;
	int              threads;
	int              size;
	volatile int32_t generation;
	volatile int32_t done;
	iron_thread_t    handles[SIM_MAX_THREADS];
} sim_pool;

static void sim_pool_run(int id) {
	if (id < sim_pool.threads) {
		int count = sim_pool.count, chunks = sim_pool.chunks;
		for (int c; (c = iron_atomic_increment(&sim_pool.next)) < chunks;) {
			sim_pool.fn(sim_pool.job, (int)((int64_t)count * c / chunks), (int)((int64_t)count * (c + 1) / chunks));
		}
	}
}

static void sim_pool_worker(void *param) {
	int     id   = (int)(intptr_t)param;
	int32_t seen = 0;
	for (;;) {
		while (sim_pool.generation == seen) {
		}
		iron_atomic_compare_exchange(&sim_pool.generation, seen, seen); // Fence
		seen = sim_pool.generation;
		if (sim_pool.fn == NULL) {
			return;
		}
		sim_pool_run(id);
		iron_atomic_increment(&sim_pool.done);
	}
}

static void sim_pool_start() {
	int threads = iron_hardware_threads() * 3 / 4;
	threads     = threads > SIM_MAX_THREADS ? SIM_MAX_THREADS : threads;
	if (threads < 2) {
		return;
	}
	sim_pool.generation = 0;
	sim_pool.size       = threads;
	for (int i = 1; i < threads; i++) {
		iron_thread_init(&sim_pool.handles[i], sim_pool_worker, (void *)(intptr_t)i);
	}
}

static void sim_pool_stop() {
	if (sim_pool.size == 0) {
		return;
	}
	sim_pool.fn = NULL;
	iron_atomic_increment(&sim_pool.generation);
	for (int i = 1; i < sim_pool.size; i++) {
		iron_thread_wait_and_destroy(&sim_pool.handles[i]);
	}
	sim_pool.size = 0;
}

static int sim_threads() {
	return sim_pool.size > 0 ? sim_pool.size : 1;
}

// Runs fn over [0, count) on threads, the result does not depend on the thread count
static void sim_parallel(void (*fn)(sim_job_t *, int, int), sim_job_t *job, int count, int min_per_thread) {
	int threads = sim_threads();
	if (threads > count / min_per_thread) {
		threads = count / min_per_thread;
	}
	if (threads < 1) {
		threads = 1;
	}
	if (threads > 1) {
		sim_pool.fn      = fn;
		sim_pool.job     = job;
		sim_pool.count   = count;
		sim_pool.threads = threads;
		sim_pool.chunks  = count < threads * SIM_CHUNKS ? count : threads * SIM_CHUNKS;
		sim_pool.next    = 0;
		sim_pool.done    = 0;
		iron_atomic_increment(&sim_pool.generation);
		sim_pool_run(0);
		while (sim_pool.done < sim_pool.size - 1) {
		}
		iron_atomic_compare_exchange(&sim_pool.done, 0, 0); // Fence
		return;
	}
	fn(job, 0, count);
}

// Lame parameters over density
static void sim_lame(physics_sim_t *s, int owner, float *mu, float *lambda) {
	float e = s->stiffness[owner], nu = SIM_POISSON;
	*mu     = e / (2.0f * (1.0f + nu));
	*lambda = e * nu / ((1.0f + nu) * (1.0f - 2.0f * nu));
}

// Jacobi eigen decomposition, vectors in the columns of v
static void sim_eigen(float *m, float *val, float *v) {
	float a[9];
	memcpy(a, m, sizeof(a));
	memset(v, 0, sizeof(float) * 9);
	v[0] = v[4] = v[8] = 1.0f;
	for (int sweep = 0; sweep < 8; sweep++) {
		for (int p = 0; p < 2; p++) {
			for (int q = p + 1; q < 3; q++) {
				float apq = a[p * 3 + q];
				if (fabsf(apq) < 1e-12f) {
					continue;
				}
				float theta = (a[q * 3 + q] - a[p * 3 + p]) / (2.0f * apq);
				float t     = (theta >= 0.0f ? 1.0f : -1.0f) / (fabsf(theta) + sqrtf(theta * theta + 1.0f));
				float c = 1.0f / sqrtf(t * t + 1.0f), sn = t * c;
				for (int k = 0; k < 3; k++) {
					float akp = a[k * 3 + p], akq = a[k * 3 + q];
					a[k * 3 + p] = c * akp - sn * akq;
					a[k * 3 + q] = sn * akp + c * akq;
					float vkp = v[k * 3 + p], vkq = v[k * 3 + q];
					v[k * 3 + p] = c * vkp - sn * vkq;
					v[k * 3 + q] = sn * vkp + c * vkq;
				}
				for (int k = 0; k < 3; k++) {
					float apk = a[p * 3 + k], aqk = a[q * 3 + k];
					a[p * 3 + k] = c * apk - sn * aqk;
					a[q * 3 + k] = sn * apk + c * aqk;
				}
			}
		}
	}
	val[0] = a[0];
	val[1] = a[4];
	val[2] = a[8];
}

static void sim_solid_stress(physics_sim_t *s, int p) {
	float  mu, lambda;
	float *f = &s->deform[p * 9], *tau = &s->stress[p * 9];
	sim_lame(s, s->owner[p], &mu, &lambda);

	// b = F F^T = U diag(sigma^2) U^T
	float b[9];
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			b[i * 3 + j] = f[i * 3] * f[j * 3] + f[i * 3 + 1] * f[j * 3 + 1] + f[i * 3 + 2] * f[j * 3 + 2];
		}
	}

	// mu (b - I) + lambda ln(J) I
	if (s->material[p] == PHYSICS_MATERIAL_JELLY) {
		float sig2[3], u[9], sig[3];
		sim_eigen(b, sig2, u);
		for (int i = 0; i < 3; i++) {
			sig[i] = fminf(fmaxf(sqrtf(fmaxf(sig2[i], 0.0f)), SIM_JELLY_MIN_STRETCH), SIM_JELLY_MAX_STRETCH);
		}
		for (int i = 0; i < 3; i++) {
			for (int k = 0; k < 3; k++) {
				f[i * 3 + k] = b[i * 3 + k] = 0.0f;
				for (int e = 0; e < 3; e++) {
					f[i * 3 + k] += u[i * 3 + e] * sig[e] * u[k * 3 + e];
					b[i * 3 + k] += u[i * 3 + e] * sig[e] * sig[e] * u[k * 3 + e];
				}
			}
		}
		float lnj = logf(sig[0] * sig[1] * sig[2]);
		for (int i = 0; i < 9; i++) {
			tau[i] = mu * (b[i] - (i % 4 == 0));
		}
		for (int i = 0; i < 3; i++) {
			tau[i * 4] += lambda * lnj;
		}
		return;
	}

	// Sand
	float sig2[3], u[9], e[3], ep[3];
	sim_eigen(b, sig2, u);
	for (int i = 0; i < 3; i++) {
		e[i] = 0.5f * logf(fmaxf(sig2[i], 1e-8f));
	}
	float tr      = e[0] + e[1] + e[2];
	float dev[3]  = {e[0] - tr / 3.0f, e[1] - tr / 3.0f, e[2] - tr / 3.0f};
	float norm    = sqrtf(dev[0] * dev[0] + dev[1] * dev[1] + dev[2] * dev[2]);
	float sin_phi = sinf(SIM_SAND_FRICTION * 3.14159265f / 180.0f);
	float alpha   = sqrtf(2.0f / 3.0f) * 2.0f * sin_phi / (3.0f - sin_phi);
	float slide   = norm + (3.0f * lambda + 2.0f * mu) / (2.0f * mu) * tr * alpha;
	for (int i = 0; i < 3; i++) {
		ep[i] = slide <= 0.0f ? e[i] : tr >= 0.0f || norm < 1e-9f ? 0.0f : e[i] - slide * dev[i] / norm;
	}

	// F = U diag(exp(ep - e)) U^T F, tau = U diag(2 mu ep + lambda tr(ep)) U^T
	float trp = ep[0] + ep[1] + ep[2], scale[3], diag[3], m[9], g[9];
	for (int i = 0; i < 3; i++) {
		scale[i] = expf(ep[i] - e[i]);
		diag[i]  = 2.0f * mu * ep[i] + lambda * trp;
	}
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			m[i * 3 + j]   = 0.0f;
			tau[i * 3 + j] = 0.0f;
			for (int k = 0; k < 3; k++) {
				m[i * 3 + j] += u[i * 3 + k] * scale[k] * u[j * 3 + k];
				tau[i * 3 + j] += u[i * 3 + k] * diag[k] * u[j * 3 + k];
			}
		}
	}
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			g[i * 3 + j] = m[i * 3] * f[j] + m[i * 3 + 1] * f[3 + j] + m[i * 3 + 2] * f[6 + j];
		}
	}
	memcpy(f, g, sizeof(g));
}

// Particles to faces, items are ranges of z layers
static void sim_particles_to_grid_ranges(sim_job_t *job, int begin, int end) {
	physics_sim_t *s   = job->s;
	float          h   = s->cell;
	int           *box = job->box;
	for (int r = begin; r < end; r++) {
		int lk0 = job->layers[1 + r], lk1 = job->layers[2 + r];
		for (int a = 0; a < 3; a++) {
			sim_field_t f  = sim_face(s, a, NULL);
			int         k1 = lk1 < f.n[2] ? lk1 : f.n[2];
			for (int k = lk0 > box[4] ? lk0 : box[4]; k < k1 && k < box[5]; k++) {
				for (int j = box[2]; j < box[3] && j < f.n[1]; j++) {
					int i0 = FIX(&f, box[0], j, k), i1 = FIX(&f, box[1] < f.n[0] ? box[1] : f.n[0], j, k);
					memset(s->tmp[a] + i0, 0, sizeof(float) * (i1 - i0));
					memset(s->mass[a] + i0, 0, sizeof(float) * (i1 - i0));
				}
			}
		}
		for (int p = 0; p < s->count; p++) {
			for (int a = 0; a < 3; a++) {
				sim_field_t f  = sim_face(s, a, NULL);
				int         k0 = lk0, k1 = lk1 < f.n[2] ? lk1 : f.n[2];
				int         kb = job->cells[p * 2 + (a == 2)];
				if (kb + 2 < k0 || kb >= k1) {
					continue;
				}
				float x[3], w[3][3];
				int   base[3];
				for (int b = 0; b < 3; b++) {
					x[b]    = (s->pos[p * 3 + b] - s->min[b]) / h - f.off[b];
					base[b] = sim_weights(x[b], w[b]);
				}
				float c[3];
				memcpy(c, &s->affine[p * 9 + a * 3], sizeof(c));
				if (s->material[p] != PHYSICS_MATERIAL_LIQUID) {
					for (int b = 0; b < 3; b++) {
						c[b] -= job->dt * 4.0f / (h * h) * s->stress[p * 9 + a * 3 + b];
					}
				}
				float *mom = s->tmp[a];
				float *ms  = s->mass[a];
				float  vp  = s->pvel[p * 3 + a];
				for (int dk = 0; dk < 3; dk++) {
					int k = base[2] + dk;
					if (k < k0 || k >= k1) {
						continue;
					}
					for (int dj = 0; dj < 3; dj++) {
						int j = base[1] + dj;
						if (j < 0 || j >= f.n[1]) {
							continue;
						}
						for (int di = 0; di < 3; di++) {
							int i = base[0] + di;
							if (i < 0 || i >= f.n[0]) {
								continue;
							}
							float wt   = w[0][di] * w[1][dj] * w[2][dk] * SIM_PARTICLE_MASS;
							float d[3] = {(i - x[0]) * h, (j - x[1]) * h, (k - x[2]) * h};
							int   id   = FIX(&f, i, j, k);
							ms[id] += wt;
							mom[id] += wt * (vp + c[0] * d[0] + c[1] * d[1] + c[2] * d[2]);
						}
					}
				}
			}
		}
	}
}

static void sim_particles_to_grid(physics_sim_t *s, float dt, int gas) {
	int  threads = sim_threads();
	int  n       = s->nz + 1;
	int *count   = calloc(n + 1, sizeof(int));
	int *cells   = malloc(sizeof(int) * 2 * (s->count + 1));
	int  lo[3] = {s->nx, s->ny, s->nz}, hi[3] = {-1, -1, -1};
	for (int p = 0; p < s->count; p++) {
		int c[3];
		for (int a = 0; a < 3; a++) {
			c[a]  = (int)floorf((s->pos[p * 3 + a] - s->min[a]) / s->cell);
			lo[a] = c[a] < lo[a] ? c[a] : lo[a];
			hi[a] = c[a] > hi[a] ? c[a] : hi[a];
		}
		float z          = (s->pos[p * 3 + 2] - s->min[2]) / s->cell;
		cells[p * 2]     = (int)floorf(z - 0.5f - 0.5f);
		cells[p * 2 + 1] = (int)floorf(z - 0.0f - 0.5f);
		count[c[2] < 0 ? 0 : c[2] >= n ? n - 1 : c[2]]++;
	}
	int reach[6], box[6] = {0, s->nx + 1, 0, s->ny + 1, 0, s->nz + 1};
	for (int a = 0; a < 3; a++) {
		reach[a * 2]     = lo[a] > hi[a] ? 0 : lo[a] - 1 < 0 ? 0 : lo[a] - 1;
		reach[a * 2 + 1] = lo[a] > hi[a] ? 0 : hi[a] + 3 > box[a * 2 + 1] ? box[a * 2 + 1] : hi[a] + 3;
		if (!gas) {
			box[a * 2]     = s->mass_box[a * 2] < reach[a * 2] ? s->mass_box[a * 2] : reach[a * 2];
			box[a * 2 + 1] = s->mass_box[a * 2 + 1] > reach[a * 2 + 1] ? s->mass_box[a * 2 + 1] : reach[a * 2 + 1];
		}
	}
	memcpy(s->mass_box, reach, sizeof(reach));
	int layers[SIM_MAX_THREADS + 2] = {threads};
	int sum = 0, r = 1;
	for (int k = 0; k < n && r < threads; k++) {
		sum += count[k];
		if (sum * (int64_t)threads >= (int64_t)s->count * r) {
			layers[1 + r++] = k + 1;
		}
	}
	while (r < threads) {
		layers[1 + r++] = n;
	}
	layers[1 + threads] = n;
	free(count);
	sim_job_t job = {.s = s, .dt = dt, .layers = layers, .box = box, .cells = cells};
	sim_parallel(sim_particles_to_grid_ranges, &job, threads, 1);
	free(cells);
}

// Faces to particles
static void sim_grid_to_particles_range(sim_job_t *job, int begin, int end) {
	physics_sim_t *s     = job->s;
	float          h     = s->cell;
	float          scale = 4.0f / (h * h);
	for (int p = begin; p < end; p++) {
		for (int a = 0; a < 3; a++) {
			sim_field_t f = sim_face(s, a, s->vel[a]);
			float       x[3], w[3][3];
			int         base[3];
			for (int b = 0; b < 3; b++) {
				x[b]    = (s->pos[p * 3 + b] - s->min[b]) / h - f.off[b];
				base[b] = sim_weights(x[b], w[b]);
			}
			float v = 0.0f, c[3] = {0.0f, 0.0f, 0.0f};
			for (int dk = 0; dk < 3; dk++) {
				int k = base[2] + dk;
				k     = k < 0 ? 0 : k >= f.n[2] ? f.n[2] - 1 : k;
				for (int dj = 0; dj < 3; dj++) {
					int j = base[1] + dj;
					j     = j < 0 ? 0 : j >= f.n[1] ? f.n[1] - 1 : j;
					for (int di = 0; di < 3; di++) {
						int i    = base[0] + di;
						i        = i < 0 ? 0 : i >= f.n[0] ? f.n[0] - 1 : i;
						float wt = w[0][di] * w[1][dj] * w[2][dk];
						float u  = f.data[FIX(&f, i, j, k)] * wt;
						v += u;
						c[0] += u * (base[0] + di - x[0]) * h;
						c[1] += u * (base[1] + dj - x[1]) * h;
						c[2] += u * (base[2] + dk - x[2]) * h;
					}
				}
			}
			s->pvel[p * 3 + a] = v;
			for (int b = 0; b < 3; b++) {
				s->affine[p * 9 + a * 3 + b] = c[b] * scale;
			}
		}

		// F = (I + dt C) F
		if (s->material[p] != PHYSICS_MATERIAL_LIQUID) {
			float *f = &s->deform[p * 9], *c = &s->affine[p * 9], g[9];
			for (int a = 0; a < 3; a++) {
				for (int b = 0; b < 3; b++) {
					g[a * 3 + b] = f[a * 3 + b] + job->dt * (c[a * 3] * f[b] + c[a * 3 + 1] * f[3 + b] + c[a * 3 + 2] * f[6 + b]);
				}
			}
			memcpy(f, g, sizeof(g));
			sim_solid_stress(s, p);
		}
	}
}

static void sim_grid_to_particles(physics_sim_t *s, float dt) {
	sim_job_t job = {.s = s, .dt = dt};
	sim_parallel(sim_grid_to_particles_range, &job, s->count, SIM_MIN_PARTICLES);
}

static void sim_advect_air_layers(sim_job_t *job, int begin, int end) {
	physics_sim_t *s     = job->s;
	int            a     = job->a;
	float          scale = job->dt / s->cell;
	sim_field_t    f     = sim_face(s, a, NULL);
	sim_field_t    src   = sim_face(s, a, job->src);
	for (int k = begin; k < end; k++) {
		for (int j = 0; j < f.n[1]; j++) {
			for (int i = 0; i < f.n[0]; i++) {
				float x = i + f.off[0], y = j + f.off[1], z = k + f.off[2];
				float v[3];
				sim_velocity_at(s, x, y, z, v);
				job->field[FIX(&f, i, j, k)] = sim_sample(&src, x - v[0] * scale, y - v[1] * scale, z - v[2] * scale);
			}
		}
	}
}

static void sim_advect_air(physics_sim_t *s, float dt) {
	float *old[3];
	for (int a = 0; a < 3; a++) {
		sim_field_t f = sim_face(s, a, NULL);
		old[a]        = malloc(sizeof(float) * sim_field_size(&f));
		memcpy(old[a], s->vel[a], sizeof(float) * sim_field_size(&f));
	}
	float *live[3] = {s->vel[0], s->vel[1], s->vel[2]};
	for (int a = 0; a < 3; a++) {
		s->vel[a] = old[a];
	}
	for (int a = 0; a < 3; a++) {
		sim_field_t f   = sim_face(s, a, NULL);
		sim_job_t   job = {.s = s, .dt = dt, .field = live[a], .src = old[a], .a = a};
		sim_parallel(sim_advect_air_layers, &job, f.n[2], 1);
	}
	for (int a = 0; a < 3; a++) {
		s->vel[a] = live[a];
		free(old[a]);
	}
}

static void sim_advect_cells_layers(sim_job_t *job, int begin, int end) {
	physics_sim_t *s     = job->s;
	float         *field = job->field;
	float          scale = job->dt / s->cell;
	sim_field_t    f     = sim_cells(s, job->src);
	for (int k = begin; k < end; k++) {
		for (int j = 0; j < s->ny; j++) {
			for (int i = 0; i < s->nx; i++) {
				float x = i + 0.5f, y = j + 0.5f, z = k + 0.5f;
				float v[3];
				sim_velocity_at(s, x, y, z, v);
				field[CIX(s, i, j, k)] = sim_sample(&f, x - v[0] * scale, y - v[1] * scale, z - v[2] * scale);
			}
		}
	}
}

static void sim_advect_cells(physics_sim_t *s, float *field, float dt) {
	memcpy(s->cell_tmp[0], field, sizeof(float) * s->nx * s->ny * s->nz);
	sim_job_t job = {.s = s, .dt = dt, .field = field, .src = s->cell_tmp[0]};
	sim_parallel(sim_advect_cells_layers, &job, s->nz, 1);
}

static void sim_mark_solid_layers(sim_job_t *job, int begin, int end) {
	physics_sim_t *s = job->s;
	for (int k = begin; k < end; k++) {
		for (int j = 0; j < s->ny; j++) {
			for (int i = 0; i < s->nx; i++) {
				int           c = CIX(s, i, j, k);
				vec4_t        p = {s->min[0] + (i + 0.5f) * s->cell, s->min[1] + (j + 0.5f) * s->cell, s->min[2] + (k + 0.5f) * s->cell, 1.0f};
				physics_hit_t hit;
				s->solid_base[c]             = physics_collide_point(&p, s->cell, &hit) ? 1 : 0;
				s->solid_base_vel[c * 3]     = hit.vel.x;
				s->solid_base_vel[c * 3 + 1] = hit.vel.y;
				s->solid_base_vel[c * 3 + 2] = hit.vel.z;
				s->solid_base_body[c]        = s->solid_base[c] ? hit.body : NULL;
			}
		}
	}
}

static void sim_mark_closed_layers(sim_job_t *job, int begin, int end) {
	physics_sim_t *s    = job->s;
	int            a    = job->a;
	int            n[3] = {s->nx, s->ny, s->nz};
	sim_field_t    f    = sim_face(s, a, NULL);
	for (int k = begin; k < end; k++) {
		for (int j = 0; j < f.n[1]; j++) {
			for (int i = 0; i < f.n[0]; i++) {
				int c[3] = {i, j, k};
				int hi   = c[a] < n[a] && s->solid[CIX(s, c[0], c[1], c[2])];
				c[a] -= 1;
				int lo                         = c[a] >= 0 && s->solid[CIX(s, c[0], c[1], c[2])];
				s->closed[a][FIX(&f, i, j, k)] = hi ? 1 : lo ? 2 : 0;
			}
		}
	}
}

// Faces beside solid cells move with the collider
static void sim_close_faces(physics_sim_t *s) {
	for (int i = 0; i < s->closed_count; i++) {
		int *l             = &s->closed_list[i * 3];
		s->vel[l[0]][l[1]] = s->solid_vel[l[2] * 3 + l[0]];
	}
}

// Each thread owns whole slabs, sums are added per slab in order
typedef struct {
	physics_sim_t   *s;
	float           *coef[3]; // 1 / density, 0 when closed
	uint8_t         *fixed;
	int              free_surface;
	int              slabs;
	int              threads;
	volatile int32_t arrived;
	volatile int32_t generation;
	double           sum[2][SIM_SLABS];
	float            max[2][SIM_SLABS];
	int              box[SIM_SLABS][6];
	int              region[6];
	int              zero[6];
} sim_solve_t;

static void sim_barrier(sim_solve_t *p) {
	if (p->threads == 1) {
		return;
	}
	int32_t gen = p->generation;
	if (iron_atomic_increment(&p->arrived) == p->threads - 1) {
		iron_atomic_exchange(&p->arrived, 0);
		iron_atomic_increment(&p->generation);
	}
	else {
		while (p->generation == gen) {
		}
		iron_atomic_compare_exchange(&p->generation, gen, gen); // Fence
	}
}

static double sim_sum(sim_solve_t *p, int i) {
	double sum = 0.0;
	for (int sl = 0; sl < p->slabs; sl++) {
		sum += p->sum[i][sl];
	}
	return sum;
}

static float sim_max(sim_solve_t *p, int i) {
	float max = 0.0f;
	for (int sl = 0; sl < p->slabs; sl++) {
		max = fmaxf(max, p->max[i][sl]);
	}
	return max;
}

// Sum over the faces of coef * (q_c - q_neighbor)
static void sim_apply(physics_sim_t *s, float **coef, uint8_t *fixed, float *q, float *out, int *b) {
	int    nx = s->nx, ny = s->ny, nz = s->nz;
	float *cu = coef[0], *cv = coef[1], *cw = coef[2];
	for (int k = b[4]; k < b[5]; k++) {
		for (int j = b[2]; j < b[3]; j++) {
			for (int i = b[0]; i < b[1]; i++) {
				int c = i + nx * (j + ny * k);
				if (fixed[c]) {
					out[c] = 0.0f;
					continue;
				}
				float qc  = q[c];
				int   u   = i + (nx + 1) * (j + ny * k);
				int   v   = i + nx * (j + (ny + 1) * k);
				float sum = cu[u] * (qc - (i > 0 ? q[c - 1] : 0.0f)) + cu[u + 1] * (qc - (i < nx - 1 ? q[c + 1] : 0.0f)) +
				            cv[v] * (qc - (j > 0 ? q[c - nx] : 0.0f)) + cv[v + nx] * (qc - (j < ny - 1 ? q[c + nx] : 0.0f)) +
				            cw[c] * (qc - (k > 0 ? q[c - nx * ny] : 0.0f)) + cw[c + nx * ny] * (qc - (k < nz - 1 ? q[c + nx * ny] : 0.0f));
				out[c]    = sum + SIM_PRESSURE_EPS * qc;
			}
		}
	}
}

// MIC(0) within a slab, written over diag
static void sim_mic_build(physics_sim_t *s, float **coef, float *diag, int k0, int k1, int *b) {
	int    nx = s->nx, ny = s->ny, nxy = nx * ny;
	float *cu = coef[0], *cv = coef[1], *cw = coef[2];
	for (int k = b[4]; k < b[5]; k++) {
		for (int j = b[2]; j < b[3]; j++) {
			for (int i = b[0]; i < b[1]; i++) {
				int c = i + nx * (j + ny * k);
				if (diag[c] <= 0.0f) {
					diag[c] = 0.0f;
					continue;
				}
				int   u = i + (nx + 1) * (j + ny * k);
				int   v = i + nx * (j + (ny + 1) * k);
				float e = diag[c];
				if (i > 0) {
					float a  = cu[u] * diag[c - 1];
					float aj = j < ny - 1 ? cv[v - 1 + nx] : 0.0f;
					float ak = k < k1 - 1 ? cw[c - 1 + nxy] : 0.0f;
					e -= a * a + SIM_MIC_TAU * cu[u] * (aj + ak) * diag[c - 1] * diag[c - 1];
				}
				if (j > 0) {
					float a  = cv[v] * diag[c - nx];
					float ai = i < nx - 1 ? cu[u - (nx + 1) + 1] : 0.0f;
					float ak = k < k1 - 1 ? cw[c - nx + nxy] : 0.0f;
					e -= a * a + SIM_MIC_TAU * cv[v] * (ai + ak) * diag[c - nx] * diag[c - nx];
				}
				if (k > k0) {
					float a  = cw[c] * diag[c - nxy];
					float ai = i < nx - 1 ? cu[u - (nx + 1) * ny + 1] : 0.0f;
					float aj = j < ny - 1 ? cv[v - nx * (ny + 1) + nx] : 0.0f;
					e -= a * a + SIM_MIC_TAU * cw[c] * (ai + aj) * diag[c - nxy] * diag[c - nxy];
				}
				if (e < SIM_MIC_SIGMA * diag[c]) {
					e = diag[c];
				}
				diag[c] = 1.0f / sqrtf(e);
			}
		}
	}
}

// Solves L L^T z = r
static void sim_mic_apply(physics_sim_t *s, float **coef, float *mic, float *r, float *z, int k0, int k1, int *b) {
	int    nx = s->nx, ny = s->ny, nxy = nx * ny;
	float *cu = coef[0], *cv = coef[1], *cw = coef[2];
	for (int k = b[4]; k < b[5]; k++) {
		for (int j = b[2]; j < b[3]; j++) {
			for (int i = b[0]; i < b[1]; i++) {
				int c = i + nx * (j + ny * k);
				if (mic[c] == 0.0f) {
					z[c] = 0.0f;
					continue;
				}
				int   u = i + (nx + 1) * (j + ny * k);
				int   v = i + nx * (j + (ny + 1) * k);
				float t = r[c];
				if (i > 0) {
					t += cu[u] * mic[c - 1] * z[c - 1];
				}
				if (j > 0) {
					t += cv[v] * mic[c - nx] * z[c - nx];
				}
				if (k > k0) {
					t += cw[c] * mic[c - nxy] * z[c - nxy];
				}
				z[c] = t * mic[c];
			}
		}
	}
	for (int k = b[5] - 1; k >= b[4]; k--) {
		for (int j = b[3] - 1; j >= b[2]; j--) {
			for (int i = b[1] - 1; i >= b[0]; i--) {
				int c = i + nx * (j + ny * k);
				if (mic[c] == 0.0f) {
					continue;
				}
				int   u = i + (nx + 1) * (j + ny * k);
				int   v = i + nx * (j + (ny + 1) * k);
				float t = z[c];
				if (i < nx - 1) {
					t += cu[u + 1] * mic[c] * z[c + 1];
				}
				if (j < ny - 1) {
					t += cv[v + nx] * mic[c] * z[c + nx];
				}
				if (k < k1 - 1) {
					t += cw[c + nxy] * mic[c] * z[c + nxy];
				}
				z[c] = t * mic[c];
			}
		}
	}
}

// MIC(0) preconditioned conjugate gradient, warm started
static void sim_solve_run(sim_solve_t *p, int id) {
	physics_sim_t *s  = p->s;
	int            nx = s->nx, ny = s->ny, nz = s->nz, nxy = nx * ny;
	float        **coef = p->coef;
	float         *q    = s->pressure;
	float         *r    = s->cell_tmp[1];
	float         *z    = s->cell_tmp[2];
	float         *d    = s->cell_tmp[3];
	float         *ad   = s->cell_tmp[4];
	float         *diag = s->cell_tmp[5];
	float         *u = s->vel[0], *v = s->vel[1], *w = s->vel[2];

#define SIM_SLAB_BEGIN                                   \
	for (int sl = id; sl < p->slabs; sl += p->threads) { \
		int k0 = sl * nz / p->slabs, k1 = (sl + 1) * nz / p->slabs, *b = p->box[sl];
#define SIM_BOX_CELLS(c)                     \
	for (int bk = b[4]; bk < b[5]; bk++)     \
		for (int bj = b[2]; bj < b[3]; bj++) \
			for (int c = b[0] + nx * (bj + ny * bk), ce = c + b[1] - b[0]; c < ce; c++)
#define SIM_SLAB_END }

	int *g = p->region, *zr = p->zero;
	SIM_SLAB_BEGIN
	for (int k = k0 > zr[4] ? k0 : zr[4]; k < k1 && k < zr[5]; k++) {
		for (int j = zr[2]; j < zr[3]; j++) {
			for (int i = zr[0]; i < zr[1]; i++) {
				int c = i + nx * (j + ny * k);
				if (p->fixed[c]) {
					q[c] = 0.0f;
				}
			}
		}
	}
	for (int a = 0; a < 3; a++) {
		sim_field_t f    = sim_face(s, a, coef[a]);
		int         kend = a == 2 && k1 == nz ? nz + 1 : k1;
		for (int k = k0 > g[4] ? k0 : g[4]; k < kend && k < g[5] + (a == 2); k++) {
			for (int j = g[2]; j < g[3] + (a == 1); j++) {
				for (int i = g[0]; i < g[1] + (a == 0); i++) {
					int fi     = FIX(&f, i, j, k);
					f.data[fi] = s->closed[a][fi] ? 0.0f : p->free_surface ? 1.0f : 1.0f / (s->mass[a][fi] + SIM_AIR_DENSITY);
				}
			}
		}
	}
	SIM_SLAB_END
	sim_barrier(p);

	// Residual
	SIM_SLAB_BEGIN
	int box[6] = {nx, 0, ny, 0, k1, k0};
	for (int k = k0 > g[4] ? k0 : g[4]; k < k1 && k < g[5]; k++) {
		for (int j = g[2]; j < g[3]; j++) {
			for (int i = g[0]; i < g[1]; i++) {
				if (!p->fixed[i + nx * (j + ny * k)]) {
					box[0] = i < box[0] ? i : box[0];
					box[1] = i >= box[1] ? i + 1 : box[1];
					box[2] = j < box[2] ? j : box[2];
					box[3] = j >= box[3] ? j + 1 : box[3];
					box[4] = k < box[4] ? k : box[4];
					box[5] = k >= box[5] ? k + 1 : box[5];
				}
			}
		}
	}
	if (box[4] >= box[5]) {
		memset(box, 0, sizeof(box));
	}
	memcpy(b, box, sizeof(box));
	float *zero[5] = {r, z, d, ad, diag};
	for (int k = k0 > g[4] ? k0 : g[4]; k < k1 && k < g[5]; k++) {
		for (int j = g[2]; j < g[3]; j++) {
			for (int t = 0; t < 5; t++) {
				memset(zero[t] + g[0] + nx * (j + ny * k), 0, sizeof(float) * (g[1] - g[0]));
			}
		}
	}
	sim_apply(s, coef, p->fixed, q, ad, b);
	float bmax = 0.0f;
	for (int k = b[4]; k < b[5]; k++) {
		for (int j = b[2]; j < b[3]; j++) {
			for (int i = b[0]; i < b[1]; i++) {
				int c = i + nx * (j + ny * k);
				if (p->fixed[c]) {
					r[c] = diag[c] = 0.0f;
					continue;
				}
				int   fu  = i + (nx + 1) * (j + ny * k);
				int   fv  = i + nx * (j + (ny + 1) * k);
				float out = u[fu + 1] - u[fu] + v[fv + nx] - v[fv] + w[c + nxy] - w[c];
				r[c]      = -out - ad[c];
				bmax      = fmaxf(bmax, fabsf(out));
				diag[c]   = coef[0][fu] + coef[0][fu + 1] + coef[1][fv] + coef[1][fv + nx] + coef[2][c] + coef[2][c + nxy] + SIM_PRESSURE_EPS;
			}
		}
	}
	sim_mic_build(s, coef, diag, k0, k1, b);
	sim_mic_apply(s, coef, diag, r, z, k0, k1, b);
	double rz  = 0.0;
	float  err = 0.0f;
	SIM_BOX_CELLS(c) {
		d[c] = z[c];
		rz += (double)r[c] * z[c];
		err = fmaxf(err, fabsf(r[c]));
	}
	p->sum[0][sl] = rz;
	p->max[0][sl] = err;
	p->max[1][sl] = bmax;
	SIM_SLAB_END
	sim_barrier(p);

	float bmax = sim_max(p, 1);
	if (bmax < 1e-9f) {
		SIM_SLAB_BEGIN
		for (int k = k0 > g[4] ? k0 : g[4]; k < k1 && k < g[5]; k++) {
			for (int j = g[2]; j < g[3]; j++) {
				memset(q + g[0] + nx * (j + ny * k), 0, sizeof(float) * (g[1] - g[0]));
			}
		}
		SIM_SLAB_END
		return;
	}
	double rz        = sim_sum(p, 0);
	float  err       = sim_max(p, 0);
	float  tolerance = SIM_PCG_TOLERANCE * bmax;
	for (int it = 0; it < SIM_PCG_ITERS && err >= tolerance; it++) {
		SIM_SLAB_BEGIN
		sim_apply(s, coef, p->fixed, d, ad, b);
		double dad = 0.0;
		SIM_BOX_CELLS(c) {
			dad += (double)d[c] * ad[c];
		}
		p->sum[1][sl] = dad;
		SIM_SLAB_END
		sim_barrier(p);

		double dad = sim_sum(p, 1);
		if (dad <= 0.0) {
			break;
		}
		float alpha = (float)(rz / dad);
		SIM_SLAB_BEGIN
		SIM_BOX_CELLS(c) {
			q[c] += alpha * d[c];
			r[c] -= alpha * ad[c];
		}
		sim_mic_apply(s, coef, diag, r, z, k0, k1, b);
		double rz_new  = 0.0;
		float  err_new = 0.0f;
		SIM_BOX_CELLS(c) {
			rz_new += (double)r[c] * z[c];
			err_new = fmaxf(err_new, fabsf(r[c]));
		}
		p->sum[0][sl] = rz_new;
		p->max[0][sl] = err_new;
		SIM_SLAB_END
		sim_barrier(p);

		double rz_new = sim_sum(p, 0);
		float  beta   = (float)(rz_new / rz);
		rz            = rz_new;
		err           = sim_max(p, 0);
		SIM_SLAB_BEGIN
		SIM_BOX_CELLS(c) {
			d[c] = z[c] + beta * d[c];
		}
		SIM_SLAB_END
		sim_barrier(p);
	}

	// Pressure gradient
	SIM_SLAB_BEGIN
	for (int k = k0 > g[4] ? k0 : g[4]; k < k1 && k < g[5]; k++) {
		for (int j = g[2]; j < g[3]; j++) {
			for (int i = g[0]; i <= g[1]; i++) {
				int   fi  = i + (nx + 1) * (j + ny * k);
				int   c   = i + nx * (j + ny * k);
				float qhi = i < nx ? q[c] : 0.0f;
				float qlo = i > 0 ? q[c - 1] : 0.0f;
				u[fi] -= coef[0][fi] * (qhi - qlo);
			}
		}
		for (int j = g[2]; j <= g[3]; j++) {
			for (int i = g[0]; i < g[1]; i++) {
				int   fi  = i + nx * (j + (ny + 1) * k);
				int   c   = i + nx * (j + ny * k);
				float qhi = j < ny ? q[c] : 0.0f;
				float qlo = j > 0 ? q[c - nx] : 0.0f;
				v[fi] -= coef[1][fi] * (qhi - qlo);
			}
		}
	}
	for (int k = k0 > g[4] ? k0 : g[4]; k < (k1 == nz ? nz + 1 : k1) && k <= g[5]; k++) {
		for (int j = g[2]; j < g[3]; j++) {
			for (int i = g[0]; i < g[1]; i++) {
				int   c   = i + nx * (j + ny * k);
				float qhi = k < nz ? q[c] : 0.0f;
				float qlo = k > 0 ? q[c - nxy] : 0.0f;
				w[c] -= coef[2][c] * (qhi - qlo);
			}
		}
	}
	SIM_SLAB_END

#undef SIM_SLAB_BEGIN
#undef SIM_SLAB_END
#undef SIM_BOX_CELLS
}

static void sim_solve_items(sim_job_t *job, int begin, int end) {
	for (int id = begin; id < end; id++) {
		sim_solve_run(job->solve, id);
	}
}

// Without gas, free surface liquid
static void sim_project(physics_sim_t *s, int gas) {
	int      n       = s->nx * s->ny * s->nz;
	uint8_t *fixed   = malloc(n);
	int      size[3] = {s->nx, s->ny, s->nz};
	int      lo[3] = {0, 0, 0}, hi[3] = {s->nx - 1, s->ny - 1, s->nz - 1};
	if (gas) {
		memcpy(fixed, s->solid, n);
	}
	else {
		memset(fixed, 1, n);
		for (int a = 0; a < 3; a++) {
			lo[a] = size[a];
			hi[a] = -1;
		}
		for (int i = 0; i < s->count; i++) {
			if (s->material[i] != PHYSICS_MATERIAL_LIQUID) {
				continue;
			}
			int c[3];
			for (int a = 0; a < 3; a++) {
				c[a] = (int)floorf((s->pos[i * 3 + a] - s->min[a]) / s->cell);
			}
			if (c[0] >= 0 && c[1] >= 0 && c[2] >= 0 && c[0] < s->nx && c[1] < s->ny && c[2] < s->nz) {
				int id    = CIX(s, c[0], c[1], c[2]);
				fixed[id] = s->solid[id];
				for (int a = 0; a < 3 && !fixed[id]; a++) {
					lo[a] = c[a] < lo[a] ? c[a] : lo[a];
					hi[a] = c[a] > hi[a] ? c[a] : hi[a];
				}
			}
		}
	}
	sim_solve_t p = {.s = s, .coef = {s->tmp[0], s->tmp[1], s->tmp[2]}, .fixed = fixed, .free_surface = !gas};
	int        *g = p.region, *zr = p.zero, *last = s->pressure_box;
	for (int a = 0; a < 3 && lo[0] <= hi[0]; a++) {
		g[a * 2]     = lo[a] > 0 ? lo[a] - 1 : 0;
		g[a * 2 + 1] = hi[a] + 2 < size[a] ? hi[a] + 2 : size[a];
	}
	for (int a = 0; a < 3; a++) {
		zr[a * 2]     = last[a * 2] < g[a * 2] ? last[a * 2] : g[a * 2];
		zr[a * 2 + 1] = last[a * 2 + 1] > g[a * 2 + 1] ? last[a * 2 + 1] : g[a * 2 + 1];
	}
	if (last[1] == 0) {
		memcpy(zr, g, sizeof(p.zero));
	}
	memcpy(last, g, sizeof(p.region));
	p.slabs   = s->nz < SIM_SLABS ? s->nz : SIM_SLABS;
	p.threads = sim_threads();
	if (p.threads > p.slabs) {
		p.threads = p.slabs;
	}
	if (s->nx * s->ny * s->nz < SIM_THREAD_MIN_CELLS) {
		p.threads = 1;
	}
	sim_job_t job = {.s = s, .solve = &p};
	sim_parallel(sim_solve_items, &job, p.threads, 1);
	free(fixed);
}

static void sim_center_velocity_layers(sim_job_t *job, int begin, int end) {
	physics_sim_t *s  = job->s;
	float         *ux = s->cell_tmp[0], *uy = s->cell_tmp[1], *uz = s->cell_tmp[2];
	for (int k = begin; k < end; k++) {
		for (int j = 0; j < s->ny; j++) {
			for (int i = 0; i < s->nx; i++) {
				float v[3];
				sim_velocity_at(s, i + 0.5f, j + 0.5f, k + 0.5f, v);
				int c = CIX(s, i, j, k);
				ux[c] = v[0];
				uy[c] = v[1];
				uz[c] = v[2];
			}
		}
	}
}

static void sim_curl_layers(sim_job_t *job, int begin, int end) {
	physics_sim_t *s  = job->s;
	float         *ux = s->cell_tmp[0], *uy = s->cell_tmp[1], *uz = s->cell_tmp[2], *len = s->cell_tmp[3];
	float         *ox = s->cell_tmp[4], *oy = s->cell_tmp[5], *oz = job->field;
	float          h  = s->cell;
	int            di = 1, dj = s->nx, dk = s->nx * s->ny;
	for (int k = begin < 1 ? 1 : begin; k < end && k < s->nz - 1; k++) {
		for (int j = 1; j < s->ny - 1; j++) {
			for (int i = 1; i < s->nx - 1; i++) {
				int c  = CIX(s, i, j, k);
				ox[c]  = ((uz[c + dj] - uz[c - dj]) - (uy[c + dk] - uy[c - dk])) * 0.5f / h;
				oy[c]  = ((ux[c + dk] - ux[c - dk]) - (uz[c + di] - uz[c - di])) * 0.5f / h;
				oz[c]  = ((uy[c + di] - uy[c - di]) - (ux[c + dj] - ux[c - dj])) * 0.5f / h;
				len[c] = sqrtf(ox[c] * ox[c] + oy[c] * oy[c] + oz[c] * oz[c]);
			}
		}
	}
}

// Vorticity confinement
static void sim_vorticity(physics_sim_t *s, float dt) {
	if (s->vorticity <= 0.0f) {
		return;
	}
	int    n   = s->nx * s->ny * s->nz;
	float *len = s->cell_tmp[3];
	float *ox = s->cell_tmp[4], *oy = s->cell_tmp[5];
	float *oz = malloc(sizeof(float) * n);
	float  h  = s->cell;
	int    di = 1, dj = s->nx, dk = s->nx * s->ny;

	sim_job_t job = {.s = s, .field = oz};
	sim_parallel(sim_center_velocity_layers, &job, s->nz, 1);
	memset(len, 0, sizeof(float) * n);
	sim_parallel(sim_curl_layers, &job, s->nz, 1);

	float eps = s->vorticity * h * dt;
	for (int k = 2; k < s->nz - 2; k++) {
		for (int j = 2; j < s->ny - 2; j++) {
			for (int i = 2; i < s->nx - 2; i++) {
				int   c  = CIX(s, i, j, k);
				float nx = (len[c + di] - len[c - di]) * 0.5f;
				float ny = (len[c + dj] - len[c - dj]) * 0.5f;
				float nz = (len[c + dk] - len[c - dk]) * 0.5f;
				float nl = sqrtf(nx * nx + ny * ny + nz * nz);
				if (nl < 1e-9f) {
					continue;
				}
				nx /= nl;
				ny /= nl;
				nz /= nl;
				float force[3] = {ny * oz[c] - nz * oy[c], nz * ox[c] - nx * oz[c], nx * oy[c] - ny * ox[c]};
				for (int a = 0; a < 3; a++) {
					sim_field_t f     = sim_face(s, a, s->vel[a]);
					int         id0   = FIX(&f, i, j, k);
					int         fc[3] = {i, j, k};
					fc[a] += 1;
					int   id1  = FIX(&f, fc[0], fc[1], fc[2]);
					float air0 = SIM_AIR_DENSITY / (s->mass[a][id0] + SIM_AIR_DENSITY);
					float air1 = SIM_AIR_DENSITY / (s->mass[a][id1] + SIM_AIR_DENSITY);
					f.data[id0] += eps * force[a] * 0.5f * air0;
					f.data[id1] += eps * force[a] * 0.5f * air1;
				}
			}
		}
	}
	free(oz);
}

static void sim_viscosity_copy(sim_job_t *job, int begin, int end) {
	physics_sim_t *s = job->s;
	int           *m = s->mass_box;
	for (int t = begin; t < end; t++) {
		int         a = t / (s->nz + 1), k = t % (s->nz + 1);
		sim_field_t f  = sim_face(s, a, s->vel[a]);
		int         i0 = m[0] > 0 ? m[0] - 1 : 0, i1 = m[1] < f.n[0] ? m[1] + 1 : f.n[0];
		if (k < m[4] - 1 || k > m[5] || k >= f.n[2]) {
			continue;
		}
		for (int j = m[2] > 0 ? m[2] - 1 : 0; j <= m[3] && j < f.n[1]; j++) {
			memcpy(&s->tmp[a][FIX(&f, i0, j, k)], &f.data[FIX(&f, i0, j, k)], sizeof(float) * (i1 - i0));
		}
	}
}

static void sim_viscosity_layers(sim_job_t *job, int begin, int end) {
	physics_sim_t *s = job->s;
	int           *m = s->mass_box;
	for (int t = begin; t < end; t++) {
		int         a = t / (s->nz + 1), k = t % (s->nz + 1);
		sim_field_t f = sim_face(s, a, s->vel[a]);
		if (k < 1 || k < m[4] || k >= m[5] || k >= f.n[2] - 1) {
			continue;
		}
		float *src = s->tmp[a];
		int    di = 1, dj = f.n[0], dk = f.n[0] * f.n[1];
		for (int j = m[2] > 1 ? m[2] : 1; j < m[3] && j < f.n[1] - 1; j++) {
			for (int i = m[0] > 1 ? m[0] : 1; i < m[1] && i < f.n[0] - 1; i++) {
				int   id     = FIX(&f, i, j, k);
				float liquid = fminf(s->mass[a][id], 1.0f);
				if (liquid <= 0.0f) {
					continue;
				}
				float avg = (src[id - di] + src[id + di] + src[id - dj] + src[id + dj] + src[id - dk] + src[id + dk]) / 6.0f;
				f.data[id] += (avg - src[id]) * s->viscosity * liquid;
			}
		}
	}
}

static void sim_viscosity(physics_sim_t *s) {
	if (s->viscosity <= 0.0f) {
		return;
	}
	sim_job_t job = {.s = s};
	sim_parallel(sim_viscosity_copy, &job, 3 * (s->nz + 1), 1);
	sim_parallel(sim_viscosity_layers, &job, 3 * (s->nz + 1), 1);
}

static void sim_max_speed_items(sim_job_t *job, int begin, int end) {
	physics_sim_t *s      = job->s;
	int            blocks = job->a;
	for (int t = begin; t < end; t++) {
		float *v = s->pvel, m = 0.0f;
		int    i0 = t * SIM_MIN_PARTICLES * 3, i1 = i0 + SIM_MIN_PARTICLES * 3;
		if (t < blocks) {
			i1 = i1 < s->count * 3 ? i1 : s->count * 3;
		}
		else {
			int         l = t - blocks, a = l / (s->nz + 1), k = l % (s->nz + 1);
			sim_field_t f = sim_face(s, a, s->vel[a]);
			v             = f.data;
			i0            = k * f.n[0] * f.n[1];
			i1            = k < f.n[2] ? i0 + f.n[0] * f.n[1] : i0;
		}
		for (int i = i0; i < i1; i++) {
			m = fmaxf(m, fabsf(v[i]));
		}
		job->field[t] = m;
	}
}

static float sim_max_speed(physics_sim_t *s) {
	int       blocks = (s->count + SIM_MIN_PARTICLES - 1) / SIM_MIN_PARTICLES;
	int       items  = blocks + 3 * (s->nz + 1);
	float    *max    = malloc(sizeof(float) * items);
	sim_job_t job    = {.s = s, .a = blocks, .field = max};
	sim_parallel(sim_max_speed_items, &job, items, 1);
	float m = 0.0f;
	for (int i = 0; i < items; i++) {
		m = fmaxf(m, max[i]);
	}
	free(max);
	return m * sqrtf(3.0f);
}

static void sim_move_particles(sim_job_t *job, int begin, int end) {
	physics_sim_t *s      = job->s;
	float          radius = s->cell * 0.25f;
	for (int p = begin; p < end; p++) {
		float *x   = &s->pos[p * 3];
		float *v   = &s->pvel[p * 3];
		vec4_t pos = {x[0] + v[0] * job->dt, x[1] + v[1] * job->dt, x[2] + v[2] * job->dt, 1.0f};
		if (s->material[p] == PHYSICS_MATERIAL_SAND) {
			// Sand sticks to colliders
			vec4_t        probe = pos;
			physics_hit_t cv;
			if (physics_collide_point(&probe, s->cell * 2.0f, &cv) && cv.body == NULL) {
				float n[3]  = {probe.x - pos.x, probe.y - pos.y, probe.z - pos.z};
				float len   = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
				float vc[3] = {cv.vel.x, cv.vel.y, cv.vel.z};
				if (len > 1e-9f) {
					float dn  = ((pos.x - x[0]) * n[0] + (pos.y - x[1]) * n[1] + (pos.z - x[2]) * n[2]) / (len * len);
					float vn  = (v[0] * n[0] + v[1] * n[1] + v[2] * n[2]) / (len * len);
					float vcn = (vc[0] * n[0] + vc[1] * n[1] + vc[2] * n[2]) / (len * len);
					float d[3];
					for (int a = 0; a < 3; a++) {
						v[a] = vn * n[a] + vc[a] - vcn * n[a];
						d[a] = dn * n[a] + (vc[a] - vcn * n[a]) * job->dt;
					}
					pos = (vec4_t){x[0] + d[0], x[1] + d[1], x[2] + d[2], 1.0f};
				}
			}
		}
		vec4_t        before = pos;
		physics_hit_t hit;
		job->bodies[p] = NULL;
		if (physics_collide_point(&pos, radius, &hit) && hit.body != NULL && s->material[p] != PHYSICS_MATERIAL_LIQUID) {
			// Grains push bodies by hitting them
			float n[3] = {pos.x - before.x, pos.y - before.y, pos.z - before.z};
			float len  = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
			float vn   = len > 1e-9f ? ((v[0] - hit.vel.x) * n[0] + (v[1] - hit.vel.y) * n[1] + (v[2] - hit.vel.z) * n[2]) / len : 0.0f;
			if (vn < 0.0f) {
				job->bodies[p] = hit.body;
				for (int a = 0; a < 3; a++) {
					job->field[p * 3 + a] = vn * n[a] / len;
				}
			}
		}
		x[0] = pos.x;
		x[1] = pos.y;
		x[2] = pos.z;
	}
}

static float sim_liquid(physics_sim_t *s, int i, int j, int k) {
	int   c[3] = {i, j, k};
	float m    = 0.0f;
	for (int a = 0; a < 3; a++) {
		sim_field_t f  = sim_face(s, a, s->mass[a]);
		int         lo = FIX(&f, c[0], c[1], c[2]);
		c[a] += 1;
		m += s->mass[a][lo] + s->mass[a][FIX(&f, c[0], c[1], c[2])];
		c[a] -= 1;
	}
	return fminf(m / 6.0f, 1.0f);
}

static vec4_t sim_cell_center(physics_sim_t *s, int c) {
	int i = c % s->nx, j = c / s->nx % s->ny, k = c / (s->nx * s->ny);
	return (vec4_t){s->min[0] + (i + 0.5f) * s->cell, s->min[1] + (j + 0.5f) * s->cell, s->min[2] + (k + 0.5f) * s->cell, 1.0f};
}

// Buoyancy and drag as forces, the pressure overshoots for light bodies
static void sim_push_bodies_cells(sim_job_t *job, int begin, int end) {
	physics_sim_t *s    = job->s;
	float          unit = SIM_DENSITY * s->cell * s->cell * s->cell;
	vec4_t         g    = physics_get_gravity();
	int            n[3] = {s->nx, s->ny, s->nz};
	for (int b = begin; b < end; b++) {
		int           c = s->body_cells[b];
		int           i = c % s->nx, j = c / s->nx % s->ny, k = c / (s->nx * s->ny);
		void         *body = s->solid_body[c];
		vec4_t        q    = sim_cell_center(s, c);
		physics_hit_t hit;
		job->field[b * 4 + 3] = 0.0f;
		if (!physics_collide_point(&q, 1e-4f, &hit) || hit.body != body) {
			continue;
		}

		// Submerged when the first cell outside sideways or up holds liquid
		float wet = 0.0f, u[3] = {0.0f, 0.0f, 0.0f};
		for (int dir = 0; dir < 6; dir++) {
			if (dir == 4) {
				continue;
			}
			int a = dir / 2, step = dir % 2 ? 1 : -1, p[3] = {i, j, k};
			for (int m = 0; m < 16; m++) {
				p[a] += step;
				if (p[a] < 0 || p[a] >= n[a]) {
					break;
				}
				int nb = CIX(s, p[0], p[1], p[2]);
				if (s->solid[nb] && s->solid_body[nb] == body) {
					continue;
				}
				float l = s->solid[nb] ? 0.0f : sim_liquid(s, p[0], p[1], p[2]);
				if (l > wet) {
					wet = l;
					sim_velocity_at(s, p[0] + 0.5f, p[1] + 0.5f, p[2] + 0.5f, u);
				}
				break;
			}
		}
		if (wet <= 0.0f) {
			continue;
		}
		float *v   = &s->solid_vel[c * 3];
		float  m   = unit * wet * job->dt;
		float *out = &job->field[b * 4];
		out[0]     = (-g.x + (u[0] - v[0]) * SIM_BODY_DRAG) * m;
		out[1]     = (-g.y + (u[1] - v[1]) * SIM_BODY_DRAG) * m;
		out[2]     = (-g.z + (u[2] - v[2]) * SIM_BODY_DRAG) * m;
		out[3]     = 1.0f;
	}
}

static void sim_push_bodies(physics_sim_t *s, float dt) {
	float    *push = malloc(sizeof(float) * 4 * (s->body_cell_count + 1));
	sim_job_t job  = {.s = s, .dt = dt, .field = push};
	sim_parallel(sim_push_bodies_cells, &job, s->body_cell_count, 1);
	for (int b = 0; b < s->body_cell_count; b++) {
		float *p = &push[b * 4];
		if (p[3] > 0.0f) {
			int c = s->body_cells[b];
			physics_body_apply_impulse_at(s->solid_body[c], (vec4_t){p[0], p[1], p[2], 0.0f}, sim_cell_center(s, c));
		}
	}
	free(push);
}

// Blends liquid and air by mass
static void sim_blend_layers(sim_job_t *job, int begin, int end) {
	physics_sim_t *s       = job->s;
	int            a       = job->a;
	float          dt      = job->dt;
	vec4_t         g       = physics_get_gravity();
	float          gravity = a == 0 ? g.x : a == 1 ? g.y : g.z;
	sim_field_t    f       = sim_face(s, a, s->vel[a]);
	int            nxy     = f.n[0] * f.n[1];
	for (int id = begin * nxy; id < end * nxy; id++) {
		float liquid = s->mass[a][id];
		if (!job->gas) {
			f.data[id] = liquid > 0.0f ? s->tmp[a][id] / liquid + gravity * dt : 0.0f;
			continue;
		}
		float total = liquid + SIM_AIR_DENSITY;
		f.data[id]  = (s->tmp[a][id] + SIM_AIR_DENSITY * f.data[id]) / total;
		f.data[id] += gravity * dt * liquid / total;
	}
}

static void sim_substep(physics_sim_t *s, float dt, unsigned emit) {
	int n = s->nx * s->ny * s->nz;

	int gas = emit != 0;
	for (int c = 0; c < n && !gas && s->gas_count > 0; c++) {
		gas = s->temperature[c] > 1e-3f;
		for (int g = 0; g < s->gas_count && !gas; g++) {
			gas = s->density[g][c] > 1e-3f;
		}
	}
	if (gas) {
		sim_advect_air(s, dt);
	}
	sim_particles_to_grid(s, dt, gas);
	for (int a = 0; a < 3; a++) {
		sim_field_t f   = sim_face(s, a, NULL);
		sim_job_t   job = {.s = s, .dt = dt, .a = a, .gas = gas};
		sim_parallel(sim_blend_layers, &job, f.n[2], 1);
	}

	// Attractors and wind
	int pull = physics_field_any(0), wind = gas && physics_field_any(1);
	for (int a = 0; a < 3 && (pull || wind); a++) {
		sim_field_t f = sim_face(s, a, s->vel[a]);
		for (int k = 0; k < f.n[2]; k++) {
			for (int j = 0; j < f.n[1]; j++) {
				for (int i = 0; i < f.n[0]; i++) {
					int   id = FIX(&f, i, j, k);
					float m  = s->mass[a][id];
					if (!wind && m <= 0.0f) {
						continue;
					}
					float  air = gas ? SIM_AIR_DENSITY / (m + SIM_AIR_DENSITY) : 0.0f;
					vec4_t p   = {s->min[0] + (i + f.off[0]) * s->cell, s->min[1] + (j + f.off[1]) * s->cell, s->min[2] + (k + f.off[2]) * s->cell, 1.0f};
					if (pull && m > 0.0f) {
						vec4_t g = physics_field_force(p);
						f.data[id] += (a == 0 ? g.x : a == 1 ? g.y : g.z) * dt * (1.0f - air);
					}
					if (wind) {
						vec4_t w = physics_field_wind(p);
						f.data[id] += ((a == 0 ? w.x : a == 1 ? w.y : w.z) - f.data[id]) * fminf(dt * SIM_WIND_RATE, 1.0f) * air;
					}
				}
			}
		}
	}

	// Gas sources
	sim_field_t fw = sim_face(s, 2, s->vel[2]);
	if (emit) {
		float jitter = s->buoyancy * dt * 4.0f;
		for (int k = 0; k < s->nz; k++) {
			for (int j = 0; j < s->ny; j++) {
				for (int i = 0; i < s->nx; i++) {
					int c = CIX(s, i, j, k);
					if (!s->emitter[c] || !((emit >> (s->emitter[c] - 1)) & 1u)) {
						continue;
					}
					float *density    = s->density[s->emitter[c] - 1];
					density[c]        = fmaxf(density[c], 1.0f);
					s->temperature[c] = fmaxf(s->temperature[c], 1.0f);
					for (int a = 0; a < 3; a++) {
						sim_field_t f = sim_face(s, a, s->vel[a]);
						f.data[FIX(&f, i, j, k)] += (sim_random(s) - 0.5f) * jitter;
					}
				}
			}
		}
	}

	// Buoyancy
	for (int k = 0; k < fw.n[2] && s->gas_count > 0; k++) {
		for (int j = 0; j < fw.n[1]; j++) {
			for (int i = 0; i < fw.n[0]; i++) {
				float t0  = k > 0 ? s->temperature[CIX(s, i, j, k - 1)] : 0.0f;
				float t1  = k < s->nz ? s->temperature[CIX(s, i, j, k)] : 0.0f;
				int   id  = FIX(&fw, i, j, k);
				float air = SIM_AIR_DENSITY / (s->mass[2][id] + SIM_AIR_DENSITY);
				fw.data[id] += dt * s->buoyancy * (t0 + t1) * 0.5f * air;
			}
		}
	}

	if (gas) {
		sim_vorticity(s, dt);
	}
	sim_viscosity(s);
	sim_close_faces(s);
	sim_project(s, gas);
	sim_push_bodies(s, dt);
	sim_close_faces(s);

	// Advect particles
	sim_grid_to_particles(s, dt);
	void    **bodies = malloc(sizeof(void *) * (s->count + 1));
	float    *push   = malloc(sizeof(float) * 3 * (s->count + 1));
	sim_job_t job    = {.s = s, .dt = dt, .bodies = bodies, .field = push};
	sim_parallel(sim_move_particles, &job, s->count, SIM_MIN_PARTICLES);
	float grain = SIM_PARTICLE_MASS * SIM_DENSITY * s->cell * s->cell * s->cell;
	for (int p = 0; p < s->count; p++) {
		if (bodies[p] != NULL) {
			float *d = &push[p * 3];
			physics_body_apply_impulse_at(bodies[p], (vec4_t){d[0] * grain, d[1] * grain, d[2] * grain, 0.0f},
			                              (vec4_t){s->pos[p * 3], s->pos[p * 3 + 1], s->pos[p * 3 + 2], 1.0f});
		}
	}
	free(bodies);
	free(push);
	float max[3] = {s->min[0] + s->nx * s->cell, s->min[1] + s->ny * s->cell, s->min[2] + s->nz * s->cell};
	for (int p = 0; p < s->count;) {
		float *x   = &s->pos[p * 3];
		int    out = 0;
		for (int a = 0; a < 3; a++) {
			out |= x[a] < s->min[a] || x[a] >= max[a];
		}
		if (out) {
			sim_remove_particle(s, p);
			continue;
		}
		p++;
	}

	// Advect gas
	if (gas) {
		for (int g = 0; g < s->gas_count; g++) {
			sim_advect_cells(s, s->density[g], dt);
		}
		sim_advect_cells(s, s->temperature, dt);
		float fade = 1.0f / (1.0f + dt * s->dissipation);
		float cool = 1.0f / (1.0f + dt * s->cooling);
		for (int c = 0; c < n; c++) {
			for (int g = 0; g < s->gas_count; g++) {
				s->density[g][c] *= fade;
			}
			s->temperature[c] *= cool;
		}
	}
}

void physics_sim_step(physics_sim_t *s, float dt, unsigned emit) {
	if (dt <= 0.0f) {
		return;
	}
	sim_pool_start();
	int n = s->nx * s->ny * s->nz;
	if (s->collider_version != physics_collider_version()) {
		s->collider_version = physics_collider_version();
		sim_job_t job       = {.s = s};
		sim_parallel(sim_mark_solid_layers, &job, s->nz, 1);
	}

	// Colliders and obstacle points
	memcpy(s->solid, s->solid_base, n);
	memcpy(s->solid_vel, s->solid_base_vel, sizeof(float) * 3 * n);
	memcpy(s->solid_body, s->solid_base_body, sizeof(void *) * n);
	for (int i = 0; i < s->obstacle_count; i++) {
		float *o = &s->obstacles[i * 6];
		int    c[3];
		for (int a = 0; a < 3; a++) {
			c[a] = (int)floorf((o[a] - s->min[a]) / s->cell);
		}
		if (c[0] < 0 || c[1] < 0 || c[2] < 0 || c[0] >= s->nx || c[1] >= s->ny || c[2] >= s->nz) {
			continue;
		}
		int id       = CIX(s, c[0], c[1], c[2]);
		s->solid[id] = 1;
		memcpy(&s->solid_vel[id * 3], &o[3], sizeof(float) * 3);
		s->solid_body[id] = NULL;
	}
	int closed = 0;
	for (int a = 0; a < 3; a++) {
		sim_field_t f   = sim_face(s, a, NULL);
		sim_job_t   job = {.s = s, .a = a};
		sim_parallel(sim_mark_closed_layers, &job, f.n[2], 1);
		for (int id = 0; id < sim_field_size(&f); id++) {
			closed += s->closed[a][id] != 0;
		}
	}
	s->closed_list  = realloc(s->closed_list, sizeof(int) * 3 * (closed + 1));
	s->closed_count = 0;
	for (int a = 0; a < 3; a++) {
		sim_field_t f    = sim_face(s, a, NULL);
		int         step = a == 0 ? 1 : a == 1 ? s->nx : s->nx * s->ny;
		for (int k = 0; k < f.n[2]; k++) {
			for (int j = 0; j < f.n[1]; j++) {
				for (int i = 0; i < f.n[0]; i++) {
					int id = FIX(&f, i, j, k);
					if (s->closed[a][id] != 0) {
						int *l = &s->closed_list[s->closed_count++ * 3];
						l[0]   = a;
						l[1]   = id;
						l[2]   = CIX(s, i, j, k) - (s->closed[a][id] == 2 ? step : 0);
					}
				}
			}
		}
	}
	int bodies = 0;
	for (int c = 0; c < n; c++) {
		bodies += s->solid_body[c] != NULL;
	}
	s->body_cells      = realloc(s->body_cells, sizeof(int) * (bodies + 1));
	s->body_cell_count = 0;
	for (int c = 0; c < n && s->body_cell_count < bodies; c++) {
		if (s->solid_body[c] != NULL) {
			s->body_cells[s->body_cell_count++] = c;
		}
	}
	// CFL, solids also limit by the elastic wave speed
	float wave = 0.0f;
	for (int p = 0; p < s->count; p++) {
		if (s->material[p] != PHYSICS_MATERIAL_LIQUID) {
			float mu, lambda;
			sim_lame(s, s->owner[p], &mu, &lambda);
			wave = fmaxf(wave, sqrtf(lambda + 2.0f * mu));
		}
	}
	int   max_steps = wave > 0.0f ? SIM_ELASTIC_SUBSTEPS : SIM_MAX_SUBSTEPS;
	float t         = 0.0f;
	int   steps     = 0;
	while (t < dt - 1e-6f && steps < max_steps) {
		float speed = sim_max_speed(s) + sqrtf(s->cell * 9.81f);
		float sub   = fminf(dt - t, SIM_CFL * s->cell / speed);
		if (wave > 0.0f) {
			sub = fminf(sub, SIM_ELASTIC_CFL * s->cell / (wave + speed));
		}
		if (steps == max_steps - 1) {
			sub = dt - t;
		}
		sim_substep(s, sub, emit);
		t += sub;
		steps++;
	}
	sim_pool_stop();
}

int physics_sim_sample(physics_sim_t *s, vec4_t p, vec4_t *vel, float *liquid) {
	float x = (p.x - s->min[0]) / s->cell, y = (p.y - s->min[1]) / s->cell, z = (p.z - s->min[2]) / s->cell;
	if (x < 0.0f || y < 0.0f || z < 0.0f || x >= s->nx || y >= s->ny || z >= s->nz) {
		return 0;
	}
	float v[3], m = 0.0f;
	sim_velocity_at(s, x, y, z, v);
	for (int a = 0; a < 3; a++) {
		sim_field_t f = sim_face(s, a, s->mass[a]);
		m += sim_sample(&f, x, y, z) / 3.0f;
	}
	*vel    = (vec4_t){v[0], v[1], v[2], 0.0f};
	*liquid = fminf(m, 1.0f);
	return 1;
}

// Polynomial smooth minimum
static inline float sim_smin(float a, float b, float k) {
	float h = fmaxf(k - fabsf(a - b), 0.0f) / k;
	return fminf(a, b) - h * h * k * 0.25f;
}

float *physics_sim_particle_field(float *pos, int count, float spacing, float voxel, int max_res, int *nx, int *ny, int *nz, float *min, float *out_voxel) {
	*nx = *ny = *nz = 0;
	if (count == 0) {
		return NULL;
	}

	float radius = spacing * 0.6f;
	float blend  = spacing;
	float reach  = radius + blend;
	float lo[3]  = {1e9f, 1e9f, 1e9f};
	float hi[3]  = {-1e9f, -1e9f, -1e9f};
	for (int i = 0; i < count; i++) {
		for (int a = 0; a < 3; a++) {
			lo[a] = fminf(lo[a], pos[i * 3 + a]);
			hi[a] = fmaxf(hi[a], pos[i * 3 + a]);
		}
	}
	for (int a = 0; a < 3; a++) {
		lo[a] -= reach + voxel;
		hi[a] += reach + voxel;
	}

	// World aligned lattice in quarter octave steps, so the surface does not shimmer
	float  base = voxel;
	int    n[3];
	double size = 0.0;
	for (int step = 0;; step++) {
		voxel = base * exp2f(step * 0.25f);
		size  = 1.0;
		for (int a = 0; a < 3; a++) {
			min[a] = floorf(lo[a] / voxel) * voxel;
			n[a]   = (int)ceilf((hi[a] - min[a]) / voxel) + 1;
			size *= n[a];
		}
		if (size <= (double)max_res * max_res * max_res) {
			break;
		}
	}
	memcpy(lo, min, sizeof(lo));
	int    cells = n[0] * n[1] * n[2];
	float *field = malloc(sizeof(float) * cells);
	for (int i = 0; i < cells; i++) {
		field[i] = reach;
	}

	for (int i = 0; i < count; i++) {
		float *p = &pos[i * 3];
		int    c0[3], c1[3];
		for (int a = 0; a < 3; a++) {
			c0[a] = (int)fmaxf(0.0f, ceilf((p[a] - reach - lo[a]) / voxel));
			c1[a] = (int)fminf((float)(n[a] - 1), floorf((p[a] + reach - lo[a]) / voxel));
		}
		for (int z = c0[2]; z <= c1[2]; z++) {
			float dz = lo[2] + z * voxel - p[2];
			for (int y = c0[1]; y <= c1[1]; y++) {
				float dy = lo[1] + y * voxel - p[1];
				for (int x = c0[0]; x <= c1[0]; x++) {
					float  dx = lo[0] + x * voxel - p[0];
					float  d  = sqrtf(dx * dx + dy * dy + dz * dz) - radius;
					float *v  = &field[x + n[0] * (y + n[1] * z)];
					*v        = sim_smin(*v, d, blend);
				}
			}
		}
	}

	*nx        = n[0];
	*ny        = n[1];
	*nz        = n[2];
	*out_voxel = voxel;
	return field;
}

#endif
