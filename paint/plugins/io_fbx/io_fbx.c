#include "iron_array.h"
#include "iron_obj.h"
#include "ufbx/ufbx.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAT_SPLIT_MAX  64
#define MAX_UDIM_TILES 100

static bool         has_next          = false;
static int          current_node      = 0;
static float        scale_pos         = 1.0;
static ufbx_scene  *active_base_scene = NULL;
static ufbx_scene  *active_eval_scene = NULL;
static bool         active_tex1       = false;
static raw_mesh_t **mat_split_meshes  = NULL;
static int          mat_split_count   = 0;
static int          mat_split_idx     = 0;
extern int          plugins_split_by;

static bool valid_mesh(ufbx_node *n) {
	return n->mesh != NULL && n->mesh->num_triangles > 0;
}

static bool mixed_uvs(ufbx_scene *scene) {
	bool found_tex    = false;
	bool found_no_tex = false;
	for (int i = 0; i < (int)scene->nodes.count; ++i) {
		ufbx_node *n = scene->nodes.data[i];
		if (valid_mesh(n)) {
			if (n->mesh->vertex_uv.exists) {
				found_tex = true;
			}
			else {
				found_no_tex = true;
			}
		}
	}
	return found_tex && found_no_tex;
}

static bool mixed_cols(ufbx_scene *scene) {
	bool found_col    = false;
	bool found_no_col = false;
	for (int i = 0; i < (int)scene->nodes.count; ++i) {
		ufbx_node *n = scene->nodes.data[i];
		if (valid_mesh(n)) {
			if (n->mesh->vertex_color.exists) {
				found_col = true;
			}
			else {
				found_no_col = true;
			}
		}
	}
	return found_col && found_no_col;
}

typedef struct {
	float *data;
	int    count;
	int    cap;
} fbuf_t;

static void fbuf_push(fbuf_t *b, float v) {
	if (b->count >= b->cap) {
		b->cap  = b->cap ? b->cap * 2 : 256;
		b->data = realloc(b->data, b->cap * sizeof(float));
	}
	b->data[b->count++] = v;
}

static raw_mesh_t *mat_build_raw(char *name, fbuf_t *pf, fbuf_t *nf, fbuf_t *tf) {
	int   vertex_count = pf->count / 3;
	float sp           = 0.0f;
	for (int i = 0; i < pf->count; ++i) {
		float f = fabsf(pf->data[i]);
		if (f > sp)
			sp = f;
	}
	if (sp == 0.0f)
		sp = 1.0f;
	float inv = 1.0f / sp;

	short *posa = malloc(sizeof(short) * vertex_count * 4);
	short *nora = malloc(sizeof(short) * vertex_count * 2);
	for (int i = 0; i < vertex_count; ++i) {
		posa[i * 4]     = pf->data[i * 3] * 32767.0f * inv;
		posa[i * 4 + 1] = pf->data[i * 3 + 1] * 32767.0f * inv;
		posa[i * 4 + 2] = pf->data[i * 3 + 2] * 32767.0f * inv;
		nora[i * 2]     = nf->data[i * 3] * 32767.0f;
		nora[i * 2 + 1] = nf->data[i * 3 + 1] * 32767.0f;
		posa[i * 4 + 3] = nf->data[i * 3 + 2] * 32767.0f;
	}

	short *texa = NULL;
	if (tf->count > 0) {
		texa = malloc(sizeof(short) * vertex_count * 2);
		for (int i = 0; i < vertex_count; ++i) {
			texa[i * 2]     = tf->data[i * 2] * 32767.0f;
			texa[i * 2 + 1] = (1.0f - tf->data[i * 2 + 1]) * 32767.0f;
		}
	}

	uint32_t *inda = malloc(sizeof(uint32_t) * vertex_count);
	for (int i = 0; i < vertex_count; ++i)
		inda[i] = i;

	raw_mesh_t *raw = calloc(sizeof(raw_mesh_t), 1);
	raw->name       = malloc(strlen(name) + 1);
	strcpy(raw->name, name);

	raw->posa         = malloc(sizeof(i16_array_t));
	raw->posa->buffer = posa;
	raw->posa->length = raw->posa->capacity = vertex_count * 4;

	raw->nora         = malloc(sizeof(i16_array_t));
	raw->nora->buffer = nora;
	raw->nora->length = raw->nora->capacity = vertex_count * 2;

	if (texa) {
		raw->texa         = malloc(sizeof(i16_array_t));
		raw->texa->buffer = texa;
		raw->texa->length = raw->texa->capacity = vertex_count * 2;
	}

	raw->inda         = malloc(sizeof(u32_array_t));
	raw->inda->buffer = inda;
	raw->inda->length = raw->inda->capacity = vertex_count;

	raw->scale_pos = sp;
	raw->scale_tex = 1.0f;
	return raw;
}

