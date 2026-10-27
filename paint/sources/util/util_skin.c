#include "../global.h"

// Skinned mesh blob: rest mesh, 4 bone weights per vertex and the skinning matrix of every bone for every frame
// Layout, 4-byte aligned:
//   util_skin_header_t, name (name_len bytes padded to 4),
//   f32 pos[vc * 3], f32 nor[vc * 3], f32 tex[vc * 2], u32 ind[ic], u16 joints[vc * 4], f32 weights[vc * 4],
//   f32 mats[frames * joints * 12] (3 rows of 4 per bone)
// Trailing bone section:
//   u32 magic, i32 parents[joints], f32 heads[joints * 3], f32 tails[joints * 3] (rest pose, object space)

#define UTIL_SKIN_MAGIC   0x4e494b53 // "SKIN"
#define UTIL_SKIN_VERSION 1
#define UTIL_SKIN_BONES   0x454e4f42 // "BONE"

typedef struct util_skin_header {
	u32 magic;
	u32 version;
	u32 vertex_count;
	u32 index_count;
	u32 joint_count;
	u32 frame_count;
	f32 scale_pos;
	u32 name_len;
} util_skin_header_t;

typedef struct util_skin_view {
	util_skin_header_t *h;
	char               *name;
	f32                *pos;
	f32                *nor;
	f32                *tex;
	u32                *ind;
	u16                *joints;
	f32                *weights;
	f32                *mats;
	i32                *parents; // NULL when the blob has no bone section
	f32                *heads;
	f32                *tails;
} util_skin_view_t;

static u32 util_skin_pad4(u32 n) {
	return (n + 3) & ~3u;
}

static u32 util_skin_size(u32 vc, u32 ic, u32 jc, u32 fc, u32 name_len) {
	return sizeof(util_skin_header_t) + util_skin_pad4(name_len) + vc * (3 + 3 + 2) * 4 + ic * 4 + vc * 4 * 2 + vc * 4 * 4 + fc * jc * 12 * 4;
}

static bool util_skin_view(buffer_t *blob, util_skin_view_t *v) {
	if (blob == NULL || blob->length < sizeof(util_skin_header_t)) {
		return false;
	}
	util_skin_header_t *h = (util_skin_header_t *)blob->buffer;
	if (h->magic != UTIL_SKIN_MAGIC || h->version != UTIL_SKIN_VERSION ||
	    blob->length < util_skin_size(h->vertex_count, h->index_count, h->joint_count, h->frame_count, h->name_len)) {
		return false;
	}
	u8 *p   = blob->buffer + sizeof(util_skin_header_t);
	v->h    = h;
	v->name = (char *)p;
	p += util_skin_pad4(h->name_len);
	v->pos = (f32 *)p;
	p += h->vertex_count * 3 * 4;
	v->nor = (f32 *)p;
	p += h->vertex_count * 3 * 4;
	v->tex = (f32 *)p;
	p += h->vertex_count * 2 * 4;
	v->ind = (u32 *)p;
	p += h->index_count * 4;
	v->joints = (u16 *)p;
	p += h->vertex_count * 4 * 2;
	v->weights = (f32 *)p;
	p += h->vertex_count * 4 * 4;
	v->mats    = (f32 *)p;
	v->parents = NULL;
	v->heads   = NULL;
	v->tails   = NULL;
	u32 size   = util_skin_size(h->vertex_count, h->index_count, h->joint_count, h->frame_count, h->name_len);
	if (blob->length >= size + 4 + h->joint_count * 7 * 4 && *(u32 *)(blob->buffer + size) == UTIL_SKIN_BONES) {
		p          = blob->buffer + size + 4;
		v->parents = (i32 *)p;
		v->heads   = (f32 *)(p + h->joint_count * 4);
		v->tails   = (f32 *)(p + h->joint_count * 4 * 4);
	}
	return true;
}

i32 util_skin_frame_count(buffer_t *blob) {
	util_skin_view_t v;
	return util_skin_view(blob, &v) ? (i32)v.h->frame_count : 0;
}

