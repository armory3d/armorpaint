
#ifdef WITH_PLUGINS

// Bake Mesh Maps plugin based on the work of Falkor.
// Cpu baking onto the low poly uv layout, used by the Bake Maps plugin (assets/plugins/bake_maps.c).
// For each low poly texel the surface point is either the low poly itself, or the closest
// high poly hit of a ray cast along the interpolated normal. Texels are split by rows
// between worker threads so the interface keeps running while baking.
// The functions are registered for scripts in bake_maps_init().

#include "engine.h"
#include "iron_array.h"
#include "iron_image.h"
#include "iron_input.h"
#include "iron_map.h"
#include "iron_obj.h"
#include "iron_path.h"
#include "iron_raycast.h"
#include "iron_string.h"
#include "iron_system.h"
#include "iron_thread.h"
#include "iron_ui.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

// Paint types, after the engine headers they build on
#include "../../sources/types.h"

extern context_t  *g_context;
extern project_t  *g_project;
extern ui_t       *g_ui;
extern ui_theme_t *g_theme;
extern any_map_t  *import_mesh_importers;
gpu_texture_t     *gpu_create_texture_from_bytes(buffer_t *data, i32 width, i32 height, i32 format);
void               gpu_delete_texture(gpu_texture_t *texture);
buffer_t          *gpu_get_texture_pixels(gpu_texture_t *image);
void               import_texture_run(char *path, bool hdr_as_envmap);
void               project_reimport_texture(asset_t *asset);
void               console_error(char *s);
buffer_t          *iron_load_blob(char *file);
bool               bake_to_target(render_target_t *rt, bake_type_t bake_type, void (*done)(void *), void *data);
gpu_texture_t     *gpu_create_render_target(i32 width, i32 height, i32 format);
i32                config_get_texture_res_x();
i32                config_get_texture_res_y();
extern bool        bake_texture_node_baking;
extern i32         render_path_raytrace_bake_current_sample;
unsigned char     *stbi_load_from_memory(unsigned char const *buffer, int len, int *x, int *y, int *comp, int req_comp);
void               stbi_image_free(void *p);

#define BAKE_TYPE_CAVITY 14 // Cpu only, signed curvature of the sampled surface

typedef struct bake_rays_node {
	f32 min[3];
	f32 max[3];
	i32 first; // Child index for inner nodes, first triangle for leaves
	i32 count; // 0 for inner nodes
} bake_rays_node_t;

typedef struct bake_rays_bvh {
	f32              *pos; // 9 floats per triangle
	f32              *nor;
	i32               count;
	i32              *index;
	bake_rays_node_t *nodes;
	i32               node_count;
} bake_rays_bvh_t;

typedef struct bake_rays_tri {
	f32 p[9]; // Low poly positions
	f32 n[9]; // Low poly normals
	f32 uv[6];
	f32 a[9];   // Ray directions, normals averaged across hard edges when enabled
	f32 col[9]; // Vertex colors
	i32 object; // Index in the low poly objects
	f32 scale;  // Mesh scale, positions are baked unscaled like on the gpu
} bake_rays_tri_t;

static bake_rays_bvh_t bake_rays_hp  = {0}; // High poly
static bake_rays_bvh_t bake_rays_lpb = {0}; // Low poly, for occlusion when baking without high poly

static f32 bake_rays_hp_min[3];
static f32 bake_rays_hp_max[3];

static bake_rays_tri_t *bake_rays_lp         = NULL;
static f32             *bake_rays_lp_miss    = NULL; // Fraction of rays missing the high poly per triangle
static bool             bake_rays_miss_dirty = true;
static u32              bake_rays_version    = 0; // Changes with the geometry, cage or high poly
static i32              bake_rays_lp_count   = 0;
static f32              bake_rays_lp_min[3];
static f32              bake_rays_lp_max[3];
static bool             bake_rays_average = true;

static i32  bake_rays_w;
static i32  bake_rays_h;
static f32  bake_rays_dist   = 0.0;
static i32 *bake_rays_texmap = NULL; // Low poly triangle per texel, -1 when empty
static u8  *bake_rays_out    = NULL; // RGBA8 result of the current map
static u8  *bake_rays_mask   = NULL;
static u8  *bake_rays_matid  = NULL; // Material id per texel, read from the layer
static f32 *bake_rays_cav    = NULL; // Raw cavity per texel

// Current map and its settings, fixed while the workers run
typedef struct bake_rays_job {
	i32  type;
	bool use_high;
	i32  rays;
	f32  ao_strength;
	f32  cavity_contrast;
	f32  cavity_texels;
	f32  ray_min;
	f32  ray_max;
	f32  curv_strength;
	f32  curv_radius;
	f32  curv_offset;
	i32  curv_smooth;
	i32  axis;
	bool up_y;
} bake_rays_job_t;

#define BAKE_RAYS_MAX_THREADS 32
static bake_rays_job_t bake_rays_job;
static bake_rays_job_t bake_rays_settings = {
    .ao_strength     = 1.0,
    .ray_min         = 0.01,
    .ray_max         = 10.0,
    .curv_strength   = 1.0,
    .curv_radius     = 1.0,
    .cavity_texels   = 2.0,
    .cavity_contrast = 1.0,
};
static iron_thread_t bake_rays_threads[BAKE_RAYS_MAX_THREADS];
static i32           bake_rays_thread_count = 0;
static iron_mutex_t  bake_rays_mutex;
static bool          bake_rays_mutex_ready = false;
static i32           bake_rays_next_row    = 0;
static i32           bake_rays_rows_done   = 0;
static volatile bool bake_rays_cancelled   = false;
static bool          bake_rays_working     = false;

static f32 bake_rays_fmin(f32 a, f32 b) {
	return a < b ? a : b;
}

static f32 bake_rays_fmax(f32 a, f32 b) {
	return a > b ? a : b;
}

static i32 bake_rays_imin(i32 a, i32 b) {
	return a < b ? a : b;
}

static i32 bake_rays_imax(i32 a, i32 b) {
	return a > b ? a : b;
}

static void bake_rays_v3_sub(f32 *r, f32 *a, f32 *b) {
	r[0] = a[0] - b[0];
	r[1] = a[1] - b[1];
	r[2] = a[2] - b[2];
}

