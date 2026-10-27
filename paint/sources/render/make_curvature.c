#include "../global.h"

// Curvature Texture bake on the cpu

typedef struct make_curvature_mesh {
	i32  count; // Triangles
	f32 *p;     // Per corner position
	f32 *n;     // Per corner normal
	f32 *uv;    // Per corner uv
	f32  diag;  // Bounding box diagonal
	u64  key;
} make_curvature_mesh_t;

typedef struct make_curvature_cell {
	u64 key;
	f32 bins[6][4]; // Weight and weighted normal sum per dominant axis direction
	f32 w;
	f32 p[3]; // Weighted position sum, tells convex from concave
} make_curvature_cell_t;

typedef struct make_curvature_grid {
	make_curvature_cell_t      *cells;
	u32                         capacity;
	u32                         count;
	f32                         size;
	struct make_curvature_grid *shards; // When set, cells live in these grids, picked by key
	i32                         shard_count;
} make_curvature_grid_t;

static make_curvature_mesh_t make_curvature_mesh  = {0};
static any_map_t            *make_curvature_baked = NULL; // Preview id -> settings the image was baked with

static void make_curvature_norm(f32 *v) {
	f32 l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
	if (l > 0.0) {
		v[0] /= l;
		v[1] /= l;
		v[2] /= l;
	}
}

static u64 make_curvature_objects_key() {
	u64                    key  = 1469598103934665603ull;
	mesh_object_t_array_t *objs = g_project->_->paint_objects;
	for (i32 i = 0; i < objs->length; ++i) {
		mesh_object_t *o = objs->buffer[i];
		u64            v = (u64)(uintptr_t)o->data ^ ((u64)(o->data != NULL && o->data->index_array != NULL ? o->data->index_array->length : 0) << 40);
		key              = (key ^ v) * 1099511628211ull;
	}
	return key;
}

static bool make_curvature_usable(mesh_object_t *o) {
	return o->data != NULL && o->data->index_array != NULL && mesh_data_get_vertex_array(o->data, "pos") != NULL &&
	       mesh_data_get_vertex_array(o->data, "tex") != NULL;
}

static void make_curvature_free_mesh() {
	free(make_curvature_mesh.p);
	free(make_curvature_mesh.n);
	free(make_curvature_mesh.uv);
	memset(&make_curvature_mesh, 0, sizeof(make_curvature_mesh_t));
}

static void make_curvature_read_mesh() {
	make_curvature_free_mesh();
	mesh_object_t_array_t *objs  = g_project->_->paint_objects;
	i32                    count = 0;
	for (i32 i = 0; i < objs->length; ++i) {
		if (make_curvature_usable(objs->buffer[i])) {
			count += objs->buffer[i]->data->index_array->length / 3;
		}
	}
	make_curvature_mesh_t *m = &make_curvature_mesh;
	m->count                 = count;
	m->p                     = malloc(sizeof(f32) * count * 9);
	m->n                     = malloc(sizeof(f32) * count * 9);
	m->uv                    = malloc(sizeof(f32) * count * 6);
	i32 c                    = 0;
	for (i32 i = 0; i < objs->length; ++i) {
		mesh_object_t *o = objs->buffer[i];
		if (!make_curvature_usable(o)) {
			continue;
		}
		mesh_data_t *d   = o->data;
		i16_array_t *pos = mesh_data_get_vertex_array(d, "pos")->values;
		i16_array_t *nor = mesh_data_get_vertex_array(d, "nor") != NULL ? mesh_data_get_vertex_array(d, "nor")->values : NULL;
		i16_array_t *tex = mesh_data_get_vertex_array(d, "tex")->values;
		f32          s   = d->scale_pos;
		for (i32 j = 0; j < d->index_array->length; ++j, ++c) {
			u32  vi = d->index_array->buffer[j];
			f32 *p  = m->p + c * 3;
			f32 *n  = m->n + c * 3;
			p[0]    = pos->buffer[vi * 4] / 32767.0 * s;
			p[1]    = pos->buffer[vi * 4 + 1] / 32767.0 * s;
			p[2]    = pos->buffer[vi * 4 + 2] / 32767.0 * s;
			n[0]    = nor != NULL ? nor->buffer[vi * 2] / 32767.0 : 0.0;
			n[1]    = nor != NULL ? nor->buffer[vi * 2 + 1] / 32767.0 : 0.0;
			n[2]    = pos->buffer[vi * 4 + 3] / 32767.0;
			make_curvature_norm(n);
			m->uv[c * 2]     = tex->buffer[vi * 2] / 32767.0;
			m->uv[c * 2 + 1] = tex->buffer[vi * 2 + 1] / 32767.0;
		}
	}
	f32 lo[3] = {1e30, 1e30, 1e30};
	f32 hi[3] = {-1e30, -1e30, -1e30};
	for (i32 i = 0; i < count * 3; ++i) {
		for (i32 k = 0; k < 3; ++k) {
			lo[k] = fminf(lo[k], m->p[i * 3 + k]);
			hi[k] = fmaxf(hi[k], m->p[i * 3 + k]);
		}
	}
	m->diag = count > 0 ? sqrtf((hi[0] - lo[0]) * (hi[0] - lo[0]) + (hi[1] - lo[1]) * (hi[1] - lo[1]) + (hi[2] - lo[2]) * (hi[2] - lo[2])) : 1.0;
}