static void build_material_split(ufbx_scene *scene) {
	char  *names[MAT_SPLIT_MAX] = {0};
	fbuf_t pf[MAT_SPLIT_MAX]    = {0};
	fbuf_t nf[MAT_SPLIT_MAX]    = {0};
	fbuf_t tf[MAT_SPLIT_MAX]    = {0};
	int    num_mats             = 0;

	uint32_t max_tris = 1;
	for (size_t ni = 0; ni < scene->nodes.count; ++ni) {
		ufbx_node *n = scene->nodes.data[ni];
		if (n->mesh && n->mesh->max_face_triangles > max_tris)
			max_tris = n->mesh->max_face_triangles;
	}
	uint32_t *indices = malloc(sizeof(uint32_t) * max_tris * 3);

	for (size_t ni = 0; ni < scene->nodes.count; ++ni) {
		ufbx_node *n = scene->nodes.data[ni];
		if (!n->mesh)
			continue;
		ufbx_mesh  *mesh       = n->mesh;
		ufbx_matrix normal_mat = ufbx_get_compatible_matrix_for_normals(n);
		bool        has_tex    = mesh->vertex_uv.exists;

		for (size_t fi = 0; fi < mesh->faces.count; ++fi) {
			ufbx_face face = mesh->faces.data[fi];

			uint32_t    midx     = (mesh->face_material.count > 0) ? mesh->face_material.data[fi] : 0;
			const char *mat_name = (midx < n->materials.count && n->materials.data[midx]) ? n->materials.data[midx]->name.data : n->name.data;

			int m = -1;
			for (int k = 0; k < num_mats; ++k) {
				if (strcmp(names[k], mat_name) == 0) {
					m = k;
					break;
				}
			}
			if (m == -1) {
				if (num_mats >= MAT_SPLIT_MAX)
					continue;
				m        = num_mats++;
				names[m] = malloc(strlen(mat_name) + 1);
				strcpy(names[m], mat_name);
			}

			uint32_t num_triangles = ufbx_triangulate_face(indices, max_tris * 3, mesh, face);
			for (uint32_t v_ix = 0; v_ix < num_triangles * 3; ++v_ix) {
				uint32_t  a = indices[v_ix];
				ufbx_vec3 v = ufbx_transform_position(&n->geometry_to_world, ufbx_get_vertex_vec3(&mesh->vertex_position, a));
				fbuf_push(&pf[m], v.x);
				fbuf_push(&pf[m], -v.z);
				fbuf_push(&pf[m], v.y);
				ufbx_vec3 nv = ufbx_transform_direction(&normal_mat, ufbx_get_vertex_vec3(&mesh->vertex_normal, a));
				fbuf_push(&nf[m], nv.x);
				fbuf_push(&nf[m], -nv.z);
				fbuf_push(&nf[m], nv.y);
				if (has_tex) {
					ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh->vertex_uv, a);
					fbuf_push(&tf[m], uv.x);
					fbuf_push(&tf[m], uv.y);
				}
			}
		}
	}

	free(indices);

	mat_split_meshes = malloc(sizeof(raw_mesh_t *) * num_mats);
	mat_split_count  = num_mats;
	for (int m = 0; m < num_mats; ++m) {
		mat_split_meshes[m] = mat_build_raw(names[m], &pf[m], &nf[m], &tf[m]);
		free(names[m]);
		free(pf[m].data);
		free(nf[m].data);
		free(tf[m].data);
	}
}

