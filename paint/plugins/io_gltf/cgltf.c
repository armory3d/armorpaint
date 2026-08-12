
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
static int   frame_count  = 0;

void io_gltf_parse_mesh(raw_mesh_t *raw, cgltf_mesh *mesh, float *to_world, float *scale) {
	cgltf_primitive *prim = NULL;
	uint32_t        *inda = NULL;

	for (int i = 0; i < mesh->primitives_count; ++i) {
		// TODO: handle all primitives
		prim              = &mesh->primitives[i];
		cgltf_accessor *a = prim->indices;
		inda              = malloc(sizeof(uint32_t) * a->count);
		for (cgltf_size i = 0; i < a->count; ++i) {
			inda[i] = cgltf_accessor_read_index(a, i);
		}
	}

	if (inda == NULL) {
		return;
	}

	int    index_count  = prim->indices->count;
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
			raw->name = malloc(strlen(n->name) + 1);
			strcpy(raw->name, n->name);
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

int io_gltf_frame_count() {
	return frame_count;
}

void *io_gltf_parse_skinned(char *buf, size_t size, const char *path, int frame) {
	cgltf_options options = {0};
	cgltf_data   *data    = NULL;
	if (cgltf_parse(&options, buf, size, &data) != cgltf_result_success)
		return NULL;
	cgltf_load_buffers(&options, data, path);

	// Apply animation at the given frame index by directly writing TRS on each target node
	frame_count = 0;
	if (data->animations_count > 0) {
		cgltf_animation *anim = &data->animations[0];
		for (cgltf_size c = 0; c < anim->channels_count; c++) {
			cgltf_animation_channel *ch = &anim->channels[c];
			if (ch->target_node == NULL)
				continue;
			if ((int)ch->sampler->output->count > frame_count)
				frame_count = (int)ch->sampler->output->count;
			cgltf_size fi = (cgltf_size)frame;
			if (fi >= ch->sampler->output->count)
				fi = ch->sampler->output->count - 1;
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

	// Find first mesh node; prefer one that also has a skin
	cgltf_node *mesh_node = NULL;
	for (cgltf_size i = 0; i < data->nodes_count; i++) {
		if (data->nodes[i].mesh != NULL) {
			if (mesh_node == NULL)
				mesh_node = &data->nodes[i];
			if (data->nodes[i].skin != NULL) {
				mesh_node = &data->nodes[i];
				break;
			}
		}
	}
	if (mesh_node == NULL) {
		cgltf_free(data);
		return NULL;
	}

	cgltf_mesh *mesh        = mesh_node->mesh;
	cgltf_skin *skin        = mesh_node->skin;
	cgltf_size  joint_count = skin ? skin->joints_count : 0;

	// Build per-joint skinning matrices: skin_mat[j] = joint_world[j] * ibm[j]
	float *skin_mats = NULL;
	if (joint_count > 0) {
		skin_mats = malloc(sizeof(float) * 16 * joint_count);
		for (cgltf_size j = 0; j < joint_count; j++) {
			float jw[16];
			cgltf_node_transform_world(skin->joints[j], jw);

			float ibm[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
			if (skin->inverse_bind_matrices)
				cgltf_accessor_read_float(skin->inverse_bind_matrices, j, ibm, 16);

			// Column-major multiply: sm = jw * ibm
			float *sm = &skin_mats[j * 16];
			for (int col = 0; col < 4; col++)
				for (int row = 0; row < 4; row++)
					sm[col * 4 + row] = jw[0 * 4 + row] * ibm[col * 4 + 0] + jw[1 * 4 + row] * ibm[col * 4 + 1] + jw[2 * 4 + row] * ibm[col * 4 + 2] +
					                    jw[3 * 4 + row] * ibm[col * 4 + 3];
		}
	}

	// Read indices
	cgltf_primitive *prim = NULL;
	uint32_t        *inda = NULL;
	for (int i = 0; i < (int)mesh->primitives_count; i++) {
		prim              = &mesh->primitives[i];
		cgltf_accessor *a = prim->indices;
		inda              = malloc(sizeof(uint32_t) * a->count);
		for (cgltf_size k = 0; k < a->count; k++)
			inda[k] = cgltf_accessor_read_index(a, k);
	}
	if (inda == NULL) {
		free(skin_mats);
		cgltf_free(data);
		return NULL;
	}

	int    index_count  = (int)prim->indices->count;
	int    vertex_count = -1;
	float *posa32       = NULL;
	float *nora32       = NULL;
	float *texa32       = NULL;
	float *joints32     = NULL;
	float *weights32    = NULL;

	for (int i = 0; i < (int)prim->attributes_count; i++) {
		cgltf_attribute *att = &prim->attributes[i];
		cgltf_size       vc  = att->data->count;
		if (att->type == cgltf_attribute_type_position) {
			vertex_count = (int)vc;
			posa32       = malloc(sizeof(float) * vc * 3);
			for (cgltf_size k = 0; k < vc; k++)
				cgltf_accessor_read_float(att->data, k, posa32 + k * 3, 3);
		}
		else if (att->type == cgltf_attribute_type_normal) {
			nora32 = malloc(sizeof(float) * vc * 3);
			for (cgltf_size k = 0; k < vc; k++)
				cgltf_accessor_read_float(att->data, k, nora32 + k * 3, 3);
		}
		else if (att->type == cgltf_attribute_type_texcoord) {
			texa32 = malloc(sizeof(float) * vc * 2);
			for (cgltf_size k = 0; k < vc; k++)
				cgltf_accessor_read_float(att->data, k, texa32 + k * 2, 2);
		}
		else if (att->type == cgltf_attribute_type_joints) {
			joints32 = malloc(sizeof(float) * vc * 4);
			for (cgltf_size k = 0; k < vc; k++)
				cgltf_accessor_read_float(att->data, k, joints32 + k * 4, 4);
		}
		else if (att->type == cgltf_attribute_type_weights) {
			weights32 = malloc(sizeof(float) * vc * 4);
			for (cgltf_size k = 0; k < vc; k++)
				cgltf_accessor_read_float(att->data, k, weights32 + k * 4, 4);
		}
	}

	if (vertex_count == -1) {
		free(skin_mats);
		free(inda);
		cgltf_free(data);
		return NULL;
	}

	// Apply linear blend skinning in GLTF space
	bool skinning_applied = false;
	if (skin_mats != NULL && joints32 != NULL && weights32 != NULL) {
		float *sp = malloc(sizeof(float) * vertex_count * 3);
		float *sn = nora32 ? malloc(sizeof(float) * vertex_count * 3) : NULL;

		for (int i = 0; i < vertex_count; i++) {
			float px = posa32[i * 3], py = posa32[i * 3 + 1], pz = posa32[i * 3 + 2];
			float nx = 0, ny = 0, nz = 0;
			if (nora32) {
				nx = nora32[i * 3];
				ny = nora32[i * 3 + 1];
				nz = nora32[i * 3 + 2];
			}

			float opx = 0, opy = 0, opz = 0;
			float onx = 0, ony = 0, onz = 0;

			for (int ji = 0; ji < 4; ji++) {
				float w = weights32[i * 4 + ji];
				if (w == 0.0f)
					continue;
				int j = (int)joints32[i * 4 + ji];
				if (j < 0 || j >= (int)joint_count)
					continue;
				float *m = &skin_mats[j * 16];
				opx += w * (m[0] * px + m[4] * py + m[8] * pz + m[12]);
				opy += w * (m[1] * px + m[5] * py + m[9] * pz + m[13]);
				opz += w * (m[2] * px + m[6] * py + m[10] * pz + m[14]);
				if (sn) {
					onx += w * (m[0] * nx + m[4] * ny + m[8] * nz);
					ony += w * (m[1] * nx + m[5] * ny + m[9] * nz);
					onz += w * (m[2] * nx + m[6] * ny + m[10] * nz);
				}
			}
			sp[i * 3]     = opx;
			sp[i * 3 + 1] = opy;
			sp[i * 3 + 2] = opz;
			if (sn) {
				float len = sqrtf(onx * onx + ony * ony + onz * onz);
				if (len > 1e-6f) {
					onx /= len;
					ony /= len;
					onz /= len;
				}
				sn[i * 3]     = onx;
				sn[i * 3 + 1] = ony;
				sn[i * 3 + 2] = onz;
			}
		}

		free(posa32);
		posa32 = sp;
		if (nora32) {
			free(nora32);
			nora32 = sn;
		}
		free(joints32);
		free(weights32);
		skinning_applied = true;
	}

	// Apply coordinate conversion (Y-up -> Z-up) and world transform.
	// For skinned meshes the skinning already produced world-space positions, so
	// use identity for the world transform and only do the axis swap.
	float node_m[16];
	float node_scale[3] = {1.0f, 1.0f, 1.0f};
	if (skinning_applied) {
		memset(node_m, 0, sizeof(node_m));
		node_m[0] = node_m[5] = node_m[10] = node_m[15] = 1.0f;
	}
	else {
		cgltf_node_transform_world(mesh_node, node_m);
		if (mesh_node->has_scale) {
			node_scale[0] = mesh_node->scale[0];
			node_scale[1] = mesh_node->scale[1];
			node_scale[2] = mesh_node->scale[2];
		}
	}

	float *m = node_m;
	for (int i = 0; i < vertex_count; i++) {
		float x           = posa32[i * 3];
		float y           = -posa32[i * 3 + 2];
		float z           = posa32[i * 3 + 1];
		posa32[i * 3]     = m[0] * x + m[4] * y + m[8] * z + m[12];
		posa32[i * 3 + 1] = m[1] * x + m[5] * y + m[9] * z + m[13];
		posa32[i * 3 + 2] = m[2] * x + m[6] * y + m[10] * z + m[14];
	}

	if (nora32 != NULL) {
		for (int i = 0; i < vertex_count; i++) {
			float x   = nora32[i * 3] / node_scale[0];
			float y   = -nora32[i * 3 + 2] / node_scale[2];
			float z   = nora32[i * 3 + 1] / node_scale[1];
			float tx  = m[0] * x + m[4] * y + m[8] * z;
			float ty  = m[1] * x + m[5] * y + m[9] * z;
			float tz  = m[2] * x + m[6] * y + m[10] * z;
			float len = sqrtf(tx * tx + ty * ty + tz * tz);
			if (len > 1e-6f) {
				tx /= len;
				ty /= len;
				tz /= len;
			}
			nora32[i * 3]     = tx;
			nora32[i * 3 + 1] = ty;
			nora32[i * 3 + 2] = tz;
		}
	}

	// Pack positions to (-1, 1) range
	float hx = 0.0f, hy = 0.0f, hz = 0.0f;
	for (int i = 0; i < vertex_count; i++) {
		float f = fabsf(posa32[i * 3]);
		if (f > hx)
			hx = f;
		f = fabsf(posa32[i * 3 + 1]);
		if (f > hy)
			hy = f;
		f = fabsf(posa32[i * 3 + 2]);
		if (f > hz)
			hz = f;
	}
	float _scale_pos = fmaxf(hx, fmaxf(hy, hz));
	if (_scale_pos > scale_pos)
		scale_pos = _scale_pos;
	float inv = 1.0f / scale_pos;

	// Pack to 16-bit
	short *posa = malloc(sizeof(short) * vertex_count * 4);
	for (int i = 0; i < vertex_count; i++) {
		posa[i * 4]     = (short)(posa32[i * 3] * 32767 * inv);
		posa[i * 4 + 1] = (short)(posa32[i * 3 + 1] * 32767 * inv);
		posa[i * 4 + 2] = (short)(posa32[i * 3 + 2] * 32767 * inv);
	}

	short *nora = malloc(sizeof(short) * vertex_count * 2);
	if (nora32 != NULL) {
		for (int i = 0; i < vertex_count; i++) {
			nora[i * 2]     = (short)(nora32[i * 3] * 32767);
			nora[i * 2 + 1] = (short)(nora32[i * 3 + 1] * 32767);
			posa[i * 4 + 3] = (short)(nora32[i * 3 + 2] * 32767);
		}
	}
	else {
		// Calc flat normals from triangles
		for (int i = 0; i < index_count / 3; i++) {
			int   i1 = inda[i * 3], i2 = inda[i * 3 + 1], i3 = inda[i * 3 + 2];
			float vax = posa32[i1 * 3], vay = posa32[i1 * 3 + 1], vaz = posa32[i1 * 3 + 2];
			float vbx = posa32[i2 * 3], vby = posa32[i2 * 3 + 1], vbz = posa32[i2 * 3 + 2];
			float vcx = posa32[i3 * 3], vcy = posa32[i3 * 3 + 1], vcz = posa32[i3 * 3 + 2];
			float cbx = vcx - vbx, cby = vcy - vby, cbz = vcz - vbz;
			float abx = vax - vbx, aby = vay - vby, abz = vaz - vbz;
			float x = cbx, y = cby, z = cbz;
			cbx     = y * abz - z * aby;
			cby     = z * abx - x * abz;
			cbz     = x * aby - y * abx;
			float n = sqrtf(cbx * cbx + cby * cby + cbz * cbz);
			if (n > 0.0f) {
				float inv_n = 1.0f / n;
				cbx *= inv_n;
				cby *= inv_n;
				cbz *= inv_n;
			}
			nora[i1 * 2]     = (short)(cbx * 32767);
			nora[i1 * 2 + 1] = (short)(cby * 32767);
			posa[i1 * 4 + 3] = (short)(cbz * 32767);
			nora[i2 * 2]     = (short)(cbx * 32767);
			nora[i2 * 2 + 1] = (short)(cby * 32767);
			posa[i2 * 4 + 3] = (short)(cbz * 32767);
			nora[i3 * 2]     = (short)(cbx * 32767);
			nora[i3 * 2 + 1] = (short)(cby * 32767);
			posa[i3 * 4 + 3] = (short)(cbz * 32767);
		}
	}

	short *texa = NULL;
	if (texa32 != NULL) {
		texa = malloc(sizeof(short) * vertex_count * 2);
		for (int i = 0; i < vertex_count; i++) {
			texa[i * 2]     = (short)(texa32[i * 2] * 32767);
			texa[i * 2 + 1] = (short)(texa32[i * 2 + 1] * 32767);
		}
	}

	raw_mesh_t *raw = (raw_mesh_t *)calloc(sizeof(raw_mesh_t), 1);
	if (mesh_node->name != NULL) {
		raw->name = malloc(strlen(mesh_node->name) + 1);
		strcpy(raw->name, mesh_node->name);
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
	raw->scale_tex = 1.0f;

	free(posa32);
	free(nora32);
	free(texa32);
	free(skin_mats);
	cgltf_free(data);
	return raw;
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
