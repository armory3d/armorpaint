
#include "../global.h"

static void export_obj_write_string(u8_array_t *out, char *str) {
	for (i32 i = 0; i < string_length(str); ++i) {
		u8_array_push(out, char_code_at(str, i));
	}
}

void export_obj_run(char *path, mesh_object_t_array_t *paint_objects) {
	u8_array_t *o = u8_array_create_from_raw((u8[]){}, 0);
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

		// Merge shared vertices and remap indices
		i16_array_t *posa2  = i16_array_create(len * 3);
		i16_array_t *nora2  = i16_array_create(len * 3);
		i16_array_t *texa2  = i16_array_create(len * 2);
		i32_array_t *posmap = i32_array_create(len);
		i32_array_t *normap = i32_array_create(len);
		i32_array_t *texmap = i32_array_create(len);

		i32 pi = 0;
		i32 ni = 0;
		i32 ti = 0;
		for (i32 i = 0; i < len; ++i) {
			bool found = false;
			for (i32 j = 0; j < pi; ++j) {
				if (posa2->buffer[j * 3] == posa->buffer[i * 4] && posa2->buffer[j * 3 + 1] == posa->buffer[i * 4 + 1] &&
				    posa2->buffer[j * 3 + 2] == posa->buffer[i * 4 + 2]) {
					posmap->buffer[i] = j;
					found             = true;
					break;
				}
			}
			if (!found) {
				posmap->buffer[i]         = pi;
				posa2->buffer[pi * 3]     = posa->buffer[i * 4];
				posa2->buffer[pi * 3 + 1] = posa->buffer[i * 4 + 1];
				posa2->buffer[pi * 3 + 2] = posa->buffer[i * 4 + 2];
				pi++;
			}

			found = false;
			for (i32 j = 0; j < ni; ++j) {
				if (nora2->buffer[j * 3] == nora->buffer[i * 2] && nora2->buffer[j * 3 + 1] == nora->buffer[i * 2 + 1] &&
				    nora2->buffer[j * 3 + 2] == posa->buffer[i * 4 + 3]) {
					normap->buffer[i] = j;
					found             = true;
					break;
				}
			}
			if (!found) {
				normap->buffer[i]         = ni;
				nora2->buffer[ni * 3]     = nora->buffer[i * 2];
				nora2->buffer[ni * 3 + 1] = nora->buffer[i * 2 + 1];
				nora2->buffer[ni * 3 + 2] = posa->buffer[i * 4 + 3];
				ni++;
			}

			found = false;
			for (i32 j = 0; j < ti; ++j) {
				if (texa2->buffer[j * 2] == texa->buffer[i * 2] && texa2->buffer[j * 2 + 1] == texa->buffer[i * 2 + 1]) {
					texmap->buffer[i] = j;
					found             = true;
					break;
				}
			}
			if (!found) {
				texmap->buffer[i]         = ti;
				texa2->buffer[ti * 2]     = texa->buffer[i * 2];
				texa2->buffer[ti * 2 + 1] = texa->buffer[i * 2 + 1];
				ti++;
			}
		}

		export_obj_write_string(o, string_tmp("o %s\n", p->base->name));
		for (i32 i = 0; i < pi; ++i) {
			export_obj_write_string(o, "v ");
			f32 f = posa2->buffer[i * 3] * sc;
			export_obj_write_string(o, f32_to_string(f));
			export_obj_write_string(o, " ");
			f = posa2->buffer[i * 3 + 2] * sc;
			export_obj_write_string(o, f32_to_string(f));
			export_obj_write_string(o, " ");
			f = -posa2->buffer[i * 3 + 1] * sc;
			export_obj_write_string(o, f32_to_string(f));
			export_obj_write_string(o, "\n");
		}
		for (i32 i = 0; i < ni; ++i) {
			export_obj_write_string(o, "vn ");
			f32 f = nora2->buffer[i * 3] * inv;
			export_obj_write_string(o, f32_to_string(f));
			export_obj_write_string(o, " ");
			f = nora2->buffer[i * 3 + 2] * inv;
			export_obj_write_string(o, f32_to_string(f));
			export_obj_write_string(o, " ");
			f = -nora2->buffer[i * 3 + 1] * inv;
			export_obj_write_string(o, f32_to_string(f));
			export_obj_write_string(o, "\n");
		}
		for (i32 i = 0; i < ti; ++i) {
			export_obj_write_string(o, "vt ");
			f32 f = texa2->buffer[i * 2] * inv;
			export_obj_write_string(o, f32_to_string(f));
			export_obj_write_string(o, " ");
			f = 1.0 - texa2->buffer[i * 2 + 1] * inv;
			export_obj_write_string(o, f32_to_string(f));
			export_obj_write_string(o, "\n");
		}

		u32_array_t *inda = mesh->index_array;
		for (i32 i = 0; i < math_floor(inda->length / 3.0); ++i) {
			i32 pi1 = posmap->buffer[inda->buffer[i * 3]] + 1 + poff;
			i32 pi2 = posmap->buffer[inda->buffer[i * 3 + 1]] + 1 + poff;
			i32 pi3 = posmap->buffer[inda->buffer[i * 3 + 2]] + 1 + poff;
			i32 ni1 = normap->buffer[inda->buffer[i * 3]] + 1 + noff;
			i32 ni2 = normap->buffer[inda->buffer[i * 3 + 1]] + 1 + noff;
			i32 ni3 = normap->buffer[inda->buffer[i * 3 + 2]] + 1 + noff;
			i32 ti1 = texmap->buffer[inda->buffer[i * 3]] + 1 + toff;
			i32 ti2 = texmap->buffer[inda->buffer[i * 3 + 1]] + 1 + toff;
			i32 ti3 = texmap->buffer[inda->buffer[i * 3 + 2]] + 1 + toff;
			export_obj_write_string(o, "f ");
			export_obj_write_string(o, i32_to_string(pi1));
			export_obj_write_string(o, "/");
			export_obj_write_string(o, i32_to_string(ti1));
			export_obj_write_string(o, "/");
			export_obj_write_string(o, i32_to_string(ni1));
			export_obj_write_string(o, " ");
			export_obj_write_string(o, i32_to_string(pi2));
			export_obj_write_string(o, "/");
			export_obj_write_string(o, i32_to_string(ti2));
			export_obj_write_string(o, "/");
			export_obj_write_string(o, i32_to_string(ni2));
			export_obj_write_string(o, " ");
			export_obj_write_string(o, i32_to_string(pi3));
			export_obj_write_string(o, "/");
			export_obj_write_string(o, i32_to_string(ti3));
			export_obj_write_string(o, "/");
			export_obj_write_string(o, i32_to_string(ni3));
			export_obj_write_string(o, "\n");
		}
		poff += pi;
		noff += ni;
		toff += ti;

		array_delete(posa2);
		array_delete(nora2);
		array_delete(texa2);
		array_delete(posmap);
		array_delete(normap);
		array_delete(texmap);
	}

	if (!ends_with(path, ".obj")) {
		path = string_tmp("%s.obj", path);
	}
	iron_file_save_bytes(path, o, 0);
	array_delete(o);
}

