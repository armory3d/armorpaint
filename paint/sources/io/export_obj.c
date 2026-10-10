
#include "../global.h"

static void export_obj_reserve(u8_array_t *out, u32 size) {
	if (out->length + size > out->capacity) {
		u32 cap = out->capacity < 1024 ? 1024 : out->capacity * 2;
		while (cap < out->length + size) {
			cap *= 2;
		}
		out->buffer   = realloc(out->buffer, cap);
		out->capacity = cap;
	}
}

static void export_obj_write_string(u8_array_t *out, char *str) {
	u32 len = strlen(str);
	export_obj_reserve(out, len);
	memcpy(out->buffer + out->length, str, len);
	out->length += len;
}

static void export_obj_write_char(u8_array_t *out, char c) {
	export_obj_reserve(out, 1);
	out->buffer[out->length++] = c;
}

static void export_obj_write_i32(u8_array_t *out, i32 i) {
	char s[16];
	i32  l = 0;
	u32  u = i < 0 ? -(u32)i : (u32)i;
	do {
		s[l++] = '0' + u % 10;
		u /= 10;
	} while (u > 0);
	export_obj_reserve(out, l + 1);
	if (i < 0) {
		out->buffer[out->length++] = '-';
	}
	while (l > 0) {
		out->buffer[out->length++] = s[--l];
	}
}

static void export_obj_write_f32(u8_array_t *out, f32 f) {
	char s[64];
	i32  l = snprintf(s, sizeof(s), "%f", f);
	if (l <= 0 || l >= (i32)sizeof(s)) {
		return;
	}
	while (l > 1 && s[l - 1] == '0') {
		l--;
	}
	if (s[l - 1] == '.') {
		l--;
	}
	export_obj_reserve(out, l);
	memcpy(out->buffer + out->length, s, l);
	out->length += l;
}

static void export_obj_write_vec(u8_array_t *out, char *prefix, f32 x, f32 y, f32 z, bool has_z) {
	export_obj_write_string(out, prefix);
	export_obj_write_f32(out, x);
	export_obj_write_char(out, ' ');
	export_obj_write_f32(out, y);
	if (has_z) {
		export_obj_write_char(out, ' ');
		export_obj_write_f32(out, z);
	}
	export_obj_write_char(out, '\n');
}

static void export_obj_write_face(u8_array_t *out, i32 *p, i32 *t, i32 *n) {
	export_obj_write_char(out, 'f');
	for (i32 k = 0; k < 3; ++k) {
		export_obj_write_char(out, ' ');
		export_obj_write_i32(out, p[k]);
		export_obj_write_char(out, '/');
		export_obj_write_i32(out, t[k]);
		export_obj_write_char(out, '/');
		export_obj_write_i32(out, n[k]);
	}
	export_obj_write_char(out, '\n');
}

static u64 export_obj_key(i16 a, i16 b, i16 c) {
	return (u64)(u16)a | ((u64)(u16)b << 16) | ((u64)(u16)c << 32);
}

static i32 export_obj_dedupe(u64 *keys, i32 len, i32 *map, i32 *first) {
	u32 cap = 16;
	while (cap < (u32)len * 2) {
		cap *= 2;
	}
	i32 *table = calloc(cap, sizeof(i32)); // Unique index + 1, 0 is empty
	i32  count = 0;
	for (i32 i = 0; i < len; ++i) {
		u64 k = keys[i];
		u32 s = (u32)((k * 0x9E3779B97F4A7C15ull) >> 32) & (cap - 1);
		while (true) {
			i32 e = table[s];
			if (e == 0) {
				table[s]     = count + 1;
				first[count] = i;
				map[i]       = count++;
				break;
			}
			if (keys[first[e - 1]] == k) {
				map[i] = e - 1;
				break;
			}
			s = (s + 1) & (cap - 1);
		}
	}
	free(table);
	return count;
}