static f32 bake_rays_v3_dot(f32 *a, f32 *b) {
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void bake_rays_v3_cross(f32 *r, f32 *a, f32 *b) {
	f32 x = a[1] * b[2] - a[2] * b[1];
	f32 y = a[2] * b[0] - a[0] * b[2];
	f32 z = a[0] * b[1] - a[1] * b[0];
	r[0]  = x;
	r[1]  = y;
	r[2]  = z;
}

static void bake_rays_v3_norm(f32 *a) {
	f32 l = sqrtf(bake_rays_v3_dot(a, a));
	if (l > 0.0) {
		a[0] /= l;
		a[1] /= l;
		a[2] /= l;
	}
}

// Read triangles of a mesh, positions and normals in mesh units
static i32 bake_rays_count_tris(mesh_object_t *o) {
	mesh_data_t *d = o->data;
	if (d == NULL || d->index_array == NULL || mesh_data_get_vertex_array(d, "pos") == NULL) {
		return 0;
	}
	return d->index_array->length / 3;
}

static void bake_rays_read_vertex(mesh_data_t *d, u32 i, f32 *p, f32 *n, f32 *uv) {
	i16_array_t *pos    = mesh_data_get_vertex_array(d, "pos")->values;
	f32          s      = d->scale_pos;
	p[0]                = pos->buffer[i * 4] / 32767.0 * s;
	p[1]                = pos->buffer[i * 4 + 1] / 32767.0 * s;
	p[2]                = pos->buffer[i * 4 + 2] / 32767.0 * s;
	vertex_array_t *nor = mesh_data_get_vertex_array(d, "nor");
	if (nor != NULL) {
		n[0] = nor->values->buffer[i * 2] / 32767.0;
		n[1] = nor->values->buffer[i * 2 + 1] / 32767.0;
		n[2] = pos->buffer[i * 4 + 3] / 32767.0;
		bake_rays_v3_norm(n);
	}
	else {
		n[0] = n[1] = n[2] = 0.0;
	}
	vertex_array_t *tex = mesh_data_get_vertex_array(d, "tex");
	uv[0]               = tex != NULL ? tex->values->buffer[i * 2] / 32767.0 : 0.0;
	uv[1]               = tex != NULL ? tex->values->buffer[i * 2 + 1] / 32767.0 : 0.0;
}

// Bvh

static void bake_rays_node_bounds(bake_rays_bvh_t *bvh, bake_rays_node_t *node) {
	for (i32 k = 0; k < 3; ++k) {
		node->min[k] = 1e30;
		node->max[k] = -1e30;
	}
	for (i32 i = node->first; i < node->first + node->count; ++i) {
		f32 *p = bvh->pos + bvh->index[i] * 9;
		for (i32 v = 0; v < 3; ++v) {
			for (i32 k = 0; k < 3; ++k) {
				node->min[k] = bake_rays_fmin(node->min[k], p[v * 3 + k]);
				node->max[k] = bake_rays_fmax(node->max[k], p[v * 3 + k]);
			}
		}
	}
}

static f32 bake_rays_centroid(bake_rays_bvh_t *bvh, i32 tri, i32 axis) {
	f32 *p = bvh->pos + tri * 9;
	return (p[axis] + p[3 + axis] + p[6 + axis]) / 3.0;
}

// Midpoint split bvh, iterative to avoid deep recursion
static void bake_rays_build_bvh(bake_rays_bvh_t *bvh) {
	i32 max_nodes   = bvh->count * 2 + 1;
	bvh->nodes      = malloc(sizeof(bake_rays_node_t) * max_nodes);
	bvh->node_count = 1;
	bvh->nodes[0]   = (bake_rays_node_t){.first = 0, .count = bvh->count};
	bake_rays_node_bounds(bvh, &bvh->nodes[0]);

	i32 stack[128];
	i32 sp      = 0;
	stack[sp++] = 0;
	while (sp > 0) {
		i32               ni   = stack[--sp];
		bake_rays_node_t *node = &bvh->nodes[ni];
		if (node->count <= 4 || sp >= 126) {
			continue;
		}

		// Split along the longest axis of the centroid bounds
		f32 cmin[3] = {1e30, 1e30, 1e30};
		f32 cmax[3] = {-1e30, -1e30, -1e30};
		for (i32 i = node->first; i < node->first + node->count; ++i) {
			for (i32 k = 0; k < 3; ++k) {
				f32 c   = bake_rays_centroid(bvh, bvh->index[i], k);
				cmin[k] = bake_rays_fmin(cmin[k], c);
				cmax[k] = bake_rays_fmax(cmax[k], c);
			}
		}
		i32 axis = 0;
		if (cmax[1] - cmin[1] > cmax[axis] - cmin[axis]) {
			axis = 1;
		}
		if (cmax[2] - cmin[2] > cmax[axis] - cmin[axis]) {
			axis = 2;
		}
		f32 mid = (cmin[axis] + cmax[axis]) * 0.5;

		i32 i = node->first;
		i32 j = node->first + node->count - 1;
		while (i <= j) {
			if (bake_rays_centroid(bvh, bvh->index[i], axis) < mid) {
				i++;
			}
			else {
				i32 t         = bvh->index[i];
				bvh->index[i] = bvh->index[j];
				bvh->index[j] = t;
				j--;
			}
		}
		i32 left_count = i - node->first;
		if (left_count == 0 || left_count == node->count) {
			left_count = node->count / 2; // Degenerate split, all centroids equal
		}

		i32 l         = bvh->node_count++;
		i32 r         = bvh->node_count++;
		bvh->nodes[l] = (bake_rays_node_t){.first = node->first, .count = left_count};
		bvh->nodes[r] = (bake_rays_node_t){.first = node->first + left_count, .count = node->count - left_count};
		bake_rays_node_bounds(bvh, &bvh->nodes[l]);
		bake_rays_node_bounds(bvh, &bvh->nodes[r]);
		node        = &bvh->nodes[ni];
		node->first = l;
		node->count = 0;
		stack[sp++] = l;
		stack[sp++] = r;
	}
}

static void bake_rays_free_bvh(bake_rays_bvh_t *bvh) {
	free(bvh->pos);
	free(bvh->nor);
	free(bvh->index);
	free(bvh->nodes);
	*bvh = (bake_rays_bvh_t){0};
}

static bool bake_rays_box_hit(bake_rays_node_t *node, f32 *o, f32 *inv, f32 tmin, f32 tmax) {
	f32 t0 = tmin;
	f32 t1 = tmax;
	for (i32 k = 0; k < 3; ++k) {
		f32 a = (node->min[k] - o[k]) * inv[k];
		f32 b = (node->max[k] - o[k]) * inv[k];
		if (a > b) {
			f32 t = a;
			a     = b;
			b     = t;
		}
		t0 = a > t0 ? a : t0;
		t1 = b < t1 ? b : t1;
		if (t0 > t1) {
			return false;
		}
	}
	return true;
}

// Hit along the ray in [tmin, tmax] closest to the origin, or closest to the distance target
// when target >= 0, returns the triangle and barycentrics.
// cull skips surfaces facing away from the ray origin, like the inside of a bolt head.
// any returns on the first hit, for occlusion tests.
static i32 bake_rays_trace(bake_rays_bvh_t *bvh, f32 *o, f32 *dir, f32 tmin, f32 tmax, f32 target, bool cull, bool any, f32 *out_t, f32 *out_u, f32 *out_v) {
	*out_t = tmax;
	if (bvh->count == 0) {
		return -1;
	}
	f32 inv[3] = {1.0 / (fabsf(dir[0]) > 1e-12 ? dir[0] : 1e-12), 1.0 / (fabsf(dir[1]) > 1e-12 ? dir[1] : 1e-12),
	              1.0 / (fabsf(dir[2]) > 1e-12 ? dir[2] : 1e-12)};
	i32 stack[128];
	i32 sp      = 0;
	stack[sp++] = 0;
	i32 best    = -1;
	f32 best_t  = tmax;
	f32 lo      = tmin; // Interval that can still hold a better hit
	f32 hi      = tmax;
	f32 best_k  = 1e30;
	while (sp > 0) {
		bake_rays_node_t *node = &bvh->nodes[stack[--sp]];
		if (!bake_rays_box_hit(node, o, inv, lo, hi)) {
			continue;
		}
		if (node->count == 0) {
			if (sp < 126) {
				stack[sp++] = node->first;
				stack[sp++] = node->first + 1;
			}
			continue;
		}
		for (i32 i = node->first; i < node->first + node->count; ++i) {
			i32  tri = bvh->index[i];
			f32 *p   = bvh->pos + tri * 9;
			f32  e1[3], e2[3], pv[3], tv[3], qv[3];
			bake_rays_v3_sub(e1, p + 3, p);
			bake_rays_v3_sub(e2, p + 6, p);
			bake_rays_v3_cross(pv, dir, e2);
			f32 det = bake_rays_v3_dot(e1, pv);
			if (fabsf(det) < 1e-14) {
				continue;
			}
			f32 inv_det = 1.0 / det;
			bake_rays_v3_sub(tv, o, p);
			f32 u = bake_rays_v3_dot(tv, pv) * inv_det;
			if (u < 0.0 || u > 1.0) {
				continue;
			}
			bake_rays_v3_cross(qv, tv, e1);
			f32 v = bake_rays_v3_dot(dir, qv) * inv_det;
			if (v < 0.0 || u + v > 1.0) {
				continue;
			}
			f32 t = bake_rays_v3_dot(e2, qv) * inv_det;
			f32 k = target >= 0.0 ? fabsf(t - target) : t;
			if (t >= lo && t <= hi && k < best_k) {
				if (cull) {
					f32 *hn = bvh->nor + tri * 9;
					if ((hn[0] + hn[3] + hn[6]) * dir[0] + (hn[1] + hn[4] + hn[7]) * dir[1] + (hn[2] + hn[5] + hn[8]) * dir[2] > 0.0) {
						continue;
					}
				}
				best_t = t;
				best_k = k;
				best   = tri;
				*out_u = u;
				*out_v = v;
				if (target >= 0.0) {
					lo = bake_rays_fmax(tmin, target - k);
					hi = bake_rays_fmin(tmax, target + k);
				}
				else {
					hi = t;
				}
				if (any) {
					*out_t = best_t;
					return best;
				}
			}
		}
	}
	*out_t = best_t;
	return best;
}

static i32 *bake_rays_island = NULL; // UV island per low poly triangle, built for the inpaint tool

static void bake_rays_free_low() {
	free(bake_rays_island);
	bake_rays_island = NULL;
	free(bake_rays_lp);
	free(bake_rays_lp_miss);
	bake_rays_lp       = NULL;
	bake_rays_lp_miss  = NULL;
	bake_rays_lp_count = 0;
	bake_rays_free_bvh(&bake_rays_lpb);
}

static void bake_rays_free_images() {
	free(bake_rays_texmap);
	free(bake_rays_out);
	free(bake_rays_mask);
	free(bake_rays_matid);
	free(bake_rays_cav);
	bake_rays_cav    = NULL;
	bake_rays_texmap = NULL;
	bake_rays_out    = NULL;
	bake_rays_mask   = NULL;
	bake_rays_matid  = NULL;
}

static void bake_rays_join() {
	if (!bake_rays_working) {
		return;
	}
	for (i32 i = 0; i < bake_rays_thread_count; ++i) {
		iron_thread_wait_and_destroy(&bake_rays_threads[i]);
	}
	bake_rays_thread_count = 0;
	bake_rays_working      = false;
}

// High poly triangles and bvh read from imported raw meshes, NULL releases them.
// The raw meshes can be freed afterwards.
static bool bake_rays_set_high_raw(raw_mesh_t_array_t *parts) {
	bake_rays_join();
	bake_rays_free_bvh(&bake_rays_hp);
	bake_rays_miss_dirty = true;
	bake_rays_version++;
	if (parts == NULL) {
		return false;
	}
	i32 count = 0;
	for (i32 i = 0; i < parts->length; ++i) {
		raw_mesh_t *m = parts->buffer[i];
		if (m->posa != NULL && m->inda != NULL) {
			count += m->inda->length / 3;
		}
	}
	if (count == 0) {
		return false;
	}
	bake_rays_hp.count = count;
	bake_rays_hp.pos   = malloc(sizeof(f32) * 9 * count);
	bake_rays_hp.nor   = malloc(sizeof(f32) * 9 * count);
	bake_rays_hp.index = malloc(sizeof(i32) * count);
	if (bake_rays_hp.pos == NULL || bake_rays_hp.nor == NULL || bake_rays_hp.index == NULL) {
		bake_rays_free_bvh(&bake_rays_hp);
		return false;
	}
	i32 ti = 0;
	for (i32 i = 0; i < parts->length; ++i) {
		raw_mesh_t *m = parts->buffer[i];
		if (m->posa == NULL || m->inda == NULL) {
			continue;
		}
		i16 *pos = m->posa->buffer;
		i16 *nor = m->nora != NULL ? m->nora->buffer : NULL;
		f32  sc  = m->scale_pos;
		i32  n   = m->inda->length / 3;
		for (i32 t = 0; t < n; ++t) {
			f32 *p  = bake_rays_hp.pos + ti * 9;
			f32 *nn = bake_rays_hp.nor + ti * 9;
			for (i32 v = 0; v < 3; ++v) {
				u32 vi        = m->inda->buffer[t * 3 + v];
				p[v * 3]      = pos[vi * 4] / 32767.0 * sc;
				p[v * 3 + 1]  = pos[vi * 4 + 1] / 32767.0 * sc;
				p[v * 3 + 2]  = pos[vi * 4 + 2] / 32767.0 * sc;
				nn[v * 3]     = nor != NULL ? nor[vi * 2] / 32767.0 : 0.0;
				nn[v * 3 + 1] = nor != NULL ? nor[vi * 2 + 1] / 32767.0 : 0.0;
				nn[v * 3 + 2] = nor != NULL ? pos[vi * 4 + 3] / 32767.0 : 0.0;
				bake_rays_v3_norm(nn + v * 3);
			}
			// Missing normals, use the face normal per corner
			f32 e1[3], e2[3], fn[3];
			bake_rays_v3_sub(e1, p + 3, p);
			bake_rays_v3_sub(e2, p + 6, p);
			bake_rays_v3_cross(fn, e1, e2);
			bake_rays_v3_norm(fn);
			for (i32 v = 0; v < 3; ++v) {
				f32 *c = nn + v * 3;
				if (bake_rays_v3_dot(c, c) < 0.25) {
					c[0] = fn[0];
					c[1] = fn[1];
					c[2] = fn[2];
				}
			}
			bake_rays_hp.index[ti] = ti;
			ti++;
		}
	}
	bake_rays_build_bvh(&bake_rays_hp);
	memcpy(bake_rays_hp_min, bake_rays_hp.nodes[0].min, sizeof(f32) * 3);
	memcpy(bake_rays_hp_max, bake_rays_hp.nodes[0].max, sizeof(f32) * 3);
	return true;
}

typedef struct bake_rays_corner {
	i64 key[3];
	i32 index; // Triangle * 3 + vertex
} bake_rays_corner_t;

static int bake_rays_corner_compare(const void *a, const void *b) {
	const bake_rays_corner_t *ca = a;
	const bake_rays_corner_t *cb = b;
	for (i32 k = 0; k < 3; ++k) {
		if (ca->key[k] != cb->key[k]) {
			return ca->key[k] < cb->key[k] ? -1 : 1;
		}
	}
	return 0;
}

// Ray directions per corner, vertices sharing a position get the same averaged normal
// so the cage stays closed over hard edges
static void bake_rays_update_ray_normals() {
	bake_rays_miss_dirty = true;
	bake_rays_version++;
	for (i32 i = 0; i < bake_rays_lp_count; ++i) {
		memcpy(bake_rays_lp[i].a, bake_rays_lp[i].n, sizeof(f32) * 9);
	}
	if (!bake_rays_average || bake_rays_lp_count == 0) {
		return;
	}

	f32 diag[3];
	bake_rays_v3_sub(diag, bake_rays_lp_max, bake_rays_lp_min);
	f32 eps = sqrtf(bake_rays_v3_dot(diag, diag)) * 1e-5;
	if (eps <= 0.0) {
		eps = 1e-6;
	}
	i32                 n       = bake_rays_lp_count * 3;
	bake_rays_corner_t *corners = malloc(sizeof(bake_rays_corner_t) * n);
	for (i32 i = 0; i < n; ++i) {
		f32 *p = bake_rays_lp[i / 3].p + (i % 3) * 3;
		for (i32 k = 0; k < 3; ++k) {
			corners[i].key[k] = (i64)llroundf(p[k] / eps);
		}
		corners[i].index = i;
	}
	qsort(corners, n, sizeof(bake_rays_corner_t), bake_rays_corner_compare);

	i32 start = 0;
	while (start < n) {
		i32 end = start + 1;
		while (end < n && bake_rays_corner_compare(&corners[start], &corners[end]) == 0) {
			end++;
		}
		// Sum the distinct normals of this position
		f32 sum[3] = {0.0, 0.0, 0.0};
		for (i32 i = start; i < end; ++i) {
			f32 *nn  = bake_rays_lp[corners[i].index / 3].n + (corners[i].index % 3) * 3;
			bool dup = false;
			for (i32 j = start; j < i; ++j) {
				f32 *mm = bake_rays_lp[corners[j].index / 3].n + (corners[j].index % 3) * 3;
				if (bake_rays_v3_dot(nn, mm) > 0.999) {
					dup = true;
					break;
				}
			}
			if (!dup) {
				sum[0] += nn[0];
				sum[1] += nn[1];
				sum[2] += nn[2];
			}
		}
		bake_rays_v3_norm(sum);
		for (i32 i = start; i < end; ++i) {
			f32 *a = bake_rays_lp[corners[i].index / 3].a + (corners[i].index % 3) * 3;
			a[0]   = sum[0];
			a[1]   = sum[1];
			a[2]   = sum[2];
		}
		start = end;
	}
	free(corners);
}

static u64 bake_maps_low_key;
static u64 bake_maps_paint_objects_key();

// Low poly triangles with uvs, read from the paint objects
static bool bake_rays_set_low() {
	bake_rays_join();
	bake_rays_free_low();
	bake_rays_miss_dirty = true;
	bake_rays_version++;
	mesh_object_t_array_t *lows = g_project->_->paint_objects;
	for (i32 i = 0; i < lows->length; ++i) {
		mesh_object_t *o = lows->buffer[i];
		if (o->data != NULL && mesh_data_get_vertex_array(o->data, "tex") != NULL) {
			bake_rays_lp_count += bake_rays_count_tris(o);
		}
	}
	if (bake_rays_lp_count == 0) {
		return false;
	}
	bake_rays_lp      = malloc(sizeof(bake_rays_tri_t) * bake_rays_lp_count);
	bake_rays_lp_miss = calloc(bake_rays_lp_count, sizeof(f32));
	for (i32 k = 0; k < 3; ++k) {
		bake_rays_lp_min[k] = 1e30;
		bake_rays_lp_max[k] = -1e30;
	}
	i32 ti = 0;
	for (i32 i = 0; i < lows->length; ++i) {
		mesh_object_t *o = lows->buffer[i];
		if (o->data == NULL || mesh_data_get_vertex_array(o->data, "tex") == NULL) {
			continue;
		}
		vertex_array_t *col = mesh_data_get_vertex_array(o->data, "col");
		i32             n   = bake_rays_count_tris(o);
		for (i32 t = 0; t < n; ++t) {
			bake_rays_tri_t *tri = &bake_rays_lp[ti++];
			tri->object          = i;
			tri->scale           = o->data->scale_pos > 0.0 ? o->data->scale_pos : 1.0;
			for (i32 v = 0; v < 3; ++v) {
				u32 vi = o->data->index_array->buffer[t * 3 + v];
				bake_rays_read_vertex(o->data, vi, tri->p + v * 3, tri->n + v * 3, tri->uv + v * 2);
				for (i32 k = 0; k < 3; ++k) {
					bake_rays_lp_min[k] = bake_rays_fmin(bake_rays_lp_min[k], tri->p[v * 3 + k]);
					bake_rays_lp_max[k] = bake_rays_fmax(bake_rays_lp_max[k], tri->p[v * 3 + k]);
					tri->col[v * 3 + k] = col != NULL ? col->values->buffer[vi * 4 + k] / 32767.0 : 1.0;
				}
			}
			// Missing normals, use the face normal per corner
			f32 e1[3], e2[3], fn[3];
			bake_rays_v3_sub(e1, tri->p + 3, tri->p);
			bake_rays_v3_sub(e2, tri->p + 6, tri->p);
			bake_rays_v3_cross(fn, e1, e2);
			bake_rays_v3_norm(fn);
			for (i32 v = 0; v < 3; ++v) {
				f32 *c = tri->n + v * 3;
				if (bake_rays_v3_dot(c, c) < 0.25) {
					memcpy(c, fn, sizeof(f32) * 3);
				}
			}
		}
	}
	bake_rays_update_ray_normals();
	bake_maps_low_key = bake_maps_paint_objects_key();
	return true;
}

// Bake

// Low poly triangle index of each texel, the last triangle drawn wins like on the gpu
static void bake_rays_build_texmap() {
	i32 w = bake_rays_w;
	i32 h = bake_rays_h;
	for (i32 i = 0; i < w * h; ++i) {
		bake_rays_texmap[i] = -1;
	}
	for (i32 ti = 0; ti < bake_rays_lp_count; ++ti) {
		f32 *uv = bake_rays_lp[ti].uv;
		f32  x0 = uv[0] * w, y0 = uv[1] * h;
		f32  x1 = uv[2] * w, y1 = uv[3] * h;
		f32  x2 = uv[4] * w, y2 = uv[5] * h;
		f32  area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
		if (fabsf(area) < 1e-8) {
			continue;
		}
		i32 minx = bake_rays_imax(0, (i32)floorf(bake_rays_fmin(x0, bake_rays_fmin(x1, x2))));
		i32 maxx = bake_rays_imin(w - 1, (i32)ceilf(bake_rays_fmax(x0, bake_rays_fmax(x1, x2))));
		i32 miny = bake_rays_imax(0, (i32)floorf(bake_rays_fmin(y0, bake_rays_fmin(y1, y2))));
		i32 maxy = bake_rays_imin(h - 1, (i32)ceilf(bake_rays_fmax(y0, bake_rays_fmax(y1, y2))));
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
				bake_rays_texmap[y * w + x] = ti;
			}
		}
	}
}

// Surface sample of a texel: low poly point, or the high poly hit when projecting
typedef struct bake_rays_surf {
	bake_rays_tri_t *tri;
	f32              p[3];  // Sampled position
	f32              n[3];  // Sampled normal
	f32              ln[3]; // Low poly normal
	f32              bc[3]; // Low poly barycentrics
	bool             high;  // Sample comes from the high poly
	f32              t;     // Distance from the cage to the high poly
} bake_rays_surf_t;

