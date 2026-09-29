
#include "../global.h"

i16_array_t *util_mesh_va0;

static mesh_object_t_array_t *util_mesh_merged_objects = NULL;

void util_mesh_remove_merged() {
	if (util_mesh_merged_objects != NULL) {
		util_mesh_merged_objects->length = 0;
	}
	if (g_context->merged_object != NULL) {
		mesh_data_delete(g_context->merged_object->data);
		mesh_object_remove(g_context->merged_object);
		g_context->merged_object = NULL;
	}
}

mesh_object_t_array_t *util_mesh_get_visible() {
	mesh_object_t_array_t *ar = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_object_t *p = g_project->_->paint_objects->buffer[i];
		if (p->base->visible) {
			any_array_push(ar, p);
		}
	}
	return ar;
}

mesh_object_t_array_t *util_mesh_dedup_data(mesh_object_t_array_t *objects) {
	mesh_object_t_array_t *ar = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < objects->length; ++i) {
		mesh_object_t *p    = objects->buffer[i];
		bool           seen = false;
		for (i32 j = 0; j < ar->length && !seen; ++j) {
			seen = ar->buffer[j]->data == p->data;
		}
		if (!seen) {
			any_array_push(ar, p);
		}
	}
	return ar;
}

mesh_object_t_array_t *util_mesh_get_unique_data() {
	return util_mesh_dedup_data(g_project->_->paint_objects);
}

mesh_object_t_array_t *util_mesh_get_hierarchy(mesh_object_t *o) {
	// The object and all its descendants, one object per mesh data
	mesh_object_t_array_t *ar = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_object_t *p = g_project->_->paint_objects->buffer[i];
		object_t      *q = p->base;
		while (q != NULL && q != o->base) {
			q = q->parent;
		}
		bool seen = q == NULL;
		for (i32 j = 0; j < ar->length && !seen; ++j) {
			seen = ar->buffer[j]->data == p->data;
		}
		if (!seen) {
			any_array_push(ar, p);
		}
	}
	return ar;
}

i32 util_mesh_data_owner(mesh_data_t *data) {
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		if (g_project->_->paint_objects->buffer[i]->data == data) {
			return i;
		}
	}
	return -1;
}

bool util_mesh_data_is_shared(mesh_data_t *data) {
	i32 users = 0;
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		if (g_project->_->paint_objects->buffer[i]->data == data) {
			users++;
		}
	}
	return users > 1;
}

void util_mesh_sync_scale_world(mesh_data_t *data) {
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_object_t *p = g_project->_->paint_objects->buffer[i];
		if (p->data == data && p->base->transform->scale_world != data->scale_pos) {
			p->base->transform->scale_world = data->scale_pos;
			transform_build_matrix(p->base->transform);
		}
	}
}

void util_mesh_unshare_data(mesh_object_t *o) {
	if (!util_mesh_data_is_shared(o->data)) {
		return;
	}
	mesh_object_set_data(o, util_mesh_data_duplicate(o->data));
	o->data->name = string_copy(o->base->name);
}

char *util_mesh_link_name(i32 source_index, char *object_name) {
	return string("%s@%d", object_name, source_index);
}

bool util_mesh_link_parse(char *name, i32 *source_index, char **object_name) {
	if (name == NULL) {
		return false;
	}
	i32 sep = string_last_index_of(name, "@");
	if (sep < 0) {
		return false;
	}
	i32 len = string_length(name);
	if (sep + 1 == len) {
		return false;
	}
	for (i32 i = sep + 1; i < len; ++i) {
		if (name[i] < '0' || name[i] > '9') {
			return false;
		}
	}
	*source_index = parse_int(substring(name, sep + 1, len));
	*object_name  = substring(name, 0, sep);
	return true;
}

#define ATLAS_MAX_STRIDE 8
#define ATLAS_MAX_SLOTS  (ATLAS_MAX_STRIDE * ATLAS_MAX_STRIDE)

static i32 _util_mesh_atlas_stride_for(i32 count) {
	i32 stride = 1;
	while (stride * stride < count && stride < ATLAS_MAX_STRIDE) {
		stride++;
	}
	return stride;
}

static i32  util_mesh_atlas_stride_merged = 1;
static i32  util_mesh_udim_tiles[ATLAS_MAX_SLOTS]; // Sorted tile ids, one atlas slot per tile
static i32  util_mesh_udim_tile_count  = 0;
static bool util_mesh_udim_tiles_spent = false;

// Tile id from the ".1001" name suffix given by udim import, -1 if none
i32 util_mesh_udim_tile(char *name) {
	if (name == NULL) {
		return -1;
	}
	i32 len = string_length(name);
	if (len < 5 || name[len - 5] != '.' || name[len - 4] != '1') {
		return -1;
	}
	i32 id = 0;
	for (i32 i = len - 4; i < len; ++i) {
		if (name[i] < '0' || name[i] > '9') {
			return -1;
		}
		id = id * 10 + (name[i] - '0');
	}
	return id > 1000 ? id : -1;
}

static void _util_mesh_udim_build_tiles() {
	mesh_object_t_array_t *paint_objects = g_project->_->paint_objects;
	util_mesh_udim_tile_count            = 0;
	util_mesh_udim_tiles_spent           = false;
	for (i32 i = 0; i < paint_objects->length; ++i) {
		i32 id = util_mesh_udim_tile(paint_objects->buffer[i]->base->name);
		if (id < 0) {
			continue;
		}
		i32 pos = 0;
		while (pos < util_mesh_udim_tile_count && util_mesh_udim_tiles[pos] < id) {
			pos++;
		}
		if (pos < util_mesh_udim_tile_count && util_mesh_udim_tiles[pos] == id) {
			continue;
		}
		if (util_mesh_udim_tile_count == ATLAS_MAX_SLOTS) {
			util_mesh_udim_tiles_spent = true;
			continue;
		}
		for (i32 j = util_mesh_udim_tile_count; j > pos; --j) {
			util_mesh_udim_tiles[j] = util_mesh_udim_tiles[j - 1];
		}
		util_mesh_udim_tiles[pos] = id;
		util_mesh_udim_tile_count++;
	}
}

// Shared layers of a mesh split by udim tile keep every tile in its own atlas slot
bool util_mesh_udim_active() {
	return util_mesh_udim_tile_count > 1;
}

i32 util_mesh_udim_slot(i32 tile) {
	for (i32 i = 0; i < util_mesh_udim_tile_count; ++i) {
		if (util_mesh_udim_tiles[i] == tile) {
			return i;
		}
	}
	return util_mesh_udim_tiles_spent ? ATLAS_MAX_SLOTS - 1 : 0;
}

bool util_mesh_udim_layer(slot_layer_t *l) {
	if (!util_mesh_udim_active() || l == NULL || l->uv_map == 1) {
		return false;
	}
	i32 mask = slot_layer_get_object_mask(l);
	return mask == 0 || mask > g_project->_->paint_objects->length;
}

static i32 _util_mesh_atlas_slot_for_object(mesh_object_t *o) {
	if (util_mesh_udim_active()) {
		return util_mesh_udim_slot(util_mesh_udim_tile(o->base->name));
	}
	return 0;
}

static void _util_mesh_atlas_build_slots() {
	_util_mesh_udim_build_tiles();
	util_mesh_atlas_stride_merged = util_mesh_udim_active() ? _util_mesh_atlas_stride_for(util_mesh_udim_tile_count) : 1;
}

i32 util_mesh_atlas_stride() {
	return util_mesh_atlas_stride_merged;
}

i32 util_mesh_atlas_slot(object_t *object) {
	mesh_object_t_array_t *paint_objects = g_project->_->paint_objects;
	for (i32 i = 0; i < paint_objects->length; ++i) {
		if (paint_objects->buffer[i]->base == object) {
			return _util_mesh_atlas_slot_for_object(paint_objects->buffer[i]);
		}
	}
	return -1; // Merged object or 2d plane, uvs already in atlas space
}

void util_mesh_delete_data_uncache(void *data) {
	mesh_data_t *md = (mesh_data_t *)data;
	if (md->_->handle != NULL) {
		mesh_data_t *cached = any_map_get(data_cached_meshes, md->_->handle);
		if (cached == md) {
			map_delete(data_cached_meshes, md->_->handle);
		}
	}
	mesh_data_delete(md);
}

mesh_data_t *util_mesh_data_duplicate(mesh_data_t *source) {
	mesh_data_t *raw = calloc(1, sizeof(mesh_data_t));
	raw->name        = string_copy(source->name);
	raw->scale_pos   = source->scale_pos;
	raw->scale_tex   = source->scale_tex;

	raw->vertex_arrays = (vertex_array_t_array_t *)any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < source->vertex_arrays->length; ++i) {
		vertex_array_t *src = source->vertex_arrays->buffer[i];
		vertex_array_t *va =
		    ALLOC_INIT(vertex_array_t,
		               {.attrib = string_copy(src->attrib), .data = string_copy(src->data), .values = i16_array_create_from_array(src->values)});
		any_array_push(raw->vertex_arrays, va);
	}

	raw->index_array = u32_array_create_from_array(source->index_array);

	mesh_data_t *md    = mesh_data_create(raw);
	md->_->owns_arrays = true;
	md->_->skin_blob   = source->_->skin_blob;
	md->_->skin_frames = source->_->skin_frames;
	return md;
}

static f32 util_mesh_pack_merged_positions(mesh_object_t_array_t *paint_objects, object_t *parent, i16_array_t *va0, i16_array_t *va1) {
	bool   bake       = parent != NULL;
	mat4_t inv_parent = bake ? mat4_inv(parent->transform->world) : mat4_identity();
	f32    max_scale  = 0.0;

	for (i32 i = 0; i < paint_objects->length; ++i) {
		mesh_object_t *o = paint_objects->buffer[i];
		if (!bake) {
			max_scale = math_max(max_scale, o->data->scale_pos);
			continue;
		}
		mat4_t       m     = mat4_mult_mat(o->base->transform->world_unpack, inv_parent);
		i16_array_t *pos   = o->data->vertex_arrays->buffer[0]->values;
		i32          count = math_floor(pos->length / 4.0);
		for (i32 j = 0; j < count; ++j) {
			vec4_t p  = (vec4_t){pos->buffer[j * 4] / 32767.0, pos->buffer[j * 4 + 1] / 32767.0, pos->buffer[j * 4 + 2] / 32767.0, 1.0};
			p         = vec4_apply_mat4(p, m);
			max_scale = math_max(max_scale, math_max(math_abs(p.x), math_max(math_abs(p.y), math_abs(p.z))));
		}
	}
	if (max_scale <= 0.0) {
		max_scale = 1.0;
	}

	i32 voff = 0;
	for (i32 i = 0; i < paint_objects->length; ++i) {
		mesh_object_t *o     = paint_objects->buffer[i];
		i16_array_t   *pos   = o->data->vertex_arrays->buffer[0]->values;
		i16_array_t   *nor   = o->data->vertex_arrays->buffer[1]->values;
		i32            count = math_floor(pos->length / 4.0);

		if (bake) {
			mat4_t m  = mat4_mult_mat(o->base->transform->world_unpack, inv_parent);
			mat4_t nm = mat4_transpose3(mat4_inv(m));
			for (i32 j = 0; j < count; ++j) {
				i32    k = (voff + j) * 4;
				vec4_t p = (vec4_t){pos->buffer[j * 4] / 32767.0, pos->buffer[j * 4 + 1] / 32767.0, pos->buffer[j * 4 + 2] / 32767.0, 1.0};
				p        = vec4_apply_mat4(p, m);
				vec4_t n = (vec4_t){nor->buffer[j * 2] / 32767.0, nor->buffer[j * 2 + 1] / 32767.0, pos->buffer[j * 4 + 3] / 32767.0, 0.0};
				n        = vec4_norm(vec4_apply_mat4(n, nm));

				va0->buffer[k]                  = math_floor(p.x / max_scale * 32767);
				va0->buffer[k + 1]              = math_floor(p.y / max_scale * 32767);
				va0->buffer[k + 2]              = math_floor(p.z / max_scale * 32767);
				va0->buffer[k + 3]              = math_floor(n.z * 32767);
				va1->buffer[(voff + j) * 2]     = math_floor(n.x * 32767);
				va1->buffer[(voff + j) * 2 + 1] = math_floor(n.y * 32767);
			}
		}
		else {
			f32 scale = o->data->scale_pos;
			for (i32 j = 0; j < count; ++j) {
				i32 k              = (voff + j) * 4;
				va0->buffer[k]     = math_floor((pos->buffer[j * 4] * scale) / (float)max_scale);
				va0->buffer[k + 1] = math_floor((pos->buffer[j * 4 + 1] * scale) / (float)max_scale);
				va0->buffer[k + 2] = math_floor((pos->buffer[j * 4 + 2] * scale) / (float)max_scale);
				va0->buffer[k + 3] = pos->buffer[j * 4 + 3];
			}
			for (i32 j = 0; j < nor->length; ++j) {
				va1->buffer[j + voff * 2] = nor->buffer[j];
			}
		}

		voff += count;
	}
	return max_scale;
}