void export_obj_run(char *path, mesh_object_t_array_t *paint_objects) {
	u8_array_t *o = u8_array_create(0);
	export_obj_write_string(o, "# armorpaint.org\n");

	i32 poff = 0;
	i32 noff = 0;
	i32 toff = 0;
	for (i32 i = 0; i < paint_objects->length; ++i) {
		mesh_object_t *p    = paint_objects->buffer[i];
		mesh_data_t   *mesh = p->data;
		f32            inv  = 1 / 32767.0;
		f32            sc   = p->data->scale_pos * inv;
		i16_array_t   *posa = mesh->vertex_arrays->buffer[0]->values;
		i16_array_t   *nora = mesh->vertex_arrays->buffer[1]->values;
		i16_array_t   *texa = mesh->vertex_arrays->buffer[2]->values;
		i32            len  = math_floor(posa->length / 4.0);
		i32            size = len > 0 ? len : 1;

		// Merge shared vertices and remap indices
		u64 *keys   = malloc(size * sizeof(u64));
		i32 *posmap = malloc(size * sizeof(i32));
		i32 *normap = malloc(size * sizeof(i32));
		i32 *texmap = malloc(size * sizeof(i32));
		i32 *posfst = malloc(size * sizeof(i32));
		i32 *norfst = malloc(size * sizeof(i32));
		i32 *texfst = malloc(size * sizeof(i32));
		for (i32 j = 0; j < len; ++j)
			keys[j] = export_obj_key(posa->buffer[j * 4], posa->buffer[j * 4 + 1], posa->buffer[j * 4 + 2]);
		i32 pi = export_obj_dedupe(keys, len, posmap, posfst);
		for (i32 j = 0; j < len; ++j)
			keys[j] = export_obj_key(nora->buffer[j * 2], nora->buffer[j * 2 + 1], posa->buffer[j * 4 + 3]);
		i32 ni = export_obj_dedupe(keys, len, normap, norfst);
		for (i32 j = 0; j < len; ++j)
			keys[j] = export_obj_key(texa->buffer[j * 2], texa->buffer[j * 2 + 1], 0);
		i32 ti = export_obj_dedupe(keys, len, texmap, texfst);

		export_obj_write_string(o, "o ");
		export_obj_write_string(o, p->base->name);
		export_obj_write_char(o, '\n');
		for (i32 j = 0; j < pi; ++j) {
			i32 v = posfst[j];
			export_obj_write_vec(o, "v ", posa->buffer[v * 4] * sc, posa->buffer[v * 4 + 2] * sc, -posa->buffer[v * 4 + 1] * sc, true);
		}
		for (i32 j = 0; j < ni; ++j) {
			i32 v = norfst[j];
			export_obj_write_vec(o, "vn ", nora->buffer[v * 2] * inv, posa->buffer[v * 4 + 3] * inv, -nora->buffer[v * 2 + 1] * inv, true);
		}
		for (i32 j = 0; j < ti; ++j) {
			i32 v = texfst[j];
			export_obj_write_vec(o, "vt ", texa->buffer[v * 2] * inv, 1.0 - texa->buffer[v * 2 + 1] * inv, 0.0, false);
		}

		u32_array_t *inda = mesh->index_array;
		for (i32 j = 0; j < math_floor(inda->length / 3.0); ++j) {
			i32 pf[3];
			i32 tf[3];
			i32 nf[3];
			for (i32 k = 0; k < 3; ++k) {
				i32 v = inda->buffer[j * 3 + k];
				pf[k] = posmap[v] + 1 + poff;
				tf[k] = texmap[v] + 1 + toff;
				nf[k] = normap[v] + 1 + noff;
			}
			export_obj_write_face(o, pf, tf, nf);
		}
		poff += pi;
		noff += ni;
		toff += ti;

		free(keys);
		free(posmap);
		free(normap);
		free(texmap);
		free(posfst);
		free(norfst);
		free(texfst);
	}

	if (!ends_with(path, ".obj")) {
		path = string_tmp("%s.obj", path);
	}
	iron_file_save_bytes(path, o, 0);
	array_delete(o);
}