// px, py in texels, extrapolated from the given triangle like gpu derivatives
static void bake_rays_surface_at(i32 ti, f32 px, f32 py, bool use_high, bake_rays_surf_t *s) {
	bake_rays_tri_t *tri = &bake_rays_lp[ti];
	f32             *uv  = tri->uv;
	f32              w = bake_rays_w, h = bake_rays_h;
	f32              x0 = uv[0] * w, y0 = uv[1] * h;
	f32              x1 = uv[2] * w, y1 = uv[3] * h;
	f32              x2 = uv[4] * w, y2 = uv[5] * h;
	f32              area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
	f32              b0   = ((x1 - px) * (y2 - py) - (x2 - px) * (y1 - py)) / area;
	f32              b1   = ((x2 - px) * (y0 - py) - (x0 - px) * (y2 - py)) / area;
	f32              b2   = 1.0 - b0 - b1;

	f32 rn[3];
	for (i32 k = 0; k < 3; ++k) {
		s->p[k]  = tri->p[k] * b0 + tri->p[3 + k] * b1 + tri->p[6 + k] * b2;
		s->ln[k] = tri->n[k] * b0 + tri->n[3 + k] * b1 + tri->n[6 + k] * b2;
		rn[k]    = tri->a[k] * b0 + tri->a[3 + k] * b1 + tri->a[6 + k] * b2;
	}
	bake_rays_v3_norm(s->ln);
	bake_rays_v3_norm(rn);
	memcpy(s->n, s->ln, sizeof(f32) * 3);
	s->tri   = tri;
	s->bc[0] = b0;
	s->bc[1] = b1;
	s->bc[2] = b2;
	s->high  = false;
	s->t     = 0.0;
	if (!use_high || bake_rays_hp.count == 0) {
		return;
	}

	// Start on the cage in front of the low poly surface and search inwards
	f32 o[3]   = {s->p[0] + rn[0] * bake_rays_dist, s->p[1] + rn[1] * bake_rays_dist, s->p[2] + rn[2] * bake_rays_dist};
	f32 dir[3] = {-rn[0], -rn[1], -rn[2]};
	f32 t, u, v;
	// Keep the high poly surface nearest to the low poly, not the first one met from the cage:
	// parts like eyelids or teeth inside the cage would otherwise shadow the real surface
	i32 hit = bake_rays_trace(&bake_rays_hp, o, dir, 0.0, bake_rays_dist * 2.0, bake_rays_dist, true, false, &t, &u, &v);
	if (hit < 0) {
		return;
	}
	f32 *hn = bake_rays_hp.nor + hit * 9;
	f32  w0 = 1.0 - u - v;
	for (i32 k = 0; k < 3; ++k) {
		s->n[k] = hn[k] * w0 + hn[3 + k] * u + hn[6 + k] * v;
		s->p[k] = o[k] + dir[k] * t;
	}
	bake_rays_v3_norm(s->n);
	s->high = true;
	s->t    = t;
}

// Small hash based random numbers, one stream per texel
static u32 bake_rays_hash(u32 x) {
	x ^= x >> 16;
	x *= 0x7feb352d;
	x ^= x >> 15;
	x *= 0x846ca68b;
	x ^= x >> 16;
	return x;
}

static f32 bake_rays_radical_inverse(u32 bits) {
	bits = (bits << 16) | (bits >> 16);
	bits = ((bits & 0x55555555) << 1) | ((bits & 0xAAAAAAAA) >> 1);
	bits = ((bits & 0x33333333) << 2) | ((bits & 0xCCCCCCCC) >> 2);
	bits = ((bits & 0x0F0F0F0F) << 4) | ((bits & 0xF0F0F0F0) >> 4);
	bits = ((bits & 0x00FF00FF) << 8) | ((bits & 0xFF00FF00) >> 8);
	return bits * 2.3283064365386963e-10;
}

// Cosine weighted direction around n, same distribution as the gpu bake shaders,
// hammersley points rotated per texel to turn banding into noise
static void bake_rays_hemisphere(f32 *n, i32 i, i32 count, f32 r0, f32 r1, f32 *dir) {
	f32 f0 = fmodf((i + 0.5) / count + r0, 1.0);
	f32 f1 = fmodf(bake_rays_radical_inverse(i) + r1, 1.0);
	f32 z  = f0 * 2.0 - 1.0;
	f32 a  = f1 * 6.283185307;
	f32 r  = sqrtf(bake_rays_fmax(1.0 - z * z, 0.0));
	dir[0] = n[0] + r * cosf(a);
	dir[1] = n[1] + r * sinf(a);
	dir[2] = n[2] + z;
	bake_rays_v3_norm(dir);
}

static f32 bake_rays_id_hash(f32 a, f32 b) {
	f32 s = sinf(a * 12.9898 + b * 78.233) * 43758.5453;
	return s - floorf(s);
}

static u8 bake_rays_unorm(f32 f) {
	return (u8)floorf(bake_rays_fmin(bake_rays_fmax(f, 0.0), 1.0) * 255.0);
}

static void bake_rays_store(u8 *o, f32 r, f32 g, f32 b) {
	o[0] = bake_rays_unorm(r);
	o[1] = bake_rays_unorm(g);
	o[2] = bake_rays_unorm(b);
	o[3] = 255;
}

// Vector to color, swizzled for y up
static void bake_rays_store_vector(u8 *o, f32 *v, f32 scale) {
	f32 c[3] = {v[0] * scale + 0.5, v[1] * scale + 0.5, v[2] * scale + 0.5};
	if (bake_rays_job.up_y) {
		bake_rays_store(o, c[0], c[2], 1.0 - c[1]);
	}
	else {
		bake_rays_store(o, c[0], c[1], c[2]);
	}
}

static void bake_rays_texel_normal(bake_rays_surf_t *s, u8 *o) {
	if (!s->high) {
		o[0] = o[1] = 128; // Flat
		o[2] = o[3] = 255;
		return;
	}
	f32 *n = s->ln;
	f32  t[3], b[3];
	// Same frame as the viewport decoding (cotangent_frame in globals.h): T and B are the
	// surface gradients of u and v, scaled together so the longest is unit. They are neither
	// orthogonal nor unit on stretched or sheared uv islands, an orthonormal frame then shows
	// seams between islands.
	f32 *p  = s->tri->p;
	f32 *uv = s->tri->uv;
	f32  e1[3], e2[3], dp2perp[3], dp1perp[3];
	bake_rays_v3_sub(e1, p + 3, p);
	bake_rays_v3_sub(e2, p + 6, p);
	bake_rays_v3_cross(dp2perp, e2, n);
	bake_rays_v3_cross(dp1perp, n, e1);
	f32 du1 = uv[2] - uv[0], dv1 = uv[3] - uv[1];
	f32 du2 = uv[4] - uv[0], dv2 = uv[5] - uv[1];
	for (i32 k = 0; k < 3; ++k) {
		t[k] = dp2perp[k] * du1 + dp1perp[k] * du2;
		b[k] = dp2perp[k] * dv1 + dp1perp[k] * dv2;
	}
	// The edge frame carries the sign of the winding, the viewport frame follows +grad u, +grad v
	f32 e12[3];
	bake_rays_v3_cross(e12, e1, e2);
	f32 sign = bake_rays_v3_dot(e12, n) < 0.0 ? -1.0 : 1.0;
	f32 m    = bake_rays_fmax(bake_rays_v3_dot(t, t), bake_rays_v3_dot(b, b));
	if (m <= 0.0) {
		o[0] = o[1] = 128;
		o[2] = o[3] = 255;
		return;
	}
	f32 inv = sign / sqrtf(m);
	for (i32 k = 0; k < 3; ++k) {
		t[k] *= inv;
		b[k] *= inv;
	}

	// The viewport rebuilds normalize(x T + y' B + z N) with y' = -y, solve for (x, y', z)
	f32 *h = s->n;
	f32  bxn[3], nxt[3], txb[3];
	bake_rays_v3_cross(bxn, b, n);
	bake_rays_v3_cross(nxt, n, t);
	bake_rays_v3_cross(txb, t, b);
	f32 det = bake_rays_v3_dot(t, bxn);
	if (fabsf(det) < 1e-12) {
		o[0] = o[1] = 128;
		o[2] = o[3] = 255;
		return;
	}
	f32 c[3] = {bake_rays_v3_dot(h, bxn) / det, bake_rays_v3_dot(h, nxt) / det, bake_rays_v3_dot(h, txb) / det};
	bake_rays_v3_norm(c);
	bake_rays_store(o, c[0] * 0.5 + 0.5, -c[1] * 0.5 + 0.5, c[2] * 0.5 + 0.5);
}

// Occlusion rays from the sampled surface, against the mesh the sample lies on
static void bake_rays_texel_occlusion(bake_rays_surf_t *s, i32 x, i32 y, u8 *o) {
	bake_rays_bvh_t *bvh   = s->high ? &bake_rays_hp : &bake_rays_lpb;
	i32              count = bake_rays_job.rays;
	u32              seed  = bake_rays_hash((u32)(y * bake_rays_w + x) * 2654435761u + (u32)bake_rays_job.type);
	f32              r0    = (seed & 0xffff) / 65536.0;
	f32              r1    = (bake_rays_hash(seed) & 0xffff) / 65536.0;
	i32              type  = bake_rays_job.type;
	f32              n[3]  = {s->n[0], s->n[1], s->n[2]};
	if (type == BAKE_TYPE_THICKNESS) {
		n[0] = -n[0];
		n[1] = -n[1];
		n[2] = -n[2];
	}

	f32 accum   = 0.0;
	f32 bent[3] = {0.0, 0.0, 0.0};
	for (i32 i = 0; i < count; ++i) {
		f32 dir[3];
		bake_rays_hemisphere(n, i, count, r0, r1, dir);
		f32 t, u, v;
		i32 hit = bake_rays_trace(bvh, s->p, dir, bake_rays_job.ray_min, bake_rays_job.ray_max, -1.0, false, type != BAKE_TYPE_THICKNESS, &t, &u, &v);
		if (type == BAKE_TYPE_THICKNESS) {
			if (hit >= 0) {
				accum += t * 2.0;
			}
		}
		else if (hit < 0) {
			accum += 1.0;
			bent[0] += dir[0];
			bent[1] += dir[1];
			bent[2] += dir[2];
		}
	}
	accum /= count;

	if (type == BAKE_TYPE_BENT_NORMAL) {
		if (bake_rays_v3_dot(bent, bent) < 1e-12) {
			memcpy(bent, s->n, sizeof(f32) * 3);
		}
		bake_rays_v3_norm(bent);
		bake_rays_store_vector(o, bent, 0.5);
		return;
	}
	if (type == BAKE_TYPE_OCCLUSION) {
		accum = 1.0 - (1.0 - accum) * bake_rays_job.ao_strength;
	}
	bake_rays_store(o, accum, accum, accum);
}

static void bake_rays_texel_curvature(bake_rays_surf_t *s, i32 ti, i32 x, i32 y, u8 *o) {
	// Normal derivatives along the texel grid, extrapolated within the triangle
	bake_rays_surf_t sx, sy;
	bake_rays_surface_at(ti, x + 1.5, y + 0.5, bake_rays_job.use_high, &sx);
	bake_rays_surface_at(ti, x + 0.5, y + 1.5, bake_rays_job.use_high, &sy);
	f32 dx[3], dy[3];
	bake_rays_v3_sub(dx, sx.n, s->n);
	bake_rays_v3_sub(dy, sy.n, s->n);
	f32 c = bake_rays_fmax(bake_rays_v3_dot(dx, dx), bake_rays_v3_dot(dy, dy));
	f32 r = bake_rays_fmax(bake_rays_job.curv_radius, 0.001);
	c     = powf(c, (1.0 / r) * 0.25) * bake_rays_job.curv_strength * 2.0 + bake_rays_job.curv_offset / 10.0;
	c     = bake_rays_fmin(bake_rays_fmax(c, 0.0), 1.0);
	if (bake_rays_job.axis != BAKE_AXIS_XYZ) {
		static f32 axes[7][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {-1, 0, 0}, {0, -1, 0}, {0, 0, -1}};
		c *= bake_rays_v3_dot(s->n, axes[bake_rays_job.axis]);
	}
	bake_rays_store(o, c, c, c);
}

// Cavity from the signed curvature of the sampled surface: normals converging around a texel
// mean a groove (dark), diverging normals a ridge (bright). Neighbours are taken a few texels
// away, extrapolated within the triangle like the gpu derivatives.
static void bake_rays_texel_cavity(bake_rays_surf_t *s, i32 ti, i32 x, i32 y, u8 *o) {
	static const f32 dx[4] = {1, -1, 0, 0};
	static const f32 dy[4] = {0, 0, 1, -1};
	f32              r     = bake_rays_job.cavity_texels;
	f32              h     = 0.0;
	i32              n     = 0;
	for (i32 k = 0; k < 4; ++k) {
		bake_rays_surf_t q;
		bake_rays_surface_at(ti, x + 0.5 + dx[k] * r, y + 0.5 + dy[k] * r, bake_rays_job.use_high, &q);
		if (q.high != s->high) {
			continue; // One of the samples missed the high poly
		}
		f32 dp[3], dn[3];
		bake_rays_v3_sub(dp, q.p, s->p);
		bake_rays_v3_sub(dn, q.n, s->n);
		f32 l2 = bake_rays_v3_dot(dp, dp);
		if (l2 < 1e-20) {
			continue;
		}
		h += bake_rays_v3_dot(dn, dp) / l2;
		n++;
	}
	h = n > 0 ? h / n : 0.0;
	if (bake_rays_cav != NULL) {
		bake_rays_cav[y * bake_rays_w + x] = h; // Levels are applied once the whole map is known
	}
	bake_rays_store(o, 0.5, 0.5, 0.5);
}

static void bake_rays_texel(i32 x, i32 y) {
	i32 i  = y * bake_rays_w + x;
	i32 ti = bake_rays_texmap[i];
	u8 *o  = bake_rays_out + i * 4;
	if (ti < 0) {
		return;
	}
	bake_rays_surf_t s;
	i32              type = bake_rays_job.type;
	bool             high =
	    bake_rays_job.use_high && type != BAKE_TYPE_TEXCOORD && type != BAKE_TYPE_OBJECTID && type != BAKE_TYPE_MATERIALID && type != BAKE_TYPE_VERTEX_COLOR;
	bake_rays_surface_at(ti, x + 0.5, y + 0.5, high, &s);
	bake_rays_mask[i] = 1;

	if (type == BAKE_TYPE_NORMAL) {
		bake_rays_texel_normal(&s, o);
	}
	else if (type == BAKE_TYPE_HEIGHT) {
		f32 hgt = s.high ? 0.5 + 0.5 * (bake_rays_dist - s.t) / bake_rays_dist : 0.5; // 0.5 is the low poly surface
		bake_rays_store(o, hgt, hgt, hgt);
	}
	else if (type == BAKE_TYPE_CAVITY) {
		bake_rays_texel_cavity(&s, ti, x, y, o);
	}
	else if (type == BAKE_TYPE_OCCLUSION || type == BAKE_TYPE_THICKNESS || type == BAKE_TYPE_BENT_NORMAL) {
		bake_rays_texel_occlusion(&s, x, y, o);
	}
	else if (type == BAKE_TYPE_CURVATURE) {
		bake_rays_texel_curvature(&s, ti, x, y, o);
	}
	else if (type == BAKE_TYPE_NORMAL_OBJECT) {
		bake_rays_store_vector(o, s.n, 0.5);
	}
	else if (type == BAKE_TYPE_POSITION) {
		f32 p[3] = {s.p[0] / s.tri->scale, s.p[1] / s.tri->scale, s.p[2] / s.tri->scale};
		bake_rays_store_vector(o, p, 0.5);
	}
	else if (type == BAKE_TYPE_TEXCOORD) {
		f32 *uv = s.tri->uv;
		bake_rays_store(o, uv[0] * s.bc[0] + uv[2] * s.bc[1] + uv[4] * s.bc[2], uv[1] * s.bc[0] + uv[3] * s.bc[1] + uv[5] * s.bc[2], 0.0);
	}
	else if (type == BAKE_TYPE_VERTEX_COLOR) {
		f32 *c = s.tri->col;
		bake_rays_store(o, c[0] * s.bc[0] + c[3] * s.bc[1] + c[6] * s.bc[2], c[1] * s.bc[0] + c[4] * s.bc[1] + c[7] * s.bc[2],
		                c[2] * s.bc[0] + c[5] * s.bc[1] + c[8] * s.bc[2]);
	}
	else if (type == BAKE_TYPE_OBJECTID || type == BAKE_TYPE_MATERIALID) {
		f32 id = type == BAKE_TYPE_OBJECTID ? s.tri->object : (bake_rays_matid != NULL ? bake_rays_matid[i] / 255.0 : 0.0);
		id += 1.0 / 255.0;
		bake_rays_store(o, bake_rays_id_hash(id, id * 20.0), bake_rays_id_hash(id * 20.0, id), bake_rays_id_hash(id, id * 40.0));
	}
}