static mesh_data_t *util_mesh_build_merged_data(mesh_object_t_array_t *paint_objects, char *name, object_t *parent) {
	i32 vlen = 0;
	i32 ilen = 0;
	for (i32 i = 0; i < paint_objects->length; ++i) {
		vlen += paint_objects->buffer[i]->data->vertex_arrays->buffer[0]->values->length;
		ilen += paint_objects->buffer[i]->data->index_array->length;
	}
	vlen                = math_floor(vlen / 4.0);
	i16_array_t *va0    = i16_array_create(vlen * 4);
	i16_array_t *va1    = i16_array_create(vlen * 2);
	i16_array_t *va2    = i16_array_create(vlen * 2);
	i16_array_t *vatex1 = mesh_data_get_vertex_array(paint_objects->buffer[0]->data, "tex1") != NULL ? i16_array_create(vlen * 2) : NULL;
	i16_array_t *vacol  = mesh_data_get_vertex_array(paint_objects->buffer[0]->data, "col") != NULL ? i16_array_create(vlen * 4) : NULL;
	i32          tex1i  = 3;
	i32          coli   = vatex1 != NULL ? 4 : 3;
	u32_array_t *ia     = u32_array_create(ilen);

	_util_mesh_atlas_build_slots();
	i32 atlas_stride = util_mesh_atlas_stride_merged;

	// Pos, nor
	f32 max_scale = util_mesh_pack_merged_positions(paint_objects, parent, va0, va1);

	i32 voff = 0;
	i32 ioff = 0;
	for (i32 i = 0; i < paint_objects->length; ++i) {
		vertex_array_t_array_t *vas = paint_objects->buffer[i]->data->vertex_arrays;
		u32_array_t            *ias = paint_objects->buffer[i]->data->index_array;

		// Tex
		i32 slot      = _util_mesh_atlas_slot_for_object(paint_objects->buffer[i]);
		f32 tile_step = 32767.0f / atlas_stride;
		f32 tile_x    = atlas_stride > 1 ? (slot % atlas_stride) * tile_step : 0.0f;
		f32 tile_y    = atlas_stride > 1 ? (slot / atlas_stride) * tile_step : 0.0f;
		for (i32 j = 0; j < vas->buffer[2]->values->length / 2; ++j) {
			va2->buffer[j * 2 + voff * 2]     = vas->buffer[2]->values->buffer[j * 2] / (float)atlas_stride + tile_x;
			va2->buffer[j * 2 + 1 + voff * 2] = vas->buffer[2]->values->buffer[j * 2 + 1] / (float)atlas_stride + tile_y;
		}
		// Tex1
		if (vatex1 != NULL) {
			for (i32 j = 0; j < vas->buffer[tex1i]->values->length; ++j) {
				vatex1->buffer[j + voff * 2] = vas->buffer[tex1i]->values->buffer[j];
			}
		}
		// Col
		if (vacol != NULL) {
			for (i32 j = 0; j < vas->buffer[coli]->values->length; ++j) {
				vacol->buffer[j + voff * 4] = vas->buffer[coli]->values->buffer[j];
			}
		}
		// Indices
		for (i32 j = 0; j < ias->length; ++j) {
			ia->buffer[j + ioff] = ias->buffer[j] + voff;
		}

		voff += math_floor(vas->buffer[0]->values->length / 4.0);
		ioff += math_floor(ias->length);
	}
	mesh_data_t *raw = ALLOC_INIT(mesh_data_t, {.name          = name,
	                                            .vertex_arrays = any_array_create_from_raw(
	                                                (void *[]){
	                                                    ALLOC_INIT(vertex_array_t, {.values = va0, .attrib = "pos", .data = "short4norm"}),
	                                                    ALLOC_INIT(vertex_array_t, {.values = va1, .attrib = "nor", .data = "short2norm"}),
	                                                    ALLOC_INIT(vertex_array_t, {.values = va2, .attrib = "tex", .data = "short2norm"}),
	                                                },
	                                                3),
	                                            .index_array = ia,
	                                            .scale_pos   = max_scale,
	                                            .scale_tex   = 1.0});
	if (vatex1 != NULL) {
		vertex_array_t *va = ALLOC_INIT(vertex_array_t, {.values = vatex1, .attrib = "tex1", .data = "short2norm"});
		any_array_push(raw->vertex_arrays, va);
	}
	if (vacol != NULL) {
		vertex_array_t *va = ALLOC_INIT(vertex_array_t, {.values = vacol, .attrib = "col", .data = "short4norm"});
		any_array_push(raw->vertex_arrays, va);
	}
	return raw;
}

void util_mesh_merge(mesh_object_t_array_t *paint_objects) {
	if (paint_objects == NULL) {
		paint_objects = g_project->_->paint_objects;
	}
	if (paint_objects->length == 0) {
		return;
	}
	g_context->merged_object_is_atlas = paint_objects->length < g_project->_->paint_objects->length;

	object_t    *parent         = context_main_object()->base;
	bool         merged_visible = g_context->merged_object == NULL || g_context->merged_object->base->visible;
	mesh_data_t *raw            = util_mesh_build_merged_data(paint_objects, g_context->paint_object->base->name, parent);

	util_mesh_remove_merged();
	if (util_mesh_merged_objects == NULL) {
		util_mesh_merged_objects = any_array_create(0);
	}
	for (i32 i = 0; i < paint_objects->length; ++i) {
		any_array_push(util_mesh_merged_objects, paint_objects->buffer[i]);
	}

	mesh_data_t *md                           = mesh_data_create(raw);
	md->_->owns_arrays                        = true;
	shader_data_t *paint_material             = g_project->_->materials->buffer[0]->data;
	g_context->merged_object                  = mesh_object_create(md, paint_material);
	g_context->merged_object->base->name      = string("%s_merged", g_context->paint_object->base->name);
	g_context->merged_object->force_context   = "paint";
	g_context->merged_object->frustum_culling = false;
	g_context->merged_object->base->visible   = merged_visible;
	object_set_parent(g_context->merged_object->base, parent);
	transform_build_matrix(g_context->merged_object->base->transform);
	render_path_raytrace_ready = false;
}

bool util_mesh_merge_refresh() {
	mesh_object_t_array_t *objects = util_mesh_merged_objects;
	if (g_context->merged_object == NULL || objects == NULL || objects->length == 0) {
		return false;
	}
	mesh_data_t *md = g_context->merged_object->data;
	if (md->vertex_arrays->length < 2) {
		return false;
	}
	i16_array_t *va0  = md->vertex_arrays->buffer[0]->values;
	i16_array_t *va1  = md->vertex_arrays->buffer[1]->values;
	i32          vlen = 0;
	for (i32 i = 0; i < objects->length; ++i) {
		if (array_index_of(g_project->_->paint_objects, objects->buffer[i]) < 0) {
			return false; // Object was removed
		}
		vlen += objects->buffer[i]->data->vertex_arrays->buffer[0]->values->length;
	}
	vlen = math_floor(vlen / 4.0);
	if (vlen * 4 != va0->length || vlen * 2 != va1->length) {
		return false; // Vertex count changed
	}

	md->scale_pos = util_mesh_pack_merged_positions(objects, g_context->merged_object->base->parent, va0, va1);
	mesh_data_build_vertices(md->_->vertex_buffer, md->vertex_arrays);

	transform_t *t = g_context->merged_object->base->transform;
	t->scale_world = md->scale_pos;
	transform_build_matrix(t);
	return true;
}

void util_mesh_transform_changed() {
	if (g_context->merged_object != NULL && !util_mesh_merge_refresh()) {
		mesh_object_t_array_t *objects = any_array_create(0);
		if (util_mesh_merged_objects != NULL) {
			for (i32 i = 0; i < util_mesh_merged_objects->length; ++i) {
				if (array_index_of(g_project->_->paint_objects, util_mesh_merged_objects->buffer[i]) >= 0) {
					any_array_push(objects, util_mesh_merged_objects->buffer[i]);
				}
			}
		}
		util_mesh_merge(objects->length > 0 ? objects : NULL);
		array_delete(objects);
	}
	if (g_context->viewport_mode == VIEWPORT_MODE_PATH_TRACE) {
		sculpt_bake_to_mesh();
	}
	render_path_raytrace_ready = false;
}

void util_mesh_visibility_changed() {
	mesh_object_t_array_t *visibles = util_mesh_get_visible();
	util_mesh_merge(visibles);
	array_delete(visibles);
	util_uv_uvmap_cached       = false;
	util_uv_trianglemap_cached = false;
	util_uv_dilatemap_cached   = false;
	g_context->ddirty          = 2;
}

static void util_mesh_bake_transform(mesh_object_t *o, mat4_t inv_world) {
	mat4_t       m         = mat4_mult_mat(o->base->transform->world_unpack, inv_world);
	mat4_t       nm        = mat4_transpose3(mat4_inv(m));
	i16_array_t *va0       = o->data->vertex_arrays->buffer[0]->values;
	i16_array_t *va1       = o->data->vertex_arrays->buffer[1]->values;
	i32          num_verts = math_floor(va0->length / 4.0);
	f32_array_t *pos       = f32_array_create(num_verts * 3);
	f32          max_scale = 0.0;

	for (i32 i = 0; i < num_verts; ++i) {
		vec4_t p = (vec4_t){va0->buffer[i * 4] / 32767.0, va0->buffer[i * 4 + 1] / 32767.0, va0->buffer[i * 4 + 2] / 32767.0, 1.0};
		p        = vec4_apply_mat4(p, m);

		pos->buffer[i * 3]     = p.x;
		pos->buffer[i * 3 + 1] = p.y;
		pos->buffer[i * 3 + 2] = p.z;
		max_scale              = math_max(max_scale, math_max(math_abs(p.x), math_max(math_abs(p.y), math_abs(p.z))));

		vec4_t n = (vec4_t){va1->buffer[i * 2] / 32767.0, va1->buffer[i * 2 + 1] / 32767.0, va0->buffer[i * 4 + 3] / 32767.0, 0.0};
		n        = vec4_norm(vec4_apply_mat4(n, nm));

		va1->buffer[i * 2]     = math_floor(n.x * 32767);
		va1->buffer[i * 2 + 1] = math_floor(n.y * 32767);
		va0->buffer[i * 4 + 3] = math_floor(n.z * 32767);
	}

	if (max_scale <= 0.0) {
		max_scale = 1.0;
	}
	for (i32 i = 0; i < num_verts; ++i) {
		va0->buffer[i * 4]     = math_floor(pos->buffer[i * 3] / max_scale * 32767);
		va0->buffer[i * 4 + 1] = math_floor(pos->buffer[i * 3 + 1] / max_scale * 32767);
		va0->buffer[i * 4 + 2] = math_floor(pos->buffer[i * 3 + 2] / max_scale * 32767);
	}
	o->data->scale_pos = max_scale;
}

static void util_mesh_join_geometry(mesh_object_t_array_t *objects) {
	// Keep the first object and join the geometry of the rest into it
	mesh_object_t *main_object = objects->buffer[0];
	mat4_t         inv_world   = mat4_inv(main_object->base->transform->world);
	for (i32 i = 0; i < objects->length; ++i) {
		util_mesh_unshare_data(objects->buffer[i]);
	}
	for (i32 i = 0; i < objects->length; ++i) {
		util_mesh_bake_transform(objects->buffer[i], inv_world);
	}

	mesh_data_t *raw = util_mesh_build_merged_data(objects, main_object->data->name, NULL);
	util_mesh_remove_merged();

	mesh_data_t *md    = mesh_data_create(raw);
	md->_->owns_arrays = true;
	sys_notify_on_next_frame(&util_mesh_delete_data_uncache, main_object->data);
	mesh_object_set_data(main_object, md);
	transform_build_matrix(main_object->base->transform);
	md->_->handle = string_copy(raw->name);
	any_map_set(data_cached_meshes, md->_->handle, md);
}

static void util_mesh_geometry_joined() {
	util_mesh_merge(NULL);
	util_uv_uvmap_cached                              = false;
	util_uv_trianglemap_cached                        = false;
	util_uv_dilatemap_cached                          = false;
	g_context->ddirty                                 = 2;
	ui_base_hwnds->buffer[TAB_AREA_SIDEBAR0]->redraws = 2;
}

void util_mesh_merge_geometry() {
	mesh_object_t_array_t *objects = g_project->_->paint_objects;
	if (objects->length < 2) {
		return;
	}

	mesh_object_t *main_object = objects->buffer[0];
	util_mesh_join_geometry(objects);

	string_array_t *merged_names = string_array_create(0);
	for (i32 i = 1; i < objects->length; ++i) {
		mesh_object_t *o = objects->buffer[i];
		string_array_push(merged_names, string_copy(o->base->name));
		object_set_parent(o->base, NULL);
		data_delete_mesh(o->data->_->handle);
		mesh_object_remove(o);
	}

	g_project->_->paint_objects = any_array_create_from_raw(
	    (void *[]){
	        main_object,
	    },
	    1);
	context_select_paint_object(main_object);

	for (i32 i = 0; i < merged_names->length; ++i) {
		tab_timeline_on_mesh_deleted(merged_names->buffer[i]);
	}

	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		g_project->_->layers->buffer[i]->object_mask = 0;
	}
	g_context->layer_filter = 0;
	tab_stages_prune();
	tab_meshes_reset_preview_map();
	util_mesh_geometry_joined();
}

void util_mesh_merge_geometry_down(mesh_object_t *main_object, mesh_object_t *below) {
	mesh_object_t_array_t *objects    = g_project->_->paint_objects;
	i32                    main_index = array_index_of(objects, main_object);
	i32                    index      = array_index_of(objects, below);
	if (main_index < 0 || index < 0 || main_index == index) {
		return;
	}

	util_mesh_join_geometry(any_array_create_from_raw_tmp((void *[]){main_object, below}, 2));

	while (below->base->children->length > 0) {
		object_t *child  = below->base->children->buffer[0];
		mat4_t    world  = child->transform->world;
		object_t *parent = child == main_object->base ? below->base->parent : main_object->base;
		object_set_parent(child, parent);
		mat4_t parent_world = child->parent != NULL ? child->parent->transform->world : mat4_identity();
		transform_set_matrix(child->transform, mat4_mult_mat(world, mat4_inv(parent_world)));
	}

	char *merged_name = string_copy(below->base->name);
	array_splice(objects, index, 1);
	if (g_project->atlas_objects != NULL && index < g_project->atlas_objects->length) {
		i32_array_splice(g_project->atlas_objects, index, 1);
	}
	object_set_parent(below->base, NULL);
	util_mesh_delete_data_uncache(below->data);
	mesh_object_remove(below);

	i32 merged_mask = index + 1;
	i32 new_mask    = (main_index < index ? main_index : main_index - 1) + 1;
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		l->object_mask  = l->object_mask == merged_mask ? new_mask : l->object_mask > merged_mask ? l->object_mask - 1 : l->object_mask;
	}
	g_context->layer_filter = g_context->layer_filter == merged_mask  ? new_mask
	                          : g_context->layer_filter > merged_mask ? g_context->layer_filter - 1
	                                                                  : g_context->layer_filter;

	tab_meshes_sort_hierarchy();
	context_select_paint_object(main_object);
	tab_timeline_on_mesh_deleted(merged_name);
	tab_stages_prune();
	util_mesh_geometry_joined();
}

void util_mesh_swap_axis(mesh_object_t_array_t *objects, i32 a, i32 b) {
	if (objects == NULL) {
		objects = util_mesh_get_unique_data();
	}
	for (i32 i = 0; i < objects->length; ++i) {
		mesh_object_t *o = objects->buffer[i];
		// Remapping vertices, buckle up
		// 0 - x, 1 - y, 2 - z
		vertex_array_t_array_t *vas = o->data->vertex_arrays;
		i16_array_t            *pa  = vas->buffer[0]->values;
		i16_array_t            *na0 = a == 2 ? vas->buffer[0]->values : vas->buffer[1]->values;
		i16_array_t            *na1 = b == 2 ? vas->buffer[0]->values : vas->buffer[1]->values;
		i32                     c   = a == 2 ? 3 : a;
		i32                     d   = b == 2 ? 3 : b;
		i32                     e   = a == 2 ? 4 : 2;
		i32                     f   = b == 2 ? 4 : 2;
		for (i32 i = 0; i < math_floor(pa->length / 4.0); ++i) {
			i32 t                  = pa->buffer[i * 4 + a];
			pa->buffer[i * 4 + a]  = pa->buffer[i * 4 + b];
			pa->buffer[i * 4 + b]  = -t;
			t                      = na0->buffer[i * e + c];
			na0->buffer[i * e + c] = na1->buffer[i * f + d];
			na1->buffer[i * f + d] = -t;
		}
		mesh_data_t *g = o->data;
		mesh_data_build_vertices(g->_->vertex_buffer, vas);
	}
	util_mesh_merge(NULL);
}