void export_obj_run_fast(char *path, mesh_object_t_array_t *paint_objects) {
	// Skips merging shared vertices

	u8_array_t *o = u8_array_create(0);
	export_obj_write_string(o, "# armorpaint.org\n");

	i32 poff = 0;
	i32 noff = 0;
	i32 toff = 0;
	for (i32 i = 0; i < paint_objects->length; ++i) {
		mesh_object_t *p    = paint_objects->buffer[i];
		mesh_data_t   *mesh = p->data;
		f32            inv  = 1 / 32767.0;
		f32            sc   = p->data->scale_pos * inv;
		i16_array_t   *posa = mesh->vertex_arrays->buffer[0]->values;
		i16_array_t   *nora = mesh->vertex_arrays->buffer[1]->values;
		i16_array_t   *texa = mesh->vertex_arrays->buffer[2]->values;

		i32 pi = posa->length / 4.0;
		i32 ni = pi;
		i32 ti = pi;

		export_obj_write_string(o, "o ");
		export_obj_write_string(o, p->base->name);
		export_obj_write_char(o, '\n');
		for (i32 j = 0; j < pi; ++j) {
			export_obj_write_vec(o, "v ", posa->buffer[j * 4] * sc, posa->buffer[j * 4 + 2] * sc, -posa->buffer[j * 4 + 1] * sc, true);
		}
		for (i32 j = 0; j < ni; ++j) {
			export_obj_write_vec(o, "vn ", nora->buffer[j * 2] * inv, posa->buffer[j * 4 + 3] * inv, -nora->buffer[j * 2 + 1] * inv, true);
		}
		for (i32 j = 0; j < ti; ++j) {
			export_obj_write_vec(o, "vt ", texa->buffer[j * 2] * inv, 1.0 - texa->buffer[j * 2 + 1] * inv, 0.0, false);
		}

		u32_array_t *inda = mesh->index_array;
		for (i32 j = 0; j < math_floor(inda->length / 3.0); ++j) {
			i32 pf[3];
			i32 tf[3];
			i32 nf[3];
			for (i32 k = 0; k < 3; ++k) {
				i32 v = inda->buffer[j * 3 + k];
				pf[k] = v + 1 + poff;
				tf[k] = v + 1 + toff;
				nf[k] = v + 1 + noff;
			}
			export_obj_write_face(o, pf, tf, nf);
		}
		poff += pi;
		noff += ni;
		toff += ti;
	}

	if (!ends_with(path, ".obj")) {
		path = string_tmp("%s.obj", path);
	}
	iron_file_save_bytes(path, o, 0);
	array_delete(o);
}