static u64 make_curvature_cell_key(i32 x, i32 y, i32 z) {
	return ((u64)(x + (1 << 20)) << 42) | ((u64)(y + (1 << 20)) << 21) | (u64)(z + (1 << 20));
}

static i32 make_curvature_shard(u64 key, i32 count) {
	return (i32)((key * 0xff51afd7ed558ccdull) >> 40) % count; // Mixed apart from the slot hash
}

static make_curvature_cell_t *make_curvature_cell(make_curvature_grid_t *g, u64 key, bool insert) {
	if (g->shards != NULL) {
		g = &g->shards[make_curvature_shard(key, g->shard_count)];
	}
	if (insert && g->count * 4 >= g->capacity * 3) {
		make_curvature_grid_t bigger = {.cells = calloc(g->capacity * 2, sizeof(make_curvature_cell_t)), .capacity = g->capacity * 2, .size = g->size};
		for (u32 i = 0; i < g->capacity; ++i) {
			if (g->cells[i].key != 0) {
				*make_curvature_cell(&bigger, g->cells[i].key, true) = g->cells[i];
			}
		}
		free(g->cells);
		*g = bigger;
	}
	u32 i = (u32)((key * 0x9e3779b97f4a7c15ull) >> 32) & (g->capacity - 1);
	while (g->cells[i].key != key) {
		if (g->cells[i].key == 0) {
			if (!insert) {
				return NULL;
			}
			g->cells[i].key = key;
			g->count++;
			break;
		}
		i = (i + 1) & (g->capacity - 1);
	}
	return &g->cells[i];
}

// Cells are spread over shards by key, so threads can fill one shard each
static make_curvature_grid_t make_curvature_grid_sharded(f32 size, i32 count, u32 capacity) {
	make_curvature_grid_t g = {.size = size, .shards = calloc(count, sizeof(make_curvature_grid_t)), .shard_count = count};
	for (i32 i = 0; i < count; ++i) {
		g.shards[i] = (make_curvature_grid_t){.cells = calloc(capacity, sizeof(make_curvature_cell_t)), .capacity = capacity, .size = size};
	}
	return g;
}

static void make_curvature_grid_free(make_curvature_grid_t *g) {
	for (i32 i = 0; i < g->shard_count; ++i) {
		free(g->shards[i].cells);
	}
	free(g->shards);
}

static i32 make_curvature_bin(f32 *n) {
	f32 ax = fabsf(n[0]), ay = fabsf(n[1]), az = fabsf(n[2]);
	if (ax >= ay && ax >= az) {
		return n[0] >= 0.0 ? 0 : 1;
	}
	if (ay >= az) {
		return n[1] >= 0.0 ? 2 : 3;
	}
	return n[2] >= 0.0 ? 4 : 5;
}