void export_obj_run_fast(char *path, mesh_object_t_array_t *paint_objects) {
	// Skips merging shared vertices

	u8_array_t *o = u8_array_create_from_raw((u8[]){}, 0);
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

		export_obj_write_string(o, string_tmp("o %s\n", p->base->name));
		for (i32 i = 0; i < pi; ++i) {
			export_obj_write_string(o, "v ");
			f32 f = posa->buffer[i * 4] * sc;
			export_obj_write_string(o, f32_to_string(f));
			export_obj_write_string(o, " ");
			f = posa->buffer[i * 4 + 2] * sc;
			export_obj_write_string(o, f32_to_string(f));
			export_obj_write_string(o, " ");
			f = -posa->buffer[i * 4 + 1] * sc;
			export_obj_write_string(o, f32_to_string(f));
			export_obj_write_string(o, "\n");
		}
		for (i32 i = 0; i < ni; ++i) {
			export_obj_write_string(o, "vn ");
			f32 f = nora->buffer[i * 2] * inv;
			export_obj_write_string(o, f32_to_string(f));
			export_obj_write_string(o, " ");
			f = posa->buffer[i * 4 + 3] * inv;
			export_obj_write_string(o, f32_to_string(f));
			export_obj_write_string(o, " ");
			f = -nora->buffer[i * 2 + 1] * inv;
			export_obj_write_string(o, f32_to_string(f));
			export_obj_write_string(o, "\n");
		}
		for (i32 i = 0; i < ti; ++i) {
			export_obj_write_string(o, "vt ");
			f32 f = texa->buffer[i * 2] * inv;
			export_obj_write_string(o, f32_to_string(f));
			export_obj_write_string(o, " ");
			f = 1.0 - texa->buffer[i * 2 + 1] * inv;
			export_obj_write_string(o, f32_to_string(f));
			export_obj_write_string(o, "\n");
		}

		u32_array_t *inda = mesh->index_array;
		for (i32 i = 0; i < math_floor(inda->length / 3.0); ++i) {
			i32 pi1 = inda->buffer[i * 3] + 1 + poff;
			i32 pi2 = inda->buffer[i * 3 + 1] + 1 + poff;
			i32 pi3 = inda->buffer[i * 3 + 2] + 1 + poff;
			i32 ni1 = inda->buffer[i * 3] + 1 + noff;
			i32 ni2 = inda->buffer[i * 3 + 1] + 1 + noff;
			i32 ni3 = inda->buffer[i * 3 + 2] + 1 + noff;
			i32 ti1 = inda->buffer[i * 3] + 1 + toff;
			i32 ti2 = inda->buffer[i * 3 + 1] + 1 + toff;
			i32 ti3 = inda->buffer[i * 3 + 2] + 1 + toff;
			export_obj_write_string(o, "f ");
			export_obj_write_string(o, i32_to_string(pi1));
			export_obj_write_string(o, "/");
			export_obj_write_string(o, i32_to_string(ti1));
			export_obj_write_string(o, "/");
			export_obj_write_string(o, i32_to_string(ni1));
			export_obj_write_string(o, " ");
			export_obj_write_string(o, i32_to_string(pi2));
			export_obj_write_string(o, "/");
			export_obj_write_string(o, i32_to_string(ti2));
			export_obj_write_string(o, "/");
			export_obj_write_string(o, i32_to_string(ni2));
			export_obj_write_string(o, " ");
			export_obj_write_string(o, i32_to_string(pi3));
			export_obj_write_string(o, "/");
			export_obj_write_string(o, i32_to_string(ti3));
			export_obj_write_string(o, "/");
			export_obj_write_string(o, i32_to_string(ni3));
			export_obj_write_string(o, "\n");
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

static bool export_obj_sculpt_layer_mask(slot_layer_t *l, f32_array_t *pmask, i32 len, i16_array_t *texa, u32_array_t *inda, f32 inv) {
	slot_layer_t_array_t *masks = slot_layer_get_masks(l, true);
	if (masks == NULL) {
		return false;
	}
#ifdef IRON_BGRA
	i32 r_off = 2;
#else
	i32 r_off = 0;
#endif
	bool any = false;
	for (i32 mi = 0; mi < masks->length; ++mi) {
		slot_layer_t *m = masks->buffer[mi];
		if (!slot_layer_is_visible(m) || m->texpaint == NULL) {
			continue;
		}
		if (!any) {
			for (i32 i = 0; i < len; ++i) {
				pmask->buffer[i] = 1.0;
			}
			any = true;
		}
		buffer_t *mp   = gpu_get_texture_pixels(m->texpaint);
		i32       mw   = m->texpaint->width;
		i32       mh   = m->texpaint->height;
		f32       opac = slot_layer_get_opacity(m);
		for (i32 i = 0; i < len; ++i) {
			i32 vid = inda->buffer[i];
			f32 u   = texa->buffer[vid * 2] * inv;
			f32 v   = texa->buffer[vid * 2 + 1] * inv;
			i32 x   = math_floor(u * mw);
			i32 y   = math_floor(v * mh);
			x       = x < 0 ? 0 : (x >= mw ? mw - 1 : x);
			y       = y < 0 ? 0 : (y >= mh ? mh - 1 : y);
			f32 r   = buffer_get_u8(mp, (y * mw + x) * 4 + r_off) / 255.0;
			pmask->buffer[i] *= (1.0 - opac) + r * opac;
		}
	}
	if (any) {
		for (i32 i = 0; i < len; ++i) {
			f32 c            = pmask->buffer[i];
			pmask->buffer[i] = c < 0.0 ? 0.0 : (c > 1.0 ? 1.0 : c);
		}
	}
	return any;
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

	u8_array_t *o = u8_array_create_from_raw((u8[]){}, 0);
	export_obj_write_string(o, "# armorpaint.org\n");

	mesh_object_t *p    = paint_objects->buffer[0];
	mesh_data_t   *mesh = p->data;
	f32            sc   = mesh->scale_pos;
	i16_array_t   *texa = mesh->vertex_arrays->buffer[2]->values;
	u32_array_t   *inda = mesh->index_array;
	i32            len  = math_floor(inda->length);
	i32            tris = math_floor(len / 3.0);
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
	if (export_obj_sculpt_layer_mask(sculpt_layers->buffer[0], pmask, len, texa, inda, inv) && base_pixels != NULL) {
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
		bool      masked = export_obj_sculpt_layer_mask(sculpt_layers->buffer[k], pmask, len, texa, inda, inv);
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

	export_obj_write_string(o, string_tmp("o %s\n", p->base->name));

	for (i32 i = 0; i < len; ++i) {
		f32 x = cpos->buffer[i * 3] * sc;
		f32 y = cpos->buffer[i * 3 + 1] * sc;
		f32 z = cpos->buffer[i * 3 + 2] * sc;
		export_obj_write_string(o, "v ");
		export_obj_write_string(o, f32_to_string(x));
		export_obj_write_string(o, " ");
		export_obj_write_string(o, f32_to_string(z));
		export_obj_write_string(o, " ");
		export_obj_write_string(o, f32_to_string(-y));
		export_obj_write_string(o, "\n");
	}

	for (i32 t = 0; t < tris; ++t) {
		i32 i0 = t * 3, i1 = t * 3 + 1, i2 = t * 3 + 2;
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
		export_obj_write_string(o, "vn ");
		export_obj_write_string(o, f32_to_string(nx));
		export_obj_write_string(o, " ");
		export_obj_write_string(o, f32_to_string(nz));
		export_obj_write_string(o, " ");
		export_obj_write_string(o, f32_to_string(-ny));
		export_obj_write_string(o, "\n");
	}

	for (i32 i = 0; i < len; ++i) {
		i32 vid = inda->buffer[i];
		f32 u   = texa->buffer[vid * 2] * inv;
		f32 v   = 1.0 - texa->buffer[vid * 2 + 1] * inv;
		export_obj_write_string(o, "vt ");
		export_obj_write_string(o, f32_to_string(u));
		export_obj_write_string(o, " ");
		export_obj_write_string(o, f32_to_string(v));
		export_obj_write_string(o, "\n");
	}

	for (i32 t = 0; t < tris; ++t) {
		i32 b = t * 3 + 1;
		export_obj_write_string(o, "f ");
		export_obj_write_string(o, i32_to_string(b));
		export_obj_write_string(o, "/");
		export_obj_write_string(o, i32_to_string(b));
		export_obj_write_string(o, "/");
		export_obj_write_string(o, i32_to_string(t + 1));
		export_obj_write_string(o, " ");
		export_obj_write_string(o, i32_to_string(b + 1));
		export_obj_write_string(o, "/");
		export_obj_write_string(o, i32_to_string(b + 1));
		export_obj_write_string(o, "/");
		export_obj_write_string(o, i32_to_string(t + 1));
		export_obj_write_string(o, " ");
		export_obj_write_string(o, i32_to_string(b + 2));
		export_obj_write_string(o, "/");
		export_obj_write_string(o, i32_to_string(b + 2));
		export_obj_write_string(o, "/");
		export_obj_write_string(o, i32_to_string(t + 1));
		export_obj_write_string(o, "\n");
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