static void build_udim_split(ufbx_scene *scene) {
	int    tile_ids[MAX_UDIM_TILES] = {0};
	fbuf_t pf[MAX_UDIM_TILES]       = {0};
	fbuf_t nf[MAX_UDIM_TILES]       = {0};
	fbuf_t tf[MAX_UDIM_TILES]       = {0};
	int    num_tiles                = 0;

	const char *base_name = "Mesh";
	for (size_t ni = 0; ni < scene->nodes.count; ++ni) {
		ufbx_node *n = scene->nodes.data[ni];
		if (n->mesh && n->name.length > 0) {
			base_name = n->name.data;
			break;
		}
	}

	uint32_t max_tris = 1;
	for (size_t ni = 0; ni < scene->nodes.count; ++ni) {
		ufbx_node *n = scene->nodes.data[ni];
		if (n->mesh && n->mesh->max_face_triangles > max_tris)
			max_tris = n->mesh->max_face_triangles;
	}
	uint32_t *indices = malloc(sizeof(uint32_t) * max_tris * 3);

	for (size_t ni = 0; ni < scene->nodes.count; ++ni) {
		ufbx_node *n = scene->nodes.data[ni];
		if (!n->mesh)
			continue;
		ufbx_mesh  *mesh       = n->mesh;
		ufbx_matrix normal_mat = ufbx_get_compatible_matrix_for_normals(n);
		bool        has_tex    = mesh->vertex_uv.exists;

		for (size_t fi = 0; fi < mesh->faces.count; ++fi) {
			ufbx_face face          = mesh->faces.data[fi];
			uint32_t  num_triangles = ufbx_triangulate_face(indices, max_tris * 3, mesh, face);

			for (uint32_t tri = 0; tri < num_triangles; ++tri) {
				uint32_t a0 = indices[tri * 3];
				uint32_t a1 = indices[tri * 3 + 1];
				uint32_t a2 = indices[tri * 3 + 2];

				int tile_u = 0, tile_v = 0;
				if (has_tex) {
					ufbx_vec2 uv0 = ufbx_get_vertex_vec2(&mesh->vertex_uv, a0);
					ufbx_vec2 uv1 = ufbx_get_vertex_vec2(&mesh->vertex_uv, a1);
					ufbx_vec2 uv2 = ufbx_get_vertex_vec2(&mesh->vertex_uv, a2);
					tile_u        = (int)((uv0.x + uv1.x + uv2.x) / 3.0);
					tile_v        = (int)((uv0.y + uv1.y + uv2.y) / 3.0);
				}
				int tile_id = 1000 + tile_v * 10 + tile_u + 1;

				int t = -1;
				for (int k = 0; k < num_tiles; ++k) {
					if (tile_ids[k] == tile_id) {
						t = k;
						break;
					}
				}
				if (t == -1) {
					if (num_tiles >= MAX_UDIM_TILES)
						continue;
					t           = num_tiles++;
					tile_ids[t] = tile_id;
				}

				uint32_t verts[3] = {a0, a1, a2};
				for (int vi = 0; vi < 3; ++vi) {
					uint32_t  a = verts[vi];
					ufbx_vec3 v = ufbx_transform_position(&n->geometry_to_world, ufbx_get_vertex_vec3(&mesh->vertex_position, a));
					fbuf_push(&pf[t], v.x);
					fbuf_push(&pf[t], -v.z);
					fbuf_push(&pf[t], v.y);
					ufbx_vec3 nv = ufbx_transform_direction(&normal_mat, ufbx_get_vertex_vec3(&mesh->vertex_normal, a));
					fbuf_push(&nf[t], nv.x);
					fbuf_push(&nf[t], -nv.z);
					fbuf_push(&nf[t], nv.y);
					if (has_tex) {
						ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh->vertex_uv, a);
						fbuf_push(&tf[t], uv.x - tile_u);
						fbuf_push(&tf[t], uv.y - tile_v);
					}
				}
			}
		}
	}

	free(indices);

	char tile_name[256];
	mat_split_meshes = malloc(sizeof(raw_mesh_t *) * num_tiles);
	mat_split_count  = num_tiles;
	for (int t = 0; t < num_tiles; ++t) {
		snprintf(tile_name, sizeof(tile_name), "%s.%d", base_name, tile_ids[t]);
		mat_split_meshes[t] = mat_build_raw(tile_name, &pf[t], &nf[t], &tf[t]);
		free(pf[t].data);
		free(nf[t].data);
		free(tf[t].data);
	}
}