static void util_skin_vertex(util_skin_view_t *v, i32 frame, i32 i, f32 *op, f32 *on) {
	f32 *p   = &v->pos[i * 3];
	f32 *n   = &v->nor[i * 3];
	f32 *fm  = &v->mats[(size_t)frame * v->h->joint_count * 12];
	f32  sum = 0.0f;
	op[0] = op[1] = op[2] = 0.0f;
	on[0] = on[1] = on[2] = 0.0f;
	for (i32 k = 0; k < 4; ++k) {
		f32 w = v->weights[i * 4 + k];
		u32 j = v->joints[i * 4 + k];
		if (w == 0.0f || j >= v->h->joint_count) {
			continue;
		}
		f32 *m = &fm[j * 12];
		for (i32 r = 0; r < 3; ++r) {
			op[r] += w * (m[r * 4] * p[0] + m[r * 4 + 1] * p[1] + m[r * 4 + 2] * p[2] + m[r * 4 + 3]);
			on[r] += w * (m[r * 4] * n[0] + m[r * 4 + 1] * n[1] + m[r * 4 + 2] * n[2]);
		}
		sum += w;
	}
	if (sum == 0.0f) {
		// Unweighted vertices stay in the rest pose
		memcpy(op, p, sizeof(f32) * 3);
		memcpy(on, n, sizeof(f32) * 3);
	}
	f32 l = sqrtf(on[0] * on[0] + on[1] * on[1] + on[2] * on[2]);
	if (l > 1e-6f) {
		on[0] /= l;
		on[1] /= l;
		on[2] /= l;
	}
}

static void util_skin_pack(util_skin_view_t *v, i32 frame, i16 *posa, i16 *nora) {
	f32 inv = 32767.0f / v->h->scale_pos;
	for (u32 i = 0; i < v->h->vertex_count; ++i) {
		f32 p[3], n[3];
		util_skin_vertex(v, frame, i, p, n);
		posa[i * 4]     = (i16)(p[0] * inv);
		posa[i * 4 + 1] = (i16)(p[1] * inv);
		posa[i * 4 + 2] = (i16)(p[2] * inv);
		posa[i * 4 + 3] = (i16)(n[2] * 32767.0f);
		nora[i * 2]     = (i16)(n[0] * 32767.0f);
		nora[i * 2 + 1] = (i16)(n[1] * 32767.0f);
	}
}

bool util_skin_apply(buffer_t *blob, i32 frame, i16_array_t *posa, i16_array_t *nora, f32 *scale_pos) {
	util_skin_view_t v;
	if (!util_skin_view(blob, &v) || v.h->frame_count == 0 || posa->length != v.h->vertex_count * 4 || nora->length != v.h->vertex_count * 2) {
		return false;
	}
	util_skin_pack(&v, frame % v.h->frame_count, posa->buffer, nora->buffer);
	*scale_pos = v.h->scale_pos;
	return true;
}

// parents, heads and tails can be NULL to leave out the bone section
buffer_t *util_skin_blob_create(char *name, i32 vc, f32 *pos, f32 *nor, f32 *tex, i32 ic, u32 *ind, u16 *joints, f32 *weights, i32 jc, i32 fc, f32 *mats,
                                i32 *parents, f32 *heads, f32 *tails) {
	u32       name_len = name != NULL ? (u32)strlen(name) : 0;
	u32       size     = util_skin_size(vc, ic, jc, fc, name_len);
	bool      bones    = parents != NULL && heads != NULL && tails != NULL;
	buffer_t *blob     = buffer_create(size + (bones ? 4 + jc * 7 * 4 : 0));
	memset(blob->buffer, 0, blob->length);
	util_skin_header_t *h = (util_skin_header_t *)blob->buffer;
	h->magic              = UTIL_SKIN_MAGIC;
	h->version            = UTIL_SKIN_VERSION;
	h->vertex_count       = vc;
	h->index_count        = ic;
	h->joint_count        = jc;
	h->frame_count        = fc;
	h->scale_pos          = 1.0f;
	h->name_len           = name_len;
	util_skin_view_t v;
	util_skin_view(blob, &v);
	memcpy(v.name, name, name_len);
	memcpy(v.pos, pos, sizeof(f32) * vc * 3);
	memcpy(v.nor, nor, sizeof(f32) * vc * 3);
	memcpy(v.tex, tex, sizeof(f32) * vc * 2);
	memcpy(v.ind, ind, sizeof(u32) * ic);
	memcpy(v.joints, joints, sizeof(u16) * vc * 4);
	memcpy(v.weights, weights, sizeof(f32) * vc * 4);
	memcpy(v.mats, mats, sizeof(f32) * fc * jc * 12);
	if (bones) {
		*(u32 *)(blob->buffer + size) = UTIL_SKIN_BONES;
		util_skin_view(blob, &v);
		memcpy(v.parents, parents, sizeof(i32) * jc);
		memcpy(v.heads, heads, sizeof(f32) * jc * 3);
		memcpy(v.tails, tails, sizeof(f32) * jc * 3);
	}

	f32 scale = 1e-6f;
	for (i32 f = 0; f < fc; ++f) {
		for (i32 i = 0; i < vc; ++i) {
			f32 p[3], n[3];
			util_skin_vertex(&v, f, i, p, n);
			scale = fmaxf(scale, fmaxf(fabsf(p[0]), fmaxf(fabsf(p[1]), fabsf(p[2]))));
		}
	}
	h->scale_pos = scale;
	return blob;
}