static const f32 make_curvature_axes[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};

// Trilinear splat of a weighted normal at p, or gather of the normals around p facing the same side as n
// and of the surrounding positions
static void make_curvature_grid_visit(make_curvature_grid_t *g, f32 *p, f32 *n, f32 w, bool splat, f32 *sum) {
	f32 gx = p[0] / g->size - 0.5, gy = p[1] / g->size - 0.5, gz = p[2] / g->size - 0.5;
	i32 x0 = (i32)floorf(gx), y0 = (i32)floorf(gy), z0 = (i32)floorf(gz);
	f32 f[3] = {gx - x0, gy - y0, gz - z0};
	i32 bin  = make_curvature_bin(n);
	// Bins fade in smoothly with the direction, a hard cut would draw lines where the normal crosses it
	f32 use[6];
	for (i32 i = 0; i < 6; ++i) {
		const f32 *a = make_curvature_axes[i];
		use[i]       = fminf(fmaxf((n[0] * a[0] + n[1] * a[1] + n[2] * a[2] + 0.6) / 0.6, 0.0), 1.0);
	}
	for (i32 c = 0; c < 8; ++c) {
		i32                    dx = c & 1, dy = (c >> 1) & 1, dz = c >> 2;
		f32                    cw   = (dx ? f[0] : 1.0 - f[0]) * (dy ? f[1] : 1.0 - f[1]) * (dz ? f[2] : 1.0 - f[2]);
		make_curvature_cell_t *cell = make_curvature_cell(g, make_curvature_cell_key(x0 + dx, y0 + dy, z0 + dz), splat);
		if (cell == NULL) {
			continue;
		}
		if (splat) {
			f32 *b = cell->bins[bin];
			b[0] += w * cw;
			b[1] += n[0] * w * cw;
			b[2] += n[1] * w * cw;
			b[3] += n[2] * w * cw;
			cell->w += w * cw;
			cell->p[0] += p[0] * w * cw;
			cell->p[1] += p[1] * w * cw;
			cell->p[2] += p[2] * w * cw;
			continue;
		}
		sum[4] += cell->w * cw;
		sum[5] += cell->p[0] * cw;
		sum[6] += cell->p[1] * cw;
		sum[7] += cell->p[2] * cw;
		for (i32 i = 0; i < 6; ++i) {
			if (use[i] > 0.0) {
				for (i32 k = 0; k < 4; ++k) {
					sum[k] += cell->bins[i][k] * cw * use[i];
				}
			}
		}
	}
}

// Point on the smooth surface the vertex normals describe (phong tessellation), sampling the flat
// triangles would make every facet show up in the curvature
static void make_curvature_phong(f32 *P, f32 *N, f32 b0, f32 b1, f32 b2, f32 *out) {
	f32 b[3] = {b0, b1, b2};
	f32 p[3];
	for (i32 k = 0; k < 3; ++k) {
		p[k]   = P[k] * b0 + P[3 + k] * b1 + P[6 + k] * b2;
		out[k] = p[k] * 0.25;
	}
	for (i32 i = 0; i < 3; ++i) {
		f32 *pi = P + i * 3;
		f32 *ni = N + i * 3;
		f32  d  = (p[0] - pi[0]) * ni[0] + (p[1] - pi[1]) * ni[1] + (p[2] - pi[2]) * ni[2];
		for (i32 k = 0; k < 3; ++k) {
			out[k] += (p[k] - d * ni[k]) * b[i] * 0.75;
		}
	}
}

typedef struct make_curvature_eval {
	f32 *out;
	u8  *mask;
	f32  strength;
	f32  exponent;
	f32  offset;
	f32  axis[3];
	bool use_axis;
	bool convex;
} make_curvature_eval_t;