void io_fbx_parse_mesh(raw_mesh_t *raw, ufbx_mesh *mesh, ufbx_matrix *to_world, ufbx_matrix *to_world_unscaled, bool force_tex, bool force_col) {
	uint32_t  indices_size = mesh->max_face_triangles * 3;
	uint32_t *indices      = (uint32_t *)malloc(sizeof(uint32_t) * indices_size);

	bool has_tex  = mesh->vertex_uv.exists;
	bool has_tex1 = active_tex1 && mesh->uv_sets.count > 1;
	bool has_col  = mesh->vertex_color.exists;

	int    numtri  = mesh->num_triangles;
	float *posa32  = (float *)malloc(sizeof(float) * numtri * 3 * 3);
	float *nora32  = (float *)malloc(sizeof(float) * numtri * 3 * 3);
	float *texa32  = (has_tex || force_tex) ? (float *)malloc(sizeof(float) * numtri * 3 * 2) : NULL;
	float *texa132 = has_tex1 ? (float *)malloc(sizeof(float) * numtri * 3 * 2) : NULL;
	float *cola32  = (has_col || force_col) ? (float *)malloc(sizeof(float) * numtri * 3 * 4) : NULL;
	int    pi      = 0;
	int    ni      = 0;
	int    ti      = 0;
	int    ti1     = 0;
	int    ci      = 0;

	for (int j = 0; j < (int)mesh->faces.count; ++j) {
		ufbx_face face          = mesh->faces.data[j];
		uint32_t  num_triangles = ufbx_triangulate_face(indices, indices_size, mesh, face);
		for (uint32_t v_ix = 0; v_ix < num_triangles * 3; v_ix++) {
			uint32_t a = indices[v_ix];

			ufbx_vec3 v = ufbx_transform_position(to_world, ufbx_get_vertex_vec3(&mesh->vertex_position, a));
			// posa32[pi++] = v.x;
			// posa32[pi++] = v.y;
			// posa32[pi++] = v.z;
			posa32[pi++] = v.x;
			posa32[pi++] = -v.z;
			posa32[pi++] = v.y;

			v = ufbx_transform_direction(to_world_unscaled, ufbx_get_vertex_vec3(&mesh->vertex_normal, a));
			// nora32[ni++] = v.x;
			// nora32[ni++] = v.y;
			// nora32[ni++] = v.z;
			nora32[ni++] = v.x;
			nora32[ni++] = -v.z;
			nora32[ni++] = v.y;

			if (has_tex) {
				texa32[ti++] = ufbx_get_vertex_vec2(&mesh->vertex_uv, a).x;
				texa32[ti++] = ufbx_get_vertex_vec2(&mesh->vertex_uv, a).y;
			}
			else if (force_tex) {
				texa32[ti++] = 0;
				texa32[ti++] = 0;
			}

			if (has_tex1) {
				texa132[ti1++] = ufbx_get_vertex_vec2(&mesh->uv_sets.data[1].vertex_uv, a).x;
				texa132[ti1++] = ufbx_get_vertex_vec2(&mesh->uv_sets.data[1].vertex_uv, a).y;
			}

			if (has_col) {
				cola32[ci++] = ufbx_get_vertex_vec4(&mesh->vertex_color, a).x;
				cola32[ci++] = ufbx_get_vertex_vec4(&mesh->vertex_color, a).y;
				cola32[ci++] = ufbx_get_vertex_vec4(&mesh->vertex_color, a).z;
				cola32[ci++] = ufbx_get_vertex_vec4(&mesh->vertex_color, a).w;
			}
			else if (force_col) {
				cola32[ci++] = 0;
				cola32[ci++] = 0;
				cola32[ci++] = 0;
				cola32[ci++] = 0;
			}
		}
	}

	free(indices);

	int vertex_count = pi / 3;
	int index_count  = vertex_count;

	uint32_t *inda = malloc(sizeof(uint32_t) * index_count);
	for (int i = 0; i < index_count; ++i) {
		inda[i] = i;
	}

	// Pack positions to (-1, 1) range
	float hx = 0.0;
	float hy = 0.0;
	float hz = 0.0;
	for (int i = 0; i < vertex_count; ++i) {
		float f = fabsf(posa32[i * 3]);
		if (hx < f)
			hx = f;
		f = fabsf(posa32[i * 3 + 1]);
		if (hy < f)
			hy = f;
		f = fabsf(posa32[i * 3 + 2]);
		if (hz < f)
			hz = f;
	}
	float _scale_pos = fmax(hx, fmax(hy, hz));
	if (_scale_pos > scale_pos)
		scale_pos = _scale_pos;
	float inv = 1 / scale_pos;

	// Pack into 16bit
	short *posa = malloc(sizeof(short) * vertex_count * 4);
	for (int i = 0; i < vertex_count; ++i) {
		posa[i * 4]     = posa32[i * 3] * 32767 * inv;
		posa[i * 4 + 1] = posa32[i * 3 + 1] * 32767 * inv;
		posa[i * 4 + 2] = posa32[i * 3 + 2] * 32767 * inv;
	}

	short *nora = malloc(sizeof(short) * vertex_count * 2);
	if (nora32 != NULL) {
		for (int i = 0; i < vertex_count; ++i) {
			nora[i * 2]     = nora32[i * 3] * 32767;
			nora[i * 2 + 1] = nora32[i * 3 + 1] * 32767;
			posa[i * 4 + 3] = nora32[i * 3 + 2] * 32767;
		}
		free(nora32);
	}

	free(posa32);

	short *texa = NULL;
	if (texa32 != NULL) {
		texa = malloc(sizeof(short) * vertex_count * 2);
		for (int i = 0; i < vertex_count; ++i) {
			texa[i * 2]     = texa32[i * 2] * 32767;
			texa[i * 2 + 1] = (1.0 - texa32[i * 2 + 1]) * 32767;
		}
		free(texa32);
	}

	short *texa1 = NULL;
	if (texa132 != NULL) {
		texa1 = malloc(sizeof(short) * vertex_count * 2);
		for (int i = 0; i < vertex_count; ++i) {
			texa1[i * 2]     = texa132[i * 2] * 32767;
			texa1[i * 2 + 1] = (1.0 - texa132[i * 2 + 1]) * 32767;
		}
		free(texa132);
	}

	short *cola = NULL;
	if (cola32 != NULL) {
		cola = malloc(sizeof(short) * vertex_count * 4);
		for (int i = 0; i < vertex_count; ++i) {
			cola[i * 4]     = cola32[i * 4] * 32767;
			cola[i * 4 + 1] = cola32[i * 4 + 1] * 32767;
			cola[i * 4 + 2] = cola32[i * 4 + 2] * 32767;
			cola[i * 4 + 3] = cola32[i * 4 + 3] * 32767;
		}
		free(cola32);
	}

	raw->posa         = (i16_array_t *)malloc(sizeof(i16_array_t));
	raw->posa->buffer = posa;
	raw->posa->length = raw->posa->capacity = vertex_count * 4;

	raw->nora         = (i16_array_t *)malloc(sizeof(i16_array_t));
	raw->nora->buffer = nora;
	raw->nora->length = raw->nora->capacity = vertex_count * 2;

	if (texa != NULL) {
		raw->texa         = (i16_array_t *)malloc(sizeof(i16_array_t));
		raw->texa->buffer = texa;
		raw->texa->length = raw->texa->capacity = vertex_count * 2;
	}

	if (texa1 != NULL) {
		raw->texa1         = (i16_array_t *)malloc(sizeof(i16_array_t));
		raw->texa1->buffer = texa1;
		raw->texa1->length = raw->texa1->capacity = vertex_count * 2;
	}

	if (cola != NULL) {
		raw->cola         = (i16_array_t *)malloc(sizeof(i16_array_t));
		raw->cola->buffer = cola;
		raw->cola->length = raw->cola->capacity = vertex_count * 4;
	}

	raw->inda         = (u32_array_t *)malloc(sizeof(u32_array_t));
	raw->inda->buffer = inda;
	raw->inda->length = raw->inda->capacity = index_count;

	raw->scale_pos = scale_pos;
	raw->scale_tex = 1.0;
}