void util_mesh_flip_normals(mesh_object_t_array_t *objects) {
	if (objects == NULL) {
		objects = util_mesh_get_unique_data();
	}
	for (i32 i = 0; i < objects->length; ++i) {
		mesh_object_t          *o   = objects->buffer[i];
		vertex_array_t_array_t *vas = o->data->vertex_arrays;
		i16_array_t            *va0 = vas->buffer[0]->values;
		i16_array_t            *va1 = vas->buffer[1]->values;
		for (i32 i = 0; i < va0->length / 4.0; ++i) {
			va0->buffer[i * 4 + 3] = -va0->buffer[i * 4 + 3];
			va1->buffer[i * 2]     = -va1->buffer[i * 2];
			va1->buffer[i * 2 + 1] = -va1->buffer[i * 2 + 1];
		}
		mesh_data_t *g = o->data;
		mesh_data_build_vertices(g->_->vertex_buffer, vas);
	}
	render_path_raytrace_ready = false;
}

i32 util_mesh_calc_normals_sort(i32 *pa, i32 *pb) {
	i32 a    = *(pa);
	i32 b    = *(pb);
	i32 diff = util_mesh_va0->buffer[a * 4] - util_mesh_va0->buffer[b * 4];
	if (diff != 0)
		return diff;
	diff = util_mesh_va0->buffer[a * 4 + 1] - util_mesh_va0->buffer[b * 4 + 1];
	if (diff != 0)
		return diff;
	return util_mesh_va0->buffer[a * 4 + 2] - util_mesh_va0->buffer[b * 4 + 2];
}

void util_mesh_calc_normals(mesh_object_t_array_t *objects, bool smooth) {
	vec4_t va = (vec4_t){0.0, 0.0, 0.0, 1.0};
	vec4_t vb = (vec4_t){0.0, 0.0, 0.0, 1.0};
	vec4_t vc = (vec4_t){0.0, 0.0, 0.0, 1.0};
	vec4_t cb = (vec4_t){0.0, 0.0, 0.0, 1.0};
	vec4_t ab = (vec4_t){0.0, 0.0, 0.0, 1.0};
	if (objects == NULL) {
		objects = util_mesh_get_unique_data();
	}
	for (i32 i = 0; i < objects->length; ++i) {
		mesh_object_t *o           = objects->buffer[i];
		mesh_data_t   *g           = o->data;
		u32_array_t   *inda        = g->index_array;
		i16_array_t   *va0         = o->data->vertex_arrays->buffer[0]->values;
		i16_array_t   *va1         = o->data->vertex_arrays->buffer[1]->values;
		i32            num_verts   = math_floor(va0->length / 4.0);
		f32_array_t   *smooth_vals = NULL;
		i32_array_t   *vert_map    = NULL;
		if (smooth) {
			smooth_vals          = f32_array_create(num_verts * 3);
			vert_map             = i32_array_create(num_verts);
			i32_array_t *indices = i32_array_create_from_raw((i32[]){}, 0);
			for (i32 j = 0; j < num_verts; ++j) {
				i32_array_push(indices, j);
			}
			util_mesh_va0 = va0;
			i32_array_sort(indices, &util_mesh_calc_normals_sort);
			if (indices->length > 0) {
				i32 unique_id                        = indices->buffer[0];
				vert_map->buffer[indices->buffer[0]] = unique_id;
				for (i32 j = 1; j < indices->length; ++j) {
					i32 curr = indices->buffer[j];
					i32 prev = indices->buffer[j - 1];
					if (va0->buffer[curr * 4] == va0->buffer[prev * 4] && va0->buffer[curr * 4 + 1] == va0->buffer[prev * 4 + 1] &&
					    va0->buffer[curr * 4 + 2] == va0->buffer[prev * 4 + 2]) {
						vert_map->buffer[curr] = unique_id;
					}
					else {
						unique_id              = curr;
						vert_map->buffer[curr] = unique_id;
					}
				}
			}
		}

		for (i32 i = 0; i < math_floor(inda->length / 3.0); ++i) {
			i32 i1 = inda->buffer[i * 3];
			i32 i2 = inda->buffer[i * 3 + 1];
			i32 i3 = inda->buffer[i * 3 + 2];
			va     = (vec4_t){va0->buffer[i1 * 4], va0->buffer[i1 * 4 + 1], va0->buffer[i1 * 4 + 2], 1.0};
			vb     = (vec4_t){va0->buffer[i2 * 4], va0->buffer[i2 * 4 + 1], va0->buffer[i2 * 4 + 2], 1.0};
			vc     = (vec4_t){va0->buffer[i3 * 4], va0->buffer[i3 * 4 + 1], va0->buffer[i3 * 4 + 2], 1.0};
			cb     = vec4_sub(vc, vb);
			ab     = vec4_sub(va, vb);
			cb     = vec4_cross(cb, ab);
			if (smooth) {
				i32 u1 = vert_map->buffer[i1];
				i32 u2 = vert_map->buffer[i2];
				i32 u3 = vert_map->buffer[i3];
				smooth_vals->buffer[u1 * 3] += cb.x;
				smooth_vals->buffer[u1 * 3 + 1] += cb.y;
				smooth_vals->buffer[u1 * 3 + 2] += cb.z;
				if (u2 != u1) {
					smooth_vals->buffer[u2 * 3] += cb.x;
					smooth_vals->buffer[u2 * 3 + 1] += cb.y;
					smooth_vals->buffer[u2 * 3 + 2] += cb.z;
				}
				if (u3 != u1 && u3 != u2) {
					smooth_vals->buffer[u3 * 3] += cb.x;
					smooth_vals->buffer[u3 * 3 + 1] += cb.y;
					smooth_vals->buffer[u3 * 3 + 2] += cb.z;
				}
			}
			else {
				cb                      = vec4_norm(cb);
				i32 nx                  = math_floor(cb.x * 32767);
				i32 ny                  = math_floor(cb.y * 32767);
				i32 nz                  = math_floor(cb.z * 32767);
				va1->buffer[i1 * 2]     = nx;
				va1->buffer[i1 * 2 + 1] = ny;
				va0->buffer[i1 * 4 + 3] = nz;
				va1->buffer[i2 * 2]     = nx;
				va1->buffer[i2 * 2 + 1] = ny;
				va0->buffer[i2 * 4 + 3] = nz;
				va1->buffer[i3 * 2]     = nx;
				va1->buffer[i3 * 2 + 1] = ny;
				va0->buffer[i3 * 4 + 3] = nz;
			}
		}

		if (smooth) {
			for (i32 j = 0; j < num_verts; ++j) {
				i32 u  = vert_map->buffer[j];
				f32 nx = smooth_vals->buffer[u * 3];
				f32 ny = smooth_vals->buffer[u * 3 + 1];
				f32 nz = smooth_vals->buffer[u * 3 + 2];
				f32 l  = math_sqrt(nx * nx + ny * ny + nz * nz);
				if (l > 0.0001) {
					nx /= l;
					ny /= l;
					nz /= l;
				}
				va1->buffer[j * 2]     = math_floor(nx * 32767);
				va1->buffer[j * 2 + 1] = math_floor(ny * 32767);
				va0->buffer[j * 4 + 3] = math_floor(nz * 32767);
			}
		}

		mesh_data_build_vertices(g->_->vertex_buffer, o->data->vertex_arrays);
	}

	util_mesh_merge(NULL);
	render_path_raytrace_ready = false;
}

void util_mesh_to_origin() {
	mesh_object_t_array_t *objects = util_mesh_get_unique_data();
	f32                    dx      = 0.0;
	f32                    dy      = 0.0;
	f32                    dz      = 0.0;
	for (i32 i = 0; i < objects->length; ++i) {
		mesh_object_t *o    = objects->buffer[i];
		i32            l    = 4;
		f32            sc   = o->data->scale_pos / 32767.0;
		i16_array_t   *va   = o->data->vertex_arrays->buffer[0]->values;
		f32            minx = va->buffer[0];
		f32            maxx = va->buffer[0];
		f32            miny = va->buffer[1];
		f32            maxy = va->buffer[1];
		f32            minz = va->buffer[2];
		f32            maxz = va->buffer[2];
		for (i32 i = 1; i < math_floor(va->length / (float)l); ++i) {
			if (va->buffer[i * l] < minx) {
				minx = va->buffer[i * l];
			}
			else if (va->buffer[i * l] > maxx) {
				maxx = va->buffer[i * l];
			}
			if (va->buffer[i * l + 1] < miny) {
				miny = va->buffer[i * l + 1];
			}
			else if (va->buffer[i * l + 1] > maxy) {
				maxy = va->buffer[i * l + 1];
			}
			if (va->buffer[i * l + 2] < minz) {
				minz = va->buffer[i * l + 2];
			}
			else if (va->buffer[i * l + 2] > maxz) {
				maxz = va->buffer[i * l + 2];
			}
		}
		dx += (minx + maxx) / 2.0 * sc;
		dy += (miny + maxy) / 2.0 * sc;
		dz += (minz + maxz) / 2.0 * sc;
	}
	dx /= objects->length;
	dy /= objects->length;
	dz /= objects->length;

	for (i32 i = 0; i < objects->length; ++i) {
		mesh_object_t *o         = objects->buffer[i];
		mesh_data_t   *g         = o->data;
		f32            sc        = o->data->scale_pos / 32767.0;
		i16_array_t   *va        = o->data->vertex_arrays->buffer[0]->values;
		f32            max_scale = 0.0;
		for (i32 i = 0; i < math_floor(va->length / 4.0); ++i) {
			if (math_abs(va->buffer[i * 4] * sc - dx) > max_scale) {
				max_scale = math_abs(va->buffer[i * 4] * sc - dx);
			}
			if (math_abs(va->buffer[i * 4 + 1] * sc - dy) > max_scale) {
				max_scale = math_abs(va->buffer[i * 4 + 1] * sc - dy);
			}
			if (math_abs(va->buffer[i * 4 + 2] * sc - dz) > max_scale) {
				max_scale = math_abs(va->buffer[i * 4 + 2] * sc - dz);
			}
		}
		o->base->transform->scale_world = o->data->scale_pos = o->data->scale_pos = max_scale;
		transform_build_matrix(o->base->transform);
		util_mesh_sync_scale_world(o->data);

		for (i32 i = 0; i < math_floor(va->length / 4.0); ++i) {
			va->buffer[i * 4]     = math_floor((va->buffer[i * 4] * sc - dx) / (float)max_scale * 32767);
			va->buffer[i * 4 + 1] = math_floor((va->buffer[i * 4 + 1] * sc - dy) / (float)max_scale * 32767);
			va->buffer[i * 4 + 2] = math_floor((va->buffer[i * 4 + 2] * sc - dz) / (float)max_scale * 32767);
		}

		mesh_data_build_vertices(g->_->vertex_buffer, o->data->vertex_arrays);
	}

	util_mesh_merge(NULL);
}

void util_mesh_origin_to_geometry(mesh_object_t_array_t *objects) {
	for (i32 i = 0; i < objects->length; ++i) {
		mesh_data_t *g  = objects->buffer[i]->data;
		i16_array_t *va = g->vertex_arrays->buffer[0]->values;
		i32          n  = math_floor(va->length / 4.0);
		if (n == 0) {
			continue;
		}
		f32 lo[3] = {va->buffer[0], va->buffer[1], va->buffer[2]};
		f32 hi[3] = {va->buffer[0], va->buffer[1], va->buffer[2]};
		for (i32 j = 1; j < n; ++j) {
			for (i32 k = 0; k < 3; ++k) {
				f32 v = va->buffer[j * 4 + k];
				lo[k] = v < lo[k] ? v : lo[k];
				hi[k] = v > hi[k] ? v : hi[k];
			}
		}
		f32 sc        = g->scale_pos / 32767.0;
		f32 c[3]      = {(lo[0] + hi[0]) / 2.0 * sc, (lo[1] + hi[1]) / 2.0 * sc, (lo[2] + hi[2]) / 2.0 * sc};
		f32 max_scale = 0.0;
		for (i32 k = 0; k < 3; ++k) {
			max_scale = math_max(max_scale, math_max(math_abs(lo[k] * sc - c[k]), math_abs(hi[k] * sc - c[k])));
		}
		if (max_scale == 0.0) {
			continue;
		}

		for (i32 j = 0; j < n; ++j) {
			for (i32 k = 0; k < 3; ++k) {
				va->buffer[j * 4 + k] = math_round((va->buffer[j * 4 + k] * sc - c[k]) / max_scale * 32767);
			}
		}
		g->scale_pos = max_scale;
		mesh_data_build_vertices(g->_->vertex_buffer, g->vertex_arrays);

		// Every object using this data moves by the offset, its children move back
		for (i32 j = 0; j < g_project->_->paint_objects->length; ++j) {
			mesh_object_t *p = g_project->_->paint_objects->buffer[j];
			if (p->data != g) {
				continue;
			}
			transform_t *t = p->base->transform;
			vec4_t       d = vec4_apply_quat((vec4_t){c[0] * t->scale.x, c[1] * t->scale.y, c[2] * t->scale.z, 0.0}, t->rot);
			t->loc         = (vec4_t){t->loc.x + d.x, t->loc.y + d.y, t->loc.z + d.z, t->loc.w};
			for (i32 k = 0; k < p->base->children->length; ++k) {
				transform_t *ct = ((object_t *)p->base->children->buffer[k])->transform;
				ct->loc         = (vec4_t){ct->loc.x - c[0], ct->loc.y - c[1], ct->loc.z - c[2], ct->loc.w};
			}
		}
		util_mesh_sync_scale_world(g);
	}

	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		transform_build_matrix(g_project->_->paint_objects->buffer[i]->base->transform);
	}
	util_mesh_transform_changed();
}

static void _util_mesh_apply_displacement(mesh_object_t *o, buffer_t *height, i32 res, f32 strength, f32 uv_scale) {
	mesh_data_t *g         = o->data;
	i16_array_t *va0       = g->vertex_arrays->buffer[0]->values;
	i16_array_t *va1       = g->vertex_arrays->buffer[1]->values;
	i16_array_t *va2       = g->vertex_arrays->buffer[2]->values;
	i32          num_verts = math_floor(va0->length / 4.0);
	for (i32 i = 0; i < num_verts; ++i) {
		i32 x  = math_floor(va2->buffer[i * 2] / 32767.0 * res);
		i32 y  = math_floor(va2->buffer[i * 2 + 1] / 32767.0 * res);
		i32 ix = math_floor(x * uv_scale);
		i32 iy = math_floor(y * uv_scale);
		i32 xx = ix % res;
		i32 yy = iy % res;
		f32 h  = (1.0 - buffer_get_u8(height, (yy * res + xx) * 4 + 3) / 255.0) * strength;
		va0->buffer[i * 4] -= math_floor(va1->buffer[i * 2] * h);
		va0->buffer[i * 4 + 1] -= math_floor(va1->buffer[i * 2 + 1] * h);
		va0->buffer[i * 4 + 2] -= math_floor(va0->buffer[i * 4 + 3] * h);
	}
	mesh_data_build_vertices(g->_->vertex_buffer, o->data->vertex_arrays);
}

