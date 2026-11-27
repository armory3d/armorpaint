#ifdef WITH_PHYSICS

#include "iron_physics.h"
#include "iron_system.h"
#include "iron_thread.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SIM_PARTICLE_MASS    0.125f // 8 per cell
#define SIM_MAX_SUBSTEPS     16
#define SIM_CFL              2.0f // Cells moved per substep
#define SIM_PCG_TOLERANCE    1e-4f
#define SIM_PRESSURE_EPS     1e-4f
#define SIM_MAX_THREADS      16
#define SIM_CHUNKS           32 // Per thread
#define SIM_POISSON          0.3f
#define SIM_ELASTIC_CFL      0.4f
#define SIM_ELASTIC_SUBSTEPS 128
#define SIM_DENSITY          1000.0f

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
	s->burn        = 0.8f;
	s->soot        = 0.5f;
	s->heat        = 1.0f;
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
	}
	s->emitter          = calloc(n, 1);
	s->solid_base       = calloc(n, 1);
	s->solid_base_vel   = calloc(n * 3, sizeof(float));
	s->solid_base_body  = calloc(n, sizeof(void *));
	s->collider_version = physics_collider_version() - 1;
	return s;
}

static void sim_gpu_destroy(physics_sim_t *s);

void physics_sim_destroy(physics_sim_t *s) {
	if (s == NULL) {
		return;
	}
	sim_gpu_destroy(s);
	for (int a = 0; a < 3; a++) {
		free(s->vel[a]);
		free(s->mass[a]);
	}
	for (int g = 0; g < s->gas_count; g++) {
		free(s->density[g]);
		free(s->flame[g]);
	}
	free(s->density);
	free(s->flame);
	free(s->emitter);
	free(s->solid_base);
	free(s->solid_base_vel);
	free(s->solid_base_body);
	free(s->pos);
	free(s->material);
	free(s->owner);
	free(s);
}

int physics_sim_add_gas(physics_sim_t *s) {
	s->density               = realloc(s->density, sizeof(float *) * (s->gas_count + 1));
	s->density[s->gas_count] = calloc(s->nx * s->ny * s->nz, sizeof(float));
	s->flame                 = realloc(s->flame, sizeof(float *) * (s->gas_count + 1));
	s->flame[s->gas_count]   = NULL;
	return s->gas_count++;
}

// Its emitter releases flame instead of smoke
int physics_sim_add_fire(physics_sim_t *s) {
	int g       = physics_sim_add_gas(s);
	s->flame[g] = calloc(s->nx * s->ny * s->nz, sizeof(float));
	return g;
}