// Triangles too small to cover any texel centre are splatted once at their centroid
static void make_curvature_splat_centroid(make_curvature_grid_t *g, i32 t) {
	f32 *P     = make_curvature_mesh.p + t * 9;
	f32  e1[3] = {P[3] - P[0], P[4] - P[1], P[5] - P[2]};
	f32  e2[3] = {P[6] - P[0], P[7] - P[1], P[8] - P[2]};
	f32  n[3]  = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
	f32  world = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]) * 0.5;
	if (world > 0.0) {
		f32 c[3] = {(P[0] + P[3] + P[6]) / 3.0, (P[1] + P[4] + P[7]) / 3.0, (P[2] + P[5] + P[8]) / 3.0};
		make_curvature_norm(n);
		make_curvature_grid_visit(g, c, n, world, true, NULL);
	}
}

// Walks the texels of the triangles within rows [row0, row1). Splatting weighs each texel by the surface
// area it covers and marks the triangle covered. A triangle within these rows that covers no texel centre
// is only seen by this pass, so it is splatted at its centroid right away.
static void make_curvature_raster(make_curvature_grid_t *g, i32 w, i32 h, i32 row0, i32 row1, i32 *tris, i32 count, make_curvature_eval_t *e, u8 *covered) {
	make_curvature_mesh_t *m = &make_curvature_mesh;
	for (i32 i = 0; i < count; ++i) {
		i32  t  = tris[i];
		f32 *uv = m->uv + t * 6;
		f32  x0 = uv[0] * w, y0 = uv[1] * h;
		f32  x1 = uv[2] * w, y1 = uv[3] * h;
		f32  x2 = uv[4] * w, y2 = uv[5] * h;
		f32  top    = floorf(fminf(y0, fminf(y1, y2)));
		f32  bottom = ceilf(fmaxf(y0, fmaxf(y1, y2)));
		f32 *P      = m->p + t * 9;
		f32 *N      = m->n + t * 9;
		f32  e1[3]  = {P[3] - P[0], P[4] - P[1], P[5] - P[2]};
		f32  e2[3]  = {P[6] - P[0], P[7] - P[1], P[8] - P[2]};
		f32  cr[3]  = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
		f32  world  = sqrtf(cr[0] * cr[0] + cr[1] * cr[1] + cr[2] * cr[2]) * 0.5;
		f32  area   = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
		if (fabsf(area) >= 1e-8) {
			f32  per_texel = world / (fabsf(area) * 0.5);
			i32  minx      = (i32)fmaxf(0.0, floorf(fminf(x0, fminf(x1, x2))));
			i32  maxx      = (i32)fminf(w - 1, ceilf(fmaxf(x0, fmaxf(x1, x2))));
			i32  miny      = (i32)fmaxf(row0, top);
			i32  maxy      = (i32)fminf(row1 - 1, bottom);
			bool hit       = false;
			for (i32 y = miny; y <= maxy; ++y) {
				for (i32 x = minx; x <= maxx; ++x) {
					f32 px = x + 0.5;
					f32 py = y + 0.5;
					f32 b0 = ((x1 - px) * (y2 - py) - (x2 - px) * (y1 - py)) / area;
					f32 b1 = ((x2 - px) * (y0 - py) - (x0 - px) * (y2 - py)) / area;
					f32 b2 = 1.0 - b0 - b1;
					if (b0 < -0.001 || b1 < -0.001 || b2 < -0.001) {
						continue;
					}
					f32 p[3];
					make_curvature_phong(P, N, b0, b1, b2, p);
					f32 n[3];
					for (i32 k = 0; k < 3; ++k) {
						n[k] = N[k] * b0 + N[3 + k] * b1 + N[6 + k] * b2;
					}
					make_curvature_norm(n);
					if (e == NULL) {
						make_curvature_grid_visit(g, p, n, per_texel, true, NULL);
						hit = true;
						continue;
					}
					f32 sum[8] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
					make_curvature_grid_visit(g, p, n, 0.0, false, sum);
					// Spread of the normals: 1 - |mean normal|, its root grows linearly with the curvature
					f32 c = 0.0;
					if (sum[0] > 0.0) {
						c = sqrtf(fmaxf(1.0 - sqrtf(sum[1] * sum[1] + sum[2] * sum[2] + sum[3] * sum[3]) / sum[0], 0.0));
					}
					// Surface around a ridge lies behind it, around a groove in front of it
					if (e->convex && sum[4] > 0.0) {
						f32 d = ((p[0] - sum[5] / sum[4]) * n[0] + (p[1] - sum[6] / sum[4]) * n[1] + (p[2] - sum[7] / sum[4]) * n[2]) / g->size;
						c *= fminf(fmaxf(0.5 + d * 25.0, 0.0), 1.0);
					}
					c = powf(c, e->exponent) * e->strength * 2.0 + e->offset / 10.0;
					c = fminf(fmaxf(c, 0.0), 1.0);
					if (e->use_axis) {
						c *= n[0] * e->axis[0] + n[1] * e->axis[1] + n[2] * e->axis[2];
					}
					e->out[y * w + x]  = c;
					e->mask[y * w + x] = 1;
				}
			}
			if (e == NULL && !hit && top >= row0 && bottom < row1) {
				make_curvature_splat_centroid(g, t);
				hit = true;
			}
			if (hit) {
				covered[t] = 1;
			}
		}
	}
}