void util_mesh_apply_displacement(mesh_object_t_array_t *objects, gpu_texture_t *texpaint_pack, f32 strength, f32 uv_scale) {
	if (objects == NULL) {
		objects = util_mesh_get_unique_data();
	}
	buffer_t *height = gpu_get_texture_pixels(texpaint_pack);
	for (i32 i = 0; i < objects->length; ++i) {
		_util_mesh_apply_displacement(objects->buffer[i], height, texpaint_pack->width, strength, uv_scale);
	}
}

void util_mesh_uv_unwrap(mesh_object_t_array_t *objects) {
	if (objects == NULL) {
		objects = g_project->_->paint_objects;
	}
	util_mesh_merge(objects);
	if (g_context->merged_object == NULL) {
		return;
	}
	if (g_context->merged_object->data->index_array->length == 0) {
		util_mesh_merge(NULL);
		return;
	}

	mesh_data_t *mmd         = g_context->merged_object->data;
	i16_array_t *merged_posa = mmd->vertex_arrays->buffer[0]->values;
	i16_array_t *merged_nora = mmd->vertex_arrays->buffer[1]->values;
	u32_array_t *merged_inda = mmd->index_array;

	i16_array_t *posa      = i16_array_create(merged_posa->length);
	i16_array_t *nora      = i16_array_create(merged_nora->length);
	f32          max_scale = util_mesh_pack_merged_positions(util_mesh_merged_objects, NULL, posa, nora);

	u32_array_t *inda = malloc(sizeof(u32_array_t));
	inda->length = inda->capacity = merged_inda->length;
	inda->buffer                  = malloc(merged_inda->length * sizeof(u32));
	memcpy(inda->buffer, merged_inda->buffer, merged_inda->length * sizeof(u32));

	raw_mesh_t *mesh = ALLOC_INIT(raw_mesh_t, {.posa = posa, .nora = nora, .texa = NULL, .inda = inda});
	util_uv_unwrap_run(mesh);

	i32 ioff = 0;
	for (i32 i = 0; i < objects->length; ++i) {
		mesh_data_t *md      = objects->buffer[i]->data;
		i32          ilen    = md->index_array->length;
		f32          rescale = max_scale / md->scale_pos;

		i16_array_t *new_posa = i16_array_create(ilen * 4);
		i16_array_t *new_nora = i16_array_create(ilen * 2);
		i16_array_t *new_texa = i16_array_create(ilen * 2);
		u32_array_t *new_inda = u32_array_create(ilen);

		for (i32 j = 0; j < ilen; ++j) {
			i32 src                     = (ioff + j) * 4;
			new_posa->buffer[j * 4]     = (i16)math_floor(mesh->posa->buffer[src] * rescale);
			new_posa->buffer[j * 4 + 1] = (i16)math_floor(mesh->posa->buffer[src + 1] * rescale);
			new_posa->buffer[j * 4 + 2] = (i16)math_floor(mesh->posa->buffer[src + 2] * rescale);
			new_posa->buffer[j * 4 + 3] = mesh->posa->buffer[src + 3];
		}
		for (i32 j = 0; j < ilen * 2; ++j) {
			new_nora->buffer[j] = mesh->nora->buffer[ioff * 2 + j];
		}
		for (i32 j = 0; j < ilen * 2; ++j) {
			new_texa->buffer[j] = mesh->texa->buffer[ioff * 2 + j];
		}
		for (i32 j = 0; j < ilen; ++j) {
			new_inda->buffer[j] = (u32)j;
		}

		md->vertex_arrays->buffer[0]->values = new_posa;
		md->vertex_arrays->buffer[1]->values = new_nora;
		md->vertex_arrays->buffer[2]->values = new_texa;
		md->index_array                      = new_inda;

		mesh_data_build(md);
		ioff += ilen;
	}

	free(mesh->posa->buffer);
	free(mesh->posa);
	free(mesh->nora->buffer);
	free(mesh->nora);
	free(mesh->texa->buffer);
	free(mesh->texa);
	free(mesh->inda->buffer);
	free(mesh->inda);

	util_mesh_merge(NULL);
	util_uv_uvmap_cached = false;
}

static i32 *_cc_he_vlo;
static i32 *_cc_he_vhi;

static i32 _util_mesh_subdivide_sort(i32 *pa, i32 *pb) {
	i32 a    = *pa;
	i32 b    = *pb;
	i32 diff = _cc_he_vlo[a] - _cc_he_vlo[b];
	if (diff != 0)
		return diff;
	return _cc_he_vhi[a] - _cc_he_vhi[b];
}

typedef struct decimate_edge {
	f64 cost;
	i32 a;
	i32 b;
	i32 stamp_a;
	i32 stamp_b;
	f64 x;
	f64 y;
	f64 z;
} decimate_edge_t;

typedef struct decimate_list {
	i32 *data;
	i32  length;
	i32  capacity;
} decimate_list_t;

typedef struct decimate {
	f64             *pos;   // 3 per vertex
	f64             *quad;  // Symmetric 4x4, 10 per vertex
	i32             *stamp; // Bumped when a vertex changes, invalidates its queued edges
	bool            *dead;  //
	bool            *bnd;   // On an open boundary
	decimate_list_t *adj;   // Triangles around each vertex, may hold dead ones
	i32             *tri;   // 3 per triangle
	bool            *tdead; //
	decimate_edge_t *heap;  //
	i32              heap_length;
	i32              heap_capacity;
	i32             *mark; // Scratch for the link test
	i32              mark_id;
	f64              bias; // Prefers short edges when costs tie, as in flat regions
} decimate_t;

static void _util_mesh_decimate_list_push(decimate_list_t *l, i32 v) {
	if (l->length == l->capacity) {
		l->capacity = l->capacity == 0 ? 8 : l->capacity * 2;
		l->data     = realloc(l->data, l->capacity * sizeof(i32));
	}
	l->data[l->length++] = v;
}

static void _util_mesh_decimate_heap_push(decimate_t *d, decimate_edge_t e) {
	if (d->heap_length == d->heap_capacity) {
		d->heap_capacity = d->heap_capacity == 0 ? 1024 : d->heap_capacity * 2;
		d->heap          = realloc(d->heap, d->heap_capacity * sizeof(decimate_edge_t));
	}
	i32 i = d->heap_length++;
	while (i > 0) {
		i32 p = (i - 1) / 2;
		if (d->heap[p].cost <= e.cost) {
			break;
		}
		d->heap[i] = d->heap[p];
		i          = p;
	}
	d->heap[i] = e;
}

static decimate_edge_t _util_mesh_decimate_heap_pop(decimate_t *d) {
	decimate_edge_t top  = d->heap[0];
	decimate_edge_t last = d->heap[--d->heap_length];
	i32             n    = d->heap_length;
	i32             i    = 0;
	while (true) {
		i32 c = i * 2 + 1;
		if (c >= n) {
			break;
		}
		if (c + 1 < n && d->heap[c + 1].cost < d->heap[c].cost) {
			c++;
		}
		if (last.cost <= d->heap[c].cost) {
			break;
		}
		d->heap[i] = d->heap[c];
		i          = c;
	}
	if (n > 0) {
		d->heap[i] = last;
	}
	return top;
}

// Quadric of the plane n.p + w = 0, scaled by weight
static void _util_mesh_decimate_add_plane(f64 *q, f64 nx, f64 ny, f64 nz, f64 w, f64 weight) {
	q[0] += weight * nx * nx;
	q[1] += weight * nx * ny;
	q[2] += weight * nx * nz;
	q[3] += weight * nx * w;
	q[4] += weight * ny * ny;
	q[5] += weight * ny * nz;
	q[6] += weight * ny * w;
	q[7] += weight * nz * nz;
	q[8] += weight * nz * w;
	q[9] += weight * w * w;
}

static f64 _util_mesh_decimate_error(f64 *q, f64 x, f64 y, f64 z) {
	return q[0] * x * x + 2 * q[1] * x * y + 2 * q[2] * x * z + 2 * q[3] * x + q[4] * y * y + 2 * q[5] * y * z + 2 * q[6] * y + q[7] * z * z + 2 * q[8] * z +
	       q[9];
}

static void _util_mesh_decimate_push_edge(decimate_t *d, i32 a, i32 b) {
	f64  q[10];
	f64 *qa = &d->quad[a * 10];
	f64 *qb = &d->quad[b * 10];
	for (i32 i = 0; i < 10; ++i)
		q[i] = qa[i] + qb[i];
	f64 *pa  = &d->pos[a * 3];
	f64 *pb  = &d->pos[b * 3];
	f64  ex  = pb[0] - pa[0];
	f64  ey  = pb[1] - pa[1];
	f64  ez  = pb[2] - pa[2];
	f64  len = ex * ex + ey * ey + ez * ez;

	// Boundary vertices stay put unless the whole edge runs along the boundary
	bool fix_a = d->bnd[a] && !d->bnd[b];
	bool fix_b = d->bnd[b] && !d->bnd[a];

	decimate_edge_t e  = {.a = a, .b = b, .stamp_a = d->stamp[a], .stamp_b = d->stamp[b]};
	bool            ok = false;
	if (!fix_a && !fix_b) {
		// Minimize the error: solve A p = -b
		f64 det = q[0] * (q[4] * q[7] - q[5] * q[5]) - q[1] * (q[1] * q[7] - q[5] * q[2]) + q[2] * (q[1] * q[5] - q[4] * q[2]);
		f64 tr  = (q[0] + q[4] + q[7]) / 3.0;
		if (fabs(det) > 1e-6 * tr * tr * tr && tr > 0.0) {
			f64 inv = 1.0 / det;
			e.x     = -inv * (q[3] * (q[4] * q[7] - q[5] * q[5]) - q[6] * (q[1] * q[7] - q[2] * q[5]) + q[8] * (q[1] * q[5] - q[2] * q[4]));
			e.y     = -inv * (-q[3] * (q[1] * q[7] - q[5] * q[2]) + q[6] * (q[0] * q[7] - q[2] * q[2]) - q[8] * (q[0] * q[5] - q[1] * q[2]));
			e.z     = -inv * (q[3] * (q[1] * q[5] - q[4] * q[2]) - q[6] * (q[0] * q[5] - q[2] * q[1]) + q[8] * (q[0] * q[4] - q[1] * q[1]));
			// Near-singular systems can shoot the vertex far away
			f64 mx = e.x - (pa[0] + pb[0]) * 0.5;
			f64 my = e.y - (pa[1] + pb[1]) * 0.5;
			f64 mz = e.z - (pa[2] + pb[2]) * 0.5;
			ok     = mx * mx + my * my + mz * mz <= len * 4.0;
			if (ok) {
				e.cost = _util_mesh_decimate_error(q, e.x, e.y, e.z);
			}
		}
	}
	if (!ok) {
		// Pick the best of the endpoints and the midpoint
		f64 cand[9] = {pa[0], pa[1], pa[2], pb[0], pb[1], pb[2], (pa[0] + pb[0]) * 0.5, (pa[1] + pb[1]) * 0.5, (pa[2] + pb[2]) * 0.5};
		e.cost      = -1.0;
		for (i32 i = 0; i < 3; ++i) {
			if ((fix_a && i != 0) || (fix_b && i != 1)) {
				continue;
			}
			f64 c = _util_mesh_decimate_error(q, cand[i * 3], cand[i * 3 + 1], cand[i * 3 + 2]);
			if (e.cost < 0.0 || c < e.cost) {
				e.cost = c;
				e.x    = cand[i * 3];
				e.y    = cand[i * 3 + 1];
				e.z    = cand[i * 3 + 2];
			}
		}
	}
	e.cost = (e.cost > 0.0 ? e.cost : 0.0) + d->bias * len;
	_util_mesh_decimate_heap_push(d, e);
}

static bool _util_mesh_decimate_has(i32 *t, i32 v) {
	return t[0] == v || t[1] == v || t[2] == v;
}

// Collapsing a into b must keep the surface manifold and must not flip any triangle
static bool _util_mesh_decimate_can_collapse(decimate_t *d, i32 a, i32 b, f64 x, f64 y, f64 z) {
	// Link condition: the only vertices next to both are the tips of the triangles on the edge
	d->mark_id++;
	for (i32 k = 0; k < 2; ++k) {
		i32              v = k == 0 ? a : b;
		decimate_list_t *l = &d->adj[v];
		for (i32 i = 0; i < l->length; ++i) {
			i32 *t = &d->tri[l->data[i] * 3];
			if (d->tdead[l->data[i]]) {
				continue;
			}
			for (i32 j = 0; j < 3; ++j) {
				i32 w = t[j];
				if (w == a || w == b) {
					continue;
				}
				if (k == 0) {
					d->mark[w] = d->mark_id;
				}
				else if (d->mark[w] == d->mark_id) {
					d->mark[w] = -d->mark_id; // Counted once
				}
			}
		}
	}
	i32 common = 0;
	i32 shared = 0;
	for (i32 k = 0; k < 2; ++k) {
		decimate_list_t *l = &d->adj[k == 0 ? a : b];
		for (i32 i = 0; i < l->length; ++i) {
			if (d->tdead[l->data[i]]) {
				continue;
			}
			i32 *t = &d->tri[l->data[i] * 3];
			for (i32 j = 0; j < 3; ++j) {
				if (d->mark[t[j]] == -d->mark_id) {
					d->mark[t[j]] = 0;
					common++;
				}
			}
			if (k == 0 && _util_mesh_decimate_has(t, b)) {
				shared++;
			}
		}
	}
	if (common != shared || shared == 0) {
		return false;
	}
	// An inner edge joining two boundary vertices would pinch the surface
	if (shared == 2 && d->bnd[a] && d->bnd[b]) {
		return false;
	}

	// Triangles that survive must keep facing the same way and not collapse to slivers
	for (i32 k = 0; k < 2; ++k) {
		i32              v = k == 0 ? a : b;
		decimate_list_t *l = &d->adj[v];
		for (i32 i = 0; i < l->length; ++i) {
			i32  ti = l->data[i];
			i32 *t  = &d->tri[ti * 3];
			if (d->tdead[ti] || (_util_mesh_decimate_has(t, a) && _util_mesh_decimate_has(t, b))) {
				continue;
			}
			f64 p[3][3];
			f64 o[3][3];
			for (i32 j = 0; j < 3; ++j) {
				f64 *s  = &d->pos[t[j] * 3];
				o[j][0] = s[0];
				o[j][1] = s[1];
				o[j][2] = s[2];
				if (t[j] == v) {
					p[j][0] = x;
					p[j][1] = y;
					p[j][2] = z;
				}
				else {
					p[j][0] = s[0];
					p[j][1] = s[1];
					p[j][2] = s[2];
				}
			}
			f64 n0[3];
			f64 n1[3];
			for (i32 m = 0; m < 2; ++m) {
				f64(*s)[3] = m == 0 ? o : p;
				f64 *n     = m == 0 ? n0 : n1;
				f64  ux    = s[1][0] - s[0][0];
				f64  uy    = s[1][1] - s[0][1];
				f64  uz    = s[1][2] - s[0][2];
				f64  vx    = s[2][0] - s[0][0];
				f64  vy    = s[2][1] - s[0][1];
				f64  vz    = s[2][2] - s[0][2];
				n[0]       = uy * vz - uz * vy;
				n[1]       = uz * vx - ux * vz;
				n[2]       = ux * vy - uy * vx;
			}
			f64 l0 = sqrt(n0[0] * n0[0] + n0[1] * n0[1] + n0[2] * n0[2]);
			f64 l1 = sqrt(n1[0] * n1[0] + n1[1] * n1[1] + n1[2] * n1[2]);
			if (l1 <= 1e-20 || (n0[0] * n1[0] + n0[1] * n1[1] + n0[2] * n1[2]) < 0.2 * l0 * l1) {
				return false;
			}
		}
	}
	return true;
}

