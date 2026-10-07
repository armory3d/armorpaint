
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
#include "engine.h"
#include "iron_array.h"
#include "iron_obj.h"
#include <math.h>
#include <stdarg.h>

static bool  has_next     = false;
static int   current_node = 0;
static float scale_pos    = 1.0;

static cgltf_size io_gltf_index_count(cgltf_primitive *prim) {
	if (prim->indices != NULL) {
		return prim->indices->count;
	}
	for (cgltf_size i = 0; i < prim->attributes_count; ++i) {
		if (prim->attributes[i].type == cgltf_attribute_type_position && prim->attributes[i].data != NULL) {
			return prim->attributes[i].data->count;
		}
	}
	return 0;
}

static uint32_t io_gltf_read_index(cgltf_primitive *prim, cgltf_size i) {
	return prim->indices != NULL ? (uint32_t)cgltf_accessor_read_index(prim->indices, i) : (uint32_t)i;
}

void io_gltf_parse_mesh(raw_mesh_t *raw, cgltf_mesh *mesh, float *to_world, float *scale) {
	cgltf_primitive *prim        = NULL;
	uint32_t        *inda        = NULL;
	cgltf_size       index_total = 0;

	for (int i = 0; i < mesh->primitives_count; ++i) {
		// TODO: handle all primitives
		prim        = &mesh->primitives[i];
		index_total = io_gltf_index_count(prim);
		inda        = malloc(sizeof(uint32_t) * index_total);
		for (cgltf_size i = 0; i < index_total; ++i) {
			inda[i] = io_gltf_read_index(prim, i);
		}
	}

	if (inda == NULL) {
		return;
	}

	int    index_count  = (int)index_total;
	int    vertex_count = -1;
	float *posa32       = NULL;
	float *nora32       = NULL;
	float *texa32       = NULL;

	for (int i = 0; i < prim->attributes_count; ++i) {
		cgltf_attribute *attrib = &prim->attributes[i];

		if (attrib->type == cgltf_attribute_type_position) {
			vertex_count = attrib->data->count;
			posa32       = malloc(sizeof(float) * attrib->data->count * 3);
			for (cgltf_size i = 0; i < attrib->data->count; ++i) {
				cgltf_accessor_read_float(attrib->data, i, posa32 + i * 3, 3);
			}
		}
		else if (attrib->type == cgltf_attribute_type_normal) {
			nora32 = malloc(sizeof(float) * attrib->data->count * 3);
			for (cgltf_size i = 0; i < attrib->data->count; ++i) {
				cgltf_accessor_read_float(attrib->data, i, nora32 + i * 3, 3);
			}
		}
		else if (attrib->type == cgltf_attribute_type_texcoord) {
			texa32 = malloc(sizeof(float) * attrib->data->count * 2);
			for (cgltf_size i = 0; i < attrib->data->count; ++i) {
				cgltf_accessor_read_float(attrib->data, i, texa32 + i * 2, 2);
			}
		}
	}

	if (vertex_count == -1) {
		return;
	}

	float *m = to_world;
	for (int i = 0; i < vertex_count; ++i) {
		// float x           = posa32[i * 3 + 0];
		// float y           = posa32[i * 3 + 1];
		// float z           = posa32[i * 3 + 2];
		float x           = posa32[i * 3 + 0];
		float y           = -posa32[i * 3 + 2];
		float z           = posa32[i * 3 + 1];
		posa32[i * 3 + 0] = m[0] * x + m[4] * y + m[8] * z + m[12];
		posa32[i * 3 + 1] = m[1] * x + m[5] * y + m[9] * z + m[13];
		posa32[i * 3 + 2] = m[2] * x + m[6] * y + m[10] * z + m[14];
	}

	if (nora32 != NULL) {
		for (int i = 0; i < vertex_count; ++i) {
			// float x   = nora32[i * 3 + 0] / scale[0];
			// float y   = nora32[i * 3 + 1] / scale[1];
			// float z   = nora32[i * 3 + 2] / scale[2];
			float x   = nora32[i * 3 + 0] / scale[0];
			float y   = -nora32[i * 3 + 2] / scale[2];
			float z   = nora32[i * 3 + 1] / scale[1];
			float tx  = m[0] * x + m[4] * y + m[8] * z;
			float ty  = m[1] * x + m[5] * y + m[9] * z;
			float tz  = m[2] * x + m[6] * y + m[10] * z;
			float len = sqrtf(tx * tx + ty * ty + tz * tz);
			if (len > 1e-6f) {
				tx /= len;
				ty /= len;
				tz /= len;
			}
			nora32[i * 3 + 0] = tx;
			nora32[i * 3 + 1] = ty;
			nora32[i * 3 + 2] = tz;
		}
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
	}
	else {
		// Calc normals
		for (int i = 0; i < index_count / 3; ++i) {
			int   i1  = inda[i * 3];
			int   i2  = inda[i * 3 + 1];
			int   i3  = inda[i * 3 + 2];
			float vax = posa32[i1 * 3];
			float vay = posa32[i1 * 3 + 1];
			float vaz = posa32[i1 * 3 + 2];
			float vbx = posa32[i2 * 3];
			float vby = posa32[i2 * 3 + 1];
			float vbz = posa32[i2 * 3 + 2];
			float vcx = posa32[i3 * 3];
			float vcy = posa32[i3 * 3 + 1];
			float vcz = posa32[i3 * 3 + 2];
			float cbx = vcx - vbx;
			float cby = vcy - vby;
			float cbz = vcz - vbz;
			float abx = vax - vbx;
			float aby = vay - vby;
			float abz = vaz - vbz;
			float x = cbx, y = cby, z = cbz;
			cbx     = y * abz - z * aby;
			cby     = z * abx - x * abz;
			cbz     = x * aby - y * abx;
			float n = sqrt(cbx * cbx + cby * cby + cbz * cbz);
			if (n > 0.0) {
				float inv_n = 1.0 / n;
				cbx *= inv_n;
				cby *= inv_n;
				cbz *= inv_n;
			}
			nora[i1 * 2]     = (int)(cbx * 32767);
			nora[i1 * 2 + 1] = (int)(cby * 32767);
			posa[i1 * 4 + 3] = (int)(cbz * 32767);
			nora[i2 * 2]     = (int)(cbx * 32767);
			nora[i2 * 2 + 1] = (int)(cby * 32767);
			posa[i2 * 4 + 3] = (int)(cbz * 32767);
			nora[i3 * 2]     = (int)(cbx * 32767);
			nora[i3 * 2 + 1] = (int)(cby * 32767);
			posa[i3 * 4 + 3] = (int)(cbz * 32767);
		}
	}

	short *texa = NULL;
	if (texa32 != NULL) {
		texa = malloc(sizeof(short) * vertex_count * 2);
		for (int i = 0; i < vertex_count; ++i) {
			texa[i * 2]     = texa32[i * 2] * 32767;
			texa[i * 2 + 1] = texa32[i * 2 + 1] * 32767;
		}
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

	raw->inda         = (u32_array_t *)malloc(sizeof(u32_array_t));
	raw->inda->buffer = inda;
	raw->inda->length = raw->inda->capacity = index_count;

	raw->scale_pos = scale_pos;
	raw->scale_tex = 1.0;
}

void *io_gltf_parse(char *buf, size_t size, const char *path) {
	cgltf_options options = {0};
	cgltf_data   *data    = NULL;
	cgltf_result  result  = cgltf_parse(&options, buf, size, &data);
	if (result != cgltf_result_success) {
		return NULL;
	}
	cgltf_load_buffers(&options, data, path);

	raw_mesh_t *raw = (raw_mesh_t *)calloc(sizeof(raw_mesh_t), 1);

	for (; current_node < data->nodes_count; ++current_node) {
		cgltf_node *n = &data->nodes[current_node];
		if (n->mesh != NULL) {
			const char *node_name = n->name != NULL ? n->name : (n->mesh->name != NULL ? n->mesh->name : "mesh");
			raw->name             = malloc(strlen(node_name) + 1);
			strcpy(raw->name, node_name);
			float m[16];
			cgltf_node_transform_world(n, m);
			float scale[3] = {1.0f, 1.0f, 1.0f};
			if (n->has_scale) {
				scale[0] = n->scale[0];
				scale[1] = n->scale[1];
				scale[2] = n->scale[2];
			}
			io_gltf_parse_mesh(raw, n->mesh, m, scale);
			break;
		}
	}

	current_node++;
	has_next = false;
	for (size_t i = current_node; i < data->nodes_count; ++i) {
		cgltf_node *n = &data->nodes[i];
		if (n->mesh != NULL) {
			has_next = true;
			break;
		}
	}

	cgltf_free(data);

	if (!has_next) {
		current_node = 0;
	}

	raw->has_next = has_next;

	return raw;
}

buffer_t *util_skin_blob_create(char *name, int vc, float *pos, float *nor, float *tex, int ic, uint32_t *ind, uint16_t *joints, float *weights, int jc, int fc,
                                float *mats);

// Converts a skinned gltf into a skin blob
buffer_t *io_gltf_skin_blob(char *buf, size_t size, const char *path) {
	cgltf_options options = {0};
	cgltf_data   *data    = NULL;
	if (cgltf_parse(&options, buf, size, &data) != cgltf_result_success)
		return NULL;
	cgltf_load_buffers(&options, data, path);

	cgltf_node *mesh_node = NULL;
	for (cgltf_size i = 0; i < data->nodes_count; i++) {
		if (data->nodes[i].mesh != NULL && data->nodes[i].skin != NULL) {
			mesh_node = &data->nodes[i];
			break;
		}
	}
	if (mesh_node == NULL || mesh_node->mesh->primitives_count == 0 || mesh_node->mesh->primitives[0].indices == NULL) {
		cgltf_free(data);
		return NULL;
	}

	cgltf_skin      *skin    = mesh_node->skin;
	cgltf_primitive *prim    = &mesh_node->mesh->primitives[0];
	int              vc      = -1;
	cgltf_accessor  *acc_pos = NULL, *acc_nor = NULL, *acc_tex = NULL, *acc_joints = NULL, *acc_weights = NULL;
	for (cgltf_size i = 0; i < prim->attributes_count; i++) {
		cgltf_attribute *att = &prim->attributes[i];
		if (att->type == cgltf_attribute_type_position)
			acc_pos = att->data;
		else if (att->type == cgltf_attribute_type_normal)
			acc_nor = att->data;
		else if (att->type == cgltf_attribute_type_texcoord && att->index == 0)
			acc_tex = att->data;
		else if (att->type == cgltf_attribute_type_joints && att->index == 0)
			acc_joints = att->data;
		else if (att->type == cgltf_attribute_type_weights && att->index == 0)
			acc_weights = att->data;
	}
	if (acc_pos == NULL || acc_joints == NULL || acc_weights == NULL) {
		cgltf_free(data);
		return NULL;
	}
	vc = (int)acc_pos->count;

	float    *pos     = calloc(vc * 3, sizeof(float));
	float    *nor     = calloc(vc * 3, sizeof(float));
	float    *tex     = calloc(vc * 2, sizeof(float));
	uint16_t *joints  = calloc(vc * 4, sizeof(uint16_t));
	float    *weights = calloc(vc * 4, sizeof(float));
	for (int i = 0; i < vc; i++) {
		float p[3] = {0}, n[3] = {0, 1, 0}, j[4] = {0}, w[4] = {0};
		cgltf_accessor_read_float(acc_pos, i, p, 3);
		if (acc_nor)
			cgltf_accessor_read_float(acc_nor, i, n, 3);
		if (acc_tex)
			cgltf_accessor_read_float(acc_tex, i, &tex[i * 2], 2);
		cgltf_accessor_read_float(acc_joints, i, j, 4);
		cgltf_accessor_read_float(acc_weights, i, w, 4);
		// Y-up to Z-up: (x, y, z) -> (x, -z, y)
		pos[i * 3]     = p[0];
		pos[i * 3 + 1] = -p[2];
		pos[i * 3 + 2] = p[1];
		nor[i * 3]     = n[0];
		nor[i * 3 + 1] = -n[2];
		nor[i * 3 + 2] = n[1];
		for (int k = 0; k < 4; k++) {
			joints[i * 4 + k]  = (uint16_t)j[k];
			weights[i * 4 + k] = w[k];
		}
	}

	int       ic  = (int)prim->indices->count;
	uint32_t *ind = malloc(sizeof(uint32_t) * ic);
	for (int k = 0; k < ic; k++)
		ind[k] = (uint32_t)cgltf_accessor_read_index(prim->indices, k);

	int fc = 1;
	if (data->animations_count > 0) {
		cgltf_animation *anim = &data->animations[0];
		for (cgltf_size c = 0; c < anim->channels_count; c++) {
			if ((int)anim->channels[c].sampler->output->count > fc)
				fc = (int)anim->channels[c].sampler->output->count;
		}
	}

	int    jc   = (int)skin->joints_count;
	float *mats = malloc(sizeof(float) * 12 * jc * fc);
	for (int f = 0; f < fc; f++) {
		// Pose the joint nodes at this keyframe
		if (data->animations_count > 0) {
			cgltf_animation *anim = &data->animations[0];
			for (cgltf_size c = 0; c < anim->channels_count; c++) {
				cgltf_animation_channel *ch = &anim->channels[c];
				if (ch->target_node == NULL)
					continue;
				cgltf_size fi = (cgltf_size)f < ch->sampler->output->count ? (cgltf_size)f : ch->sampler->output->count - 1;
				if (ch->target_path == cgltf_animation_path_type_translation) {
					cgltf_accessor_read_float(ch->sampler->output, fi, ch->target_node->translation, 3);
					ch->target_node->has_translation = 1;
				}
				else if (ch->target_path == cgltf_animation_path_type_rotation) {
					cgltf_accessor_read_float(ch->sampler->output, fi, ch->target_node->rotation, 4);
					ch->target_node->has_rotation = 1;
				}
				else if (ch->target_path == cgltf_animation_path_type_scale) {
					cgltf_accessor_read_float(ch->sampler->output, fi, ch->target_node->scale, 3);
					ch->target_node->has_scale = 1;
				}
			}
		}
		for (int j = 0; j < jc; j++) {
			// Column-major sm = joint_world * inverse_bind
			float jw[16];
			cgltf_node_transform_world(skin->joints[j], jw);
			float ibm[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
			if (skin->inverse_bind_matrices)
				cgltf_accessor_read_float(skin->inverse_bind_matrices, j, ibm, 16);
			float sm[16];
			for (int col = 0; col < 4; col++)
				for (int row = 0; row < 4; row++)
					sm[col * 4 + row] = jw[0 * 4 + row] * ibm[col * 4 + 0] + jw[1 * 4 + row] * ibm[col * 4 + 1] + jw[2 * 4 + row] * ibm[col * 4 + 2] +
					                    jw[3 * 4 + row] * ibm[col * 4 + 3];
			// To Z-up: C * sm * C^-1 with C mapping (x, y, z) -> (x, -z, y); stored as 3 rows of 4
			static const int   src[3] = {0, 2, 1};
			static const float sgn[3] = {1, -1, 1};
			float             *m      = &mats[(f * jc + j) * 12];
			for (int r = 0; r < 3; r++) {
				for (int c = 0; c < 3; c++)
					m[r * 4 + c] = sgn[r] * sgn[c] * sm[src[c] * 4 + src[r]];
				m[r * 4 + 3] = sgn[r] * sm[12 + src[r]];
			}
		}
	}

	buffer_t *blob = util_skin_blob_create(mesh_node->name != NULL ? mesh_node->name : "", vc, pos, nor, tex, ic, ind, joints, weights, jc, fc, mats);
	free(pos);
	free(nor);
	free(tex);
	free(joints);
	free(weights);
	free(ind);
	free(mats);
	cgltf_free(data);
	return blob;
}

typedef struct {
	char  *buf;
	size_t len;
	size_t cap;
} json_buf_t;

static void json_buf_append(json_buf_t *j, const char *fmt, ...) {
	char    tmp[1024];
	va_list ap;
	va_start(ap, fmt);
	int tl = vsnprintf(tmp, sizeof(tmp), fmt, ap);
	va_end(ap);
	while (j->len + (size_t)tl + 1 > j->cap) {
		j->cap *= 2;
		j->buf = (char *)realloc(j->buf, j->cap);
	}
	memcpy(j->buf + j->len, tmp, (size_t)tl);
	j->len += (size_t)tl;
}

void export_glb_run(char *path, any_array_t *paint_objects) {
	int n = (int)paint_objects->length;

	int      *vcs     = (int *)malloc(n * sizeof(int));
	int      *ics     = (int *)malloc(n * sizeof(int));
	uint32_t *pos_off = (uint32_t *)malloc(n * sizeof(uint32_t));
	uint32_t *nor_off = (uint32_t *)malloc(n * sizeof(uint32_t));
	uint32_t *tex_off = (uint32_t *)malloc(n * sizeof(uint32_t));
	uint32_t *idx_off = (uint32_t *)malloc(n * sizeof(uint32_t));
	float    *pmins   = (float *)malloc(n * 3 * sizeof(float));
	float    *pmaxs   = (float *)malloc(n * 3 * sizeof(float));

	// binary buffer: per object [positions][normals][texcoords][indices]
	size_t   bin_cap = 64 * 1024;
	size_t   bin_len = 0;
	uint8_t *bin     = (uint8_t *)malloc(bin_cap);

	for (int oi = 0; oi < n; oi++) {
		mesh_object_t *p    = (mesh_object_t *)paint_objects->buffer[oi];
		mesh_data_t   *mesh = p->data;
		float          inv  = 1.0f / 32767.0f;
		float          sc   = mesh->scale_pos * inv;
		i16_array_t   *posa = mesh->vertex_arrays->buffer[0]->values;
		i16_array_t   *nora = mesh->vertex_arrays->buffer[1]->values;
		i16_array_t   *texa = mesh->vertex_arrays->buffer[2]->values;
		int            vc   = (int)(posa->length / 4);
		u32_array_t   *inda = mesh->index_array;
		int            ic   = (int)inda->length;

		vcs[oi] = vc;
		ics[oi] = ic;

		// Positions (float32, VEC3), z-up to y-up
		pos_off[oi]       = (uint32_t)bin_len;
		pmins[oi * 3 + 0] = pmins[oi * 3 + 1] = pmins[oi * 3 + 2] = 1e30f;
		pmaxs[oi * 3 + 0] = pmaxs[oi * 3 + 1] = pmaxs[oi * 3 + 2] = -1e30f;
		for (int i = 0; i < vc; i++) {
			float x = posa->buffer[i * 4 + 0] * sc;
			float y = posa->buffer[i * 4 + 2] * sc;
			float z = -posa->buffer[i * 4 + 1] * sc;
			if (x < pmins[oi * 3 + 0])
				pmins[oi * 3 + 0] = x;
			if (y < pmins[oi * 3 + 1])
				pmins[oi * 3 + 1] = y;
			if (z < pmins[oi * 3 + 2])
				pmins[oi * 3 + 2] = z;
			if (x > pmaxs[oi * 3 + 0])
				pmaxs[oi * 3 + 0] = x;
			if (y > pmaxs[oi * 3 + 1])
				pmaxs[oi * 3 + 1] = y;
			if (z > pmaxs[oi * 3 + 2])
				pmaxs[oi * 3 + 2] = z;
			if (bin_len + 12 > bin_cap) {
				bin_cap *= 2;
				bin = (uint8_t *)realloc(bin, bin_cap);
			}
			memcpy(bin + bin_len, &x, 4);
			bin_len += 4;
			memcpy(bin + bin_len, &y, 4);
			bin_len += 4;
			memcpy(bin + bin_len, &z, 4);
			bin_len += 4;
		}

		// Normals (float32, VEC3), nz packed into posa[i*4+3]
		nor_off[oi] = (uint32_t)bin_len;
		for (int i = 0; i < vc; i++) {
			float x = nora->buffer[i * 2 + 0] * inv;
			float y = posa->buffer[i * 4 + 3] * inv;
			float z = -nora->buffer[i * 2 + 1] * inv;
			if (bin_len + 12 > bin_cap) {
				bin_cap *= 2;
				bin = (uint8_t *)realloc(bin, bin_cap);
			}
			memcpy(bin + bin_len, &x, 4);
			bin_len += 4;
			memcpy(bin + bin_len, &y, 4);
			bin_len += 4;
			memcpy(bin + bin_len, &z, 4);
			bin_len += 4;
		}

		// Texcoords (float32, VEC2)
		tex_off[oi] = (uint32_t)bin_len;
		for (int i = 0; i < vc; i++) {
			float u = texa->buffer[i * 2 + 0] * inv;
			float v = texa->buffer[i * 2 + 1] * inv;
			if (bin_len + 8 > bin_cap) {
				bin_cap *= 2;
				bin = (uint8_t *)realloc(bin, bin_cap);
			}
			memcpy(bin + bin_len, &u, 4);
			bin_len += 4;
			memcpy(bin + bin_len, &v, 4);
			bin_len += 4;
		}

		// Indices (uint32)
		idx_off[oi] = (uint32_t)bin_len;
		for (int i = 0; i < ic; i++) {
			uint32_t idx = inda->buffer[i];
			if (bin_len + 4 > bin_cap) {
				bin_cap *= 2;
				bin = (uint8_t *)realloc(bin, bin_cap);
			}
			memcpy(bin + bin_len, &idx, 4);
			bin_len += 4;
		}
	}

	// Pad to 4-byte boundary with zeros
	while (bin_len % 4 != 0) {
		if (bin_len >= bin_cap) {
			bin_cap *= 2;
			bin = (uint8_t *)realloc(bin, bin_cap);
		}
		bin[bin_len++] = 0;
	}

	// Build JSON string
	json_buf_t j = {(char *)malloc(4096), 0, 4096};
	json_buf_append(&j, "{\"asset\":{\"generator\":\"ArmorPaint\",\"version\":\"2.0\"},\"scene\":0,");

	// scenes
	json_buf_append(&j, "\"scenes\":[{\"name\":\"Scene\",\"nodes\":[");
	for (int i = 0; i < n; i++) {
		if (i > 0)
			json_buf_append(&j, ",");
		json_buf_append(&j, "%d", i);
	}
	json_buf_append(&j, "]}],");

	// nodes
	json_buf_append(&j, "\"nodes\":[");
	for (int i = 0; i < n; i++) {
		if (i > 0)
			json_buf_append(&j, ",");
		mesh_object_t *p = (mesh_object_t *)paint_objects->buffer[i];
		json_buf_append(&j, "{\"mesh\":%d,\"name\":\"%s\"}", i, p->base->name);
	}
	json_buf_append(&j, "],");

	// meshes
	json_buf_append(&j, "\"meshes\":[");
	for (int i = 0; i < n; i++) {
		if (i > 0)
			json_buf_append(&j, ",");
		mesh_object_t *p       = (mesh_object_t *)paint_objects->buffer[i];
		int            base_ac = i * 4;
		json_buf_append(&j, "{\"name\":\"%s\",\"primitives\":[{\"attributes\":{\"POSITION\":%d,\"NORMAL\":%d,\"TEXCOORD_0\":%d},\"indices\":%d}]}",
		                p->base->name, base_ac, base_ac + 1, base_ac + 2, base_ac + 3);
	}
	json_buf_append(&j, "],");

	// accessors
	json_buf_append(&j, "\"accessors\":[");
	for (int i = 0; i < n; i++) {
		if (i > 0)
			json_buf_append(&j, ",");
		int bv = i * 4;
		int vc = vcs[i];
		int ic = ics[i];
		// position
		json_buf_append(&j,
		                "{\"bufferView\":%d,\"componentType\":5126,\"count\":%d,\"type\":\"VEC3\","
		                "\"min\":[%.7g,%.7g,%.7g],\"max\":[%.7g,%.7g,%.7g]}",
		                bv, vc, pmins[i * 3 + 0], pmins[i * 3 + 1], pmins[i * 3 + 2], pmaxs[i * 3 + 0], pmaxs[i * 3 + 1], pmaxs[i * 3 + 2]);
		// normal
		json_buf_append(&j, ",{\"bufferView\":%d,\"componentType\":5126,\"count\":%d,\"type\":\"VEC3\"}", bv + 1, vc);
		// texcoord
		json_buf_append(&j, ",{\"bufferView\":%d,\"componentType\":5126,\"count\":%d,\"type\":\"VEC2\"}", bv + 2, vc);
		// indices
		json_buf_append(&j, ",{\"bufferView\":%d,\"componentType\":5125,\"count\":%d,\"type\":\"SCALAR\"}", bv + 3, ic);
	}
	json_buf_append(&j, "],");

	// bufferViews
	json_buf_append(&j, "\"bufferViews\":[");
	for (int i = 0; i < n; i++) {
		if (i > 0)
			json_buf_append(&j, ",");
		int vc = vcs[i];
		int ic = ics[i];
		json_buf_append(&j, "{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u}", pos_off[i], (uint32_t)(vc * 12));
		json_buf_append(&j, ",{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u}", nor_off[i], (uint32_t)(vc * 12));
		json_buf_append(&j, ",{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u}", tex_off[i], (uint32_t)(vc * 8));
		json_buf_append(&j, ",{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u}", idx_off[i], (uint32_t)(ic * 4));
	}
	json_buf_append(&j, "],");

	// buffers
	json_buf_append(&j, "\"buffers\":[{\"byteLength\":%u}]}", (uint32_t)bin_len);

	// Pad to 4-byte boundary with spaces
	while (j.len % 4 != 0) {
		if (j.len >= j.cap) {
			j.cap *= 2;
			j.buf = (char *)realloc(j.buf, j.cap);
		}
		j.buf[j.len++] = ' ';
	}

	// Write glb
	uint32_t json_chunk_len = (uint32_t)j.len;
	uint32_t bin_chunk_len  = (uint32_t)bin_len;
	uint32_t total_len      = 12 + 8 + json_chunk_len + 8 + bin_chunk_len;

	char out_path[4096];
	int  plen = (int)strlen(path);
	if (plen >= 4 && strcmp(path + plen - 4, ".glb") == 0) {
		snprintf(out_path, sizeof(out_path), "%s", path);
	}
	else {
		snprintf(out_path, sizeof(out_path), "%s.glb", path);
	}

	FILE *f = fopen(out_path, "wb");
	if (f != NULL) {
		uint32_t magic     = 0x46546C67; // "glTF"
		uint32_t version   = 2;
		uint32_t json_type = 0x4E4F534A; // "JSON"
		uint32_t bin_type  = 0x004E4942; // "BIN\0"
		fwrite(&magic, 4, 1, f);
		fwrite(&version, 4, 1, f);
		fwrite(&total_len, 4, 1, f);
		fwrite(&json_chunk_len, 4, 1, f);
		fwrite(&json_type, 4, 1, f);
		fwrite(j.buf, 1, j.len, f);
		fwrite(&bin_chunk_len, 4, 1, f);
		fwrite(&bin_type, 4, 1, f);
		fwrite(bin, 1, bin_len, f);
		fclose(f);
	}

	free(vcs);
	free(ics);
	free(pos_off);
	free(nor_off);
	free(tex_off);
	free(idx_off);
	free(pmins);
	free(pmaxs);
	free(bin);
	free(j.buf);
}