// Triangles that are not found covered by the raster pass
static void make_curvature_splat_uncovered(make_curvature_grid_t *g, u8 *covered) {
	for (i32 t = 0; t < make_curvature_mesh.count; ++t) {
		if (!covered[t]) {
			make_curvature_splat_centroid(g, t);
		}
	}
}

// Adds shard index of src to the same shard of dst
static void make_curvature_grid_merge(make_curvature_grid_t *dst, make_curvature_grid_t *src, i32 index) {
	make_curvature_grid_t *from = &src->shards[index];
	for (u32 i = 0; i < from->capacity; ++i) {
		make_curvature_cell_t *c = &from->cells[i];
		if (c->key == 0) {
			continue;
		}
		make_curvature_cell_t *d = make_curvature_cell(&dst->shards[index], c->key, true);
		f32                   *a = &d->bins[0][0];
		f32                   *b = &c->bins[0][0];
		for (i32 k = 0; k < 24; ++k) {
			a[k] += b[k];
		}
		d->w += c->w;
		d->p[0] += c->p[0];
		d->p[1] += c->p[1];
		d->p[2] += c->p[2];
	}
}

// Rows are dealt out to the threads in interleaved chunks, so every texel is always written by the
// same triangles in the same order and the result does not depend on the thread count
#define MAKE_CURVATURE_CHUNK       32
#define MAKE_CURVATURE_MAX_THREADS 64

// Triangles overlapping each chunk of rows, in triangle order: chunk c lists tris[start[c]] to tris[start[c + 1] - 1]
static i32 *make_curvature_buckets(i32 h, i32 **start_out) {
	make_curvature_mesh_t *m      = &make_curvature_mesh;
	i32                    chunks = (h + MAKE_CURVATURE_CHUNK - 1) / MAKE_CURVATURE_CHUNK;
	i32                   *range  = malloc(sizeof(i32) * math_max(m->count, 1) * 2);
	i32                   *start  = calloc(chunks + 2, sizeof(i32));
	for (i32 t = 0; t < m->count; ++t) {
		f32 *uv          = m->uv + t * 6;
		f32  top         = floorf(fminf(uv[1], fminf(uv[3], uv[5])) * h);
		f32  bottom      = ceilf(fmaxf(uv[1], fmaxf(uv[3], uv[5])) * h);
		range[t * 2]     = (i32)fmaxf(top, 0.0) / MAKE_CURVATURE_CHUNK;
		range[t * 2 + 1] = bottom < 0.0 || top >= h ? -1 : (i32)fminf(bottom, h - 1) / MAKE_CURVATURE_CHUNK;
		for (i32 c = range[t * 2]; c <= range[t * 2 + 1]; ++c) {
			start[c + 2]++;
		}
	}
	for (i32 c = 2; c <= chunks + 1; ++c) {
		start[c] += start[c - 1];
	}
	i32 *tris = malloc(sizeof(i32) * math_max(start[chunks + 1], 1));
	for (i32 t = 0; t < m->count; ++t) {
		for (i32 c = range[t * 2]; c <= range[t * 2 + 1]; ++c) {
			tris[start[c + 1]++] = t;
		}
	}
	free(range);
	*start_out = start;
	return tris;
}