void *io_fbx_parse(char *buf, size_t size) {
	if (active_base_scene == NULL) {
		ufbx_load_opts opts = {.generate_missing_normals = true};
		active_base_scene = active_eval_scene = ufbx_load_memory(buf, size, &opts, NULL);
		active_tex1                           = true;
		for (size_t i = 0; i < active_eval_scene->nodes.count; ++i) {
			ufbx_node *n = active_eval_scene->nodes.data[i];
			if (valid_mesh(n) && n->mesh->uv_sets.count < 2) {
				active_tex1 = false;
				break;
			}
		}
		if (plugins_split_by == 1 /* SPLIT_TYPE_MATERIAL */) {
			build_material_split(active_eval_scene);
			mat_split_idx = 0;
		}
		else if (plugins_split_by == 2 /* SPLIT_TYPE_UDIM */) {
			build_udim_split(active_eval_scene);
			mat_split_idx = 0;
		}
	}

	if (plugins_split_by == 1 /* SPLIT_TYPE_MATERIAL */ || plugins_split_by == 2 /* SPLIT_TYPE_UDIM */) {
		raw_mesh_t *raw = mat_split_meshes[mat_split_idx++];
		raw->has_next   = (mat_split_idx < mat_split_count);
		if (!raw->has_next) {
			ufbx_free_scene(active_base_scene);
			active_base_scene = NULL;
			active_eval_scene = NULL;
			current_node      = 0;
			active_tex1       = false;
			free(mat_split_meshes);
			mat_split_meshes = NULL;
			mat_split_count  = 0;
			mat_split_idx    = 0;
		}
		return raw;
	}

	raw_mesh_t *raw = (raw_mesh_t *)calloc(sizeof(raw_mesh_t), 1);

	bool force_tex = mixed_uvs(active_eval_scene);
	bool force_col = mixed_cols(active_eval_scene);

	for (; current_node < (int)active_eval_scene->nodes.count; ++current_node) {
		ufbx_node *n = active_eval_scene->nodes.data[current_node];
		if (valid_mesh(n)) {
			raw->name = malloc(strlen(n->name.data) + 1);
			strcpy(raw->name, n->name.data);
			ufbx_matrix normal_mat = ufbx_get_compatible_matrix_for_normals(n);
			io_fbx_parse_mesh(raw, n->mesh, &n->geometry_to_world, &normal_mat, force_tex, force_col);
			break;
		}
	}
	current_node++;

	has_next = false;
	for (int i = current_node; i < (int)active_eval_scene->nodes.count; ++i) {
		ufbx_node *n = active_eval_scene->nodes.data[i];
		if (valid_mesh(n)) {
			has_next = true;
			break;
		}
	}

	if (!has_next) {
		ufbx_free_scene(active_base_scene);
		active_base_scene = NULL;
		active_eval_scene = NULL;
		current_node      = 0;
		active_tex1       = false;
	}

	raw->has_next = has_next;
	return raw;
}