static void _util_mesh_decimate(mesh_object_t *o, f32 ratio) {
	mesh_data_t *g         = o->data;
	i16_array_t *va0       = g->vertex_arrays->buffer[0]->values;
	u32_array_t *inda      = g->index_array;
	i32          num_verts = va0->length / 4;
	i32          num_tris  = inda->length / 3;

	if (ratio >= 1.0f || num_tris == 0) {
		return;
	}
	if (ratio < 0.0f) {
		ratio = 0.0f;
	}

	// Position-weld, collapse works on the connected surface
	i32_array_t *weld_sort = i32_array_create(num_verts);
	for (i32 i = 0; i < num_verts; ++i)
		weld_sort->buffer[i] = i;
	util_mesh_va0 = va0;
	i32_array_sort(weld_sort, &util_mesh_calc_normals_sort);
	i32 *compact_id  = malloc(num_verts * sizeof(i32));
	i32  num_compact = 0;
	for (i32 i = 0; i < num_verts; ++i) {
		i32 curr = weld_sort->buffer[i];
		if (i > 0) {
			i32 prev = weld_sort->buffer[i - 1];
			if (va0->buffer[curr * 4] == va0->buffer[prev * 4] && va0->buffer[curr * 4 + 1] == va0->buffer[prev * 4 + 1] &&
			    va0->buffer[curr * 4 + 2] == va0->buffer[prev * 4 + 2]) {
				compact_id[curr] = compact_id[prev];
				continue;
			}
		}
		compact_id[curr] = num_compact++;
	}
	array_delete(weld_sort);

	decimate_t d = {0};
	d.pos        = calloc(num_compact * 3, sizeof(f64));
	d.quad       = calloc(num_compact * 10, sizeof(f64));
	d.stamp      = calloc(num_compact, sizeof(i32));
	d.dead       = calloc(num_compact, sizeof(bool));
	d.bnd        = calloc(num_compact, sizeof(bool));
	d.adj        = calloc(num_compact, sizeof(decimate_list_t));
	d.mark       = calloc(num_compact, sizeof(i32));
	d.tri        = malloc(num_tris * 3 * sizeof(i32));
	d.tdead      = calloc(num_tris, sizeof(bool));
	for (i32 i = 0; i < num_verts; ++i) {
		i32 c            = compact_id[i];
		d.pos[c * 3]     = va0->buffer[i * 4] / 32767.0;
		d.pos[c * 3 + 1] = va0->buffer[i * 4 + 1] / 32767.0;
		d.pos[c * 3 + 2] = va0->buffer[i * 4 + 2] / 32767.0;
	}

	// Face quadrics, weighted by area
	i32 alive    = 0;
	f64 area_sum = 0.0;
	f64 len2_sum = 0.0;
	for (i32 t = 0; t < num_tris; ++t) {
		i32 *v = &d.tri[t * 3];
		for (i32 k = 0; k < 3; ++k)
			v[k] = compact_id[inda->buffer[t * 3 + k]];
		if (v[0] == v[1] || v[1] == v[2] || v[0] == v[2]) {
			d.tdead[t] = true;
			continue;
		}
		f64 *p0 = &d.pos[v[0] * 3];
		f64 *p1 = &d.pos[v[1] * 3];
		f64 *p2 = &d.pos[v[2] * 3];
		f64  ux = p1[0] - p0[0], uy = p1[1] - p0[1], uz = p1[2] - p0[2];
		f64  vx = p2[0] - p0[0], vy = p2[1] - p0[1], vz = p2[2] - p0[2];
		f64  nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
		f64  l = sqrt(nx * nx + ny * ny + nz * nz);
		if (l > 0.0) {
			nx /= l;
			ny /= l;
			nz /= l;
			f64 w = -(nx * p0[0] + ny * p0[1] + nz * p0[2]);
			for (i32 k = 0; k < 3; ++k)
				_util_mesh_decimate_add_plane(&d.quad[v[k] * 10], nx, ny, nz, w, l * 0.5);
		}
		area_sum += l * 0.5;
		len2_sum += ux * ux + uy * uy + uz * uz;
		for (i32 k = 0; k < 3; ++k)
			_util_mesh_decimate_list_push(&d.adj[v[k]], t);
		alive++;
	}

	// Unique edges, open ones also get a plane perpendicular to the face to hold the outline
	i32          num_he = num_tris * 3;
	i32         *he_vlo = malloc(num_he * sizeof(i32));
	i32         *he_vhi = malloc(num_he * sizeof(i32));
	i32_array_t *order  = i32_array_create(0);
	for (i32 h = 0; h < num_he; ++h) {
		i32 a     = d.tri[h];
		i32 b     = d.tri[(h / 3) * 3 + (h % 3 + 1) % 3];
		he_vlo[h] = a < b ? a : b;
		he_vhi[h] = a < b ? b : a;
		if (!d.tdead[h / 3]) {
			i32_array_push(order, h);
		}
	}
	_cc_he_vlo = he_vlo;
	_cc_he_vhi = he_vhi;
	i32_array_sort(order, &_util_mesh_subdivide_sort);
	i32 *edges     = malloc(order->length * 2 * sizeof(i32));
	i32  num_edges = 0;
	for (i32 i = 0; i < order->length;) {
		i32 j = i + 1;
		while (j < order->length && he_vlo[order->buffer[j]] == he_vlo[order->buffer[i]] && he_vhi[order->buffer[j]] == he_vhi[order->buffer[i]])
			++j;
		i32 h                    = order->buffer[i];
		edges[num_edges * 2]     = he_vlo[h];
		edges[num_edges * 2 + 1] = he_vhi[h];
		num_edges++;
		if (j - i == 1) {
			i32  t  = h / 3;
			i32  a  = d.tri[h];
			i32  b  = d.tri[t * 3 + (h % 3 + 1) % 3];
			i32  c  = d.tri[t * 3 + (h % 3 + 2) % 3];
			f64 *pa = &d.pos[a * 3];
			f64 *pb = &d.pos[b * 3];
			f64 *pc = &d.pos[c * 3];
			f64  ex = pb[0] - pa[0], ey = pb[1] - pa[1], ez = pb[2] - pa[2];
			f64  cx = pc[0] - pa[0], cy = pc[1] - pa[1], cz = pc[2] - pa[2];
			// Face normal, then the edge plane normal = edge x face normal
			f64 fx = ey * cz - ez * cy, fy = ez * cx - ex * cz, fz = ex * cy - ey * cx;
			f64 nx = ey * fz - ez * fy, ny = ez * fx - ex * fz, nz = ex * fy - ey * fx;
			f64 l = sqrt(nx * nx + ny * ny + nz * nz);
			if (l > 0.0) {
				nx /= l;
				ny /= l;
				nz /= l;
				f64 w      = -(nx * pa[0] + ny * pa[1] + nz * pa[2]);
				f64 weight = (ex * ex + ey * ey + ez * ez) * 100.0;
				_util_mesh_decimate_add_plane(&d.quad[a * 10], nx, ny, nz, w, weight);
				_util_mesh_decimate_add_plane(&d.quad[b * 10], nx, ny, nz, w, weight);
			}
			d.bnd[a] = true;
			d.bnd[b] = true;
		}
		i = j;
	}
	free(he_vlo);
	free(he_vhi);
	array_delete(order);

	d.bias = alive > 0 && len2_sum > 0.0 ? 1e-3 * (area_sum / alive) / (len2_sum / alive) : 0.0;
	for (i32 e = 0; e < num_edges; ++e)
		_util_mesh_decimate_push_edge(&d, edges[e * 2], edges[e * 2 + 1]);
	free(edges);

	i32 target = (i32)(alive * ratio);
	while (alive > target && d.heap_length > 0) {
		decimate_edge_t e = _util_mesh_decimate_heap_pop(&d);
		i32             a = e.a;
		i32             b = e.b;
		if (d.dead[a] || d.dead[b] || d.stamp[a] != e.stamp_a || d.stamp[b] != e.stamp_b) {
			continue; // Stale
		}
		if (!_util_mesh_decimate_can_collapse(&d, a, b, e.x, e.y, e.z)) {
			continue;
		}

		// Collapse a into b
		d.pos[b * 3]     = e.x;
		d.pos[b * 3 + 1] = e.y;
		d.pos[b * 3 + 2] = e.z;
		for (i32 i = 0; i < 10; ++i)
			d.quad[b * 10 + i] += d.quad[a * 10 + i];
		d.bnd[b]  = d.bnd[b] || d.bnd[a];
		d.dead[a] = true;
		d.stamp[b]++;
		decimate_list_t *la = &d.adj[a];
		for (i32 i = 0; i < la->length; ++i) {
			i32 ti = la->data[i];
			if (d.tdead[ti]) {
				continue;
			}
			i32 *t = &d.tri[ti * 3];
			if (_util_mesh_decimate_has(t, b)) {
				d.tdead[ti] = true;
				alive--;
				continue;
			}
			for (i32 k = 0; k < 3; ++k)
				if (t[k] == a)
					t[k] = b;
			_util_mesh_decimate_list_push(&d.adj[b], ti);
		}
		free(la->data);
		la->data   = NULL;
		la->length = 0;

		// Drop dead triangles from b and requeue its edges, only they changed cost
		decimate_list_t *lb = &d.adj[b];
		i32              n  = 0;
		for (i32 i = 0; i < lb->length; ++i)
			if (!d.tdead[lb->data[i]])
				lb->data[n++] = lb->data[i];
		lb->length = n;
		d.mark_id++;
		for (i32 i = 0; i < lb->length; ++i) {
			i32 *t = &d.tri[lb->data[i] * 3];
			for (i32 k = 0; k < 3; ++k) {
				i32 w = t[k];
				if (w != b && d.mark[w] != d.mark_id) {
					d.mark[w] = d.mark_id;
					_util_mesh_decimate_push_edge(&d, w < b ? w : b, w < b ? b : w);
				}
			}
		}
	}

	// Compact the survivors
	i32 *remap    = malloc(num_compact * sizeof(i32));
	i32  new_vert = 0;
	for (i32 v = 0; v < num_compact; ++v)
		remap[v] = -1;
	u32_array_t *new_inda = u32_array_create_from_raw((u32[]){}, 0);
	for (i32 t = 0; t < num_tris; ++t) {
		if (d.tdead[t]) {
			continue;
		}
		for (i32 k = 0; k < 3; ++k) {
			i32 v = d.tri[t * 3 + k];
			if (remap[v] == -1) {
				remap[v] = new_vert++;
			}
			u32_array_push(new_inda, remap[v]);
		}
	}
	i16_array_t *new_va0 = i16_array_create(new_vert * 4);
	i16_array_t *new_va1 = i16_array_create(new_vert * 2);
	i16_array_t *new_va2 = i16_array_create(new_vert * 2);
	for (i32 v = 0; v < num_compact; ++v) {
		i32 r = remap[v];
		if (r == -1) {
			continue;
		}
		for (i32 k = 0; k < 3; ++k) {
			f64 x                      = d.pos[v * 3 + k] * 32767.0;
			x                          = x < -32767.0 ? -32767.0 : x > 32767.0 ? 32767.0 : x;
			new_va0->buffer[r * 4 + k] = (i16)round(x);
		}
	}

	for (i32 v = 0; v < num_compact; ++v)
		free(d.adj[v].data);
	free(d.adj);
	free(d.pos);
	free(d.quad);
	free(d.stamp);
	free(d.dead);
	free(d.bnd);
	free(d.mark);
	free(d.tri);
	free(d.tdead);
	free(d.heap);
	free(remap);
	free(compact_id);

	mesh_data_t *raw = ALLOC_INIT(mesh_data_t, {.name          = string("%s_decimated", o->base->name),
	                                            .vertex_arrays = any_array_create_from_raw(
	                                                (void *[]){
	                                                    ALLOC_INIT(vertex_array_t, {.values = new_va0, .attrib = "pos", .data = "short4norm"}),
	                                                    ALLOC_INIT(vertex_array_t, {.values = new_va1, .attrib = "nor", .data = "short2norm"}),
	                                                    ALLOC_INIT(vertex_array_t, {.values = new_va2, .attrib = "tex", .data = "short2norm"}),
	                                                },
	                                                3),
	                                            .index_array = new_inda,
	                                            .scale_pos   = o->data->scale_pos,
	                                            .scale_tex   = 1.0});

	mesh_data_t *new_data    = mesh_data_create(raw);
	new_data->_->owns_arrays = true;
	if (!util_mesh_data_is_shared(o->data)) {
		sys_notify_on_next_frame(&util_mesh_delete_data_uncache, o->data);
	}
	o->data = new_data;
}