void physics_sim_add_particle(physics_sim_t *s, float x, float y, float z, int material, int owner) {
	if (s->count == s->cap) {
		s->cap      = s->cap == 0 ? 1024 : s->cap * 2;
		s->pos      = realloc(s->pos, sizeof(float) * 3 * s->cap);
		s->material = realloc(s->material, s->cap);
		s->owner    = realloc(s->owner, s->cap);
	}
	int i             = s->count++;
	s->pos[i * 3]     = x;
	s->pos[i * 3 + 1] = y;
	s->pos[i * 3 + 2] = z;
	s->material[i]    = (unsigned char)material;
	s->owner[i]       = (unsigned char)owner;
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

typedef struct {
	physics_sim_t *s;
	float         *field;
	float         *src;
	void         **bodies;
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

static vec4_t sim_cell_center(physics_sim_t *s, int c) {
	int i = c % s->nx, j = c / s->nx % s->ny, k = c / (s->nx * s->ny);
	return (vec4_t){s->min[0] + (i + 0.5f) * s->cell, s->min[1] + (j + 0.5f) * s->cell, s->min[2] + (k + 0.5f) * s->cell, 1.0f};
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

// The sim steps on the gpu, kernels of shaders/compute/gpu_sim.shader over one storage buffer. Collider queries stay on the cpu, once per
// frame into the cells: solids, and for the particles the distance to the colliders. Bodies get the summed pushes after each
// substep.
// The pressure is solved with multigrid preconditioned conjugate gradients, the fields the cpu reads are read back after each frame

#include "iron_gpu.h"

buffer_t *iron_load_blob(char *file);
void      iron_delete_blob(buffer_t *buffer);

enum {
	SIM_GPU_VEL    = 0, // Header slots, S_* in gpu_sim.shader
	SIM_GPU_MASS   = 3,
	SIM_GPU_TMP    = 6,
	SIM_GPU_PM     = 9,
	SIM_GPU_PMS    = 12,
	SIM_GPU_COEF   = 15,
	SIM_GPU_CLOSED = 18,
	SIM_GPU_FORCE  = 21,
	SIM_GPU_WIND   = 24,
	SIM_GPU_TEMP   = 27,
	SIM_GPU_PRESS  = 28,
	SIM_GPU_R      = 29,
	SIM_GPU_D      = 30,
	SIM_GPU_AD     = 31,
	SIM_GPU_IDIAG  = 32,
	SIM_GPU_FIXED  = 33,
	SIM_GPU_SOLID  = 34,
	SIM_GPU_SVEL   = 35,
	SIM_GPU_SDF    = 36,
	SIM_GPU_BODY   = 37,
	SIM_GPU_EMIT   = 38,
	SIM_GPU_CT     = 39,
	SIM_GPU_DENS   = 40,
	SIM_GPU_FLAME  = 41,
	SIM_GPU_PPOS   = 42,
	SIM_GPU_PVEL   = 43,
	SIM_GPU_PAFF   = 44,
	SIM_GPU_PDEF   = 45,
	SIM_GPU_PSTR   = 46,
	SIM_GPU_PINFO  = 47,
	SIM_GPU_BCELL  = 48,
	SIM_GPU_BACC   = 49,
	SIM_GPU_BPOS   = 50,
	SIM_GPU_RED    = 51,
	SIM_GPU_SCAL   = 52,
	SIM_GPU_STIFF  = 53,
	SIM_GPU_Z      = 54,
	SIM_GPU_SLOTS  = 64,
};

enum {
	SIM_K_CLEAR,
	SIM_K_COPY,
	SIM_K_CLOSED,
	SIM_K_CLOSE_FACES,
	SIM_K_ADVECT_AIR,
	SIM_K_ADVECT,
	SIM_K_BURN,
	SIM_K_EMIT,
	SIM_K_BUOYANCY,
	SIM_K_CURL,
	SIM_K_VORT_FORCE,
	SIM_K_VORT_APPLY,
	SIM_K_FIELDS,
	SIM_K_P2G,
	SIM_K_BLEND,
	SIM_K_VISCOSITY,
	SIM_K_G2P,
	SIM_K_MOVE,
	SIM_K_FIXED,
	SIM_K_FIXED_LIQ,
	SIM_K_COEF,
	SIM_K_PCG_INIT,
	SIM_K_REDUCE,
	SIM_K_SCALARS,
	SIM_K_PCG_AD,
	SIM_K_PCG_UPDATE,
	SIM_K_PCG_DIR,
	SIM_K_GRADIENT,
	SIM_K_STATS,
	SIM_K_PUSH_BODIES,
	SIM_K_PCG_RESET,
	SIM_K_PCG_ZERO,
	SIM_K_MG_FIXED,
	SIM_K_MG_FACES,
	SIM_K_MG_DIAG,
	SIM_K_MG_SMOOTH,
	SIM_K_MG_RESIDUAL,
	SIM_K_MG_RESTRICT,
	SIM_K_MG_PROLONG,
	SIM_K_PCG_DOT,
	SIM_K_SUBSTEP,
};

#define SIM_GPU_DONE       5 // Scalars, C_* in gpu_sim.shader
#define SIM_GPU_SPEED      7
#define SIM_GPU_GAS        8
#define SIM_GPU_T          96 // Queued ahead, the frame's: time stepped, substep size, idle, most iterations, substeps
#define SIM_GPU_FRAME      5
#define SIM_GPU_REDUCE     65536 // Threads of the kernels that sum over the cells
#define SIM_GPU_BODY_FIXED 1024.0f
#define SIM_GPU_MAX_BODIES 256
#define SIM_GPU_PCG_ITERS  200
#define SIM_GPU_LEVELS     6    // Multigrid
#define SIM_GPU_COARSEST   2    // Sweeps on the coarsest level
#define SIM_GPU_SDF_RADIUS 3.0f // Cells, the colliders particles see

// Where reading back is slow, the substeps are sized on the gpu (gpu_sim.shader built with _QUEUE_AHEAD, see base/project.c). A frame's
// substeps and the iterations of their solves are then queued without waiting on the gpu: as many as the last frame needed, the ones
// past the end skip. Elsewhere the cpu sizes each substep from the stats and reads after each solve chunk, queuing no more
#ifdef IRON_WEBGPU
#define SIM_GPU_QUEUE_AHEAD 1
#else
#define SIM_GPU_QUEUE_AHEAD 0
#endif

typedef struct {
	uint32_t op[4]; // Kernel, items, threads per dispatch row, axis
	uint32_t dims[4];
	uint32_t u0[4];
	uint32_t u1[4];
	uint32_t u2[4];
	uint32_t u3[4];
	float    f0[4];
	float    f1[4]; // Gravity, dt
	float    f2[4];
	float    grid[4];
} sim_gpu_constants_t;

typedef struct {
	void     *buffer;
	uint32_t  slots[SIM_GPU_SLOTS];
	uint32_t  faces[3];
	int       capacity; // Particles
	float     wave;
	unsigned  substeps;
	int       obstacles; // In the solids last frame
	uint32_t *solid;     // Solids with the obstacles
	float    *svel;      // Velocity of the colliders, three fields
	uint32_t *base;      // Solids of the colliders
	float    *base_vel;
	int       bodies;
	void     *body[SIM_GPU_MAX_BODIES];
	int       body_cells;
	int       iters; // Of the last solve, the next one checks for convergence from there on
	int       levels;
	int       guard;      // Queued ahead, kernels skip once the frame is stepped
	uint32_t  read_words; // The range read after a frame where reading back is slow, from the scalars
	float    *read;
	int       last_steps; // Substeps of the last frame, queued ahead next
	int       budget;     // Iterations queued ahead per solve
	float     gas;        // Gas at the end of the last frame
	struct {
		int      n[3];
		uint32_t coef[3]; // Faces, coef, inverse diagonal, fixed, solution, right side, residual, the order of u1 and u2 in the shader
		uint32_t idiag;
		uint32_t fixed;
		uint32_t x;
		uint32_t b;
		uint32_t res;
	} level[SIM_GPU_LEVELS];
} sim_gpu_t;

static void *sim_gpu_kernel = NULL;
static int   sim_gpu_state  = 0; // 0 not tried, 1 ready, 2 none

static void sim_gpu_run(physics_sim_t *s, sim_gpu_constants_t *k, int kernel, int items, int axis) {
	if (items <= 0) {
		return;
	}
	sim_gpu_t *g      = s->gpu;
	uint32_t   groups = (uint32_t)(items + 63) / 64;
	uint32_t   gx     = groups < 32768 ? groups : 32768;
	k->op[0]          = (uint32_t)kernel;
	k->op[1]          = (uint32_t)items;
	k->op[2]          = gx * 64;
	k->op[3]          = (uint32_t)axis;
	k->u3[2]          = (uint32_t)g->guard;
	k->dims[0]        = (uint32_t)s->nx;
	k->dims[1]        = (uint32_t)s->ny;
	k->dims[2]        = (uint32_t)s->nz;
	k->dims[3]        = (uint32_t)(s->nx * s->ny * s->nz);
	memcpy(k->grid, s->min, sizeof(float) * 3);
	k->grid[3] = s->cell;
	gpu_kernel_dispatch(sim_gpu_kernel, g->buffer, k, sizeof(*k), gx, (groups + gx - 1) / gx);
}

// Each axis of a face field
static void sim_gpu_faces(physics_sim_t *s, sim_gpu_constants_t *k, int kernel) {
	sim_gpu_t *g = s->gpu;
	for (int a = 0; a < 3; a++) {
		sim_gpu_run(s, k, kernel, (int)g->faces[a], a);
	}
}

static void sim_gpu_fill(physics_sim_t *s, uint32_t offset, int count, uint32_t value) {
	sim_gpu_constants_t k = {.u0 = {offset, value}};
	sim_gpu_run(s, &k, SIM_K_CLEAR, count, 0);
}

static void sim_gpu_copy(physics_sim_t *s, uint32_t from, uint32_t to, int count) {
	sim_gpu_constants_t k = {.u0 = {from, to}};
	sim_gpu_run(s, &k, SIM_K_COPY, count, 0);
}

static void sim_gpu_write(physics_sim_t *s, int slot, uint32_t at, const void *data, uint64_t words) {
	sim_gpu_t *g = s->gpu;
	gpu_storage_write(g->buffer, ((uint64_t)g->slots[slot] + at) * 4, data, words * 4);
}

static void sim_gpu_read(physics_sim_t *s, int slot, uint32_t at, void *data, uint64_t words) {
	sim_gpu_t *g = s->gpu;
	gpu_storage_read(g->buffer, ((uint64_t)g->slots[slot] + at) * 4, data, words * 4);
}

static void sim_gpu_destroy(physics_sim_t *s) {
	sim_gpu_t *g = s->gpu;
	if (g == NULL) {
		return;
	}
	gpu_storage_destroy(g->buffer);
	free(g->read);
	free(g->solid);
	free(g->svel);
	free(g->base);
	free(g->base_vel);
	free(g);
	s->gpu = NULL;
}

static bool sim_gpu_create(physics_sim_t *s) {
	if (sim_gpu_state == 0) {
		sim_gpu_state = 2;
		char path[512]; // gpu_sim.shader, compiled by base/project.c
		snprintf(path, sizeof(path), "%sgpu_sim%s", data_path(), sys_shader_ext());
		buffer_t *shader = iron_load_blob(path);
		if (shader != NULL) {
			sim_gpu_kernel = gpu_kernel_create(shader->buffer, (int)shader->length);
			sim_gpu_state  = sim_gpu_kernel != NULL ? 1 : 2;
			iron_delete_blob(shader);
		}
	}
	if (sim_gpu_state != 1) {
		return false;
	}
	int        n     = s->nx * s->ny * s->nz;
	int        gases = s->gas_count;
	int        p     = s->count;
	sim_gpu_t *g     = calloc(1, sizeof(sim_gpu_t));
	g->faces[0]      = (uint32_t)((s->nx + 1) * s->ny * s->nz);
	g->faces[1]      = (uint32_t)(s->nx * (s->ny + 1) * s->nz);
	g->faces[2]      = (uint32_t)(s->nx * s->ny * (s->nz + 1));
	g->capacity      = p;

	// Layout. What the cpu reads after a frame comes first, to read it at once where reading back is slow: scalars, velocities,
	// temperature, densities, flames, liquid, particles. The stats kernel scans the velocities, and temperature, densities, flames, as consecutive fields
	uint64_t f0 = g->faces[0], f1 = g->faces[1], f2 = g->faces[2], fp = p > 0 ? 1 : 0, cp = p > 0 ? 4 : 0;
#define SIM_GPU_FACES(f, scale) {f, f0 * (scale)}, {f + 1, f1 * (scale)}, {f + 2, f2 * (scale)}
	uint64_t layout[][2] = {
	    {SIM_GPU_SCAL, 128},
	    SIM_GPU_FACES(SIM_GPU_VEL, 1),
	    {SIM_GPU_TEMP, n},
	    {SIM_GPU_DENS, gases * n},
	    {SIM_GPU_FLAME, gases * n},
	    SIM_GPU_FACES(SIM_GPU_MASS, 1),
	    {SIM_GPU_PPOS, 3 * p},
	    {SIM_GPU_PINFO, p},
	    SIM_GPU_FACES(SIM_GPU_TMP, 1),
	    SIM_GPU_FACES(SIM_GPU_PM, fp),
	    SIM_GPU_FACES(SIM_GPU_PMS, fp),
	    SIM_GPU_FACES(SIM_GPU_COEF, 1),
	    SIM_GPU_FACES(SIM_GPU_CLOSED, 1),
	    SIM_GPU_FACES(SIM_GPU_FORCE, 1),
	    SIM_GPU_FACES(SIM_GPU_WIND, 1),
	    {SIM_GPU_PRESS, n},
	    {SIM_GPU_R, n},
	    {SIM_GPU_D, n},
	    {SIM_GPU_AD, n},
	    {SIM_GPU_IDIAG, n},
	    {SIM_GPU_FIXED, n},
	    {SIM_GPU_SOLID, n},
	    {SIM_GPU_SVEL, 3 * n},
	    {SIM_GPU_SDF, cp * n},
	    {SIM_GPU_BODY, n},
	    {SIM_GPU_EMIT, n},
	    {SIM_GPU_CT, 8 * n},
	    {SIM_GPU_BCELL, n},
	    {SIM_GPU_Z, n},
	    {SIM_GPU_PVEL, 3 * p},
	    {SIM_GPU_PAFF, 9 * p},
	    {SIM_GPU_PDEF, 9 * p},
	    {SIM_GPU_PSTR, 9 * p},
	    {SIM_GPU_BACC, 12 * SIM_GPU_MAX_BODIES},
	    {SIM_GPU_BPOS, 9 * SIM_GPU_MAX_BODIES},
	    {SIM_GPU_RED, SIM_GPU_REDUCE + 1024},
	    {SIM_GPU_STIFF, 256},
	};
#undef SIM_GPU_FACES
	uint64_t at = SIM_GPU_SLOTS;
	for (size_t i = 0; i < sizeof(layout) / sizeof(layout[0]); i++) {
		g->slots[layout[i][0]] = (uint32_t)at;
		at += layout[i][1];
		if (layout[i][0] == (p > 0 ? SIM_GPU_PINFO : SIM_GPU_FLAME)) {
			g->read_words = (uint32_t)(at - g->slots[SIM_GPU_SCAL]);
		}
	}
	// Multigrid levels, halved until a side is small
	int ln[3] = {s->nx, s->ny, s->nz};
	for (g->levels = 0; g->levels < SIM_GPU_LEVELS; g->levels++) {
		int l = g->levels;
		memcpy(g->level[l].n, ln, sizeof(ln));
		if (l == 0) {
			for (int a = 0; a < 3; a++) {
				g->level[0].coef[a] = g->slots[SIM_GPU_COEF + a];
			}
			g->level[0].idiag = g->slots[SIM_GPU_IDIAG];
			g->level[0].fixed = g->slots[SIM_GPU_FIXED];
			g->level[0].x     = g->slots[SIM_GPU_Z];
			g->level[0].b     = g->slots[SIM_GPU_R];
			g->level[0].res   = g->slots[SIM_GPU_CT];
		}
		else {
			int cells = ln[0] * ln[1] * ln[2];
			for (int a = 0; a < 3; a++) {
				g->level[l].coef[a] = (uint32_t)at;
				at += (uint64_t)cells / ln[a] * (ln[a] + 1);
			}
			uint32_t *fields[] = {&g->level[l].idiag, &g->level[l].fixed, &g->level[l].x, &g->level[l].b, &g->level[l].res};
			for (int f = 0; f < 5; f++) {
				*fields[f] = (uint32_t)at;
				at += cells;
			}
		}
		if (ln[0] <= 4 || ln[1] <= 4 || ln[2] <= 4) {
			g->levels++;
			break;
		}
		for (int a = 0; a < 3; a++) {
			ln[a] = (ln[a] + 1) / 2;
		}
	}
	g->buffer = at < UINT32_MAX ? gpu_storage_create(at * 4) : NULL;
	if (g->buffer == NULL) {
		free(g);
		return false;
	}
	s->gpu      = g;
	g->read     = SIM_GPU_QUEUE_AHEAD ? malloc(sizeof(float) * g->read_words) : NULL;
	g->solid    = calloc(n, sizeof(uint32_t));
	g->svel     = calloc(3 * n, sizeof(float));
	g->base     = calloc(n, sizeof(uint32_t));
	g->base_vel = calloc(3 * n, sizeof(float));

	sim_gpu_fill(s, 0, (int)at, 0);
	gpu_storage_write(g->buffer, 0, g->slots, sizeof(g->slots));

	// The state the cpu set up: gas sources, the initial gas, particles
	uint32_t *u32 = malloc(sizeof(uint32_t) * (n > 9 * p ? n : 9 * p) + 4);
	for (int c = 0; c < n; c++) {
		u32[c] = s->emitter[c];
	}
	sim_gpu_write(s, SIM_GPU_EMIT, 0, u32, n);
	for (int i = 0; i < gases; i++) {
		sim_gpu_write(s, SIM_GPU_DENS, i * n, s->density[i], n);
		if (s->flame[i] != NULL) {
			sim_gpu_write(s, SIM_GPU_FLAME, i * n, s->flame[i], n);
		}
	}
	sim_gpu_write(s, SIM_GPU_STIFF, 0, s->stiffness, 256);
	if (p > 0) {
		sim_gpu_write(s, SIM_GPU_PPOS, 0, s->pos, 3 * p);
		float *f = (float *)u32; // Deformation starts at identity
		for (int i = 0; i < 9 * p; i++) {
			f[i] = i % 9 % 4 == 0 ? 1.0f : 0.0f;
		}
		sim_gpu_write(s, SIM_GPU_PDEF, 0, f, 9 * p);
		for (int i = 0; i < p; i++) {
			u32[i] = s->material[i] | (uint32_t)s->owner[i] << 8;
		}
		sim_gpu_write(s, SIM_GPU_PINFO, 0, u32, p);
	}
	free(u32);

	// CFL, solids also limit by the elastic wave speed
	for (int i = 0; i < p; i++) {
		if (s->material[i] != PHYSICS_MATERIAL_LIQUID) {
			float mu, lambda;
			sim_lame(s, s->owner[i], &mu, &lambda);
			g->wave = fmaxf(g->wave, sqrtf(lambda + 2.0f * mu));
		}
	}
	return true;
}

// Center, velocity and angular velocity of the bodies, for the particles hitting them
static void sim_gpu_body_motion(physics_sim_t *s) {
	sim_gpu_t *g = s->gpu;
	float      m[SIM_GPU_MAX_BODIES * 9];
	for (int i = 0; i < g->bodies; i++) {
		vec4_t c, v, vx, vy;
		physics_body_get_pos(g->body[i], &c);
		v          = physics_body_velocity_at(g->body[i], c);
		vx         = vec4_sub(physics_body_velocity_at(g->body[i], vec4_add(c, (vec4_t){1.0f, 0.0f, 0.0f, 0.0f})), v); // w x (1, 0, 0)
		vy         = vec4_sub(physics_body_velocity_at(g->body[i], vec4_add(c, (vec4_t){0.0f, 1.0f, 0.0f, 0.0f})), v); // w x (0, 1, 0)
		float b[9] = {c.x, c.y, c.z, v.x, v.y, v.z, vy.z, -vx.z, vx.y};
		memcpy(&m[i * 9], b, sizeof(b));
	}
	sim_gpu_write(s, SIM_GPU_BPOS, 0, m, 9 * g->bodies);
}

// Colliders near the particles: distance, the way out, their velocity and body
static void sim_gpu_sdf_layers(sim_job_t *job, int begin, int end) {
	physics_sim_t *s      = job->s;
	int            n      = s->nx * s->ny * s->nz;
	float         *sdf    = job->field;
	float          radius = s->cell * SIM_GPU_SDF_RADIUS;
	for (int c = begin * s->nx * s->ny; c < end * s->nx * s->ny; c++) {
		vec4_t        p = sim_cell_center(s, c);
		vec4_t        q = p;
		physics_hit_t hit;
		sdf[c]     = radius;
		sdf[c + n] = sdf[c + 2 * n] = sdf[c + 3 * n] = 0.0f;
		job->bodies[c]                               = NULL;
		if (physics_collide_point(&q, radius, &hit)) {
			float d[3] = {q.x - p.x, q.y - p.y, q.z - p.z};
			float l    = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
			sdf[c]     = radius - l;
			for (int a = 0; a < 3 && l > 1e-9f; a++) {
				sdf[c + (a + 1) * n] = d[a] / l;
			}
			// The hit is the last collider pushed out of, the nearest ones are within the distance
			q = p;
			physics_collide_point(&q, fmaxf(sdf[c], 0.0f) + s->cell * 0.25f, &hit);
			job->src[c * 3]     = hit.vel.x;
			job->src[c * 3 + 1] = hit.vel.y;
			job->src[c * 3 + 2] = hit.vel.z;
			job->bodies[c]      = hit.body;
		}
	}
}

static int sim_gpu_body_index(sim_gpu_t *g, void *body) {
	if (body == NULL) {
		return 0;
	}
	for (int i = 0; i < g->bodies; i++) {
		if (g->body[i] == body) {
			return i + 1;
		}
	}
	if (g->bodies == SIM_GPU_MAX_BODIES) {
		return 0;
	}
	g->body[g->bodies++] = body;
	return g->bodies;
}

// After the colliders moved: solids, the bodies and their cells, and the colliders the particles see
static void sim_gpu_mark(physics_sim_t *s) {
	sim_gpu_t *g   = s->gpu;
	int        n   = s->nx * s->ny * s->nz;
	sim_job_t  job = {.s = s};
	sim_parallel(sim_mark_solid_layers, &job, s->nz, 1);

	uint32_t *body = calloc(n, sizeof(uint32_t));
	float    *cvel = calloc(3 * n, sizeof(float));
	g->bodies      = 0;
	if (g->capacity > 0) {
		float *sdf    = malloc(sizeof(float) * 4 * n);
		void **bodies = malloc(sizeof(void *) * n);
		job           = (sim_job_t){.s = s, .field = sdf, .src = cvel, .bodies = bodies};
		sim_parallel(sim_gpu_sdf_layers, &job, s->nz, 1);
		for (int c = 0; c < n; c++) {
			body[c] = (uint32_t)sim_gpu_body_index(g, bodies[c]);
		}
		sim_gpu_write(s, SIM_GPU_SDF, 0, sdf, 4 * n);
		free(sdf);
		free(bodies);
	}

	// Cells inside the bodies, for the push of the liquid around them
	uint32_t *cells = malloc(sizeof(uint32_t) * (n + 1));
	g->body_cells   = 0;
	for (int c = 0; c < n; c++) {
		g->base[c] = s->solid_base[c];
		for (int a = 0; a < 3; a++) {
			g->base_vel[c + a * n] = s->solid_base[c] ? s->solid_base_vel[c * 3 + a] : cvel[c * 3 + a];
		}
		if (s->solid_base[c]) {
			body[c] = (uint32_t)sim_gpu_body_index(g, s->solid_base_body[c]);
		}
		if (s->solid_base[c] && s->solid_base_body[c] != NULL) {
			vec4_t        q = sim_cell_center(s, c);
			physics_hit_t hit;
			if (body[c] > 0 && physics_collide_point(&q, 1e-4f, &hit) && hit.body == s->solid_base_body[c]) {
				cells[g->body_cells++] = (uint32_t)c;
			}
		}
	}
	sim_gpu_write(s, SIM_GPU_BODY, 0, body, n);
	sim_gpu_write(s, SIM_GPU_BCELL, 0, cells, g->body_cells);
	sim_gpu_fill(s, g->slots[SIM_GPU_BACC], 12 * SIM_GPU_MAX_BODIES, 0);
	sim_gpu_body_motion(s);
	free(cells);
	free(body);
	free(cvel);
}

// Solids of the colliders and the obstacle points of cloth and chunks
static void sim_gpu_solids(physics_sim_t *s, bool marked) {
	sim_gpu_t *g = s->gpu;
	int        n = s->nx * s->ny * s->nz;
	if (!marked && s->obstacle_count == 0 && g->obstacles == 0) {
		return;
	}
	memcpy(g->solid, g->base, sizeof(uint32_t) * n);
	memcpy(g->svel, g->base_vel, sizeof(float) * 3 * n);
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
		g->solid[id] = 2; // Not a cell of the body nearby
		for (int a = 0; a < 3; a++) {
			g->svel[id + a * n] = o[3 + a];
		}
	}
	g->obstacles = s->obstacle_count;
	sim_gpu_write(s, SIM_GPU_SOLID, 0, g->solid, n);
	sim_gpu_write(s, SIM_GPU_SVEL, 0, g->svel, 3 * n);
	sim_gpu_constants_t k = {0};
	sim_gpu_faces(s, &k, SIM_K_CLOSED);
}

// Attractors and wind at the faces
static void sim_gpu_fields_layers(sim_job_t *job, int begin, int end) {
	physics_sim_t *s = job->s;
	sim_gpu_t     *g = s->gpu;
	for (int t = begin; t < end; t++) {
		int         a = t / (s->nz + 1), k = t % (s->nz + 1);
		int         at = a == 0 ? 0 : a == 1 ? (int)g->faces[0] : (int)(g->faces[0] + g->faces[1]);
		sim_field_t f  = sim_face(s, a, NULL);
		if (k >= f.n[2]) {
			continue;
		}
		for (int j = 0; j < f.n[1]; j++) {
			for (int i = 0; i < f.n[0]; i++) {
				int    id = at + FIX(&f, i, j, k);
				vec4_t p  = {s->min[0] + (i + f.off[0]) * s->cell, s->min[1] + (j + f.off[1]) * s->cell, s->min[2] + (k + f.off[2]) * s->cell, 1.0f};
				vec4_t fo = physics_field_force(p), w = physics_field_wind(p);
				job->field[id] = a == 0 ? fo.x : a == 1 ? fo.y : fo.z;
				job->src[id]   = a == 0 ? w.x : a == 1 ? w.y : w.z;
			}
		}
	}
}

// Partial sums to the 64 the scalars kernel adds up. Skipped once solved, as all kernels of the iterations
static void sim_gpu_reduce(physics_sim_t *s) {
	sim_gpu_t          *g   = s->gpu;
	uint32_t            red = g->slots[SIM_GPU_RED];
	sim_gpu_constants_t k   = {.u0 = {red, red + SIM_GPU_REDUCE, SIM_GPU_REDUCE}, .u3 = {0, 0, 0, 1}};
	sim_gpu_run(s, &k, SIM_K_REDUCE, 1024, 0);
	k = (sim_gpu_constants_t){.u0 = {red + SIM_GPU_REDUCE, g->slots[SIM_GPU_SCAL] + 16, 1024}, .u3 = {0, 0, 0, 1}};
	sim_gpu_run(s, &k, SIM_K_REDUCE, 64, 0);
}

// Constants of a level kernel: dims, then coef, inverse diagonal, fixed, solution, right side, residual
static sim_gpu_constants_t sim_gpu_level(sim_gpu_t *g, int l) {
	sim_gpu_constants_t k = {.u3 = {0, 0, 0, 1}};
	for (int a = 0; a < 3; a++) {
		k.u0[a] = (uint32_t)g->level[l].n[a];
		k.u1[a] = g->level[l].coef[a];
	}
	k.u1[3] = g->level[l].idiag;
	k.u2[0] = g->level[l].fixed;
	k.u2[1] = g->level[l].x;
	k.u2[2] = g->level[l].b;
	k.u2[3] = g->level[l].res;
	return k;
}

static int sim_gpu_level_cells(sim_gpu_t *g, int l) {
	return g->level[l].n[0] * g->level[l].n[1] * g->level[l].n[2];
}

// Between a level and the next coarser one: dims of both
static sim_gpu_constants_t sim_gpu_levels(sim_gpu_t *g, int l, uint32_t from, uint32_t to, uint32_t extra) {
	sim_gpu_constants_t k = {.u2 = {from, to, extra}, .u3 = {0, 0, 0, 1}};
	for (int a = 0; a < 3; a++) {
		k.u0[a] = (uint32_t)g->level[l].n[a];
		k.u1[a] = (uint32_t)g->level[l + 1].n[a];
	}
	return k;
}

// Coarse operators from the fine one
static void sim_gpu_mg_setup(physics_sim_t *s) {
	sim_gpu_t *g   = s->gpu;
	float      eps = SIM_PRESSURE_EPS;
	for (int l = 0; l + 1 < g->levels; l++) {
		sim_gpu_constants_t k = sim_gpu_levels(g, l, g->level[l].fixed, g->level[l + 1].fixed, 0);
		sim_gpu_run(s, &k, SIM_K_MG_FIXED, sim_gpu_level_cells(g, l + 1), 0);
		for (int a = 0; a < 3; a++) {
			int *n     = g->level[l + 1].n;
			int  faces = n[0] * n[1] * n[2] / n[a] * (n[a] + 1);
			k          = sim_gpu_levels(g, l, g->level[l].coef[a], g->level[l + 1].coef[a], 0);
			sim_gpu_run(s, &k, SIM_K_MG_FACES, faces, a);
		}
		eps *= 8.0f;
		k       = sim_gpu_level(g, l + 1);
		k.f0[0] = eps;
		sim_gpu_run(s, &k, SIM_K_MG_DIAG, sim_gpu_level_cells(g, l + 1), 0);
	}
}

static void sim_gpu_smooth(physics_sim_t *s, int l, int first) {
	sim_gpu_t          *g = s->gpu;
	sim_gpu_constants_t k = sim_gpu_level(g, l);
	sim_gpu_run(s, &k, SIM_K_MG_SMOOTH, sim_gpu_level_cells(g, l), first);
	sim_gpu_run(s, &k, SIM_K_MG_SMOOTH, sim_gpu_level_cells(g, l), 1 - first);
}

// z = M^-1 r, red then black on the way down, black then red on the way up
static void sim_gpu_vcycle(physics_sim_t *s) {
	sim_gpu_t *g = s->gpu;
	int        c = g->levels - 1;
	for (int l = 0; l < c; l++) {
		sim_gpu_constants_t k = {.u0 = {g->level[l].x, 0}, .u3 = {0, 0, 0, 1}};
		sim_gpu_run(s, &k, SIM_K_CLEAR, sim_gpu_level_cells(g, l), 0);
		sim_gpu_smooth(s, l, 0);
		k = sim_gpu_level(g, l);
		sim_gpu_run(s, &k, SIM_K_MG_RESIDUAL, sim_gpu_level_cells(g, l), 0);
		k = sim_gpu_levels(g, l, g->level[l].res, g->level[l + 1].b, 0);
		sim_gpu_run(s, &k, SIM_K_MG_RESTRICT, sim_gpu_level_cells(g, l + 1), 0);
	}
	sim_gpu_constants_t k = {.u0 = {g->level[c].x, 0}, .u3 = {0, 0, 0, 1}};
	sim_gpu_run(s, &k, SIM_K_CLEAR, sim_gpu_level_cells(g, c), 0);
	for (int i = 0; i < SIM_GPU_COARSEST; i++) {
		sim_gpu_smooth(s, c, 0);
	}
	for (int i = 0; i < SIM_GPU_COARSEST; i++) {
		sim_gpu_smooth(s, c, 1);
	}
	for (int l = c - 1; l >= 0; l--) {
		k = sim_gpu_levels(g, l, g->level[l].x, g->level[l + 1].x, g->level[l].idiag);
		sim_gpu_run(s, &k, SIM_K_MG_PROLONG, sim_gpu_level_cells(g, l), 0);
		sim_gpu_smooth(s, l, 1);
	}
}

// Iterations in chunks, reading after each whether the solve is done. Or a budget of them queued ahead, the ones past done skip
static void sim_gpu_project(physics_sim_t *s, int gas, int budget) {
	sim_gpu_t          *g            = s->gpu;
	int                 n            = s->nx * s->ny * s->nz;
	uint32_t            free_surface = gas ? 0 : 1;
	sim_gpu_constants_t k            = {.u0 = {free_surface}};
	sim_gpu_run(s, &k, SIM_K_FIXED, n, 0);
	if (!gas) {
		sim_gpu_run(s, &k, SIM_K_FIXED_LIQ, g->capacity, 0);
	}
	sim_gpu_faces(s, &k, SIM_K_COEF);
	sim_gpu_run(s, &k, SIM_K_PCG_ZERO, n, 0);
	sim_gpu_fill(s, g->slots[SIM_GPU_SCAL], 16, 0);
	sim_gpu_run(s, &k, SIM_K_PCG_INIT, SIM_GPU_REDUCE, 0);
	sim_gpu_mg_setup(s);
	sim_gpu_vcycle(s);
	k = (sim_gpu_constants_t){.f0 = {SIM_PCG_TOLERANCE}};
	sim_gpu_run(s, &k, SIM_K_PCG_DOT, SIM_GPU_REDUCE, 0);
	sim_gpu_reduce(s);
	sim_gpu_run(s, &k, SIM_K_SCALARS, 1, 0);
	k.u3[3] = 1;
	sim_gpu_run(s, &k, SIM_K_PCG_DIR, n, 0);
	for (int it = 0; it < SIM_GPU_PCG_ITERS;) {
		int chunk = budget > 0 ? budget : it == 0 && g->iters > 1 ? g->iters : 1;
		it += chunk;
		for (int c = 0; c < chunk; c++) {
			sim_gpu_run(s, &k, SIM_K_PCG_AD, SIM_GPU_REDUCE, 0);
			sim_gpu_reduce(s);
			k.u0[0] = 1;
			sim_gpu_run(s, &k, SIM_K_SCALARS, 1, 0);
			sim_gpu_run(s, &k, SIM_K_PCG_UPDATE, SIM_GPU_REDUCE, 0);
			sim_gpu_vcycle(s);
			sim_gpu_run(s, &k, SIM_K_PCG_DOT, SIM_GPU_REDUCE, 0);
			sim_gpu_reduce(s);
			k.u0[0] = 2;
			sim_gpu_run(s, &k, SIM_K_SCALARS, 1, 0);
			sim_gpu_run(s, &k, SIM_K_PCG_DIR, n, 0);
		}
		if (budget > 0) {
			break;
		}
		uint32_t done[2]; // Done, iterations
		sim_gpu_read(s, SIM_GPU_SCAL, SIM_GPU_DONE, done, 2);
		if (done[0] != 0) {
			float iters;
			memcpy(&iters, &done[1], sizeof(iters));
			g->iters = (int)iters;
			break;
		}
	}
	k = (sim_gpu_constants_t){0};
	sim_gpu_run(s, &k, SIM_K_PCG_RESET, n, 0);
	sim_gpu_faces(s, &k, SIM_K_GRADIENT);
}

// Of size dt, or 0 when queued ahead: the size is on the gpu, the kernels read it there
static void sim_gpu_substep(physics_sim_t *s, float dt, unsigned emit, int gas, bool pull, bool wind, int budget) {
	sim_gpu_t          *g     = s->gpu;
	int                 n     = s->nx * s->ny * s->nz;
	int                 faces = (int)(g->faces[0] + g->faces[1] + g->faces[2]);
	vec4_t              grav  = physics_get_gravity();
	sim_gpu_constants_t k     = {.f1 = {grav.x, grav.y, grav.z, dt}};
	g->substeps++;

	if (gas) {
		sim_gpu_copy(s, g->slots[SIM_GPU_VEL], g->slots[SIM_GPU_TMP], faces);
		sim_gpu_faces(s, &k, SIM_K_ADVECT_AIR);
	}
	if (g->capacity > 0) {
		sim_gpu_fill(s, g->slots[SIM_GPU_PM], faces * 2, 0);
		sim_gpu_run(s, &k, SIM_K_P2G, g->capacity, 0);
		k.u0[0] = (uint32_t)gas;
		sim_gpu_faces(s, &k, SIM_K_BLEND);
	}
	if (pull || (gas && wind)) {
		k.u0[0] = (uint32_t)gas;
		k.u0[1] = pull;
		k.u0[2] = gas && wind;
		sim_gpu_faces(s, &k, SIM_K_FIELDS);
	}
	if (emit) {
		uint32_t fire = 0;
		for (int i = 0; i < s->gas_count && i < 32; i++) {
			fire |= s->flame[i] != NULL ? 1u << i : 0;
		}
		k.u0[0] = emit;
		k.u0[1] = fire;
		k.u0[3] = s->seed * 7919u + g->substeps;
		k.f0[0] = s->buoyancy * 4.0f;
		sim_gpu_run(s, &k, SIM_K_EMIT, n, 0);
	}
	if (s->gas_count > 0) {
		k.f0[0] = s->buoyancy;
		sim_gpu_run(s, &k, SIM_K_BUOYANCY, (int)g->faces[2], 2);
	}
	if (gas && s->vorticity > 0.0f) {
		k.f0[0] = s->vorticity * s->cell;
		sim_gpu_run(s, &k, SIM_K_CURL, n, 0);
		sim_gpu_run(s, &k, SIM_K_VORT_FORCE, n, 0);
		sim_gpu_faces(s, &k, SIM_K_VORT_APPLY);
	}
	if (s->viscosity > 0.0f && g->capacity > 0) {
		sim_gpu_copy(s, g->slots[SIM_GPU_VEL], g->slots[SIM_GPU_TMP], faces);
		k.f0[0] = s->viscosity;
		sim_gpu_faces(s, &k, SIM_K_VISCOSITY);
	}
	sim_gpu_faces(s, &k, SIM_K_CLOSE_FACES);
	sim_gpu_project(s, gas, budget);
	sim_gpu_run(s, &k, SIM_K_PUSH_BODIES, g->body_cells, 0);
	sim_gpu_faces(s, &k, SIM_K_CLOSE_FACES);
	if (g->capacity > 0) {
		sim_gpu_run(s, &k, SIM_K_G2P, g->capacity, 0);
		sim_gpu_run(s, &k, SIM_K_MOVE, g->capacity, 0);
	}

	if (gas) {
		// Temperature, then the density and flame of each gas, eight at a time through the scratch fields
		uint32_t list[1 + 64];
		int      count = 0;
		list[count++]  = g->slots[SIM_GPU_TEMP];
		for (int i = 0; i < s->gas_count && i < 32; i++) {
			list[count++] = g->slots[SIM_GPU_DENS] + (uint32_t)(i * n);
			if (s->flame[i] != NULL) {
				list[count++] = g->slots[SIM_GPU_FLAME] + (uint32_t)(i * n);
			}
		}
		for (int first = 0; first < count; first += 8) {
			int group = count - first < 8 ? count - first : 8;
			memset(k.u0, 0, sizeof(k.u0) * 2);
			for (int f = 0; f < group; f++) {
				sim_gpu_copy(s, list[first + f], g->slots[SIM_GPU_CT] + (uint32_t)(f * n), n);
				(f < 4 ? k.u0 : k.u1)[f % 4] = list[first + f];
			}
			sim_gpu_run(s, &k, SIM_K_ADVECT, n, group);
		}
		k.u0[0] = (uint32_t)s->gas_count;
		k.f0[0] = s->burn; // Rates, the kernel takes them over the substep
		k.f0[1] = s->soot;
		k.f0[2] = s->heat;
		k.f0[3] = s->dissipation;
		k.f2[0] = s->cooling;
		sim_gpu_run(s, &k, SIM_K_BURN, n, 0);
	}
}

// Angular impulse as two opposite impulses a unit apart
static void sim_gpu_push_body(void *body, vec4_t center, vec4_t impulse, vec4_t torque) {
	if (vec4_dot(impulse, impulse) > 0.0f) {
		physics_body_apply_impulse_at(body, impulse, center);
	}
	float l = vec4_len(torque);
	if (l < 1e-9f) {
		return;
	}
	vec4_t axis = vec4_mult(torque, 1.0f / l);
	vec4_t e    = vec4_norm(vec4_cross(axis, fabsf(axis.x) < 0.9f ? (vec4_t){1.0f, 0.0f, 0.0f, 0.0f} : (vec4_t){0.0f, 1.0f, 0.0f, 0.0f}));
	vec4_t f    = vec4_mult(vec4_cross(torque, e), 0.5f);
	physics_body_apply_impulse_at(body, f, vec4_add(center, e));
	physics_body_apply_impulse_at(body, vec4_mult(f, -1.0f), vec4_sub(center, e));
}

// The fields the cpu reads: velocity and liquid for sampling, gases and particles for the cache. Where reading back is slow in one range
// from the scalars, copied out after. Elsewhere straight into place, the extra copy costs more than the reads
static void sim_gpu_read_field(physics_sim_t *s, int slot, uint32_t at, void *data, uint32_t words) {
	sim_gpu_t *g = s->gpu;
	if (SIM_GPU_QUEUE_AHEAD) {
		memcpy(data, g->read + g->slots[slot] - g->slots[SIM_GPU_SCAL] + at, sizeof(float) * words);
	}
	else {
		sim_gpu_read(s, slot, at, data, words);
	}
}

static void sim_gpu_read_back(physics_sim_t *s) {
	sim_gpu_t *g = s->gpu;
	int        n = s->nx * s->ny * s->nz;
	if (SIM_GPU_QUEUE_AHEAD) {
		sim_gpu_read(s, SIM_GPU_SCAL, 0, g->read, g->read_words);
	}
	for (int a = 0; a < 3; a++) {
		sim_gpu_read_field(s, SIM_GPU_VEL + a, 0, s->vel[a], g->faces[a]);
		if (g->capacity > 0) {
			sim_gpu_read_field(s, SIM_GPU_MASS + a, 0, s->mass[a], g->faces[a]);
		}
	}
	for (int i = 0; i < s->gas_count; i++) {
		sim_gpu_read_field(s, SIM_GPU_DENS, i * n, s->density[i], n);
		if (s->flame[i] != NULL) {
			sim_gpu_read_field(s, SIM_GPU_FLAME, i * n, s->flame[i], n);
		}
	}
	if (g->capacity > 0) {
		float    *pos  = malloc(sizeof(float) * 3 * g->capacity);
		uint32_t *info = malloc(sizeof(uint32_t) * g->capacity);
		sim_gpu_read_field(s, SIM_GPU_PPOS, 0, pos, 3 * g->capacity);
		sim_gpu_read_field(s, SIM_GPU_PINFO, 0, info, g->capacity);
		s->count = 0;
		for (int i = 0; i < g->capacity; i++) {
			if (info[i] & 65536) {
				continue;
			}
			memcpy(&s->pos[s->count * 3], &pos[i * 3], sizeof(float) * 3);
			s->material[s->count] = (unsigned char)(info[i] & 255);
			s->owner[s->count]    = (unsigned char)(info[i] >> 8 & 255);
			s->count++;
		}
		free(pos);
		free(info);
	}
}

// After each substep: the next one sees the bodies slowed
static void sim_gpu_push_bodies(physics_sim_t *s) {
	sim_gpu_t *g = s->gpu;
	if (g->bodies == 0) {
		return;
	}
	int32_t acc[SIM_GPU_MAX_BODIES * 12];
	sim_gpu_read(s, SIM_GPU_BACC, 0, acc, 12 * g->bodies);
	sim_gpu_fill(s, g->slots[SIM_GPU_BACC], 12 * g->bodies, 0);
	float unit  = SIM_DENSITY * s->cell * s->cell * s->cell;
	float grain = SIM_PARTICLE_MASS * unit;
	for (int i = 0; i < g->bodies; i++) {
		vec4_t c;
		physics_body_get_pos(g->body[i], &c);
		for (int part = 0; part < 2; part++) {
			float    m  = (part == 0 ? unit : grain) / SIM_GPU_BODY_FIXED;
			int32_t *v  = &acc[i * 12 + part * 6];
			vec4_t   j  = {v[0] * m, v[1] * m, v[2] * m, 0.0f};
			vec4_t   tq = {v[3] * m, v[4] * m, v[5] * m, 0.0f};
			sim_gpu_push_body(g->body[i], c, j, tq);
		}
	}
	sim_gpu_body_motion(s);
}

// Fastest face or particle, and whether there is gas
static void sim_gpu_stats(physics_sim_t *s) {
	sim_gpu_t          *g     = s->gpu;
	int                 n     = s->nx * s->ny * s->nz;
	int                 faces = (int)(g->faces[0] + g->faces[1] + g->faces[2]);
	sim_gpu_constants_t k     = {.u0 = {(uint32_t)faces, (uint32_t)g->capacity, (uint32_t)(s->gas_count > 0 ? (1 + 2 * s->gas_count) * n : 0)}};
	sim_gpu_fill(s, g->slots[SIM_GPU_SCAL] + SIM_GPU_SPEED, 2, 0);
	sim_gpu_run(s, &k, SIM_K_STATS, SIM_GPU_REDUCE, 0);
}

void physics_sim_step(physics_sim_t *s, float dt, unsigned emit) {
	if (dt <= 0.0f || s->failed) {
		return;
	}
	if (s->gpu == NULL && !sim_gpu_create(s)) {
		s->failed = 1;
		iron_log("Physics sim: no gpu compute, or the domain is too large for the gpu");
		return;
	}
	sim_gpu_t *g      = s->gpu;
	bool       marked = s->collider_version != physics_collider_version();
	bool       pull = physics_field_any(0), wind = physics_field_any(1);
	if (marked || pull || wind) {
		sim_pool_start(); // Collider queries and fields on the cpu
	}
	if (marked) {
		s->collider_version = physics_collider_version();
		sim_gpu_mark(s);
	}
	sim_gpu_solids(s, marked);
	if (pull || wind) {
		int       faces = (int)(g->faces[0] + g->faces[1] + g->faces[2]);
		float    *force = malloc(sizeof(float) * faces * 2);
		sim_job_t job   = {.s = s, .field = force, .src = force + faces};
		sim_parallel(sim_gpu_fields_layers, &job, 3 * (s->nz + 1), 1);
		sim_gpu_write(s, SIM_GPU_FORCE, 0, force, faces);
		sim_gpu_write(s, SIM_GPU_WIND, 0, force + faces, faces);
		free(force);
	}
	sim_pool_stop();

	int max_steps = g->wave > 0.0f ? SIM_ELASTIC_SUBSTEPS : SIM_MAX_SUBSTEPS;
	int steps     = 0;
	if (!SIM_GPU_QUEUE_AHEAD) {
		float t = 0.0f;
		for (; t < dt - 1e-6f && steps < max_steps; steps++) {
			sim_gpu_stats(s);
			float stats[2];
			sim_gpu_read(s, SIM_GPU_SCAL, SIM_GPU_SPEED, stats, 2);
			float speed = stats[0] * sqrtf(3.0f) + sqrtf(s->cell * 9.81f);
			float sub   = fminf(dt - t, SIM_CFL * s->cell / speed);
			if (g->wave > 0.0f) {
				sub = fminf(sub, SIM_ELASTIC_CFL * s->cell / (g->wave + speed));
			}
			if (steps == max_steps - 1) {
				sub = dt - t;
			}
			sim_gpu_substep(s, sub, emit, emit != 0 || stats[1] > 0.0f, pull, wind, 0);
			sim_gpu_push_bodies(s);
			t += sub;
		}
		sim_gpu_read_back(s);
		return;
	}

	// Sized on the gpu. Without bodies, which take the pushes after each substep, as many substeps as the last frame are queued at once,
	// more if they fall short, with the gas of the last frame: it only fades within this one. Else one at a time, reading after each
	bool   ahead = g->bodies == 0 && g->last_steps > 0;
	int    gas   = emit != 0 || g->gas > 0.0f;
	int    queue = ahead ? g->last_steps : max_steps;
	float *read  = g->read;
	sim_gpu_fill(s, g->slots[SIM_GPU_SCAL] + SIM_GPU_T, SIM_GPU_FRAME, 0);
	g->guard = 1;
	for (bool done = false; !done; queue = 1) {
		for (int i = 0; i < queue && steps < max_steps && !done; i++, steps++) {
			sim_gpu_stats(s);
			sim_gpu_constants_t k = {.u0 = {(uint32_t)steps, (uint32_t)max_steps}, .f0 = {dt, SIM_CFL, SIM_ELASTIC_CFL, g->wave}};
			sim_gpu_run(s, &k, SIM_K_SUBSTEP, 1, 0);
			if (!ahead) {
				sim_gpu_read(s, SIM_GPU_SCAL, 0, read, SIM_GPU_T + SIM_GPU_FRAME);
				uint32_t idle;
				memcpy(&idle, &read[SIM_GPU_T + 2], sizeof(idle));
				if (idle != 0) {
					break;
				}
				gas  = emit != 0 || read[SIM_GPU_GAS] > 0.0f;
				done = read[SIM_GPU_T] >= dt - 1e-6f;
			}
			sim_gpu_substep(s, 0.0f, emit, gas, pull, wind, ahead ? g->budget : 0);
			sim_gpu_push_bodies(s);
		}
		g->guard = 0;
		sim_gpu_stats(s); // The gas the next frame starts with
		sim_gpu_read_back(s);
		g->guard = 1;
		done     = !ahead || read[SIM_GPU_T] >= dt - 1e-6f || steps >= max_steps;
	}
	g->guard = 0;

	// What to queue ahead next frame: substeps, gas, and iterations past the most a solve took
	int iters     = (int)read[SIM_GPU_T + 3];
	g->last_steps = (int)read[SIM_GPU_T + 4];
	g->gas        = read[SIM_GPU_GAS];
	g->budget     = ahead && iters >= g->budget ? g->budget * 2 : iters + 2; // Doubled when a solve may have run out of it
	g->budget     = g->budget > SIM_GPU_PCG_ITERS ? SIM_GPU_PCG_ITERS : g->budget;
}

#endif