buffer_t *util_skin_blob_create(char *name, int vc, float *pos, float *nor, float *tex, int ic, uint32_t *ind, uint16_t *joints, float *weights, int jc, int fc,
                                float *mats);
raw_mesh_t *util_skin_raw_mesh(buffer_t *blob);

static void fbx_skin_mat(ufbx_matrix *m, float *out) {
	static const int   src[3] = {0, 2, 1};
	static const float sgn[3] = {1, -1, 1};
	for (int r = 0; r < 3; ++r) {
		for (int c = 0; c < 4; ++c) {
			out[r * 4 + c] = sgn[r] * (float)m->v[c * 3 + src[r]];
		}
	}
}

void *io_fbx_parse_skinned(char *buf, size_t size) {
	if (active_base_scene == NULL) {
		ufbx_load_opts load_opts = {.generate_missing_normals = true};
		active_base_scene        = ufbx_load_memory(buf, size, &load_opts, NULL);
		if (active_base_scene == NULL)
			return NULL;
		current_node = 0;
	}
	ufbx_scene *scene = active_base_scene;

	ufbx_node *mesh_node = NULL;
	for (; current_node < (int)scene->nodes.count; ++current_node) {
		if (valid_mesh(scene->nodes.data[current_node])) {
			mesh_node = scene->nodes.data[current_node];
			break;
		}
	}
	current_node++;

	has_next = false;
	for (int i = current_node; i < (int)scene->nodes.count; ++i) {
		if (valid_mesh(scene->nodes.data[i])) {
			has_next = true;
			break;
		}
	}

	raw_mesh_t *raw = NULL;
	if (mesh_node != NULL) {
		ufbx_mesh          *mesh     = mesh_node->mesh;
		ufbx_skin_deformer *skin     = mesh->skin_deformers.count > 0 ? mesh->skin_deformers.data[0] : NULL;
		int                 clusters = skin != NULL ? (int)skin->clusters.count : 0;
		int                 jc       = clusters + 1;
		bool                has_tex  = mesh->vertex_uv.exists;

		// One output vertex per triangle corner
		uint32_t  indices_size = mesh->max_face_triangles * 3;
		uint32_t *indices      = malloc(sizeof(uint32_t) * indices_size);
		int       vc           = (int)mesh->num_triangles * 3;
		float    *pos          = malloc(sizeof(float) * vc * 3);
		float    *nor          = malloc(sizeof(float) * vc * 3);
		float    *tex          = calloc(vc * 2, sizeof(float));
		uint16_t *joints       = calloc(vc * 4, sizeof(uint16_t));
		float    *weights      = calloc(vc * 4, sizeof(float));
		uint32_t *ind          = malloc(sizeof(uint32_t) * vc);
		int       o            = 0;
		for (size_t j = 0; j < mesh->faces.count; ++j) {
			uint32_t num_triangles = ufbx_triangulate_face(indices, indices_size, mesh, mesh->faces.data[j]);
			for (uint32_t k = 0; k < num_triangles * 3; ++k, ++o) {
				uint32_t  a    = indices[k];
				ufbx_vec3 p    = ufbx_get_vertex_vec3(&mesh->vertex_position, a);
				ufbx_vec3 n    = ufbx_get_vertex_vec3(&mesh->vertex_normal, a);
				pos[o * 3]     = (float)p.x;
				pos[o * 3 + 1] = (float)p.y;
				pos[o * 3 + 2] = (float)p.z;
				nor[o * 3]     = (float)n.x;
				nor[o * 3 + 1] = (float)n.y;
				nor[o * 3 + 2] = (float)n.z;
				if (has_tex) {
					ufbx_vec2 uv   = ufbx_get_vertex_vec2(&mesh->vertex_uv, a);
					tex[o * 2]     = (float)uv.x;
					tex[o * 2 + 1] = 1.0f - (float)uv.y;
				}
				ind[o] = o;

				// Up to 4 strongest weights (ufbx sorts them), normalized
				ufbx_skin_vertex sv  = {0};
				float            sum = 0.0f;
				if (skin != NULL) {
					sv = skin->vertices.data[mesh->vertex_indices.data[a]];
				}
				uint32_t count = sv.num_weights < 4 ? sv.num_weights : 4;
				for (uint32_t w = 0; w < count; ++w) {
					ufbx_skin_weight sw = skin->weights.data[sv.weight_begin + w];
					joints[o * 4 + w]   = (uint16_t)sw.cluster_index;
					weights[o * 4 + w]  = (float)sw.weight;
					sum += (float)sw.weight;
				}
				if (sum > 0.0f) {
					for (uint32_t w = 0; w < count; ++w) {
						weights[o * 4 + w] /= sum;
					}
				}
				else {
					joints[o * 4]  = (uint16_t)clusters;
					weights[o * 4] = 1.0f;
				}
			}
		}
		free(indices);

		// Sample the first animation stack at the scene frame rate
		ufbx_anim *anim  = scene->anim_stacks.count > 0 ? scene->anim_stacks.data[0]->anim : NULL;
		double     fps   = scene->settings.frames_per_second > 0.0 ? scene->settings.frames_per_second : 30.0;
		int        fc    = 1;
		double     begin = 0.0;
		if (anim != NULL && anim->time_end > anim->time_begin) {
			begin = anim->time_begin;
			fc    = (int)((anim->time_end - anim->time_begin) * fps + 0.5) + 1;
			fc    = fc > 10000 ? 10000 : fc;
		}
		float *mats = malloc(sizeof(float) * 12 * jc * fc);
		for (int f = 0; f < fc; ++f) {
			ufbx_scene *eval = anim != NULL ? ufbx_evaluate_scene(scene, anim, begin + f / fps, NULL, NULL) : NULL;
			ufbx_scene *s    = eval != NULL ? eval : scene;
			for (int c = 0; c < clusters; ++c) {
				ufbx_skin_cluster *cl = skin->clusters.data[c];
				ufbx_matrix        m  = cl->geometry_to_bone;
				if (cl->bone_node != NULL) {
					m = ufbx_matrix_mul(&s->nodes.data[cl->bone_node->typed_id]->node_to_world, &cl->geometry_to_bone);
				}
				fbx_skin_mat(&m, &mats[(f * jc + c) * 12]);
			}
			fbx_skin_mat(&s->nodes.data[mesh_node->typed_id]->geometry_to_world, &mats[(f * jc + clusters) * 12]);
			if (eval != NULL)
				ufbx_free_scene(eval);
		}

		buffer_t *blob = util_skin_blob_create(mesh_node->name.data, vc, pos, nor, tex, vc, ind, joints, weights, jc, fc, mats);
		raw            = util_skin_raw_mesh(blob);
		free(pos);
		free(nor);
		free(tex);
		free(joints);
		free(weights);
		free(ind);
		free(mats);
	}
	if (raw == NULL) {
		raw = calloc(sizeof(raw_mesh_t), 1);
	}

	if (!has_next) {
		ufbx_free_scene(active_base_scene);
		active_base_scene = NULL;
		active_eval_scene = NULL;
		current_node      = 0;
		active_tex1       = false;
	}

	raw->has_next = has_next;
	return raw;
}