static void bake_rays_worker(void *_) {
	while (!bake_rays_cancelled) {
		iron_mutex_lock(&bake_rays_mutex);
		i32 y = bake_rays_next_row++;
		iron_mutex_unlock(&bake_rays_mutex);
		if (y >= bake_rays_h) {
			return;
		}
		for (i32 x = 0; x < bake_rays_w; ++x) {
			bake_rays_texel(x, y);
		}
		iron_mutex_lock(&bake_rays_mutex);
		bake_rays_rows_done++;
		iron_mutex_unlock(&bake_rays_mutex);
	}
}

static f32 bake_rays_half_to_float(u16 h) {
	u32 sign = (h >> 15) & 1;
	i32 exp  = (h >> 10) & 0x1f;
	u32 mant = h & 0x3ff;
	f32 f    = exp == 0 ? ldexpf((f32)mant, -24) : exp == 31 ? 65504.0 : ldexpf((f32)(mant | 0x400), exp - 25);
	return sign ? -f : f;
}

// Material ids are painted into the alpha of the layer normal texture
static void bake_rays_read_matid() {
	free(bake_rays_matid);
	bake_rays_matid  = NULL;
	gpu_texture_t *t = g_context->layer != NULL ? g_context->layer->texpaint_nor : NULL;
	if (t == NULL) {
		return;
	}
	buffer_t *pixels = gpu_get_texture_pixels(t);
	if (pixels == NULL || t->height == 0) {
		return;
	}
	i32 bpp    = t->format == GPU_TEXTURE_FORMAT_RGBA128 ? 16 : t->format == GPU_TEXTURE_FORMAT_RGBA64 ? 8 : 4;
	i32 stride = pixels->length / t->height;
	if (stride < (i32)t->width * bpp) {
		return;
	}
	bake_rays_matid = malloc(bake_rays_w * bake_rays_h);
	for (i32 y = 0; y < bake_rays_h; ++y) {
		for (i32 x = 0; x < bake_rays_w; ++x) {
			i32 sx                               = x * t->width / bake_rays_w;
			i32 sy                               = y * t->height / bake_rays_h;
			u8 *p                                = pixels->buffer + sy * stride + sx * bpp;
			f32 a                                = bpp == 16 ? ((f32 *)p)[3] : bpp == 8 ? bake_rays_half_to_float(((u16 *)p)[3]) : p[3] / 255.0;
			bake_rays_matid[y * bake_rays_w + x] = bake_rays_unorm(a);
		}
	}
}

// Grow baked texels into empty neighbours to hide uv seams
static void bake_rays_dilate(i32 radius) {
	static const i32 dx[4] = {-1, 1, 0, 0};
	static const i32 dy[4] = {0, 0, -1, 1};
	i32              w     = bake_rays_w;
	i32              h     = bake_rays_h;
	u8              *mask  = malloc(w * h);
	for (i32 pass = 0; pass < radius; ++pass) {
		memcpy(mask, bake_rays_mask, w * h);
		for (i32 y = 0; y < h; ++y) {
			for (i32 x = 0; x < w; ++x) {
				if (bake_rays_mask[y * w + x]) {
					continue;
				}
				for (i32 k = 0; k < 4; ++k) {
					i32 nx = x + dx[k];
					i32 ny = y + dy[k];
					if (nx < 0 || ny < 0 || nx >= w || ny >= h || !bake_rays_mask[ny * w + nx]) {
						continue;
					}
					memcpy(bake_rays_out + (y * w + x) * 4, bake_rays_out + (ny * w + nx) * 4, 4);
					mask[y * w + x] = 1;
					break;
				}
			}
		}
		u8 *t          = bake_rays_mask;
		bake_rays_mask = mask;
		mask           = t;
	}
	free(mask);
}

// Smooth pass of the curvature, averages covered neighbours
static void bake_rays_blur() {
	i32 w   = bake_rays_w;
	i32 h   = bake_rays_h;
	u8 *src = malloc(w * h);
	for (i32 i = 0; i < w * h; ++i) {
		src[i] = bake_rays_out[i * 4];
	}
	for (i32 y = 0; y < h; ++y) {
		for (i32 x = 0; x < w; ++x) {
			if (!bake_rays_mask[y * w + x]) {
				continue;
			}
			i32 sum = 0;
			i32 n   = 0;
			for (i32 j = -1; j <= 1; ++j) {
				for (i32 k = -1; k <= 1; ++k) {
					i32 nx = x + k;
					i32 ny = y + j;
					if (nx < 0 || ny < 0 || nx >= w || ny >= h || !bake_rays_mask[ny * w + nx]) {
						continue;
					}
					sum += src[ny * w + nx];
					n++;
				}
			}
			u8  v = (u8)(sum / n);
			u8 *o = bake_rays_out + (y * w + x) * 4;
			o[0] = o[1] = o[2] = v;
		}
	}
	free(src);
}

static int bake_rays_f32_compare(const void *a, const void *b) {
	f32 x = *(const f32 *)a, y = *(const f32 *)b;
	return x < y ? -1 : x > y ? 1 : 0;
}

// Scales the cavity by the 95th percentile of its magnitude so a few sharp creases do not
// flatten the rest, then applies the contrast exponent around mid gray
static void bake_rays_cavity_levels() {
	if (bake_rays_cav == NULL) {
		return;
	}
	i32  count = 0;
	f32 *mags  = malloc(sizeof(f32) * bake_rays_w * bake_rays_h);
	for (i32 i = 0; i < bake_rays_w * bake_rays_h; ++i) {
		if (bake_rays_mask[i]) {
			mags[count++] = fabsf(bake_rays_cav[i]);
		}
	}
	if (count == 0) {
		free(mags);
		return;
	}
	qsort(mags, count, sizeof(f32), bake_rays_f32_compare);
	f32 scale = mags[(i32)(count * 0.95)];
	free(mags);
	if (scale <= 0.0) {
		scale = 1.0;
	}
	for (i32 i = 0; i < bake_rays_w * bake_rays_h; ++i) {
		if (bake_rays_mask[i]) {
			f32 f = bake_rays_fmin(bake_rays_fmax(0.5 + 0.5 * bake_rays_cav[i] / scale, 0.0), 1.0);
			f32 c = f < 0.5 ? 0.5 * powf(f * 2.0, bake_rays_job.cavity_contrast) : 1.0 - 0.5 * powf((1.0 - f) * 2.0, bake_rays_job.cavity_contrast);
			u8 *o = bake_rays_out + i * 4;
			o[0] = o[1] = o[2] = bake_rays_unorm(c);
		}
	}
}

// Preview

#define BAKE_RAYS_MISS_SAMPLES 10
static const f32 bake_rays_miss_samples[BAKE_RAYS_MISS_SAMPLES][3] = {
    {0.333, 0.333, 0.334}, {0.96, 0.02, 0.02}, {0.02, 0.96, 0.02}, {0.02, 0.02, 0.96}, {0.49, 0.49, 0.02},
    {0.02, 0.49, 0.49},    {0.49, 0.02, 0.49}, {0.7, 0.15, 0.15},  {0.15, 0.7, 0.15},  {0.15, 0.15, 0.7},
};

// True when the ray from the cage along the low poly normal misses the high poly
static bool bake_rays_misses(f32 *p, f32 *n) {
	f32 o[3]   = {p[0] + n[0] * bake_rays_dist, p[1] + n[1] * bake_rays_dist, p[2] + n[2] * bake_rays_dist};
	f32 dir[3] = {-n[0], -n[1], -n[2]};
	f32 t, u, v;
	return bake_rays_trace(&bake_rays_hp, o, dir, 0.0, bake_rays_dist * 2.0, bake_rays_dist, true, false, &t, &u, &v) < 0;
}

// Cast a few rays per low poly triangle to find areas the high poly is not reached from
static void bake_rays_update_miss() {
	if (!bake_rays_miss_dirty || bake_rays_lp == NULL) {
		return;
	}
	bake_rays_miss_dirty = false;
	for (i32 i = 0; i < bake_rays_lp_count; ++i) {
		if (bake_rays_hp.count == 0) {
			bake_rays_lp_miss[i] = 0.0;
			continue;
		}
		bake_rays_tri_t *tri    = &bake_rays_lp[i];
		i32              misses = 0;
		for (i32 s = 0; s < BAKE_RAYS_MISS_SAMPLES; ++s) {
			f32 b0 = bake_rays_miss_samples[s][0], b1 = bake_rays_miss_samples[s][1], b2 = bake_rays_miss_samples[s][2];
			f32 p[3], n[3];
			for (i32 k = 0; k < 3; ++k) {
				p[k] = tri->p[k] * b0 + tri->p[3 + k] * b1 + tri->p[6 + k] * b2;
				n[k] = tri->a[k] * b0 + tri->a[3 + k] * b1 + tri->a[6 + k] * b2;
			}
			bake_rays_v3_norm(n);
			if (bake_rays_misses(p, n)) {
				misses++;
			}
		}
		bake_rays_lp_miss[i] = misses / (f32)BAKE_RAYS_MISS_SAMPLES;
	}
}

static f32 *bake_rays_zbuf   = NULL;
static i32  bake_rays_zbuf_n = 0;

typedef struct bake_rays_view {
	f32 eye[3];
	f32 r[3];
	f32 u[3];
	f32 f[3];
	f32 focal;
	i32 w;
	i32 h;
} bake_rays_view_t;

static bool bake_rays_project(bake_rays_view_t *v, f32 *p, f32 *out) {
	f32 d[3];
	bake_rays_v3_sub(d, p, v->eye);
	f32 z = bake_rays_v3_dot(d, v->f);
	if (z < 1e-4) {
		return false;
	}
	out[0] = v->w * 0.5 + bake_rays_v3_dot(d, v->r) / z * v->focal;
	out[1] = v->h * 0.5 - bake_rays_v3_dot(d, v->u) / z * v->focal;
	out[2] = z;
	return true;
}