static void _util_mesh_smooth(mesh_object_t *o) {
	mesh_data_t *g        = o->data;
	i16_array_t *va0      = g->vertex_arrays->buffer[0]->values;
	u32_array_t *oinda    = g->index_array;
	i32          overts   = math_floor(va0->length / 4.0);
	i32          num_tris = math_floor(oinda->length / 3.0);

	// Position-weld
	i32_array_t *weld_sort = i32_array_create(overts);
	for (i32 i = 0; i < overts; ++i)
		weld_sort->buffer[i] = i;
	util_mesh_va0 = va0;
	i32_array_sort(weld_sort, &util_mesh_calc_normals_sort);

	i32_array_t *weld_id = i32_array_create(overts);
	if (overts > 0) {
		i32 rep              = weld_sort->buffer[0];
		weld_id->buffer[rep] = rep;
		for (i32 i = 1; i < overts; ++i) {
			i32 curr = weld_sort->buffer[i];
			i32 prev = weld_sort->buffer[i - 1];
			if (va0->buffer[curr * 4] == va0->buffer[prev * 4] && va0->buffer[curr * 4 + 1] == va0->buffer[prev * 4 + 1] &&
			    va0->buffer[curr * 4 + 2] == va0->buffer[prev * 4 + 2]) {
				weld_id->buffer[curr] = rep;
			}
			else {
				rep                   = curr;
				weld_id->buffer[curr] = rep;
			}
		}
	}

	i32_array_t *compact_id = i32_array_create(overts);
	i32          num_verts  = 0;
	for (i32 i = 0; i < overts; ++i)
		if (weld_id->buffer[i] == i)
			compact_id->buffer[i] = num_verts++;
	for (i32 i = 0; i < overts; ++i)
		if (weld_id->buffer[i] != i)
			compact_id->buffer[i] = compact_id->buffer[weld_id->buffer[i]];

	// Build half-edges on compact vertices
	i32          num_he = num_tris * 3;
	i32_array_t *he_vlo = i32_array_create(num_he);
	i32_array_t *he_vhi = i32_array_create(num_he);

	for (i32 t = 0; t < num_tris; ++t) {
		i32 v[3] = {(i32)compact_id->buffer[oinda->buffer[t * 3]], (i32)compact_id->buffer[oinda->buffer[t * 3 + 1]],
		            (i32)compact_id->buffer[oinda->buffer[t * 3 + 2]]};
		for (i32 k = 0; k < 3; ++k) {
			i32 a = v[k], b = v[(k + 1) % 3];
			i32 h             = t * 3 + k;
			he_vlo->buffer[h] = a < b ? a : b;
			he_vhi->buffer[h] = a < b ? b : a;
		}
	}

	i32_array_t *he_order = i32_array_create(num_he);
	for (i32 i = 0; i < num_he; ++i)
		he_order->buffer[i] = i;
	_cc_he_vlo = he_vlo->buffer;
	_cc_he_vhi = he_vhi->buffer;
	i32_array_sort(he_order, &_util_mesh_subdivide_sort);

	i32 num_edges = 0;
	for (i32 i = 0; i < num_he;) {
		i32 j = i + 1;
		while (j < num_he && he_vlo->buffer[he_order->buffer[j]] == he_vlo->buffer[he_order->buffer[i]] &&
		       he_vhi->buffer[he_order->buffer[j]] == he_vhi->buffer[he_order->buffer[i]])
			++j;
		num_edges++;
		i = j;
	}

	i32_array_t *edge_vlo = i32_array_create(num_edges);
	i32_array_t *edge_vhi = i32_array_create(num_edges);
	i32_array_t *edge_bnd = i32_array_create(num_edges);

	i32 ei = 0;
	for (i32 i = 0; i < num_he;) {
		i32 j = i + 1;
		while (j < num_he && he_vlo->buffer[he_order->buffer[j]] == he_vlo->buffer[he_order->buffer[i]] &&
		       he_vhi->buffer[he_order->buffer[j]] == he_vhi->buffer[he_order->buffer[i]])
			++j;
		i32 h0               = he_order->buffer[i];
		edge_vlo->buffer[ei] = he_vlo->buffer[h0];
		edge_vhi->buffer[ei] = he_vhi->buffer[h0];
		edge_bnd->buffer[ei] = (j - i < 2) ? 1 : 0;
		ei++;
		i = j;
	}

	// Normalized float positions for compact vertices
	f32_array_t *np = f32_array_create(num_verts * 3);
	for (i32 i = 0; i < overts; ++i) {
		if (weld_id->buffer[i] == i) {
			i32 ci                 = compact_id->buffer[i];
			np->buffer[ci * 3]     = va0->buffer[i * 4] / 32767.0f;
			np->buffer[ci * 3 + 1] = va0->buffer[i * 4 + 1] / 32767.0f;
			np->buffer[ci * 3 + 2] = va0->buffer[i * 4 + 2] / 32767.0f;
		}
	}

	f32_array_t *vsum  = f32_array_create(num_verts * 3);
	f32_array_t *vbsum = f32_array_create(num_verts * 3);
	i32_array_t *vn    = i32_array_create(num_verts);
	i32_array_t *vbn   = i32_array_create(num_verts);

	for (i32 e = 0; e < num_edges; ++e) {
		i32 vlo = edge_vlo->buffer[e];
		i32 vhi = edge_vhi->buffer[e];
		vsum->buffer[vlo * 3] += np->buffer[vhi * 3];
		vsum->buffer[vlo * 3 + 1] += np->buffer[vhi * 3 + 1];
		vsum->buffer[vlo * 3 + 2] += np->buffer[vhi * 3 + 2];
		vsum->buffer[vhi * 3] += np->buffer[vlo * 3];
		vsum->buffer[vhi * 3 + 1] += np->buffer[vlo * 3 + 1];
		vsum->buffer[vhi * 3 + 2] += np->buffer[vlo * 3 + 2];
		vn->buffer[vlo]++;
		vn->buffer[vhi]++;
		if (edge_bnd->buffer[e]) {
			vbsum->buffer[vlo * 3] += np->buffer[vhi * 3];
			vbsum->buffer[vlo * 3 + 1] += np->buffer[vhi * 3 + 1];
			vbsum->buffer[vlo * 3 + 2] += np->buffer[vhi * 3 + 2];
			vbsum->buffer[vhi * 3] += np->buffer[vlo * 3];
			vbsum->buffer[vhi * 3 + 1] += np->buffer[vlo * 3 + 1];
			vbsum->buffer[vhi * 3 + 2] += np->buffer[vlo * 3 + 2];
			vbn->buffer[vlo]++;
			vbn->buffer[vhi]++;
		}
	}

	for (i32 vi = 0; vi < num_verts; ++vi) {
		i32 n = vn->buffer[vi];
		if (n == 0)
			continue;
		f32 vx = np->buffer[vi * 3], vy = np->buffer[vi * 3 + 1], vz = np->buffer[vi * 3 + 2];
		if (vbn->buffer[vi] == 0) {
			f32 tmp                = 3.0f / 8.0f + 0.25f * math_cos(2.0f * math_pi() / (f32)n);
			f32 s                  = 5.0f / 8.0f - tmp * tmp;
			f32 beta               = s / (f32)n;
			np->buffer[vi * 3]     = (1.0f - s) * vx + beta * vsum->buffer[vi * 3];
			np->buffer[vi * 3 + 1] = (1.0f - s) * vy + beta * vsum->buffer[vi * 3 + 1];
			np->buffer[vi * 3 + 2] = (1.0f - s) * vz + beta * vsum->buffer[vi * 3 + 2];
		}
		else if (vbn->buffer[vi] == 2) {
			np->buffer[vi * 3]     = 0.75f * vx + 0.125f * vbsum->buffer[vi * 3];
			np->buffer[vi * 3 + 1] = 0.75f * vy + 0.125f * vbsum->buffer[vi * 3 + 1];
			np->buffer[vi * 3 + 2] = 0.75f * vz + 0.125f * vbsum->buffer[vi * 3 + 2];
		}
	}

	for (i32 i = 0; i < overts; ++i) {
		i32 ci                 = compact_id->buffer[i];
		va0->buffer[i * 4]     = (i16)math_floor(np->buffer[ci * 3] * 32767.0f);
		va0->buffer[i * 4 + 1] = (i16)math_floor(np->buffer[ci * 3 + 1] * 32767.0f);
		va0->buffer[i * 4 + 2] = (i16)math_floor(np->buffer[ci * 3 + 2] * 32767.0f);
	}
}

typedef struct bevel_profile {
	i32    cv;     // Welded vertex this profile sits at
	i32    wa;     // Wedge at the first end
	i32    wb;     // Wedge at the last end
	i32    cap_w;  // Wedge the corner cap walks this profile from, opposite to the edge strip
	vec4_t ctrl;   // Arc control point (sum until resolved)
	i32    ctrl_n; //
	vec4_t center; // Arc circle center, used to fit the corner sphere
	i32    first;  // Output index of the first interior point
	i32    next;   // Next profile at the same welded vertex
} bevel_profile_t;

static i32 _util_mesh_bevel_find(i32 *uf, i32 i) {
	while (uf[i] != i) {
		uf[i] = uf[uf[i]];
		i     = uf[i];
	}
	return i;
}

static void _util_mesh_bevel_union(i32 *uf, i32 a, i32 b) {
	a = _util_mesh_bevel_find(uf, a);
	b = _util_mesh_bevel_find(uf, b);
	if (a != b) {
		uf[a < b ? b : a] = a < b ? a : b;
	}
}

// Output vertex of a profile, i counts segments from wedge from_w
static i32 _util_mesh_bevel_point(bevel_profile_t *p, i32 i, i32 from_w, i32 segments) {
	if (p->wa == p->wb) {
		return p->wa;
	}
	if (from_w != p->wa) {
		i = segments - i;
	}
	if (i == 0) {
		return p->wa;
	}
	if (i == segments) {
		return p->wb;
	}
	return p->first + i - 1;
}

static void _util_mesh_bevel_push(f32_array_t *out, vec4_t p) {
	f32_array_push(out, p.x);
	f32_array_push(out, p.y);
	f32_array_push(out, p.z);
}

static vec4_t _util_mesh_bevel_get(f32_array_t *out, i32 i) {
	return (vec4_t){out->buffer[i * 3], out->buffer[i * 3 + 1], out->buffer[i * 3 + 2], 0.0};
}

static void _util_mesh_bevel_tri(u32_array_t *inda, i32 a, i32 b, i32 c) {
	if (a == b || b == c || a == c) {
		return;
	}
	u32_array_push(inda, a);
	u32_array_push(inda, b);
	u32_array_push(inda, c);
}