static void util_skin_transform(f32 *m, f32 *p, f32 *out) {
	for (i32 r = 0; r < 3; ++r) {
		out[r] = m[r * 4] * p[0] + m[r * 4 + 1] * p[1] + m[r * 4 + 2] * p[2] + m[r * 4 + 3];
	}
}

// Bones posed at a frame as segments of 7 floats: head xyz, tail xyz, relation line flag
f32 *util_skin_bones(buffer_t *blob, i32 frame, i32 *count) {
	*count = 0;
	util_skin_view_t v;
	if (!util_skin_view(blob, &v) || v.parents == NULL || v.h->frame_count == 0 || v.h->joint_count == 0) {
		return NULL;
	}
	i32  jc  = v.h->joint_count;
	f32 *fm  = &v.mats[(size_t)(frame % v.h->frame_count) * jc * 12];
	f32 *out = malloc(sizeof(f32) * 7 * jc * 2);
	i32  n   = 0;
	for (i32 j = 0; j < jc; ++j) {
		f32 *o = &out[n++ * 7];
		util_skin_transform(&fm[j * 12], &v.heads[j * 3], &o[0]);
		util_skin_transform(&fm[j * 12], &v.tails[j * 3], &o[3]);
		o[6]  = 0.0f;
		i32 p = v.parents[j];
		if (p >= 0 && p < jc) {
			f32 *t  = &v.tails[p * 3];
			f32 *h  = &v.heads[j * 3];
			f32  dx = t[0] - h[0], dy = t[1] - h[1], dz = t[2] - h[2];
			if (dx * dx + dy * dy + dz * dz > 1e-8f) {
				o = &out[n++ * 7];
				util_skin_transform(&fm[p * 12], t, &o[0]);
				util_skin_transform(&fm[j * 12], h, &o[3]);
				o[6] = 1.0f;
			}
		}
	}
	f32 inv = 1.0f / v.h->scale_pos;
	for (i32 i = 0; i < n; ++i) {
		for (i32 k = 0; k < 6; ++k) {
			out[i * 7 + k] *= inv;
		}
	}
	*count = n;
	return out;
}

raw_mesh_t *util_skin_raw_mesh(buffer_t *blob) {
	util_skin_view_t v;
	if (!util_skin_view(blob, &v)) {
		return NULL;
	}
	i32         vc  = v.h->vertex_count;
	raw_mesh_t *raw = calloc(1, sizeof(raw_mesh_t));
	raw->name       = calloc(1, v.h->name_len + 1);
	memcpy(raw->name, v.name, v.h->name_len);
	raw->posa = i16_array_create(vc * 4);
	raw->nora = i16_array_create(vc * 2);
	raw->texa = i16_array_create(vc * 2);
	raw->inda = u32_array_create(v.h->index_count);
	if (v.h->frame_count > 0) {
		util_skin_pack(&v, 0, raw->posa->buffer, raw->nora->buffer);
	}
	for (i32 i = 0; i < vc * 2; ++i) {
		raw->texa->buffer[i] = (i16)(v.tex[i] * 32767.0f);
	}
	memcpy(raw->inda->buffer, v.ind, sizeof(u32) * v.h->index_count);
	raw->vertex_count = vc;
	raw->index_count  = v.h->index_count;
	raw->scale_pos    = v.h->scale_pos;
	raw->scale_tex    = 1.0f;
	raw->blob         = blob;
	return raw;
}

static void *util_skin_import(char *path) {
	buffer_t *b    = data_get_blob(path);
	buffer_t *blob = buffer_create(b->length);
	memcpy(blob->buffer, b->buffer, b->length);
	data_delete_blob(path); // Regenerated files are picked up again on the next import
	raw_mesh_t *raw = util_skin_raw_mesh(blob);
	if (raw == NULL) {
		console_error(strings_failed_to_read_mesh_data());
	}
	return raw;
}

extern string_array_t *_path_mesh_formats;

void util_skin_init() {
	path_mesh_formats(); // Init array
	any_map_set(import_mesh_importers, "skin", util_skin_import);
	any_array_push(_path_mesh_formats, "skin");
}