void export_obj_run_sculpt(char *path, mesh_object_t_array_t *paint_objects) {
	slot_layer_t_array_t *sculpt_layers = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		if (l->texpaint_sculpt != NULL && slot_layer_is_visible(l)) {
			any_array_push(sculpt_layers, l);
		}
	}
	i32 count = sculpt_layers->length;
	if (count == 0) {
		array_delete(sculpt_layers);
		return;
	}

	u8_array_t *o = u8_array_create(0);
	export_obj_write_string(o, "# armorpaint.org\n");

	mesh_object_t *p    = paint_objects->buffer[0];
	mesh_data_t   *mesh = p->data;
	f32            sc   = mesh->scale_pos;
	i16_array_t   *texa = mesh->vertex_arrays->buffer[2]->values;
	u32_array_t   *inda = mesh->index_array;
	i32            len  = sculpt_object_texel_count(0);
	i32            nv   = math_floor(texa->length / 2.0);
	i32            tris = math_floor(inda->length / 3.0);
	f32            inv  = 1.0 / 32767.0;

	// The base render target holds the rest-pose positions
	buffer_t        *base_pixels = NULL;
	render_target_t *base_rt     = any_map_get(render_path_render_targets, "texpaint_sculpt_base");
	if (base_rt != NULL && base_rt->_image != NULL) {
		base_pixels = gpu_get_texture_pixels(base_rt->_image);
	}

	// Accumulate the combined position of every vertex across all layers and masks
	f32_array_t *cpos  = f32_array_create(len * 3);
	f32_array_t *pmask = f32_array_create(len);

	buffer_t *l0 = gpu_get_texture_pixels(sculpt_layers->buffer[0]->texpaint_sculpt);
	for (i32 i = 0; i < len; ++i) {
		cpos->buffer[i * 3]     = buffer_get_f32(l0, i * 16);
		cpos->buffer[i * 3 + 1] = buffer_get_f32(l0, i * 16 + 4);
		cpos->buffer[i * 3 + 2] = buffer_get_f32(l0, i * 16 + 8);
	}
	// Blend the base layer back toward the rest pose where its mask is dark
	if (sculpt_layer_mask_texels(sculpt_layers->buffer[0], pmask->buffer, len) && base_pixels != NULL) {
		for (i32 i = 0; i < len; ++i) {
			f32 bx                  = buffer_get_f32(base_pixels, i * 16);
			f32 by                  = buffer_get_f32(base_pixels, i * 16 + 4);
			f32 bz                  = buffer_get_f32(base_pixels, i * 16 + 8);
			f32 w                   = pmask->buffer[i];
			cpos->buffer[i * 3]     = bx + (cpos->buffer[i * 3] - bx) * w;
			cpos->buffer[i * 3 + 1] = by + (cpos->buffer[i * 3 + 1] - by) * w;
			cpos->buffer[i * 3 + 2] = bz + (cpos->buffer[i * 3 + 2] - bz) * w;
		}
	}
	// Add each additional layers displacement relative to the rest pose
	for (i32 k = 1; k < count; ++k) {
		buffer_t *lk     = gpu_get_texture_pixels(sculpt_layers->buffer[k]->texpaint_sculpt);
		bool      masked = sculpt_layer_mask_texels(sculpt_layers->buffer[k], pmask->buffer, len);
		for (i32 i = 0; i < len; ++i) {
			f32 dx = buffer_get_f32(lk, i * 16);
			f32 dy = buffer_get_f32(lk, i * 16 + 4);
			f32 dz = buffer_get_f32(lk, i * 16 + 8);
			if (base_pixels != NULL) {
				dx -= buffer_get_f32(base_pixels, i * 16);
				dy -= buffer_get_f32(base_pixels, i * 16 + 4);
				dz -= buffer_get_f32(base_pixels, i * 16 + 8);
			}
			if (masked) {
				f32 w = pmask->buffer[i];
				dx *= w;
				dy *= w;
				dz *= w;
			}
			cpos->buffer[i * 3] += dx;
			cpos->buffer[i * 3 + 1] += dy;
			cpos->buffer[i * 3 + 2] += dz;
		}
	}

	export_obj_write_string(o, "o ");
	export_obj_write_string(o, p->base->name);
	export_obj_write_char(o, '\n');

	for (i32 i = 0; i < len; ++i) {
		f32 x = cpos->buffer[i * 3] * sc;
		f32 y = cpos->buffer[i * 3 + 1] * sc;
		f32 z = cpos->buffer[i * 3 + 2] * sc;
		export_obj_write_vec(o, "v ", x, z, -y, true);
	}

	for (i32 t = 0; t < tris; ++t) {
		i32 i0  = sculpt_vertex_texel(inda->buffer[t * 3]);
		i32 i1  = sculpt_vertex_texel(inda->buffer[t * 3 + 1]);
		i32 i2  = sculpt_vertex_texel(inda->buffer[t * 3 + 2]);
		f32 x0  = cpos->buffer[i0 * 3] * sc;
		f32 y0  = cpos->buffer[i0 * 3 + 1] * sc;
		f32 z0  = cpos->buffer[i0 * 3 + 2] * sc;
		f32 x1  = cpos->buffer[i1 * 3] * sc;
		f32 y1  = cpos->buffer[i1 * 3 + 1] * sc;
		f32 z1  = cpos->buffer[i1 * 3 + 2] * sc;
		f32 x2  = cpos->buffer[i2 * 3] * sc;
		f32 y2  = cpos->buffer[i2 * 3 + 1] * sc;
		f32 z2  = cpos->buffer[i2 * 3 + 2] * sc;
		f32 e1x = x1 - x0, e1y = y1 - y0, e1z = z1 - z0;
		f32 e2x = x2 - x0, e2y = y2 - y0, e2z = z2 - z0;
		f32 nx = e1y * e2z - e1z * e2y;
		f32 ny = e1z * e2x - e1x * e2z;
		f32 nz = e1x * e2y - e1y * e2x;
		f32 nl = math_sqrt(nx * nx + ny * ny + nz * nz);
		if (nl > 0.0) {
			nx /= nl;
			ny /= nl;
			nz /= nl;
		}
		export_obj_write_vec(o, "vn ", nx, nz, -ny, true);
	}

	for (i32 i = 0; i < nv; ++i) {
		f32 u = texa->buffer[i * 2] * inv;
		f32 v = 1.0 - texa->buffer[i * 2 + 1] * inv;
		export_obj_write_vec(o, "vt ", u, v, 0.0, false);
	}

	// Positions are welded per texel, uvs stay per mesh vertex
	for (i32 t = 0; t < tris; ++t) {
		i32 pf[3];
		i32 tf[3];
		for (i32 k = 0; k < 3; ++k) {
			i32 vid = inda->buffer[t * 3 + k];
			pf[k]   = sculpt_vertex_texel(vid) + 1;
			tf[k]   = vid + 1;
		}
		i32 nf[3] = {t + 1, t + 1, t + 1};
		export_obj_write_face(o, pf, tf, nf);
	}

	if (!ends_with(path, ".obj")) {
		path = string_tmp("%s.obj", path);
	}
	iron_file_save_bytes(path, o, 0);

	array_delete(o);
	array_delete(cpos);
	array_delete(pmask);
	array_delete(sculpt_layers);
}