static void _util_mesh_bevel(mesh_object_t *o, f32 amount) {
	mesh_data_t *g           = o->data;
	i16_array_t *va0         = g->vertex_arrays->buffer[0]->values;
	i16_array_t *va2         = g->vertex_arrays->buffer[2]->values;
	u32_array_t *inda        = g->index_array;
	i32          num_verts   = va0->length / 4;
	i32          num_tris    = inda->length / 3;
	i32          num_corners = num_tris * 3;
	i32          segments    = 4;
	f32          sharp_cos   = 0.866f; // Edges sharper than 30 degrees get beveled

	if (amount <= 0.0f || num_tris == 0) {
		return;
	}

	// Position-weld
	i32_array_t *weld_sort = i32_array_create(num_verts);
	for (i32 i = 0; i < num_verts; ++i)
		weld_sort->buffer[i] = i;
	util_mesh_va0 = va0;
	i32_array_sort(weld_sort, &util_mesh_calc_normals_sort);

	i32 *compact_id  = malloc(num_verts * sizeof(i32));
	i32  num_compact = 0;
	for (i32 i = 0; i < num_verts; ++i) {
		i32 curr = weld_sort->buffer[i];
		if (i > 0) {
			i32 prev = weld_sort->buffer[i - 1];
			if (va0->buffer[curr * 4] == va0->buffer[prev * 4] && va0->buffer[curr * 4 + 1] == va0->buffer[prev * 4 + 1] &&
			    va0->buffer[curr * 4 + 2] == va0->buffer[prev * 4 + 2]) {
				compact_id[curr] = compact_id[prev];
				continue;
			}
		}
		compact_id[curr] = num_compact++;
	}
	array_delete(weld_sort);

	vec4_t *cpos = calloc(num_compact, sizeof(vec4_t));
	for (i32 i = 0; i < num_verts; ++i) {
		cpos[compact_id[i]] = (vec4_t){va0->buffer[i * 4] / 32767.0f, va0->buffer[i * 4 + 1] / 32767.0f, va0->buffer[i * 4 + 2] / 32767.0f, 0.0};
	}

	// Corner c = t * 3 + k is also the half-edge from corner k to corner k + 1
	i32 *ccv = malloc(num_corners * sizeof(i32));
	for (i32 c = 0; c < num_corners; ++c)
		ccv[c] = compact_id[inda->buffer[c]];

	vec4_t *fnor = calloc(num_tris, sizeof(vec4_t));
	bool   *fok  = calloc(num_tris, sizeof(bool));
	for (i32 t = 0; t < num_tris; ++t) {
		vec4_t p0 = cpos[ccv[t * 3]];
		vec4_t n  = vec4_cross(vec4_sub(cpos[ccv[t * 3 + 1]], p0), vec4_sub(cpos[ccv[t * 3 + 2]], p0));
		n.w       = 0.0;
		fok[t]    = vec4_len(n) > 1e-12f;
		fnor[t]   = fok[t] ? vec4_norm(n) : n;
	}

	// Group half-edges into edges
	i32 *he_vlo = malloc(num_corners * sizeof(i32));
	i32 *he_vhi = malloc(num_corners * sizeof(i32));
	for (i32 h = 0; h < num_corners; ++h) {
		i32 a     = ccv[h];
		i32 b     = ccv[(h / 3) * 3 + (h % 3 + 1) % 3];
		he_vlo[h] = a < b ? a : b;
		he_vhi[h] = a < b ? b : a;
	}
	i32_array_t *he_order = i32_array_create(num_corners);
	for (i32 i = 0; i < num_corners; ++i)
		he_order->buffer[i] = i;
	_cc_he_vlo = he_vlo;
	_cc_he_vhi = he_vhi;
	i32_array_sort(he_order, &_util_mesh_subdivide_sort);

	// Corners around a vertex that are connected through smooth edges form a wedge
	i32 *uf = malloc(num_corners * sizeof(i32));
	for (i32 c = 0; c < num_corners; ++c)
		uf[c] = c;
	i32 *sharp     = malloc(num_corners * sizeof(i32)); // Half-edge running lo -> hi, its mate is at the next slot
	i32  num_sharp = 0;

	for (i32 i = 0; i < num_corners;) {
		i32 j = i + 1;
		while (j < num_corners && he_vlo[he_order->buffer[j]] == he_vlo[he_order->buffer[i]] && he_vhi[he_order->buffer[j]] == he_vhi[he_order->buffer[i]])
			++j;
		i32 h0 = he_order->buffer[i];
		if (j - i == 2 && ccv[h0] != ccv[he_order->buffer[i + 1]]) {
			i32 h1 = he_order->buffer[i + 1];
			i32 t0 = h0 / 3;
			i32 t1 = h1 / 3;
			if (fok[t0] && fok[t1] && vec4_dot(fnor[t0], fnor[t1]) < sharp_cos) {
				sharp[num_sharp * 2]     = ccv[h0] == he_vlo[h0] ? h0 : h1;
				sharp[num_sharp * 2 + 1] = ccv[h0] == he_vlo[h0] ? h1 : h0;
				num_sharp++;
			}
			else {
				_util_mesh_bevel_union(uf, h0, t1 * 3 + (h1 % 3 + 1) % 3);
				_util_mesh_bevel_union(uf, h1, t0 * 3 + (h0 % 3 + 1) % 3);
			}
		}
		else {
			// Boundary or non-manifold edge, join corners at the same vertex
			i32 e0 = (h0 / 3) * 3 + (h0 % 3 + 1) % 3;
			for (i32 k = i + 1; k < j; ++k) {
				i32 h = he_order->buffer[k];
				i32 e = (h / 3) * 3 + (h % 3 + 1) % 3;
				_util_mesh_bevel_union(uf, h, ccv[h] == ccv[h0] ? h0 : e0);
				_util_mesh_bevel_union(uf, e, ccv[e] == ccv[h0] ? h0 : e0);
			}
		}
		i = j;
	}
	array_delete(he_order);

	if (num_sharp == 0) {
		free(compact_id);
		free(cpos);
		free(ccv);
		free(fnor);
		free(fok);
		free(he_vlo);
		free(he_vhi);
		free(uf);
		free(sharp);
		return;
	}

	i32 *wedge_of     = malloc(num_corners * sizeof(i32));
	i32 *wedge_corner = malloc(num_corners * sizeof(i32));
	i32  num_wedges   = 0;
	for (i32 c = 0; c < num_corners; ++c) {
		if (_util_mesh_bevel_find(uf, c) == c) {
			wedge_corner[num_wedges] = c;
			wedge_of[c]              = num_wedges++;
		}
	}
	for (i32 c = 0; c < num_corners; ++c)
		wedge_of[c] = wedge_of[_util_mesh_bevel_find(uf, c)];

	// Each wedge slides away from the sharp edges bounding it
	vec4_t *wsum    = calloc(num_wedges, sizeof(vec4_t));
	vec4_t *wfirst  = calloc(num_wedges, sizeof(vec4_t));
	vec4_t *wsecond = calloc(num_wedges, sizeof(vec4_t));
	i32    *wcount  = calloc(num_wedges, sizeof(i32));
	for (i32 s = 0; s < num_sharp; ++s) {
		i32    h[2] = {sharp[s * 2], sharp[s * 2 + 1]};
		i32    a    = ccv[h[0]];
		i32    b    = ccv[h[1]];
		vec4_t d    = vec4_norm(vec4_sub(cpos[b], cpos[a]));
		vec4_t in[2];
		for (i32 k = 0; k < 2; ++k) {
			i32    t = h[k] / 3;
			vec4_t q = vec4_sub(cpos[ccv[t * 3 + (h[k] % 3 + 2) % 3]], cpos[a]);
			in[k]    = vec4_norm(vec4_sub(q, vec4_mult(d, vec4_dot(q, d))));
		}
		// Wedges at the start and the end of both half-edges
		i32 w0s = wedge_of[h[0]];
		i32 w0e = wedge_of[(h[0] / 3) * 3 + (h[0] % 3 + 1) % 3];
		i32 w1s = wedge_of[h[1]];
		i32 w1e = wedge_of[(h[1] / 3) * 3 + (h[1] % 3 + 1) % 3];
		// A sharp edge that ends inside a smooth region has one wedge on both sides and does not push it
		i32  ws[4] = {w0s, w1e, w0e, w1s};
		i32  wk[4] = {0, 1, 0, 1};
		bool on[4] = {w0s != w1e, w0s != w1e, w0e != w1s, w0e != w1s};
		for (i32 k = 0; k < 4; ++k) {
			if (!on[k]) {
				continue;
			}
			i32 w = ws[k];
			if (wcount[w] == 0) {
				wfirst[w] = in[wk[k]];
			}
			else if (wcount[w] == 1) {
				wsecond[w] = in[wk[k]];
			}
			wsum[w] = vec4_add(wsum[w], in[wk[k]]);
			wcount[w]++;
		}
	}

	vec4_t *wdir = calloc(num_wedges, sizeof(vec4_t));
	for (i32 w = 0; w < num_wedges; ++w) {
		vec4_t m = {0.0, 0.0, 0.0, 0.0};
		if (wcount[w] == 1) {
			m = wfirst[w];
		}
		else if (wcount[w] == 2) {
			// Miter, stays at unit distance from both edges
			f32 s = 1.0f + vec4_dot(wfirst[w], wsecond[w]);
			m     = vec4_add(wfirst[w], wsecond[w]);
			m     = s > 0.1f ? vec4_mult(m, 1.0f / s) : vec4_norm(m);
		}
		else if (wcount[w] > 2) {
			m = vec4_norm(wsum[w]);
		}
		m.w = 0.0;
		if (vec4_len(m) > 4.0f) {
			m = vec4_mult(vec4_norm(m), 4.0f);
		}
		wdir[w] = m;
	}

	// Clamp the width so that no triangle folds over
	f64 width = amount;
	for (i32 t = 0; t < num_tris; ++t) {
		if (!fok[t]) {
			continue;
		}
		vec4_t p0 = cpos[ccv[t * 3]];
		vec4_t m0 = wdir[wedge_of[t * 3]];
		vec4_t e1 = vec4_sub(cpos[ccv[t * 3 + 1]], p0);
		vec4_t e2 = vec4_sub(cpos[ccv[t * 3 + 2]], p0);
		vec4_t d1 = vec4_sub(wdir[wedge_of[t * 3 + 1]], m0);
		vec4_t d2 = vec4_sub(wdir[wedge_of[t * 3 + 2]], m0);
		if (vec4_len(d1) + vec4_len(d2) == 0.0f) {
			continue;
		}
		// Projected area along the original normal as a function of width: qa + qb * w + qc * w^2
		vec4_t n  = vec4_cross(e1, e2);
		f64    qa = vec4_dot(n, n);
		f64    qb = vec4_dot(vec4_add(vec4_cross(e1, d2), vec4_cross(d1, e2)), n);
		f64    qc = vec4_dot(vec4_cross(d1, d2), n);
		// Keep at least a quarter of the area
		f64 k = 0.75 * qa;
		f64 r = -1.0;
		if (fabs(qc) < 1e-12 * qa) {
			if (qb < 0.0) {
				r = -k / qb;
			}
		}
		else {
			f64 disc = qb * qb - 4.0 * qc * k;
			if (disc >= 0.0) {
				f64 sq = sqrt(disc);
				f64 r1 = (-qb - sq) / (2.0 * qc);
				f64 r2 = (-qb + sq) / (2.0 * qc);
				if (r1 > r2) {
					f64 tmp = r1;
					r1      = r2;
					r2      = tmp;
				}
				r = r1 > 0.0 ? r1 : r2;
			}
		}
		if (r > 0.0 && r < width) {
			width = r;
		}
	}

	f32_array_t *out = f32_array_create_from_raw((f32[]){}, 0);
	for (i32 w = 0; w < num_wedges; ++w) {
		_util_mesh_bevel_push(out, vec4_add(cpos[ccv[wedge_corner[w]]], vec4_mult(wdir[w], (f32)width)));
	}

	// One profile per welded vertex and wedge pair, shared by all strips passing through
	bevel_profile_t *profs     = calloc(num_sharp * 2, sizeof(bevel_profile_t));
	i32              num_profs = 0;
	i32             *prof_head = malloc(num_compact * sizeof(i32));
	i32             *sharp_pr  = malloc(num_sharp * 2 * sizeof(i32));
	for (i32 v = 0; v < num_compact; ++v)
		prof_head[v] = -1;
	for (i32 s = 0; s < num_sharp; ++s) {
		i32    h0 = sharp[s * 2];
		i32    h1 = sharp[s * 2 + 1];
		i32    a  = ccv[h0];
		i32    b  = ccv[h1];
		vec4_t d  = vec4_norm(vec4_sub(cpos[b], cpos[a]));
		for (i32 end = 0; end < 2; ++end) {
			i32 v  = end == 0 ? a : b;
			i32 w0 = end == 0 ? wedge_of[h0] : wedge_of[(h0 / 3) * 3 + (h0 % 3 + 1) % 3];
			i32 w1 = end == 0 ? wedge_of[(h1 / 3) * 3 + (h1 % 3 + 1) % 3] : wedge_of[h1];
			i32 lo = w0 < w1 ? w0 : w1;
			i32 hi = w0 < w1 ? w1 : w0;
			i32 p  = prof_head[v];
			while (p != -1 && (profs[p].wa != lo || profs[p].wb != hi))
				p = profs[p].next;
			if (p == -1) {
				p              = num_profs++;
				profs[p].cv    = v;
				profs[p].wa    = lo;
				profs[p].wb    = hi;
				profs[p].cap_w = end == 0 ? w1 : w0;
				profs[p].first = -1;
				profs[p].next  = prof_head[v];
				prof_head[v]   = p;
			}
			// Arc control point sits on the edge line, level with the profile ends
			vec4_t pv     = cpos[v];
			vec4_t pw0    = _util_mesh_bevel_get(out, w0);
			vec4_t pw1    = _util_mesh_bevel_get(out, w1);
			f32    l      = (vec4_dot(vec4_sub(pw0, pv), d) + vec4_dot(vec4_sub(pw1, pv), d)) * 0.5f;
			profs[p].ctrl = vec4_add(profs[p].ctrl, vec4_add(pv, vec4_mult(d, l)));
			profs[p].ctrl_n++;
			sharp_pr[s * 2 + end] = p;
		}
	}

	for (i32 p = 0; p < num_profs; ++p) {
		bevel_profile_t *pr = &profs[p];
		if (pr->wa == pr->wb) {
			continue;
		}
		vec4_t c    = vec4_mult(pr->ctrl, 1.0f / pr->ctrl_n);
		vec4_t pa   = _util_mesh_bevel_get(out, pr->wa);
		vec4_t pb   = _util_mesh_bevel_get(out, pr->wb);
		vec4_t u    = vec4_sub(pa, c);
		vec4_t v    = vec4_sub(pb, c);
		f32    cosp = vec4_dot(vec4_norm(u), vec4_norm(v));
		cosp        = cosp < -1.0f ? -1.0f : cosp > 1.0f ? 1.0f : cosp;
		// Rational quadratic with this weight traces a circular arc tangent to both faces
		f32    cw  = sqrtf((1.0f - cosp) * 0.5f);
		f32    ch  = sqrtf((1.0f + cosp) * 0.5f);
		f32    dl  = (vec4_len(u) + vec4_len(v)) * 0.5f;
		vec4_t bis = vec4_norm(vec4_add(vec4_norm(u), vec4_norm(v)));
		pr->center = vec4_add(c, vec4_mult(bis, dl / (ch > 0.1f ? ch : 0.1f)));
		pr->first  = out->length / 3;
		for (i32 i = 1; i < segments; ++i) {
			f32    t  = (f32)i / segments;
			f32    b0 = (1.0f - t) * (1.0f - t);
			f32    b1 = 2.0f * t * (1.0f - t) * cw;
			f32    b2 = t * t;
			vec4_t q  = vec4_add(vec4_add(vec4_mult(pa, b0), vec4_mult(c, b1)), vec4_mult(pb, b2));
			_util_mesh_bevel_push(out, vec4_mult(q, 1.0f / (b0 + b1 + b2)));
		}
	}

	u32_array_t *new_inda = u32_array_create_from_raw((u32[]){}, 0);
	for (i32 t = 0; t < num_tris; ++t) {
		_util_mesh_bevel_tri(new_inda, wedge_of[t * 3], wedge_of[t * 3 + 1], wedge_of[t * 3 + 2]);
	}

	// Edge strips
	for (i32 s = 0; s < num_sharp; ++s) {
		i32              h0  = sharp[s * 2];
		i32              wa  = wedge_of[h0];
		i32              wb  = wedge_of[(h0 / 3) * 3 + (h0 % 3 + 1) % 3];
		bevel_profile_t *pra = &profs[sharp_pr[s * 2]];
		bevel_profile_t *prb = &profs[sharp_pr[s * 2 + 1]];
		for (i32 i = 0; i < segments; ++i) {
			i32 a0 = _util_mesh_bevel_point(pra, i, wa, segments);
			i32 a1 = _util_mesh_bevel_point(pra, i + 1, wa, segments);
			i32 b0 = _util_mesh_bevel_point(prb, i, wb, segments);
			i32 b1 = _util_mesh_bevel_point(prb, i + 1, wb, segments);
			_util_mesh_bevel_tri(new_inda, b0, a0, a1);
			_util_mesh_bevel_tri(new_inda, b0, a1, b1);
		}
	}

	// Corner caps where three or more wedges meet
	i32         *cap_prof  = malloc(num_profs * sizeof(i32));
	bool        *cap_used  = calloc(num_profs, sizeof(bool));
	i32_array_t *loop      = i32_array_create_from_raw((i32[]){}, 0);
	i32_array_t *ring      = i32_array_create_from_raw((i32[]){}, 0);
	i32_array_t *next_ring = i32_array_create_from_raw((i32[]){}, 0);
	for (i32 v = 0; v < num_compact; ++v) {
		i32 n = 0;
		for (i32 p = prof_head[v]; p != -1; p = profs[p].next) {
			if (profs[p].wa != profs[p].wb) {
				cap_prof[n++] = p;
				cap_used[p]   = false;
			}
		}
		if (n < 3) {
			continue;
		}

		// Walk the profiles around the vertex, against the direction the strips use them
		loop->length = 0;
		i32 p        = cap_prof[0];
		i32 start_w  = profs[p].cap_w;
		i32 w        = start_w;
		i32 walked   = 0;
		while (true) {
			cap_used[p] = true;
			walked++;
			for (i32 i = 0; i < segments; ++i)
				i32_array_push(loop, _util_mesh_bevel_point(&profs[p], i, w, segments));
			w = profs[p].wa == w ? profs[p].wb : profs[p].wa;
			if (w == start_w) {
				break;
			}
			p = -1;
			for (i32 k = 0; k < n; ++k) {
				i32 q = cap_prof[k];
				if (!cap_used[q] && (profs[q].wa == w || profs[q].wb == w)) {
					p = q;
					break;
				}
			}
			if (p == -1) {
				break;
			}
		}
		if (w != start_w || walked != n) {
			continue; // Open fan at a mesh boundary, leave it
		}

		// Fit a sphere through the arc centers when they agree, as on convex or concave corners
		vec4_t sc = {0.0, 0.0, 0.0, 0.0};
		for (i32 k = 0; k < n; ++k)
			sc = vec4_add(sc, profs[cap_prof[k]].center);
		sc         = vec4_mult(sc, 1.0f / n);
		vec4_t cc  = {0.0, 0.0, 0.0, 0.0};
		f32    rad = 0.0f;
		for (i32 i = 0; i < loop->length; ++i) {
			vec4_t q = _util_mesh_bevel_get(out, loop->buffer[i]);
			cc       = vec4_add(cc, q);
			rad += vec4_dist(q, sc);
		}
		cc          = vec4_mult(cc, 1.0f / loop->length);
		rad         = rad / loop->length;
		bool sphere = true;
		for (i32 k = 0; k < n; ++k) {
			if (vec4_dist(profs[cap_prof[k]].center, sc) > rad * 0.25f) {
				sphere = false;
			}
		}

		i32 rings    = segments / 2 > 1 ? segments / 2 : 1;
		i32 m        = loop->length;
		ring->length = 0;
		for (i32 i = 0; i < m; ++i)
			i32_array_push(ring, loop->buffer[i]);
		for (i32 r = 1; r < rings; ++r) {
			next_ring->length = 0;
			f32 f             = (f32)r / rings;
			for (i32 i = 0; i < m; ++i) {
				vec4_t q = _util_mesh_bevel_get(out, loop->buffer[i]);
				q        = vec4_add(vec4_mult(q, 1.0f - f), vec4_mult(cc, f));
				if (sphere) {
					q = vec4_add(sc, vec4_mult(vec4_norm(vec4_sub(q, sc)), rad));
				}
				i32_array_push(next_ring, out->length / 3);
				_util_mesh_bevel_push(out, q);
			}
			for (i32 i = 0; i < m; ++i) {
				i32 i1 = (i + 1) % m;
				_util_mesh_bevel_tri(new_inda, ring->buffer[i], ring->buffer[i1], next_ring->buffer[i1]);
				_util_mesh_bevel_tri(new_inda, ring->buffer[i], next_ring->buffer[i1], next_ring->buffer[i]);
			}
			i32_array_t *tmp = ring;
			ring             = next_ring;
			next_ring        = tmp;
		}
		i32    center = out->length / 3;
		vec4_t q      = sphere ? vec4_add(sc, vec4_mult(vec4_norm(vec4_sub(cc, sc)), rad)) : cc;
		_util_mesh_bevel_push(out, q);
		for (i32 i = 0; i < m; ++i)
			_util_mesh_bevel_tri(new_inda, ring->buffer[i], ring->buffer[(i + 1) % m], center);
	}

	i32          total_verts = out->length / 3;
	i16_array_t *new_va0     = i16_array_create(total_verts * 4);
	i16_array_t *new_va1     = i16_array_create(total_verts * 2);
	i16_array_t *new_va2     = i16_array_create(total_verts * 2);
	for (i32 i = 0; i < total_verts * 3; ++i) {
		f32 x                                = out->buffer[i] * 32767.0f;
		x                                    = x < -32767.0f ? -32767.0f : x > 32767.0f ? 32767.0f : x;
		new_va0->buffer[(i / 3) * 4 + i % 3] = (i16)roundf(x);
	}
	for (i32 w = 0; w < num_wedges; ++w) {
		i32 vi                     = inda->buffer[wedge_corner[w]];
		new_va2->buffer[w * 2]     = va2->buffer[vi * 2];
		new_va2->buffer[w * 2 + 1] = va2->buffer[vi * 2 + 1];
	}

	free(compact_id);
	free(cpos);
	free(ccv);
	free(fnor);
	free(fok);
	free(he_vlo);
	free(he_vhi);
	free(uf);
	free(sharp);
	free(wedge_of);
	free(wedge_corner);
	free(wsum);
	free(wfirst);
	free(wsecond);
	free(wcount);
	free(wdir);
	free(profs);
	free(prof_head);
	free(sharp_pr);
	free(cap_prof);
	free(cap_used);
	array_delete(loop);
	array_delete(ring);
	array_delete(next_ring);
	array_delete(out);

	mesh_data_t *raw         = ALLOC_INIT(mesh_data_t, {.name          = string("%s_beveled", o->base->name),
	                                                    .vertex_arrays = any_array_create_from_raw(
                                                    (void *[]){
                                                        ALLOC_INIT(vertex_array_t, {.values = new_va0, .attrib = "pos", .data = "short4norm"}),
                                                        ALLOC_INIT(vertex_array_t, {.values = new_va1, .attrib = "nor", .data = "short2norm"}),
                                                        ALLOC_INIT(vertex_array_t, {.values = new_va2, .attrib = "tex", .data = "short2norm"}),
                                                    },
                                                    3),
	                                                    .index_array = new_inda,
	                                                    .scale_pos   = o->data->scale_pos,
	                                                    .scale_tex   = 1.0});
	mesh_data_t *new_data    = mesh_data_create(raw);
	new_data->_->owns_arrays = true;
	if (!util_mesh_data_is_shared(o->data)) {
		sys_notify_on_next_frame(&util_mesh_delete_data_uncache, o->data);
	}
	o->data = new_data;
}