typedef struct make_curvature_job {
	make_curvature_grid_t     *grid;
	make_curvature_grid_t     *merged;
	struct make_curvature_job *all;
	i32                       *tris;
	i32                       *start;
	make_curvature_eval_t     *eval;
	u8                        *covered;
	f32                       *out;
	u8                        *mask;
	u8                        *next;
	i32                        w;
	i32                        h;
	i32                        index;
	i32                        count;
} make_curvature_job_t;

// Every thread owns one shard of the merged grid and takes its cells from all the per thread grids,
// always in thread order
static void make_curvature_merge_thread(void *param) {
	make_curvature_job_t *j = param;
	for (i32 i = 0; i < j->count; ++i) {
		make_curvature_grid_merge(j->merged, j->all[i].grid, j->index);
	}
}

static void make_curvature_raster_thread(void *param) {
	make_curvature_job_t *j = param;
	for (i32 y = j->index * MAKE_CURVATURE_CHUNK; y < j->h; y += j->count * MAKE_CURVATURE_CHUNK) {
		i32 c = y / MAKE_CURVATURE_CHUNK;
		make_curvature_raster(j->grid, j->w, j->h, y, math_min(y + MAKE_CURVATURE_CHUNK, j->h), j->tris + j->start[c], j->start[c + 1] - j->start[c], j->eval,
		                      j->covered);
	}
}

// One dilation pass over rows [row0, row1): empty texels next to filled ones take their average
static void make_curvature_dilate_rows(f32 *out, u8 *mask, u8 *next, i32 w, i32 h, i32 row0, i32 row1) {
	for (i32 y = row0; y < row1; ++y) {
		for (i32 x = 0; x < w; ++x) {
			next[y * w + x] = mask[y * w + x];
			if (mask[y * w + x]) {
				continue;
			}
			f32 s = 0.0;
			i32 c = 0;
			for (i32 dy = -1; dy <= 1; ++dy) {
				for (i32 dx = -1; dx <= 1; ++dx) {
					i32 xx = x + dx, yy = y + dy;
					if (xx < 0 || yy < 0 || xx >= w || yy >= h || mask[yy * w + xx] != 1) {
						continue;
					}
					s += out[yy * w + xx];
					c++;
				}
			}
			if (c > 0) {
				out[y * w + x]  = s / c;
				next[y * w + x] = 1;
			}
		}
	}
}

static void make_curvature_dilate_thread(void *param) {
	make_curvature_job_t *j = param;
	for (i32 y = j->index * MAKE_CURVATURE_CHUNK; y < j->h; y += j->count * MAKE_CURVATURE_CHUNK) {
		make_curvature_dilate_rows(j->out, j->mask, j->next, j->w, j->h, y, math_min(y + MAKE_CURVATURE_CHUNK, j->h));
	}
}

static void make_curvature_run(make_curvature_job_t *jobs, i32 count, void (*func)(void *)) {
	iron_thread_t threads[MAKE_CURVATURE_MAX_THREADS];
	for (i32 i = 1; i < count; ++i) {
		iron_thread_init(&threads[i], func, &jobs[i]);
	}
	func(&jobs[0]);
	for (i32 i = 1; i < count; ++i) {
		iron_thread_wait_and_destroy(&threads[i]);
	}
}