// Rasterize a flat shaded triangle, alpha < 1 blends without writing depth
static void bake_rays_raster(bake_rays_view_t *v, u8 *rgba, f32 *p0, f32 *p1, f32 *p2, f32 *col, f32 alpha, bake_rays_tri_t *miss_tri, f32 *miss_col) {
	f32 a[3], b[3], c[3];
	if (!bake_rays_project(v, p0, a) || !bake_rays_project(v, p1, b) || !bake_rays_project(v, p2, c)) {
		return;
	}
	f32 area = (b[0] - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (b[1] - a[1]);
	if (fabsf(area) < 1e-6) {
		return;
	}

	// Two sided lambert from the camera direction
	f32 e1[3], e2[3], fn[3];
	bake_rays_v3_sub(e1, p1, p0);
	bake_rays_v3_sub(e2, p2, p0);
	bake_rays_v3_cross(fn, e1, e2);
	bake_rays_v3_norm(fn);
	f32 shade = 0.35 + 0.65 * fabsf(bake_rays_v3_dot(fn, v->f));

	i32 minx = bake_rays_imax(0, (i32)floorf(bake_rays_fmin(a[0], bake_rays_fmin(b[0], c[0]))));
	i32 maxx = bake_rays_imin(v->w - 1, (i32)ceilf(bake_rays_fmax(a[0], bake_rays_fmax(b[0], c[0]))));
	i32 miny = bake_rays_imax(0, (i32)floorf(bake_rays_fmin(a[1], bake_rays_fmin(b[1], c[1]))));
	i32 maxy = bake_rays_imin(v->h - 1, (i32)ceilf(bake_rays_fmax(a[1], bake_rays_fmax(b[1], c[1]))));
	for (i32 y = miny; y <= maxy; ++y) {
		for (i32 x = minx; x <= maxx; ++x) {
			f32 px = x + 0.5, py = y + 0.5;
			f32 w0 = ((b[0] - px) * (c[1] - py) - (c[0] - px) * (b[1] - py)) / area;
			f32 w1 = ((c[0] - px) * (a[1] - py) - (a[0] - px) * (c[1] - py)) / area;
			f32 w2 = 1.0 - w0 - w1;
			if (w0 < 0.0 || w1 < 0.0 || w2 < 0.0) {
				continue;
			}
			f32 z = a[2] * w0 + b[2] * w1 + c[2] * w2;
			i32 i = y * v->w + x;
			if (z > bake_rays_zbuf[i]) {
				continue;
			}
			u8 *o = rgba + i * 4;
			if (alpha >= 1.0) {
				bake_rays_zbuf[i] = z;
				f32 *cc           = col;
				if (miss_tri != NULL) {
					// Perspective correct barycentrics, then trace from this surface point
					f32 q0 = w0 / a[2], q1 = w1 / b[2], q2 = w2 / c[2];
					f32 qs = q0 + q1 + q2;
					q0 /= qs;
					q1 /= qs;
					q2 /= qs;
					f32 pp[3], nn[3];
					for (i32 k = 0; k < 3; ++k) {
						pp[k] = miss_tri->p[k] * q0 + miss_tri->p[3 + k] * q1 + miss_tri->p[6 + k] * q2;
						nn[k] = miss_tri->a[k] * q0 + miss_tri->a[3 + k] * q1 + miss_tri->a[6 + k] * q2;
					}
					bake_rays_v3_norm(nn);
					if (bake_rays_misses(pp, nn)) {
						cc = miss_col;
					}
				}
				o[0] = (u8)bake_rays_fmin(cc[0] * shade, 255.0);
				o[1] = (u8)bake_rays_fmin(cc[1] * shade, 255.0);
				o[2] = (u8)bake_rays_fmin(cc[2] * shade, 255.0);
			}
			else {
				o[0] = (u8)(o[0] * (1.0 - alpha) + col[0] * alpha);
				o[1] = (u8)(o[1] * (1.0 - alpha) + col[1] * alpha);
				o[2] = (u8)(o[2] * (1.0 - alpha) + col[2] * alpha);
			}
		}
	}
}

// Software render of the low poly, high poly and cage, z is up
static void bake_rays_preview(u8 *rgba, i32 w, i32 h, f32 yaw, f32 pitch, f32 zoom, bool show_low, bool show_high, bool show_cage, bool show_miss, bool fast) {
	if (bake_rays_zbuf_n != w * h) {
		free(bake_rays_zbuf);
		bake_rays_zbuf   = malloc(sizeof(f32) * w * h);
		bake_rays_zbuf_n = w * h;
	}
	for (i32 i = 0; i < w * h; ++i) {
		bake_rays_zbuf[i] = 1e30;
		rgba[i * 4]       = 38;
		rgba[i * 4 + 1]   = 38;
		rgba[i * 4 + 2]   = 42;
		rgba[i * 4 + 3]   = 255;
	}
	if (bake_rays_lp == NULL) {
		return;
	}
	show_miss = show_miss && bake_rays_hp.count > 0;
	if (show_miss && fast) {
		bake_rays_update_miss();
	}

	f32 center[3], diag[3];
	for (i32 k = 0; k < 3; ++k) {
		center[k] = (bake_rays_lp_min[k] + bake_rays_lp_max[k]) * 0.5;
	}
	bake_rays_v3_sub(diag, bake_rays_lp_max, bake_rays_lp_min);
	f32 radius = sqrtf(bake_rays_v3_dot(diag, diag)) * 0.5;
	f32 dist   = radius * 2.5 * zoom;

	bake_rays_view_t v  = {.w = w, .h = h};
	f32              cp = cosf(pitch), sp = sinf(pitch), cy = cosf(yaw), sy = sinf(yaw);
	v.eye[0] = center[0] + dist * cp * cy;
	v.eye[1] = center[1] + dist * cp * sy;
	v.eye[2] = center[2] + dist * sp;
	bake_rays_v3_sub(v.f, center, v.eye);
	bake_rays_v3_norm(v.f);
	f32 up[3] = {0.0, 0.0, 1.0};
	bake_rays_v3_cross(v.r, v.f, up);
	bake_rays_v3_norm(v.r);
	bake_rays_v3_cross(v.u, v.r, v.f);
	v.focal = h * 0.5 / tanf(0.35);

	f32 col_low[3]  = {200.0, 196.0, 186.0};
	f32 col_miss[3] = {230.0, 30.0, 30.0};
	f32 col_high[3] = {120.0, 160.0, 190.0};
	f32 col_cage[3] = {255.0, 60.0, 60.0};

	if (show_low) {
		for (i32 i = 0; i < bake_rays_lp_count; ++i) {
			bake_rays_tri_t *tri = &bake_rays_lp[i];
			f32              m   = show_miss && fast && bake_rays_lp_miss != NULL ? bake_rays_lp_miss[i] : 0.0;
			f32              col[3];
			for (i32 k = 0; k < 3; ++k) {
				col[k] = m > 0.0 ? col_miss[k] : col_low[k];
			}
			// Exact per pixel rays when idle, per triangle estimate while orbiting
			bake_rays_raster(&v, rgba, tri->p, tri->p + 3, tri->p + 6, col, 1.0, show_miss && !fast ? tri : NULL, col_miss);
		}
	}
	if (show_high && bake_rays_hp.pos != NULL) {
		for (i32 i = 0; i < bake_rays_hp.count; ++i) {
			f32 *p = bake_rays_hp.pos + i * 9;
			bake_rays_raster(&v, rgba, p, p + 3, p + 6, col_high, 1.0, NULL, NULL);
		}
	}
	if (show_cage) {
		// Low poly pushed along its normals by the search distance
		for (i32 i = 0; i < bake_rays_lp_count; ++i) {
			bake_rays_tri_t *tri = &bake_rays_lp[i];
			f32              q[9];
			for (i32 j = 0; j < 9; ++j) {
				q[j] = tri->p[j] + tri->a[j] * bake_rays_dist;
			}
			bake_rays_raster(&v, rgba, q, q + 3, q + 6, col_cage, 0.18, NULL, NULL);
		}
	}
}

// High poly loading, the file is read straight into the ray tracer and never added to the scene

static void bake_maps_free_raw_array(void *a) {
	if (a != NULL) {
		free(((i16_array_t *)a)->buffer);
		free(a);
	}
}

static raw_mesh_t_array_t *bake_maps_parse_high(char *path) {
	raw_mesh_t_array_t *parts = any_array_create_from_raw((void *[]){}, 0);
	if (ends_with(to_lower_case(path), ".obj")) {
		buffer_t *b = data_get_blob(path);
		if (b == NULL) {
			return parts;
		}
		raw_mesh_t *part = obj_parse(b, 'o', 0, false);
		any_array_push(parts, part);
		while (part->has_next) {
			part = obj_parse(b, 'o', part->pos, false);
			if (part->inda->length > 0) {
				any_array_push(parts, part);
			}
		}
		data_delete_blob(path);
		return parts;
	}
	char *ext                           = to_lower_case(path + string_last_index_of(path, ".") + 1);
	raw_mesh_t *(*importer)(char *path) = any_map_get(import_mesh_importers, ext);
	if (importer == NULL) {
		return parts;
	}
	raw_mesh_t *mesh = importer(path);
	while (mesh != NULL) {
		any_array_push(parts, mesh);
		mesh = mesh->has_next ? importer(path) : NULL;
	}
	return parts;
}

// Script api

static f32 bake_maps_cage_dist = 0.02; // Relative to the low poly bounds diagonal

static void bake_maps_update_dist() {
	f32 diag[3];
	bake_rays_v3_sub(diag, bake_rays_lp_max, bake_rays_lp_min);
	f32 dist = sqrtf(bake_rays_v3_dot(diag, diag)) * bake_maps_cage_dist;
	if (dist != bake_rays_dist) {
		bake_rays_dist       = dist;
		bake_rays_miss_dirty = true;
		bake_rays_version++;
	}
}

static i32 bake_maps_arg_i(minic_val_t *a, int n, int i) {
	return i < n ? (i32)minic_val_to_d(a[i]) : 0;
}

static f32 bake_maps_arg_f(minic_val_t *a, int n, int i) {
	return i < n ? (f32)minic_val_to_d(a[i]) : 0.0;
}

static char *bake_maps_arg_s(minic_val_t *a, int n, int i) {
	return i < n ? minic_val_to_ptr(a[i]) : NULL;
}

// int bake_maps_load_high(char *path): high poly triangle count, 0 when the file failed to load
static minic_val_t bake_maps_load_high(minic_val_t *a, int n) {
	char *path = bake_maps_arg_s(a, n, 0);
	bake_rays_set_high_raw(NULL);
	if (path == NULL || path[0] == '\0') {
		return minic_val_int(0);
	}
	raw_mesh_t_array_t *parts = bake_maps_parse_high(path);
	bool                ok    = bake_rays_set_high_raw(parts);
	// The importers malloc the raw meshes, nothing else references them
	for (i32 i = 0; i < parts->length; ++i) {
		raw_mesh_t *m = parts->buffer[i];
		bake_maps_free_raw_array(m->posa);
		bake_maps_free_raw_array(m->nora);
		bake_maps_free_raw_array(m->texa);
		bake_maps_free_raw_array(m->texa1);
		bake_maps_free_raw_array(m->cola);
		bake_maps_free_raw_array(m->inda);
		free(m);
	}
	if (!ok) {
		console_error(string("Failed to load high poly mesh: %s", path));
		return minic_val_int(0);
	}
	return minic_val_int(bake_rays_hp.count);
}

// float bake_maps_high_fit(): size of the high poly relative to the low poly, 0 when the bounds do not overlap
static minic_val_t bake_maps_high_fit(minic_val_t *a, int n) {
	if (bake_rays_hp.count == 0 || bake_rays_lp_count == 0) {
		return minic_val_float(1.0);
	}
	for (i32 k = 0; k < 3; ++k) {
		if (bake_rays_hp_min[k] > bake_rays_lp_max[k] || bake_rays_hp_max[k] < bake_rays_lp_min[k]) {
			return minic_val_float(0.0);
		}
	}
	f32 dl[3], dh[3];
	bake_rays_v3_sub(dl, bake_rays_lp_max, bake_rays_lp_min);
	bake_rays_v3_sub(dh, bake_rays_hp_max, bake_rays_hp_min);
	f32 l = sqrtf(bake_rays_v3_dot(dl, dl));
	return minic_val_float(l > 0.0 ? sqrtf(bake_rays_v3_dot(dh, dh)) / l : 1.0);
}

// int bake_maps_set_low(): reads the paint objects, low poly triangle count, 0 without uvs
static minic_val_t bake_maps_set_low(minic_val_t *a, int n) {
	bake_rays_free_images();
	bool ok = bake_rays_set_low();
	bake_maps_update_dist();
	return minic_val_int(ok ? bake_rays_lp_count : 0);
}

// Identifies the paint objects the low poly was read from, to reread it after an import

static u64 bake_maps_paint_objects_key() {
	mesh_object_t_array_t *lows = g_project->_->paint_objects;
	u64                    key  = lows->length;
	for (i32 i = 0; i < lows->length; ++i) {
		mesh_data_t *d = lows->buffer[i]->data;
		key            = key * 31 + (u64)(uintptr_t)d;
		key            = key * 31 + (d != NULL && d->index_array != NULL ? d->index_array->length : 0);
	}
	return key;
}

// void bake_maps_set_cage(float dist, bool average): ray search distance relative to the low poly size,
// and rays along normals averaged over hard edges
static minic_val_t bake_maps_set_cage(minic_val_t *a, int n) {
	bake_maps_cage_dist = bake_maps_arg_f(a, n, 0);
	bool average        = bake_maps_arg_i(a, n, 1) != 0;
	if (average != bake_rays_average) {
		bake_rays_join();
		bake_rays_average = average;
		bake_rays_update_ray_normals();
	}
	bake_maps_update_dist();
	return minic_val_int(0);
}

// void bake_maps_set_ao(float strength, float radius, float offset)
static minic_val_t bake_maps_set_ao(minic_val_t *a, int n) {
	bake_rays_settings.ao_strength = bake_maps_arg_f(a, n, 0);
	bake_rays_settings.ray_max     = bake_maps_arg_f(a, n, 1) * 10.0;
	bake_rays_settings.ray_min     = bake_maps_arg_f(a, n, 2) * 0.01;
	return minic_val_int(0);
}

// void bake_maps_set_curvature(float strength, float radius, float offset, int smooth)
static minic_val_t bake_maps_set_curvature(minic_val_t *a, int n) {
	bake_rays_settings.curv_strength = bake_maps_arg_f(a, n, 0);
	bake_rays_settings.curv_radius   = bake_maps_arg_f(a, n, 1);
	bake_rays_settings.curv_offset   = bake_maps_arg_f(a, n, 2);
	bake_rays_settings.curv_smooth   = bake_maps_arg_i(a, n, 3);
	return minic_val_int(0);
}

// void bake_maps_set_cavity(float detail_texels, float contrast)
static minic_val_t bake_maps_set_cavity(minic_val_t *a, int n) {
	bake_rays_settings.cavity_texels   = bake_maps_arg_f(a, n, 0);
	bake_rays_settings.cavity_contrast = bake_maps_arg_f(a, n, 1);
	return minic_val_int(0);
}

// Low poly geometry, uv layout and occlusion bvh, kept for the following maps and the inpaint tool
static bool bake_rays_prepare_layout(i32 w, i32 h) {
	if (bake_rays_texmap != NULL && bake_rays_w == w && bake_rays_h == h && bake_rays_lpb.count > 0) {
		return true;
	}
	bake_rays_free_images();
	bake_rays_free_bvh(&bake_rays_lpb);
	if (bake_rays_lp == NULL && !bake_rays_set_low()) {
		return false;
	}
	bake_maps_update_dist();
	bake_rays_lpb.count = bake_rays_lp_count;
	bake_rays_lpb.pos   = malloc(sizeof(f32) * 9 * bake_rays_lp_count);
	bake_rays_lpb.nor   = malloc(sizeof(f32) * 9 * bake_rays_lp_count);
	bake_rays_lpb.index = malloc(sizeof(i32) * bake_rays_lp_count);
	for (i32 i = 0; i < bake_rays_lp_count; ++i) {
		memcpy(bake_rays_lpb.pos + i * 9, bake_rays_lp[i].p, sizeof(f32) * 9);
		memcpy(bake_rays_lpb.nor + i * 9, bake_rays_lp[i].n, sizeof(f32) * 9);
		bake_rays_lpb.index[i] = i;
	}
	bake_rays_build_bvh(&bake_rays_lpb);
	bake_rays_w      = w;
	bake_rays_h      = h;
	bake_rays_texmap = malloc(sizeof(i32) * w * h);
	bake_rays_out    = malloc(w * h * 4);
	bake_rays_mask   = malloc(w * h);
	bake_rays_build_texmap();
	return true;
}

// bool bake_maps_start(int type, int rays, int axis, int up_axis, bool use_high, int w, int h):
// starts one map on worker threads, type is a bake_type_t or 14 for cavity
static bool bake_maps_gpu_last; // Defined with the gpu bake below

static minic_val_t bake_maps_start(minic_val_t *a, int n) {
	bake_maps_gpu_last = false;
	i32  type     = bake_maps_arg_i(a, n, 0);
	bool use_high = bake_maps_arg_i(a, n, 4) != 0 && bake_rays_hp.count > 0;
	i32  w        = bake_maps_arg_i(a, n, 5);
	i32  h        = bake_maps_arg_i(a, n, 6);
	bake_rays_join();
	if (w <= 0 || h <= 0) {
		return minic_val_int(0);
	}

	if (!bake_rays_prepare_layout(w, h)) {
		return minic_val_int(0);
	}

	bake_rays_job          = bake_rays_settings;
	bake_rays_job.type     = type;
	bake_rays_job.use_high = use_high;
	bake_rays_job.rays     = bake_rays_imax(bake_maps_arg_i(a, n, 1), 1);
	bake_rays_job.axis     = bake_maps_arg_i(a, n, 2);
	bake_rays_job.up_y     = bake_maps_arg_i(a, n, 3) == BAKE_UP_AXIS_Y;
	if (type == BAKE_TYPE_MATERIALID) {
		bake_rays_read_matid();
	}
	if (type == BAKE_TYPE_CAVITY) {
		free(bake_rays_cav);
		bake_rays_cav = calloc(w * h, sizeof(f32));
	}

	// Empty texels, flat normal and mid height stay neutral
	u8 c = type == BAKE_TYPE_HEIGHT ? 128 : 0;
	for (i32 i = 0; i < w * h; ++i) {
		u8 *o = bake_rays_out + i * 4;
		o[0]  = type == BAKE_TYPE_NORMAL ? 128 : c;
		o[1]  = type == BAKE_TYPE_NORMAL ? 128 : c;
		o[2]  = type == BAKE_TYPE_NORMAL ? 255 : c;
		o[3]  = 255;
	}
	memset(bake_rays_mask, 0, w * h);

	if (!bake_rays_mutex_ready) {
		iron_mutex_init(&bake_rays_mutex);
		bake_rays_mutex_ready = true;
	}
	bake_rays_next_row  = 0;
	bake_rays_rows_done = 0;
	bake_rays_cancelled = false;
	bake_rays_working   = true;
	// Leave a core for the interface
	i32 threads            = bake_rays_imin(bake_rays_imax(iron_hardware_threads() - 1, 1), BAKE_RAYS_MAX_THREADS);
	bake_rays_thread_count = threads;
	for (i32 i = 0; i < threads; ++i) {
		iron_thread_init(&bake_rays_threads[i], bake_rays_worker, NULL);
	}
	return minic_val_int(1);
}

// Raytraced maps can bake on the gpu through the core bake path (Bake Texture node), from the low poly
// at the layer resolution. step, progress, cancel and save then work on that bake.
static render_target_t *bake_maps_gpu_rt        = NULL;
static bool             bake_maps_gpu_running   = false;
static bool             bake_maps_gpu_cancelled = false;
static bool             bake_maps_gpu_last      = false; // The finished map is the gpu one
static u8              *bake_maps_gpu_out       = NULL;  // RGBA8 like bake_rays_out
static i32              bake_maps_gpu_w         = 0;
static i32              bake_maps_gpu_h         = 0;
static i32              bake_maps_gpu_samples   = 1;
static i32              bake_maps_gpu_saved_samples;
static f32              bake_maps_gpu_saved_ao[3];

static void bake_maps_gpu_restore() {
	g_context->bake_samples     = bake_maps_gpu_saved_samples;
	g_context->bake_ao_strength = bake_maps_gpu_saved_ao[0];
	g_context->bake_ao_radius   = bake_maps_gpu_saved_ao[1];
	g_context->bake_ao_offset   = bake_maps_gpu_saved_ao[2];
}

static void bake_maps_gpu_done(void *_) {
	bake_maps_gpu_running = false;
	bake_maps_gpu_restore();
	if (bake_maps_gpu_cancelled) {
		return;
	}
	gpu_texture_t *t      = bake_maps_gpu_rt->_image;
	buffer_t      *pixels = gpu_get_texture_pixels(t);
	if (bake_maps_gpu_w != t->width || bake_maps_gpu_h != t->height) {
		free(bake_maps_gpu_out);
		bake_maps_gpu_out = malloc(t->width * t->height * 4);
		bake_maps_gpu_w   = t->width;
		bake_maps_gpu_h   = t->height;
	}
	memcpy(bake_maps_gpu_out, pixels->buffer, t->width * t->height * 4);
#ifdef IRON_BGRA
	buffer_t out = {.buffer = bake_maps_gpu_out, .length = t->width * t->height * 4};
	buffer_bgra_swap(&out); // Render targets read back in the native order
#endif
	bake_maps_gpu_last = true;
}

// bool bake_maps_gpu_supported(): raytraced maps can bake on the gpu
static minic_val_t bake_maps_gpu_supported(minic_val_t *a, int n) {
	return minic_val_int(gpu_raytrace_supported() ? 1 : 0);
}

// int bake_maps_gpu_res(): gpu maps bake at the layer resolution
static minic_val_t bake_maps_gpu_res(minic_val_t *a, int n) {
	return minic_val_int(config_get_texture_res_x());
}

// bool bake_maps_gpu_start(int type, int samples, float ao_strength, float ao_radius, float ao_offset):
// starts a raytraced map (occlusion, bent normal, thickness) on the gpu, 64 rays per sample
static minic_val_t bake_maps_gpu_start(minic_val_t *a, int n) {
	if (bake_texture_node_baking || bake_rays_working || !gpu_raytrace_supported()) {
		return minic_val_int(0);
	}
	i32 w = config_get_texture_res_x();
	i32 h = config_get_texture_res_y();
	if (bake_maps_gpu_rt == NULL) {
		bake_maps_gpu_rt         = render_target_create();
		bake_maps_gpu_rt->name   = "bake_maps_gpu";
		bake_maps_gpu_rt->width  = w;
		bake_maps_gpu_rt->height = h;
		bake_maps_gpu_rt->format = "RGBA32";
		render_path_create_render_target(bake_maps_gpu_rt);
	}
	else if (bake_maps_gpu_rt->width != w || bake_maps_gpu_rt->height != h) {
		gpu_delete_texture(bake_maps_gpu_rt->_image);
		bake_maps_gpu_rt->width  = w;
		bake_maps_gpu_rt->height = h;
		bake_maps_gpu_rt->_image = gpu_create_render_target(w, h, GPU_TEXTURE_FORMAT_RGBA32);
	}

	bake_maps_gpu_saved_samples = g_context->bake_samples;
	bake_maps_gpu_saved_ao[0]   = g_context->bake_ao_strength;
	bake_maps_gpu_saved_ao[1]   = g_context->bake_ao_radius;
	bake_maps_gpu_saved_ao[2]   = g_context->bake_ao_offset;
	bake_maps_gpu_samples       = bake_rays_imax(bake_maps_arg_i(a, n, 1), 1);
	g_context->bake_samples     = bake_maps_gpu_samples;
	g_context->bake_ao_strength = bake_maps_arg_f(a, n, 2);
	g_context->bake_ao_radius   = bake_maps_arg_f(a, n, 3);
	g_context->bake_ao_offset   = bake_maps_arg_f(a, n, 4);

	bake_maps_gpu_cancelled = false;
	bake_maps_gpu_last      = false;
	bake_maps_gpu_running   = true;
	if (!bake_to_target(bake_maps_gpu_rt, bake_maps_arg_i(a, n, 0), bake_maps_gpu_done, NULL)) {
		bake_maps_gpu_running = false;
		bake_maps_gpu_restore();
		return minic_val_int(0);
	}
	return minic_val_int(1);
}

// float bake_maps_progress(): 0..1 for the current map
static minic_val_t bake_maps_progress(minic_val_t *a, int n) {
	if (bake_maps_gpu_running) {
		return minic_val_float(bake_rays_fmin(render_path_raytrace_bake_current_sample / (f32)bake_maps_gpu_samples, 1.0));
	}
	if (!bake_rays_working || bake_rays_h == 0) {
		return minic_val_float(1.0);
	}
	iron_mutex_lock(&bake_rays_mutex);
	f32 p = bake_rays_rows_done / (f32)bake_rays_h;
	iron_mutex_unlock(&bake_rays_mutex);
	return minic_val_float(p);
}

// bool bake_maps_step(): true once the current map is done, poll every update
static minic_val_t bake_maps_step(minic_val_t *a, int n) {
	if (bake_maps_gpu_running) {
		return minic_val_int(0);
	}
	if (!bake_rays_working) {
		return minic_val_int(1);
	}
	iron_mutex_lock(&bake_rays_mutex);
	bool done = bake_rays_rows_done >= bake_rays_h;
	iron_mutex_unlock(&bake_rays_mutex);
	if (!done) {
		return minic_val_int(0);
	}
	bake_rays_join();
	if (bake_rays_job.type == BAKE_TYPE_CURVATURE) {
		for (i32 i = 0; i < bake_rays_job.curv_smooth; ++i) {
			bake_rays_blur();
		}
	}
	if (bake_rays_job.type == BAKE_TYPE_CAVITY) {
		bake_rays_cavity_levels();
	}
	bake_rays_dilate(16);
	return minic_val_int(1);
}

// void bake_maps_cancel()
static minic_val_t bake_maps_cancel(minic_val_t *a, int n) {
	if (bake_maps_gpu_running) {
		bake_maps_gpu_cancelled = true;
		g_context->pdirty       = 0; // The core bake finishes on the next update
	}
	bake_rays_cancelled = true;
	bake_rays_join();
	return minic_val_int(0);
}

// bool bake_maps_save(char *folder, char *file_name): writes the finished map as png and adds it to the
// project textures, or reloads the texture when it is already there
static minic_val_t bake_maps_save(minic_val_t *a, int n) {
	char *folder    = bake_maps_arg_s(a, n, 0);
	char *file_name = bake_maps_arg_s(a, n, 1);
	if ((bake_maps_gpu_last ? bake_maps_gpu_out : bake_rays_out) == NULL || folder == NULL || file_name == NULL) {
		return minic_val_int(0);
	}
	u8      *out    = bake_maps_gpu_last ? bake_maps_gpu_out : bake_rays_out;
	i32      w      = bake_maps_gpu_last ? bake_maps_gpu_w : bake_rays_w;
	i32      h      = bake_maps_gpu_last ? bake_maps_gpu_h : bake_rays_h;
	char    *file   = string("%s%s%s", folder, PATH_SEP, file_name);
	buffer_t pixels = {.buffer = out, .length = w * h * 4};
#ifdef IRON_BGRA
	buffer_bgra_swap(&pixels); // The png writer takes the native order, the bake is RGBA
#endif
	iron_write_png(file, &pixels, w, h, 0);
#ifdef IRON_BGRA
	buffer_bgra_swap(&pixels);
#endif

	asset_t_array_t *assets = g_project->_->assets;
	for (i32 i = 0; i < assets->length; ++i) {
		if (string_equals(assets->buffer[i]->file, file)) {
			project_reimport_texture(assets->buffer[i]);
			return minic_val_int(1);
		}
	}
	import_texture_run(file, false);
	return minic_val_int(1);
}

static gpu_texture_t *bake_maps_preview_image = NULL;
static gpu_texture_t *bake_maps_preview_old   = NULL;
static u8            *bake_maps_preview_pixels;
static i32            bake_maps_preview_size = 0;
static f32            bake_maps_preview_args[10];
static u32            bake_maps_preview_version;

static void bake_maps_free_preview() {
	if (bake_maps_preview_old != NULL) {
		gpu_delete_texture(bake_maps_preview_old);
	}
	bake_maps_preview_old   = bake_maps_preview_image;
	bake_maps_preview_image = NULL;
}

// gpu_texture_t *bake_maps_preview(int w, int h, float yaw, float pitch, float zoom, bool show_low, bool show_high,
// bool show_cage, bool show_miss, bool fast): software render of the low poly, high poly and cage,
// the low poly is reread when the paint objects changed. Call it every frame, it renders only on changes.
static minic_val_t bake_maps_preview(minic_val_t *a, int n) {
	i32 w = bake_maps_arg_i(a, n, 0);
	i32 h = bake_maps_arg_i(a, n, 1);
	if (w <= 0 || h <= 0) {
		return minic_val_ptr(NULL);
	}
	if (bake_maps_preview_size != w * h) {
		free(bake_maps_preview_pixels);
		bake_maps_preview_pixels = malloc(w * h * 4);
		bake_maps_preview_size   = w * h;
	}
	if (bake_rays_lp == NULL || bake_maps_low_key != bake_maps_paint_objects_key()) {
		bake_rays_free_images();
		bake_rays_set_low();
	}
	bake_maps_update_dist();

	// Rendered again only when something changed, call it every frame
	bool same = bake_maps_preview_image != NULL && bake_maps_preview_version == bake_rays_version;
	for (i32 i = 0; i < 10; ++i) {
		f32 f                     = bake_maps_arg_f(a, n, i);
		same                      = same && bake_maps_preview_args[i] == f;
		bake_maps_preview_args[i] = f;
	}
	if (same) {
		return minic_val_ptr(bake_maps_preview_image);
	}
	bake_maps_preview_version = bake_rays_version;

	bake_rays_preview(bake_maps_preview_pixels, w, h, bake_maps_arg_f(a, n, 2), bake_maps_arg_f(a, n, 3), bake_maps_arg_f(a, n, 4),
	                  bake_maps_arg_i(a, n, 5) != 0, bake_maps_arg_i(a, n, 6) != 0, bake_maps_arg_i(a, n, 7) != 0, bake_maps_arg_i(a, n, 8) != 0,
	                  bake_maps_arg_i(a, n, 9) != 0);
	// The previous image may still be referenced by this frame, release it one call later
	bake_maps_free_preview();
	buffer_t b              = {.buffer = bake_maps_preview_pixels, .length = w * h * 4};
	bake_maps_preview_image = gpu_create_texture_from_bytes(&b, w, h, GPU_TEXTURE_FORMAT_RGBA32);
	return minic_val_ptr(bake_maps_preview_image);
}

// float bake_maps_miss_ratio(): fraction of the sample rays missing the high poly
static minic_val_t bake_maps_miss_ratio(minic_val_t *a, int n) {
	if (bake_rays_lp == NULL || bake_rays_lp_count == 0) {
		return minic_val_float(0.0);
	}
	bake_maps_update_dist();
	bake_rays_update_miss();
	f32 sum = 0.0;
	for (i32 i = 0; i < bake_rays_lp_count; ++i) {
		sum += bake_rays_lp_miss[i];
	}
	return minic_val_float(sum / bake_rays_lp_count);
}

// void bake_maps_end_images(): releases the baked images, geometry stays cached for the preview
static minic_val_t bake_maps_end_images(minic_val_t *a, int n) {
	bake_rays_join();
	bake_rays_free_images();
	bake_rays_free_bvh(&bake_rays_lpb);
	return minic_val_int(0);
}

// void bake_maps_end(): releases everything, the high poly can take gigabytes
static minic_val_t bake_maps_end(minic_val_t *a, int n) {
	free(bake_maps_gpu_out);
	bake_maps_gpu_out  = NULL;
	bake_maps_gpu_w    = 0;
	bake_maps_gpu_h    = 0;
	bake_maps_gpu_last = false;
	bake_rays_cancelled = true;
	bake_rays_join();
	bake_rays_free_images();
	bake_rays_free_low();
	bake_rays_free_bvh(&bake_rays_hp);
	free(bake_rays_zbuf);
	bake_rays_zbuf   = NULL;
	bake_rays_zbuf_n = 0;
	free(bake_maps_preview_pixels);
	bake_maps_preview_pixels = NULL;
	bake_maps_preview_size   = 0;
	bake_maps_free_preview();
	return minic_val_int(0);
}

// Inpaint tool: a mask is painted on the mesh, then the masked texels of the baked maps are rebuilt
// from the surrounding texels of the same uv island and the png files are rewritten.
// Copy mode synthesizes texture from nearby patches, smooth mode diffuses the border inwards.

#define INPAINT_MODE_COPY 0
#define INPAINT_PATCH     4 // Patch half size, 9x9 patches
#define INPAINT_PREVIEW   1024

static u8            *inpaint_mask     = NULL; // At the bake layout resolution
static bool           inpaint_has_mask = false;
static bool           inpaint_has_last = false;
static f32            inpaint_last[3];
static bool           inpaint_dirty       = false;
static gpu_texture_t *inpaint_preview     = NULL;
static gpu_texture_t *inpaint_preview_old = NULL;

typedef struct bake_rays_ukey {
	i64 k[5]; // Quantized position and uv
	i32 corner;
} bake_rays_ukey_t;

static int bake_rays_ukey_compare(const void *a, const void *b) {
	const bake_rays_ukey_t *ka = a;
	const bake_rays_ukey_t *kb = b;
	for (i32 i = 0; i < 5; ++i) {
		if (ka->k[i] != kb->k[i]) {
			return ka->k[i] < kb->k[i] ? -1 : 1;
		}
	}
	return 0;
}

typedef struct bake_rays_uedge {
	i32 a, b, tri;
} bake_rays_uedge_t;

static int bake_rays_uedge_compare(const void *x, const void *y) {
	const bake_rays_uedge_t *ea = x;
	const bake_rays_uedge_t *eb = y;
	if (ea->a != eb->a) {
		return ea->a < eb->a ? -1 : 1;
	}
	return ea->b < eb->b ? -1 : ea->b > eb->b ? 1 : 0;
}

static i32 bake_rays_find(i32 *parent, i32 i) {
	while (parent[i] != i) {
		parent[i] = parent[parent[i]];
		i         = parent[i];
	}
	return i;
}

// Triangles sharing an edge with the same uvs on both sides belong to the same island
static void bake_rays_build_islands() {
	free(bake_rays_island);
	i32 n            = bake_rays_lp_count;
	bake_rays_island = malloc(sizeof(i32) * n);
	f32 diag[3];
	bake_rays_v3_sub(diag, bake_rays_lp_max, bake_rays_lp_min);
	f32 eps = bake_rays_fmax(sqrtf(bake_rays_v3_dot(diag, diag)) * 1e-5, 1e-6);

	bake_rays_ukey_t *keys = malloc(sizeof(bake_rays_ukey_t) * n * 3);
	for (i32 i = 0; i < n * 3; ++i) {
		bake_rays_tri_t *tri = &bake_rays_lp[i / 3];
		f32             *p   = tri->p + (i % 3) * 3;
		f32             *uv  = tri->uv + (i % 3) * 2;
		for (i32 k = 0; k < 3; ++k) {
			keys[i].k[k] = (i64)llroundf(p[k] / eps);
		}
		keys[i].k[3]   = (i64)llroundf(uv[0] * 1e5);
		keys[i].k[4]   = (i64)llroundf(uv[1] * 1e5);
		keys[i].corner = i;
	}
	qsort(keys, n * 3, sizeof(bake_rays_ukey_t), bake_rays_ukey_compare);
	i32 *corner_id = malloc(sizeof(i32) * n * 3);
	i32  id        = -1;
	for (i32 i = 0; i < n * 3; ++i) {
		if (i == 0 || bake_rays_ukey_compare(&keys[i - 1], &keys[i]) != 0) {
			id++;
		}
		corner_id[keys[i].corner] = id;
	}
	free(keys);

	bake_rays_uedge_t *edges = malloc(sizeof(bake_rays_uedge_t) * n * 3);
	for (i32 t = 0; t < n; ++t) {
		for (i32 v = 0; v < 3; ++v) {
			i32 a            = corner_id[t * 3 + v];
			i32 b            = corner_id[t * 3 + (v + 1) % 3];
			edges[t * 3 + v] = (bake_rays_uedge_t){.a = bake_rays_imin(a, b), .b = bake_rays_imax(a, b), .tri = t};
		}
	}
	free(corner_id);
	qsort(edges, n * 3, sizeof(bake_rays_uedge_t), bake_rays_uedge_compare);
	for (i32 i = 0; i < n; ++i) {
		bake_rays_island[i] = i;
	}
	for (i32 i = 1; i < n * 3; ++i) {
		if (edges[i].a == edges[i - 1].a && edges[i].b == edges[i - 1].b) {
			i32 ra = bake_rays_find(bake_rays_island, edges[i].tri);
			i32 rb = bake_rays_find(bake_rays_island, edges[i - 1].tri);
			if (ra != rb) {
				bake_rays_island[ra] = rb;
			}
		}
	}
	free(edges);
	for (i32 i = 0; i < n; ++i) {
		bake_rays_island[i] = bake_rays_find(bake_rays_island, i);
	}
}

// UV island of a texel, -1 when no triangle covers it
static i32 bake_rays_texel_island(i32 i) {
	i32 t = bake_rays_texmap[i];
	return t >= 0 ? bake_rays_island[t] : -1;
}

// Marks the texels whose surface point lies within radius of the center
static void bake_rays_stamp(u8 *mask, f32 *center, f32 radius) {
	i32 w = bake_rays_w;
	i32 h = bake_rays_h;
	for (i32 ti = 0; ti < bake_rays_lp_count; ++ti) {
		bake_rays_tri_t *tri = &bake_rays_lp[ti];
		bool             out = false;
		for (i32 k = 0; k < 3 && !out; ++k) {
			f32 lo = bake_rays_fmin(tri->p[k], bake_rays_fmin(tri->p[3 + k], tri->p[6 + k]));
			f32 hi = bake_rays_fmax(tri->p[k], bake_rays_fmax(tri->p[3 + k], tri->p[6 + k]));
			out    = center[k] + radius < lo || center[k] - radius > hi;
		}
		if (out) {
			continue;
		}
		f32 *uv   = tri->uv;
		i32  minx = bake_rays_imax(0, (i32)floorf(bake_rays_fmin(uv[0], bake_rays_fmin(uv[2], uv[4])) * w));
		i32  maxx = bake_rays_imin(w - 1, (i32)ceilf(bake_rays_fmax(uv[0], bake_rays_fmax(uv[2], uv[4])) * w));
		i32  miny = bake_rays_imax(0, (i32)floorf(bake_rays_fmin(uv[1], bake_rays_fmin(uv[3], uv[5])) * h));
		i32  maxy = bake_rays_imin(h - 1, (i32)ceilf(bake_rays_fmax(uv[1], bake_rays_fmax(uv[3], uv[5])) * h));
		for (i32 y = miny; y <= maxy; ++y) {
			for (i32 x = minx; x <= maxx; ++x) {
				i32 i = y * w + x;
				if (bake_rays_texmap[i] != ti || mask[i]) {
					continue;
				}
				bake_rays_surf_t s;
				bake_rays_surface_at(ti, x + 0.5, y + 0.5, false, &s);
				f32 d[3];
				bake_rays_v3_sub(d, s.p, center);
				if (bake_rays_v3_dot(d, d) <= radius * radius) {
					mask[i] = 1;
				}
			}
		}
	}
}

// Mouse ray against the low poly, in the bake space of the paint object
static bool inpaint_hit(f32 *hit) {
	mesh_object_t *o = g_context->paint_object;
	if (o == NULL || bake_rays_lpb.count == 0) {
		return false;
	}
	ray_t *ray    = raycast_get_ray(mouse_view_x(), mouse_view_y(), scene_camera);
	mat4_t inv    = mat4_inv(o->base->transform->world);
	vec4_t a      = vec4_apply_mat4((vec4_t){ray->origin.x, ray->origin.y, ray->origin.z, 1.0}, inv);
	vec4_t b      = vec4_apply_mat4((vec4_t){ray->origin.x + ray->dir.x, ray->origin.y + ray->dir.y, ray->origin.z + ray->dir.z, 1.0}, inv);
	f32    org[3] = {a.x, a.y, a.z};
	f32    dir[3] = {b.x - a.x, b.y - a.y, b.z - a.z};
	bake_rays_v3_norm(dir);
	f32 t, u, v;
	if (bake_rays_trace(&bake_rays_lpb, org, dir, 0.0, 1e30, -1.0, false, false, &t, &u, &v) < 0) {
		return false;
	}
	for (i32 k = 0; k < 3; ++k) {
		hit[k] = org[k] + dir[k] * t;
	}
	return true;
}

// World brush radius, as the paint shader computes it, converted to the bake space
static f32 inpaint_radius() {
	f32    world = g_context->brush_radius * g_context->brush_nodes_radius / 15.0 * 2.0;
	mat4_t m     = g_context->paint_object->base->transform->world;
	f32    sx    = sqrtf(m.m[0] * m.m[0] + m.m[1] * m.m[1] + m.m[2] * m.m[2]);
	return sx > 0.0 ? world / sx : world;
}

// bool bake_maps_inpaint_begin(char *path): uv layout at the resolution of the baked map, false without uvs
static minic_val_t bake_maps_inpaint_begin(minic_val_t *a, int n) {
	char    *path  = bake_maps_arg_s(a, n, 0);
	asset_t *asset = NULL;
	for (i32 i = 0; path != NULL && i < g_project->_->assets->length; ++i) {
		asset_t *as = g_project->_->assets->buffer[i];
		if (as->image != NULL && strcmp(as->file, path) == 0) {
			asset = as;
		}
	}
	if (asset == NULL || bake_rays_working) {
		return minic_val_int(0);
	}
	i32 w = asset->image->width;
	i32 h = asset->image->height;
	if (bake_rays_lp != NULL && bake_maps_low_key != bake_maps_paint_objects_key()) {
		bake_rays_free_images();
		bake_rays_free_low(); // Paint objects changed
	}
	bool same = bake_rays_texmap != NULL && bake_rays_w == w && bake_rays_h == h;
	if (!bake_rays_prepare_layout(w, h)) {
		return minic_val_int(0);
	}
	if (bake_rays_island == NULL) {
		bake_rays_build_islands();
	}
	if (!same || inpaint_mask == NULL) {
		free(inpaint_mask);
		inpaint_mask     = calloc(w * h, 1);
		inpaint_has_mask = false;
	}
	return minic_val_int(1);
}

// void bake_maps_inpaint_stroke(): stamps the mask from the last mouse hit to the current one
static minic_val_t bake_maps_inpaint_stroke(minic_val_t *a, int n) {
	f32 hit[3];
	if (inpaint_mask == NULL || bake_rays_texmap == NULL || !inpaint_hit(hit)) {
		return minic_val_int(0);
	}
	f32 r = inpaint_radius();
	if (inpaint_has_last) {
		// Stamps along the segment so fast strokes stay continuous
		f32 d[3]  = {hit[0] - inpaint_last[0], hit[1] - inpaint_last[1], hit[2] - inpaint_last[2]};
		f32 len   = sqrtf(bake_rays_v3_dot(d, d));
		i32 steps = bake_rays_imin((i32)(len / bake_rays_fmax(r * 0.4, 1e-6)), 64);
		for (i32 s = 1; s <= steps; ++s) {
			f32 t    = s / (f32)(steps + 1);
			f32 c[3] = {inpaint_last[0] + d[0] * t, inpaint_last[1] + d[1] * t, inpaint_last[2] + d[2] * t};
			bake_rays_stamp(inpaint_mask, c, r);
		}
	}
	bake_rays_stamp(inpaint_mask, hit, r);
	memcpy(inpaint_last, hit, sizeof(f32) * 3);
	inpaint_has_last = true;
	inpaint_has_mask = true;
	inpaint_dirty    = true;
	return minic_val_int(0);
}

// gpu_texture_t *bake_maps_inpaint_mask(): the painted mask in the red channel, for the viewport overlay
static minic_val_t bake_maps_inpaint_mask(minic_val_t *a, int n) {
	if (!inpaint_dirty && inpaint_preview != NULL) {
		return minic_val_ptr(inpaint_preview);
	}
	i32 w  = inpaint_mask != NULL ? bake_rays_w : 1;
	i32 h  = inpaint_mask != NULL ? bake_rays_h : 1;
	i32 pw = bake_rays_imin(w, INPAINT_PREVIEW);
	i32 ph = bake_rays_imin(h, INPAINT_PREVIEW);
	u8 *p  = calloc(pw * ph * 4, 1);
	for (i32 y = 0; inpaint_mask != NULL && y < h; ++y) {
		for (i32 x = 0; x < w; ++x) {
			if (inpaint_mask[y * w + x]) {
				i32 i = ((y * ph / h) * pw + x * pw / w) * 4;
				p[i]  = 255;
			}
		}
	}
	for (i32 i = 0; i < pw * ph; ++i) {
		p[i * 4 + 3] = 255;
	}
	buffer_t b = {.buffer = p, .length = pw * ph * 4};
	// The previous texture may still be bound this frame, release it one update later
	if (inpaint_preview_old != NULL) {
		gpu_delete_texture(inpaint_preview_old);
	}
	inpaint_preview_old = inpaint_preview;
	inpaint_preview     = gpu_create_texture_from_bytes(&b, pw, ph, GPU_TEXTURE_FORMAT_RGBA32);
	free(p);
	inpaint_dirty = false;
	return minic_val_ptr(inpaint_preview);
}

// void bake_maps_inpaint_clear(): clears the mask, the next stroke starts a new line
static minic_val_t bake_maps_inpaint_clear(minic_val_t *a, int n) {
	if (inpaint_mask != NULL) {
		memset(inpaint_mask, 0, bake_rays_w * bake_rays_h);
	}
	inpaint_has_mask = false;
	inpaint_has_last = false;
	inpaint_dirty    = true;
	return minic_val_int(0);
}

// Fill

typedef struct inpaint_job {
	u8  *px;    // RGBA8 pixels of the map being repaired
	u8  *known; // 1 for valid texels outside the mask, then for filled ones
	i32 *isl;   // Island per texel, -1 outside the uv layout
	i32 *src;   // Source texel copied into each texel, for coherent copy
	i32  w;
	i32  h;
} inpaint_job_t;

static bool inpaint_same(inpaint_job_t *j, i32 a, i32 b) {
	return j->isl[a] >= 0 && j->isl[a] == j->isl[b];
}

// Diffusion: onion peel from the border, then relaxation
static void inpaint_smooth(inpaint_job_t *j, i32 *todo, i32 count) {
	static const i32 dx[4] = {-1, 1, 0, 0};
	static const i32 dy[4] = {0, 0, -1, 1};
	i32              w = j->w, h = j->h;
	i32              left = count;
	u8              *next = malloc(count);
	for (i32 pass = 0; left > 0 && pass < 4096; ++pass) {
		memset(next, 0, count);
		i32 filled = 0;
		for (i32 n = 0; n < count; ++n) {
			i32 i = todo[n];
			if (j->known[i]) {
				continue;
			}
			i32 x = i % w, y = i / w;
			i32 sum[4] = {0, 0, 0, 0}, c = 0;
			for (i32 k = 0; k < 4; ++k) {
				i32 nx = x + dx[k], ny = y + dy[k];
				if (nx < 0 || ny < 0 || nx >= w || ny >= h) {
					continue;
				}
				i32 q = ny * w + nx;
				if (j->known[q] && inpaint_same(j, i, q)) {
					for (i32 ch = 0; ch < 4; ++ch) {
						sum[ch] += j->px[q * 4 + ch];
					}
					c++;
				}
			}
			if (c > 0) {
				for (i32 ch = 0; ch < 4; ++ch) {
					j->px[i * 4 + ch] = sum[ch] / c;
				}
				next[n] = 1;
				filled++;
			}
		}
		for (i32 n = 0; n < count; ++n) {
			if (next[n]) {
				j->known[todo[n]] = 1;
			}
		}
		left -= filled;
		if (filled == 0) {
			break;
		}
	}
	free(next);

	// Relaxation smooths the onion rings, Gauss-Seidel in place
	for (i32 it = 0; it < 200; ++it) {
		for (i32 n = 0; n < count; ++n) {
			i32 i = todo[n];
			i32 x = i % w, y = i / w;
			i32 sum[4] = {0, 0, 0, 0}, c = 0;
			for (i32 k = 0; k < 4; ++k) {
				i32 nx = x + dx[k], ny = y + dy[k];
				if (nx < 0 || ny < 0 || nx >= w || ny >= h) {
					continue;
				}
				i32 q = ny * w + nx;
				if (j->known[q] && inpaint_same(j, i, q)) {
					for (i32 ch = 0; ch < 4; ++ch) {
						sum[ch] += j->px[q * 4 + ch];
					}
					c++;
				}
			}
			if (c > 0) {
				for (i32 ch = 0; ch < 4; ++ch) {
					j->px[i * 4 + ch] = (sum[ch] + c / 2) / c;
				}
			}
		}
	}
}

// Sum of squared differences over the known texels of both patches, -1 when nothing to compare
static f32 inpaint_patch_cost(inpaint_job_t *j, i32 p, i32 s, f32 best) {
	i32 w = j->w, h = j->h;
	i32 px = p % w, py = p / w, sx = s % w, sy = s / w;
	f32 cost = 0.0;
	i32 n    = 0;
	for (i32 oy = -INPAINT_PATCH; oy <= INPAINT_PATCH; ++oy) {
		i32 ay = py + oy, by = sy + oy;
		if (ay < 0 || by < 0 || ay >= h || by >= h) {
			continue;
		}
		for (i32 ox = -INPAINT_PATCH; ox <= INPAINT_PATCH; ++ox) {
			i32 ax = px + ox, bx = sx + ox;
			if (ax < 0 || bx < 0 || ax >= w || bx >= w) {
				continue;
			}
			i32 a = ay * w + ax, b = by * w + bx;
			if (!j->known[a] || !j->known[b] || !inpaint_same(j, p, a) || !inpaint_same(j, s, b)) {
				continue;
			}
			for (i32 ch = 0; ch < 3; ++ch) {
				f32 d = (f32)j->px[a * 4 + ch] - (f32)j->px[b * 4 + ch];
				cost += d * d;
			}
			n++;
		}
		if (n > 8 && cost / n > best) {
			return 1e30; // Already worse than the best candidate
		}
	}
	return n > 0 ? cost / n : -1.0;
}

static u32 inpaint_rand_state = 1;

static u32 inpaint_rand() {
	inpaint_rand_state ^= inpaint_rand_state << 13;
	inpaint_rand_state ^= inpaint_rand_state >> 17;
	inpaint_rand_state ^= inpaint_rand_state << 5;
	return inpaint_rand_state;
}

// Texture synthesis: each texel of the fill front copies the center of the best matching
// patch, candidates are random texels around plus the continuations of the neighbours' sources
static void inpaint_copy(inpaint_job_t *j, i32 *todo, i32 count, i32 extent) {
	static const i32 dx[8] = {-1, 1, 0, 0, -1, 1, -1, 1};
	static const i32 dy[8] = {0, 0, -1, 1, -1, -1, 1, 1};
	i32              w = j->w, h = j->h;
	i32              search = bake_rays_imax(48, extent * 3);
	u8              *front  = malloc(count);
	i32              left   = count;
	for (i32 pass = 0; left > 0 && pass < 4096; ++pass) {
		memset(front, 0, count);
		for (i32 n = 0; n < count; ++n) {
			i32 i = todo[n];
			if (j->known[i]) {
				continue;
			}
			i32 x = i % w, y = i / w;
			for (i32 k = 0; k < 8; ++k) {
				i32 nx = x + dx[k], ny = y + dy[k];
				if (nx >= 0 && ny >= 0 && nx < w && ny < h && j->known[ny * w + nx] && inpaint_same(j, i, ny * w + nx)) {
					front[n] = 1;
					break;
				}
			}
		}
		i32 filled = 0;
		for (i32 n = 0; n < count; ++n) {
			if (!front[n]) {
				continue;
			}
			i32 i = todo[n];
			i32 x = i % w, y = i / w;
			i32 best_s = -1;
			f32 best   = 1e30;
			// Coherence: continue the patch the neighbours were copied from
			for (i32 k = 0; k < 8; ++k) {
				i32 nx = x + dx[k], ny = y + dy[k];
				if (nx < 0 || ny < 0 || nx >= w || ny >= h) {
					continue;
				}
				i32 q = ny * w + nx;
				if (!j->known[q] || j->src[q] < 0) {
					continue;
				}
				i32 sx = j->src[q] % w - dx[k], sy = j->src[q] / w - dy[k];
				if (sx < 0 || sy < 0 || sx >= w || sy >= h) {
					continue;
				}
				i32 s = sy * w + sx;
				if (!j->known[s] || j->src[s] != s || !inpaint_same(j, i, s)) {
					continue;
				}
				f32 c = inpaint_patch_cost(j, i, s, best);
				if (c >= 0.0 && c < best) {
					best   = c;
					best_s = s;
				}
			}
			// Random candidates of the same island around the texel
			for (i32 r = 0, found = 0; r < 400 && found < 32; ++r) {
				i32 sx = x + (i32)(inpaint_rand() % (2 * search + 1)) - search;
				i32 sy = y + (i32)(inpaint_rand() % (2 * search + 1)) - search;
				if (sx < 0 || sy < 0 || sx >= w || sy >= h) {
					continue;
				}
				i32 s = sy * w + sx;
				if (!j->known[s] || j->src[s] != s || !inpaint_same(j, i, s)) {
					continue;
				}
				found++;
				f32 c = inpaint_patch_cost(j, i, s, best);
				if (c >= 0.0 && c < best) {
					best   = c;
					best_s = s;
				}
			}
			if (best_s < 0) {
				continue;
			}
			memcpy(j->px + i * 4, j->px + best_s * 4, 4);
			j->src[i]   = best_s;
			j->known[i] = 1;
			filled++;
		}
		left -= filled;
		if (filled == 0) {
			break;
		}
	}
	free(front);
	if (left > 0) {
		inpaint_smooth(j, todo, count); // Texels without any source, like a fully masked island
	}
}

// Vector maps store unit vectors, keep the filled ones unit length
static void inpaint_normalize(inpaint_job_t *j, i32 *todo, i32 count) {
	for (i32 n = 0; n < count; ++n) {
		u8 *p    = j->px + todo[n] * 4;
		f32 v[3] = {p[0] / 127.5 - 1.0, p[1] / 127.5 - 1.0, p[2] / 127.5 - 1.0};
		f32 l    = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
		if (l < 1e-4) {
			continue;
		}
		for (i32 k = 0; k < 3; ++k) {
			p[k] = (u8)bake_rays_fmin(bake_rays_fmax((v[k] / l * 0.5 + 0.5) * 255.0 + 0.5, 0.0), 255.0);
		}
	}
}

// bool bake_maps_inpaint_apply(char *path, int mode, bool vector): rebuilds the masked texels of the png
// and reloads its texture, mode 0 copies patches, 1 smooths, vector keeps normals unit length
static minic_val_t bake_maps_inpaint_apply(minic_val_t *a, int n) {
	char *path   = bake_maps_arg_s(a, n, 0);
	i32   mode   = bake_maps_arg_i(a, n, 1);
	bool  vector = bake_maps_arg_i(a, n, 2) != 0;
	if (path == NULL || inpaint_mask == NULL || !inpaint_has_mask || bake_rays_texmap == NULL || bake_rays_island == NULL) {
		return minic_val_int(0);
	}
	asset_t *asset = NULL;
	for (i32 i = 0; i < g_project->_->assets->length; ++i) {
		asset_t *as = g_project->_->assets->buffer[i];
		if (strcmp(as->file, path) == 0) {
			asset = as;
		}
	}
	buffer_t *blob = iron_load_blob(path);
	if (asset == NULL || blob == NULL) {
		return minic_val_int(0);
	}
	i32 w = bake_rays_w, h = bake_rays_h;
	i32 iw, ih, comp;
	u8 *px = stbi_load_from_memory(blob->buffer, blob->length, &iw, &ih, &comp, 4);
	free(blob->buffer);
	free(blob);
	if (px == NULL || iw != w || ih != h) {
		if (px != NULL) {
			stbi_image_free(px);
		}
		return minic_val_int(0);
	}

	i32  count = 0;
	i32  minx = w, miny = h, maxx = 0, maxy = 0;
	i32 *isl = malloc(sizeof(i32) * w * h);
	for (i32 i = 0; i < w * h; ++i) {
		isl[i] = bake_rays_texel_island(i);
		if (inpaint_mask[i] && isl[i] >= 0) {
			count++;
			minx = bake_rays_imin(minx, i % w);
			maxx = bake_rays_imax(maxx, i % w);
			miny = bake_rays_imin(miny, i / w);
			maxy = bake_rays_imax(maxy, i / w);
		}
	}
	i32 *todo = malloc(sizeof(i32) * bake_rays_imax(count, 1));
	for (i32 i = 0, k = 0; i < w * h; ++i) {
		if (inpaint_mask[i] && isl[i] >= 0) {
			todo[k++] = i;
		}
	}
	inpaint_job_t j = {.px = px, .isl = isl, .w = w, .h = h};
	j.known         = malloc(w * h);
	j.src           = malloc(sizeof(i32) * w * h);
	for (i32 i = 0; i < w * h; ++i) {
		j.known[i] = isl[i] >= 0 && !inpaint_mask[i];
		j.src[i]   = j.known[i] ? i : -1;
	}
	inpaint_rand_state = 0x9e3779b9u; // Same synthesis for every map, keeps the maps consistent
	if (mode == INPAINT_MODE_COPY) {
		inpaint_copy(&j, todo, count, bake_rays_imax(maxx - minx, maxy - miny) + 1);
	}
	else {
		inpaint_smooth(&j, todo, count);
	}
	if (vector) {
		inpaint_normalize(&j, todo, count);
	}
	buffer_t b = {.buffer = px, .length = w * h * 4};
#ifdef IRON_BGRA
	buffer_bgra_swap(&b); // The png writer takes the native order, the decoded png is RGBA
#endif
	iron_write_png(path, &b, w, h, 0);
	project_reimport_texture(asset);
	free(j.known);
	free(j.src);
	free(todo);
	free(isl);
	stbi_image_free(px);
	return minic_val_int(count > 0);
}

// void bake_maps_inpaint_end(): releases the mask and its preview, the bake data stays until bake_maps_end
static minic_val_t bake_maps_inpaint_end(minic_val_t *a, int n) {
	free(inpaint_mask);
	inpaint_mask     = NULL;
	inpaint_has_mask = false;
	inpaint_has_last = false;
	if (inpaint_preview_old != NULL) {
		gpu_delete_texture(inpaint_preview_old);
	}
	inpaint_preview_old = inpaint_preview; // Released on the next update or end
	inpaint_preview     = NULL;
	inpaint_dirty       = false;
	return minic_val_int(0);
}

// Two column layout of the dialog, settings on the left and the preview on the right
static f32 bake_maps_column_x;
static f32 bake_maps_column_y;
static f32 bake_maps_column_w;
static f32 bake_maps_column_bottom;

// void bake_maps_ui_column(int column): 0 starts the left column, 1 the right column at the same height,
// -1 continues below both at full width
static minic_val_t bake_maps_ui_column(minic_val_t *a, int n) {
	i32 column = bake_maps_arg_i(a, n, 0);
	f32 gap    = 8 * UI_SCALE();
	if (column == 0) {
		bake_maps_column_x      = g_ui->_x;
		bake_maps_column_y      = g_ui->_y;
		bake_maps_column_w      = g_ui->_w;
		bake_maps_column_bottom = g_ui->_y;
		g_ui->_w                = floorf(bake_maps_column_w * 0.42);
	}
	else if (column == 1) {
		f32 lw                  = floorf(bake_maps_column_w * 0.42);
		bake_maps_column_bottom = bake_rays_fmax(bake_maps_column_bottom, g_ui->_y);
		g_ui->_x                = bake_maps_column_x + lw + gap;
		g_ui->_y                = bake_maps_column_y;
		g_ui->_w                = bake_maps_column_w - lw - gap;
	}
	else {
		g_ui->_x = bake_maps_column_x;
		g_ui->_y = bake_rays_fmax(bake_maps_column_bottom, g_ui->_y);
		g_ui->_w = bake_maps_column_w;
	}
	return minic_val_int(0);
}

// void bake_maps_ui_section(char *text): section label with the separator background
static minic_val_t bake_maps_ui_section(minic_val_t *a, int n) {
	ui_text(bake_maps_arg_s(a, n, 0), UI_ALIGN_LEFT, g_theme->SEPARATOR_COL);
	return minic_val_int(0);
}

// void bake_maps_ui_enabled(bool enabled): disables the following elements, scripts can't set g_ui->enabled
static minic_val_t bake_maps_ui_enabled(minic_val_t *a, int n) {
	g_ui->enabled = bake_maps_arg_i(a, n, 0) != 0;
	return minic_val_int(0);
}

void bake_maps_init() {
	minic_register("bake_maps_load_high", "i(p)", bake_maps_load_high);
	minic_register("bake_maps_high_fit", "f()", bake_maps_high_fit);
	minic_register("bake_maps_set_low", "i()", bake_maps_set_low);
	minic_register("bake_maps_set_cage", "v(f,b)", bake_maps_set_cage);
	minic_register("bake_maps_set_ao", "v(f,f,f)", bake_maps_set_ao);
	minic_register("bake_maps_set_curvature", "v(f,f,f,i)", bake_maps_set_curvature);
	minic_register("bake_maps_set_cavity", "v(f,f)", bake_maps_set_cavity);
	minic_register("bake_maps_start", "b(i,i,i,i,b,i,i)", bake_maps_start);
	minic_register("bake_maps_gpu_supported", "b()", bake_maps_gpu_supported);
	minic_register("bake_maps_gpu_res", "i()", bake_maps_gpu_res);
	minic_register("bake_maps_gpu_start", "b(i,i,f,f,f)", bake_maps_gpu_start);
	minic_register("bake_maps_progress", "f()", bake_maps_progress);
	minic_register("bake_maps_step", "b()", bake_maps_step);
	minic_register("bake_maps_cancel", "v()", bake_maps_cancel);
	minic_register("bake_maps_save", "b(p,p)", bake_maps_save);
	minic_register("bake_maps_preview", "p(i,i,f,f,f,b,b,b,b,b)", bake_maps_preview);
	minic_register("bake_maps_miss_ratio", "f()", bake_maps_miss_ratio);
	minic_register("bake_maps_end_images", "v()", bake_maps_end_images);
	minic_register("bake_maps_end", "v()", bake_maps_end);
	minic_register("bake_maps_ui_column", "v(i)", bake_maps_ui_column);
	minic_register("bake_maps_ui_section", "v(p)", bake_maps_ui_section);
	minic_register("bake_maps_ui_enabled", "v(b)", bake_maps_ui_enabled);
	minic_register("bake_maps_inpaint_begin", "b(p)", bake_maps_inpaint_begin);
	minic_register("bake_maps_inpaint_stroke", "v()", bake_maps_inpaint_stroke);
	minic_register("bake_maps_inpaint_mask", "p()", bake_maps_inpaint_mask);
	minic_register("bake_maps_inpaint_clear", "v()", bake_maps_inpaint_clear);
	minic_register("bake_maps_inpaint_apply", "b(p,i,b)", bake_maps_inpaint_apply);
	minic_register("bake_maps_inpaint_end", "v()", bake_maps_inpaint_end);
}

#endif