static void _util_mesh_subdivide(mesh_object_t *o) {
	mesh_data_t *g         = o->data;
	i16_array_t *va0       = g->vertex_arrays->buffer[0]->values;
	i16_array_t *va2       = g->vertex_arrays->buffer[2]->values;
	u32_array_t *inda      = g->index_array;
	i32          num_verts = math_floor(va0->length / 4.0);
	i32          num_tris  = math_floor(inda->length / 3.0);

	i32          num_he  = num_tris * 3;
	i32_array_t *he_vlo  = i32_array_create(num_he);
	i32_array_t *he_vhi  = i32_array_create(num_he);
	i32_array_t *he_edge = i32_array_create(num_he);

	for (i32 t = 0; t < num_tris; ++t) {
		i32 v[3] = {(i32)inda->buffer[t * 3], (i32)inda->buffer[t * 3 + 1], (i32)inda->buffer[t * 3 + 2]};
		for (i32 k = 0; k < 3; ++k) {
			i32 a = v[k], b = v[(k + 1) % 3];
			i32 h             = t * 3 + k;
			he_vlo->buffer[h] = a < b ? a : b;
			he_vhi->buffer[h] = a < b ? b : a;
		}
	}

	i32_array_t *he_order = i32_array_create(num_he);
	for (i32 i = 0; i < num_he; ++i)
		he_order->buffer[i] = i;
	_cc_he_vlo = he_vlo->buffer;
	_cc_he_vhi = he_vhi->buffer;
	i32_array_sort(he_order, &_util_mesh_subdivide_sort);

	i32 num_edges = 0;
	for (i32 i = 0; i < num_he;) {
		i32 j = i + 1;
		while (j < num_he && he_vlo->buffer[he_order->buffer[j]] == he_vlo->buffer[he_order->buffer[i]] &&
		       he_vhi->buffer[he_order->buffer[j]] == he_vhi->buffer[he_order->buffer[i]])
			++j;
		num_edges++;
		i = j;
	}

	i32_array_t *edge_vlo = i32_array_create(num_edges);
	i32_array_t *edge_vhi = i32_array_create(num_edges);

	i32 ei = 0;
	for (i32 i = 0; i < num_he;) {
		i32 j = i + 1;
		while (j < num_he && he_vlo->buffer[he_order->buffer[j]] == he_vlo->buffer[he_order->buffer[i]] &&
		       he_vhi->buffer[he_order->buffer[j]] == he_vhi->buffer[he_order->buffer[i]])
			++j;
		i32 h0               = he_order->buffer[i];
		edge_vlo->buffer[ei] = he_vlo->buffer[h0];
		edge_vhi->buffer[ei] = he_vhi->buffer[h0];
		for (i32 k = i; k < j; ++k)
			he_edge->buffer[he_order->buffer[k]] = ei;
		ei++;
		i = j;
	}

	i32          ep_base      = num_verts;
	i32          num_new_vert = num_verts + num_edges;
	i16_array_t *new_va0      = i16_array_create(num_new_vert * 4);
	i16_array_t *new_va1      = i16_array_create(num_new_vert * 2);
	i16_array_t *new_va2      = i16_array_create(num_new_vert * 2);
	u32_array_t *new_inda     = u32_array_create(num_tris * 12);

	for (i32 i = 0; i < num_verts; ++i) {
		new_va0->buffer[i * 4]     = va0->buffer[i * 4];
		new_va0->buffer[i * 4 + 1] = va0->buffer[i * 4 + 1];
		new_va0->buffer[i * 4 + 2] = va0->buffer[i * 4 + 2];
		new_va2->buffer[i * 2]     = va2->buffer[i * 2];
		new_va2->buffer[i * 2 + 1] = va2->buffer[i * 2 + 1];
	}

	for (i32 e = 0; e < num_edges; ++e) {
		i32 vlo                      = edge_vlo->buffer[e];
		i32 vhi                      = edge_vhi->buffer[e];
		i32 epi                      = ep_base + e;
		new_va0->buffer[epi * 4]     = (i16)math_floor((va0->buffer[vlo * 4] + va0->buffer[vhi * 4]) * 0.5f);
		new_va0->buffer[epi * 4 + 1] = (i16)math_floor((va0->buffer[vlo * 4 + 1] + va0->buffer[vhi * 4 + 1]) * 0.5f);
		new_va0->buffer[epi * 4 + 2] = (i16)math_floor((va0->buffer[vlo * 4 + 2] + va0->buffer[vhi * 4 + 2]) * 0.5f);
		new_va2->buffer[epi * 2]     = (i16)math_floor((va2->buffer[vlo * 2] + va2->buffer[vhi * 2]) * 0.5f);
		new_va2->buffer[epi * 2 + 1] = (i16)math_floor((va2->buffer[vlo * 2 + 1] + va2->buffer[vhi * 2 + 1]) * 0.5f);
	}

	for (i32 t = 0; t < num_tris; ++t) {
		i32 v0 = inda->buffer[t * 3], v1 = inda->buffer[t * 3 + 1], v2 = inda->buffer[t * 3 + 2];
		i32 e0                   = ep_base + he_edge->buffer[t * 3];
		i32 e1                   = ep_base + he_edge->buffer[t * 3 + 1];
		i32 e2                   = ep_base + he_edge->buffer[t * 3 + 2];
		i32 b                    = t * 12;
		new_inda->buffer[b]      = v0;
		new_inda->buffer[b + 1]  = e0;
		new_inda->buffer[b + 2]  = e2;
		new_inda->buffer[b + 3]  = e0;
		new_inda->buffer[b + 4]  = v1;
		new_inda->buffer[b + 5]  = e1;
		new_inda->buffer[b + 6]  = e2;
		new_inda->buffer[b + 7]  = e1;
		new_inda->buffer[b + 8]  = v2;
		new_inda->buffer[b + 9]  = e0;
		new_inda->buffer[b + 10] = e1;
		new_inda->buffer[b + 11] = e2;
	}

	mesh_data_t *raw          = ALLOC_INIT(mesh_data_t, {.name          = string("%s_subdivided", o->base->name),
	                                                     .vertex_arrays = any_array_create_from_raw(
                                                    (void *[]){
                                                        ALLOC_INIT(vertex_array_t, {.values = new_va0, .attrib = "pos", .data = "short4norm"}),
                                                        ALLOC_INIT(vertex_array_t, {.values = new_va1, .attrib = "nor", .data = "short2norm"}),
                                                        ALLOC_INIT(vertex_array_t, {.values = new_va2, .attrib = "tex", .data = "short2norm"}),
                                                    },
                                                    3),
	                                                     .index_array = new_inda,
	                                                     .scale_pos   = o->data->scale_pos,
	                                                     .scale_tex   = 1.0});
	mesh_data_t *new_data2    = mesh_data_create(raw);
	new_data2->_->owns_arrays = true;
	if (!util_mesh_data_is_shared(o->data)) {
		sys_notify_on_next_frame(&util_mesh_delete_data_uncache, o->data);
	}
	o->data = new_data2;
}

static mesh_object_t_array_t *_util_mesh_modifier_begin(mesh_object_t_array_t *objects) {
	mesh_object_t_array_t *ar = any_array_create_from_raw((void *[]){}, 0);
	if (objects == NULL) {
		objects = util_mesh_get_unique_data();
	}
	for (i32 i = 0; i < objects->length; ++i) {
		if (objects->buffer[i]->data->index_array->length > 0) {
			any_array_push(ar, objects->buffer[i]);
		}
	}
	return ar;
}

static void _util_mesh_modifier_end(mesh_object_t_array_t *objects) {
	util_mesh_calc_normals(objects, true);
	util_mesh_uv_unwrap(objects);
}

void util_mesh_decimate(mesh_object_t_array_t *objects, f32 ratio) {
	objects = _util_mesh_modifier_begin(objects);
	for (i32 i = 0; i < objects->length; ++i) {
		_util_mesh_decimate(objects->buffer[i], ratio);
	}
	_util_mesh_modifier_end(objects);
}

void util_mesh_smooth(mesh_object_t_array_t *objects) {
	objects = _util_mesh_modifier_begin(objects);
	for (i32 i = 0; i < objects->length; ++i) {
		_util_mesh_smooth(objects->buffer[i]);
	}
	_util_mesh_modifier_end(objects);
}

void util_mesh_bevel(mesh_object_t_array_t *objects, f32 amount) {
	objects = _util_mesh_modifier_begin(objects);
	for (i32 i = 0; i < objects->length; ++i) {
		_util_mesh_bevel(objects->buffer[i], amount);
	}
	_util_mesh_modifier_end(objects);
}

void util_mesh_subdivide(mesh_object_t_array_t *objects) {
	objects = _util_mesh_modifier_begin(objects);
	for (i32 i = 0; i < objects->length; ++i) {
		_util_mesh_subdivide(objects->buffer[i]);
	}
	_util_mesh_modifier_end(objects);
}

static void _util_mesh_shift_object_masks(i32 from) {
	if (g_project->_->layers != NULL) {
		for (i32 i = 0; i < g_project->_->layers->length; ++i) {
			slot_layer_t *l = g_project->_->layers->buffer[i];
			if (l->object_mask >= from) {
				++l->object_mask;
			}
		}
	}
	if (g_context->layer_filter >= from) {
		++g_context->layer_filter;
	}
}

mesh_object_t *util_mesh_duplicate_object(mesh_object_t *so) {
	// Mesh
	if (so == NULL) {
		return NULL;
	}

	mesh_data_t   *data = so->data;
	mesh_object_t *dup  = scene_add_mesh_object(data, so->material, so->base->parent);
	transform_set_matrix(dup->base->transform, so->base->transform->local);

	// Insert below the original
	i32 index = array_index_of(g_project->_->paint_objects, so);
	i32 at    = index < 0 ? g_project->_->paint_objects->length : index + 1;
	array_insert((any_array_t *)g_project->_->paint_objects, at, dup);
	_util_mesh_shift_object_masks(at + 1);

	// Ensure unique name
	dup->base->name = string_copy(_import_mesh_unique_name(so->base->name));
	tab_stages_add_object(dup->base->name);

	// Material override
	i32 mat_index = tab_meshes_get_override(so);
	if (mat_index >= 0) {
		tab_meshes_set_override_data(dup, mat_index, so->material);
		g_project->mesh_materials = i32_array_create(0);
	}

	// Physics
	i32 shape = util_physics_get_shape(so->base);
	if (shape >= 0) {
		util_physics_set(dup->base, shape, util_physics_get_mass(so->base));
	}

	tab_meshes_sort_hierarchy();
	tab_timeline_sync();

	return dup;
}

void util_mesh_duplicate() {
	mesh_object_t *dup = util_mesh_duplicate_object(g_context->paint_object);
	if (dup != NULL) {
		g_context->paint_object                           = dup;
		ui_header_handle->redraws                         = 2;
		ui_base_hwnds->buffer[TAB_AREA_SIDEBAR0]->redraws = 2;
	}
	util_mesh_merge(NULL);
	g_context->ddirty = 2;
}

void util_mesh_delete() {
	if (g_project->_->paint_objects->length < 2) {
		return;
	}
	tab_meshes_draw_context_menu_delete(g_context->paint_object);
}