// Grow the islands outwards so linear filtering and mip levels do not pull in the empty background.
// Filled texels are only read and empty ones only written, so rows can run in parallel within a pass.
static void make_curvature_dilate(f32 *out, u8 *mask, i32 w, i32 h, i32 passes, make_curvature_job_t *jobs, i32 count) {
	u8 *next = malloc(w * h);
	for (i32 i = 0; i < count; ++i) {
		jobs[i].out  = out;
		jobs[i].next = next;
	}
	for (i32 pass = 0; pass < passes; ++pass) {
		for (i32 i = 0; i < count; ++i) {
			jobs[i].mask = mask;
		}
		make_curvature_run(jobs, count, make_curvature_dilate_thread);
		u8 *t = mask;
		mask  = next;
		next  = t;
		for (i32 i = 0; i < count; ++i) {
			jobs[i].next = next;
		}
	}
	if (passes % 2 == 1) {
		memcpy(next, mask, w * h); // Leave the result in the caller's mask
	}
	free(passes % 2 == 1 ? mask : next);
}

// Input value, followed through group inputs to the group node sockets and taken from a linked Value node
static f32 make_curvature_input(ui_node_t *node, i32 socket, ui_node_canvas_t *canvas, ui_node_t_array_t *parents, i32 depth) {
	ui_node_link_t *l    = nodes_mesh_link(canvas, node, socket);
	ui_node_t      *from = l != NULL ? ui_get_node(canvas->nodes, l->from_id) : NULL;
	if (from != NULL && string_equals(from->type, "GROUP_INPUT") && depth > 0) {
		ui_node_t    *parent = parents->buffer[depth - 1];
		node_group_t *outer  = depth > 1 ? project_get_material_group_by_name(parents->buffer[depth - 2]->name) : NULL;
		if (l->from_socket < parent->inputs->length && (depth == 1 || outer != NULL)) {
			return make_curvature_input(parent, l->from_socket, outer != NULL ? outer->canvas : g_context->material->canvas, parents, depth - 1);
		}
	}
	else if (from != NULL && string_equals(from->type, "VALUE")) {
		return from->outputs->buffer[0]->default_value->buffer[0];
	}
	return node->inputs->buffer[socket]->default_value->buffer[0];
}

void make_curvature_bake(ui_node_t *node, ui_node_canvas_t *group, ui_node_t_array_t *parents, char *id) {
	ui_node_canvas_t *canvas   = group != NULL ? group : g_context->material->canvas;
	i32               res_x    = math_floor(config_get_texture_res_x());
	i32               res_y    = math_floor(config_get_texture_res_y());
	f32               strength = make_curvature_input(node, 0, canvas, parents, parents->length);
	f32               radius   = make_curvature_input(node, 1, canvas, parents, parents->length);
	f32               offset   = make_curvature_input(node, 2, canvas, parents, parents->length);
	bool              convex   = node->buttons->length > 0 && node->buttons->buffer[0]->default_value->buffer[0] > 0; // Missing in older projects
	u64               key      = make_curvature_objects_key();

	if (make_curvature_mesh.key != key || make_curvature_mesh.p == NULL) {
		make_curvature_read_mesh();
		make_curvature_mesh.key = key;
	}

	// Settings the existing image was baked with
	if (make_curvature_baked == NULL) {
		make_curvature_baked = any_map_create();
	}
	f32_array_t   *baked = any_map_get(make_curvature_baked, id);
	gpu_texture_t *image = any_map_get(g_context->node_previews, id);
	any_array_push(g_context->node_previews_used, id);
	f32 settings[7] = {strength, radius, offset, (f32)g_context->bake_axis, (f32)(key & 0xffffff), (f32)(key >> 40), (f32)convex};
	if (image != NULL && image->width == res_x && image->height == res_y && baked != NULL && memcmp(baked->buffer, settings, sizeof(settings)) == 0) {
		return;
	}
	if (baked == NULL) {
		baked = f32_array_create(7);
		any_map_set(make_curvature_baked, string_copy(id), baked);
	}
	memcpy(baked->buffer, settings, sizeof(settings));

	f32                  *out  = calloc(res_x * res_y, sizeof(f32));
	u8                   *mask = calloc(res_x * res_y, 1);
	make_curvature_eval_t e    = {.out = out, .mask = mask, .strength = strength, .exponent = 0.5 / fmaxf(radius, 0.001), .offset = offset, .convex = convex};
	if (g_context->bake_axis != BAKE_AXIS_XYZ) {
		i32 a      = g_context->bake_axis;
		i32 i      = (a == BAKE_AXIS_X || a == BAKE_AXIS_MX) ? 0 : (a == BAKE_AXIS_Y || a == BAKE_AXIS_MY) ? 1 : 2;
		e.axis[i]  = (a == BAKE_AXIS_MX || a == BAKE_AXIS_MY || a == BAKE_AXIS_MZ) ? -1.0 : 1.0;
		e.use_axis = true;
	}
	// Radius sets the neighbourhood size relative to the model, and how strongly weak curvature is boosted
	f32                  size    = make_curvature_mesh.diag * 0.0025 * fmaxf(radius, 0.05);
	i32                  threads = math_max(1, math_min(iron_hardware_threads(), MAKE_CURVATURE_MAX_THREADS));
	make_curvature_job_t jobs[MAKE_CURVATURE_MAX_THREADS];
	u8                  *covered = calloc(math_max(make_curvature_mesh.count, 1), 1);
	i32                 *start;
	i32                 *tris = make_curvature_buckets(res_y, &start);
	// Each thread splats into its own grid, merged afterwards in a fixed order
	make_curvature_grid_t grids[MAKE_CURVATURE_MAX_THREADS];
	for (i32 i = 0; i < threads; ++i) {
		grids[i] = make_curvature_grid_sharded(size, threads, 1 << 12);
		jobs[i] =
		    (make_curvature_job_t){.grid = &grids[i], .tris = tris, .start = start, .covered = covered, .w = res_x, .h = res_y, .index = i, .count = threads};
	}
	make_curvature_run(jobs, threads, make_curvature_raster_thread);
	u32 total = 0;
	for (i32 i = 0; i < threads; ++i) {
		for (i32 k = 0; k < threads; ++k) {
			total += grids[i].shards[k].count;
		}
	}
	// Cells shared by thread grids are counted more than once, a shard still grows if it fills up
	u32 capacity = 1 << 12;
	while (capacity * 3 < total / threads * 4 + 1) {
		capacity <<= 1;
	}
	make_curvature_grid_t g = make_curvature_grid_sharded(size, threads, capacity);
	for (i32 i = 0; i < threads; ++i) {
		jobs[i].merged = &g;
		jobs[i].all    = jobs;
	}
	make_curvature_run(jobs, threads, make_curvature_merge_thread);
	for (i32 i = 0; i < threads; ++i) {
		make_curvature_grid_free(&grids[i]);
	}
	make_curvature_splat_uncovered(&g, covered);
	free(covered);
	// Gather only reads the grid, every thread writes its own rows
	for (i32 i = 0; i < threads; ++i) {
		jobs[i].grid = &g;
		jobs[i].eval = &e;
	}
	make_curvature_run(jobs, threads, make_curvature_raster_thread);
	make_curvature_grid_free(&g);
	free(tris);
	free(start);
	make_curvature_dilate(out, mask, res_x, res_y, math_max(g_config->dilate_radius, 4), jobs, threads);
	u16     *hb = malloc(res_x * res_y * 2);
	buffer_t b  = {.buffer = (u8 *)hb, .length = res_x * res_y * 2};
	for (i32 i = 0; i < res_x * res_y; ++i) {
		hb[i] = float_to_half_fast(out[i]);
	}
	free(out);
	free(mask);

	if (image != NULL) {
		gpu_delete_texture(image);
	}
	image = gpu_create_texture_from_bytes_raw(&b, res_x, res_y, GPU_TEXTURE_FORMAT_R16);
	any_map_set(g_context->node_previews, string_copy(id), image);
	free(hb);
}
