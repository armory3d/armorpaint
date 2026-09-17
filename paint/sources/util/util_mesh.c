
#include "../global.h"

i16_array_t *util_mesh_va0;
i32_array_t *util_mesh_quantized;

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

void util_mesh_swap_axis(i32 a, i32 b) {
	mesh_object_t_array_t *objects = util_mesh_get_unique_data();
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

void util_mesh_flip_normals() {
	mesh_object_t_array_t *objects = util_mesh_get_unique_data();
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

void util_mesh_calc_normals(bool smooth) {
	vec4_t                 va      = (vec4_t){0.0, 0.0, 0.0, 1.0};
	vec4_t                 vb      = (vec4_t){0.0, 0.0, 0.0, 1.0};
	vec4_t                 vc      = (vec4_t){0.0, 0.0, 0.0, 1.0};
	vec4_t                 cb      = (vec4_t){0.0, 0.0, 0.0, 1.0};
	vec4_t                 ab      = (vec4_t){0.0, 0.0, 0.0, 1.0};
	mesh_object_t_array_t *objects = util_mesh_get_unique_data();
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

void util_mesh_apply_displacement(gpu_texture_t *texpaint_pack, f32 strength, f32 uv_scale) {
	buffer_t      *height    = gpu_get_texture_pixels(texpaint_pack);
	i32            res       = texpaint_pack->width;
	mesh_object_t *o         = g_project->_->paint_objects->buffer[0];
	mesh_data_t   *g         = o->data;
	i16_array_t   *va0       = g->vertex_arrays->buffer[0]->values;
	i16_array_t   *va1       = g->vertex_arrays->buffer[1]->values;
	i16_array_t   *va2       = g->vertex_arrays->buffer[2]->values;
	i32            num_verts = math_floor(va0->length / 4.0);
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

void util_mesh_uv_unwrap() {
	util_mesh_merge(g_project->_->paint_objects);
	if (g_context->merged_object == NULL) {
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
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_data_t *md      = g_project->_->paint_objects->buffer[i]->data;
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

void util_mesh_uv_unwrap_per_object(mesh_object_t *mo) {
	mesh_data_t *md      = mo->data;
	i16_array_t *md_posa = md->vertex_arrays->buffer[0]->values;
	i16_array_t *md_nora = md->vertex_arrays->buffer[1]->values;
	u32_array_t *md_inda = md->index_array;

	i16_array_t *posa = malloc(sizeof(i16_array_t));
	posa->length = posa->capacity = md_posa->length;
	posa->buffer                  = malloc(md_posa->length * sizeof(i16));
	memcpy(posa->buffer, md_posa->buffer, md_posa->length * sizeof(i16));

	i16_array_t *nora = malloc(sizeof(i16_array_t));
	nora->length = nora->capacity = md_nora->length;
	nora->buffer                  = malloc(md_nora->length * sizeof(i16));
	memcpy(nora->buffer, md_nora->buffer, md_nora->length * sizeof(i16));

	u32_array_t *inda = malloc(sizeof(u32_array_t));
	inda->length = inda->capacity = md_inda->length;
	inda->buffer                  = malloc(md_inda->length * sizeof(u32));
	memcpy(inda->buffer, md_inda->buffer, md_inda->length * sizeof(u32));

	raw_mesh_t *mesh = ALLOC_INIT(raw_mesh_t, {.posa = posa, .nora = nora, .texa = NULL, .inda = inda});

	util_uv_unwrap_run(mesh);
	md->vertex_arrays->buffer[0]->values = mesh->posa;
	md->vertex_arrays->buffer[1]->values = mesh->nora;
	md->vertex_arrays->buffer[2]->values = mesh->texa;
	md->index_array                      = mesh->inda;
	mesh_data_build(md);
	util_uv_uvmap_cached = false;
}

i32 util_mesh_decimate_sort(i32 *pa, i32 *pb) {
	i32 a    = *(pa);
	i32 b    = *(pb);
	i32 diff = util_mesh_quantized->buffer[a * 3] - util_mesh_quantized->buffer[b * 3];
	if (diff != 0)
		return diff;
	diff = util_mesh_quantized->buffer[a * 3 + 1] - util_mesh_quantized->buffer[b * 3 + 1];
	if (diff != 0)
		return diff;
	return util_mesh_quantized->buffer[a * 3 + 2] - util_mesh_quantized->buffer[b * 3 + 2];
}

void util_mesh_decimate(f32 strength) {
	mesh_object_t_array_t *objects   = g_project->_->paint_objects;
	mesh_object_t         *o         = objects->buffer[0];
	mesh_data_t           *g         = o->data;
	i16_array_t           *va0       = g->vertex_arrays->buffer[0]->values;
	i16_array_t           *va1       = g->vertex_arrays->buffer[1]->values;
	i16_array_t           *va2       = g->vertex_arrays->buffer[2]->values;
	u32_array_t           *inda      = g->index_array;
	i32                    num_verts = math_floor(va0->length / 4.0);
	i32                    min_x     = 32767;
	i32                    max_x     = -32767;
	i32                    min_y     = 32767;
	i32                    max_y     = -32767;
	i32                    min_z     = 32767;
	i32                    max_z     = -32767;
	for (i32 i = 0; i < num_verts; ++i) {
		i32 x = va0->buffer[i * 4];
		i32 y = va0->buffer[i * 4 + 1];
		i32 z = va0->buffer[i * 4 + 2];
		if (x < min_x)
			min_x = x;
		if (x > max_x)
			max_x = x;
		if (y < min_y)
			min_y = y;
		if (y > max_y)
			max_y = y;
		if (z < min_z)
			min_z = z;
		if (z > max_z)
			max_z = z;
	}
	i32 box_size = math_max(max_x - min_x, math_max(max_y - min_y, max_z - min_z));

	f32 cells = 200.0 * (1.0 - strength);
	if (cells < 2.0)
		cells = 2.0;
	i32 cell_size = math_floor(box_size / (float)cells);
	if (cell_size < 1)
		cell_size = 1;

	util_mesh_quantized  = i32_array_create(num_verts * 3);
	i32_array_t *indices = i32_array_create_from_raw((i32[]){}, 0);

	for (i32 i = 0; i < num_verts; ++i) {
		util_mesh_quantized->buffer[i * 3]     = math_floor((va0->buffer[i * 4] - min_x) / (float)cell_size);
		util_mesh_quantized->buffer[i * 3 + 1] = math_floor((va0->buffer[i * 4 + 1] - min_y) / (float)cell_size);
		util_mesh_quantized->buffer[i * 3 + 2] = math_floor((va0->buffer[i * 4 + 2] - min_z) / (float)cell_size);
		i32_array_push(indices, i);
	}

	i32_array_sort(indices, &util_mesh_decimate_sort);
	i32_array_t *remap           = i32_array_create(num_verts);
	i32          new_verts_count = 0;
	i32_array_t *unique_indices  = i32_array_create_from_raw((i32[]){}, 0);

	if (indices->length > 0) {
		i32 start_of_cell                 = 0;
		remap->buffer[indices->buffer[0]] = 0;
		for (i32 i = 1; i <= indices->length; ++i) {
			bool is_new_cell = false;
			if (i < indices->length) {
				i32 curr = indices->buffer[i];
				i32 prev = indices->buffer[i - 1];
				if (util_mesh_quantized->buffer[curr * 3] != util_mesh_quantized->buffer[prev * 3] ||
				    util_mesh_quantized->buffer[curr * 3 + 1] != util_mesh_quantized->buffer[prev * 3 + 1] ||
				    util_mesh_quantized->buffer[curr * 3 + 2] != util_mesh_quantized->buffer[prev * 3 + 2]) {
					is_new_cell = true;
				}
			}
			else {
				is_new_cell = true;
			}
			if (is_new_cell) {
				i32_array_push(unique_indices, indices->buffer[start_of_cell]);
				for (i32 k = start_of_cell; k < i; ++k) {
					remap->buffer[indices->buffer[k]] = new_verts_count;
				}
				new_verts_count++;
				start_of_cell = i;
			}
		}
	}

	i16_array_t *new_va0 = i16_array_create(new_verts_count * 4);
	i16_array_t *new_va1 = i16_array_create(new_verts_count * 2);
	i16_array_t *new_va2 = i16_array_create(new_verts_count * 2);
	for (i32 i = 0; i < new_verts_count; ++i) {
		i32 old_idx                = unique_indices->buffer[i];
		new_va0->buffer[i * 4]     = va0->buffer[old_idx * 4];
		new_va0->buffer[i * 4 + 1] = va0->buffer[old_idx * 4 + 1];
		new_va0->buffer[i * 4 + 2] = va0->buffer[old_idx * 4 + 2];
		new_va0->buffer[i * 4 + 3] = va0->buffer[old_idx * 4 + 3];
		new_va1->buffer[i * 2]     = va1->buffer[old_idx * 2];
		new_va1->buffer[i * 2 + 1] = va1->buffer[old_idx * 2 + 1];
		new_va2->buffer[i * 2]     = va2->buffer[old_idx * 2];
		new_va2->buffer[i * 2 + 1] = va2->buffer[old_idx * 2 + 1];
	}

	u32_array_t *new_inda = u32_array_create_from_raw((u32[]){}, 0);
	for (i32 i = 0; i < math_floor(inda->length / 3.0); ++i) {
		i32 i1 = remap->buffer[inda->buffer[i * 3]];
		i32 i2 = remap->buffer[inda->buffer[i * 3 + 1]];
		i32 i3 = remap->buffer[inda->buffer[i * 3 + 2]];
		if (i1 != i2 && i1 != i3 && i2 != i3) {
			u32_array_push(new_inda, i1);
			u32_array_push(new_inda, i2);
			u32_array_push(new_inda, i3);
		}
	}

	mesh_data_t *raw = ALLOC_INIT(mesh_data_t, {.name          = string("%s_decimated", o->base->name),
	                                            .vertex_arrays = any_array_create_from_raw(
	                                                (void *[]){
	                                                    ALLOC_INIT(vertex_array_t, {.values = new_va0, .attrib = "pos", .data = "short4norm"}),
	                                                    ALLOC_INIT(vertex_array_t, {.values = new_va1, .attrib = "nor", .data = "short2norm"}),
	                                                    ALLOC_INIT(vertex_array_t, {.values = new_va2, .attrib = "tex", .data = "short2norm"}),
	                                                },
	                                                3),
	                                            .index_array = u32_array_create_from_array(new_inda),
	                                            .scale_pos   = o->data->scale_pos,
	                                            .scale_tex   = 1.0});

	mesh_data_t *new_data    = mesh_data_create(raw);
	new_data->_->owns_arrays = true;
	if (!util_mesh_data_is_shared(o->data)) {
		sys_notify_on_next_frame(&util_mesh_delete_data_uncache, o->data);
	}
	o->data = new_data;
	util_mesh_calc_normals(true);
	util_mesh_uv_unwrap();
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

void util_mesh_smooth() {
	mesh_object_t *o        = g_project->_->paint_objects->buffer[0];
	mesh_data_t   *g        = o->data;
	i16_array_t   *va0      = g->vertex_arrays->buffer[0]->values;
	u32_array_t   *oinda    = g->index_array;
	i32            overts   = math_floor(va0->length / 4.0);
	i32            num_tris = math_floor(oinda->length / 3.0);

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

	util_mesh_calc_normals(true);
	util_mesh_uv_unwrap();
}

void util_mesh_bevel(f32 amount) {
	mesh_object_t *o         = g_project->_->paint_objects->buffer[0];
	mesh_data_t   *g         = o->data;
	i16_array_t   *va0       = g->vertex_arrays->buffer[0]->values;
	i16_array_t   *va2       = g->vertex_arrays->buffer[2]->values;
	u32_array_t   *inda      = g->index_array;
	i32            num_verts = math_floor(va0->length / 4.0);
	i32            num_tris  = math_floor(inda->length / 3.0);

	if (amount < 0.0f)
		amount = 0.0f;
	if (amount > 0.49f)
		amount = 0.49f;

	// Position-weld
	i32_array_t *weld_sort = i32_array_create(num_verts);
	for (i32 i = 0; i < num_verts; ++i)
		weld_sort->buffer[i] = i;
	util_mesh_va0 = va0;
	i32_array_sort(weld_sort, &util_mesh_calc_normals_sort);

	i32_array_t *weld_id = i32_array_create(num_verts);
	if (num_verts > 0) {
		i32 rep              = weld_sort->buffer[0];
		weld_id->buffer[rep] = rep;
		for (i32 i = 1; i < num_verts; ++i) {
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

	// One integer per unique position
	i32_array_t *compact_id  = i32_array_create(num_verts);
	i32          num_compact = 0;
	for (i32 i = 0; i < num_verts; ++i)
		if (weld_id->buffer[i] == i)
			compact_id->buffer[i] = num_compact++;
	for (i32 i = 0; i < num_verts; ++i)
		if (weld_id->buffer[i] != i)
			compact_id->buffer[i] = compact_id->buffer[weld_id->buffer[i]];

	i32          inner_base  = num_verts;
	i32          cap_base    = num_verts + num_tris * 3;
	i32          total_verts = cap_base + num_compact;
	i16_array_t *new_va0     = i16_array_create(total_verts * 4);
	i16_array_t *new_va1     = i16_array_create(total_verts * 2);
	i16_array_t *new_va2     = i16_array_create(total_verts * 2);

	for (i32 i = 0; i < num_verts; ++i) {
		new_va0->buffer[i * 4]     = va0->buffer[i * 4];
		new_va0->buffer[i * 4 + 1] = va0->buffer[i * 4 + 1];
		new_va0->buffer[i * 4 + 2] = va0->buffer[i * 4 + 2];
		new_va0->buffer[i * 4 + 3] = 0;
		new_va2->buffer[i * 2]     = va2->buffer[i * 2];
		new_va2->buffer[i * 2 + 1] = va2->buffer[i * 2 + 1];
	}

	for (i32 t = 0; t < num_tris; ++t) {
		i32 i0  = inda->buffer[t * 3];
		i32 i1  = inda->buffer[t * 3 + 1];
		i32 i2  = inda->buffer[t * 3 + 2];
		f32 cx  = (va0->buffer[i0 * 4] + va0->buffer[i1 * 4] + va0->buffer[i2 * 4]) / 3.0f;
		f32 cy  = (va0->buffer[i0 * 4 + 1] + va0->buffer[i1 * 4 + 1] + va0->buffer[i2 * 4 + 1]) / 3.0f;
		f32 cz  = (va0->buffer[i0 * 4 + 2] + va0->buffer[i1 * 4 + 2] + va0->buffer[i2 * 4 + 2]) / 3.0f;
		f32 ctx = (va2->buffer[i0 * 2] + va2->buffer[i1 * 2] + va2->buffer[i2 * 2]) / 3.0f;
		f32 cty = (va2->buffer[i0 * 2 + 1] + va2->buffer[i1 * 2 + 1] + va2->buffer[i2 * 2 + 1]) / 3.0f;
		for (i32 k = 0; k < 3; ++k) {
			i32 vi                      = inda->buffer[t * 3 + k];
			i32 ni                      = inner_base + t * 3 + k;
			new_va0->buffer[ni * 4]     = (i16)math_floor(va0->buffer[vi * 4] * (1.0f - amount) + cx * amount);
			new_va0->buffer[ni * 4 + 1] = (i16)math_floor(va0->buffer[vi * 4 + 1] * (1.0f - amount) + cy * amount);
			new_va0->buffer[ni * 4 + 2] = (i16)math_floor(va0->buffer[vi * 4 + 2] * (1.0f - amount) + cz * amount);
			new_va0->buffer[ni * 4 + 3] = 0;
			new_va2->buffer[ni * 2]     = (i16)math_floor(va2->buffer[vi * 2] * (1.0f - amount) + ctx * amount);
			new_va2->buffer[ni * 2 + 1] = (i16)math_floor(va2->buffer[vi * 2 + 1] * (1.0f - amount) + cty * amount);
		}
	}

	f32_array_t *cap_sx  = f32_array_create(num_compact);
	f32_array_t *cap_sy  = f32_array_create(num_compact);
	f32_array_t *cap_sz  = f32_array_create(num_compact);
	f32_array_t *cap_su  = f32_array_create(num_compact);
	f32_array_t *cap_sv  = f32_array_create(num_compact);
	i32_array_t *cap_cnt = i32_array_create(num_compact);

	for (i32 t = 0; t < num_tris; ++t) {
		for (i32 k = 0; k < 3; ++k) {
			i32 c  = compact_id->buffer[(i32)inda->buffer[t * 3 + k]];
			i32 ni = inner_base + t * 3 + k;
			cap_sx->buffer[c] += new_va0->buffer[ni * 4];
			cap_sy->buffer[c] += new_va0->buffer[ni * 4 + 1];
			cap_sz->buffer[c] += new_va0->buffer[ni * 4 + 2];
			cap_su->buffer[c] += new_va2->buffer[ni * 2];
			cap_sv->buffer[c] += new_va2->buffer[ni * 2 + 1];
			cap_cnt->buffer[c]++;
		}
	}
	for (i32 c = 0; c < num_compact; ++c) {
		i32 ni  = cap_base + c;
		i32 cnt = cap_cnt->buffer[c];
		if (cnt > 0) {
			new_va0->buffer[ni * 4]     = (i16)math_floor(cap_sx->buffer[c] / cnt);
			new_va0->buffer[ni * 4 + 1] = (i16)math_floor(cap_sy->buffer[c] / cnt);
			new_va0->buffer[ni * 4 + 2] = (i16)math_floor(cap_sz->buffer[c] / cnt);
			new_va0->buffer[ni * 4 + 3] = 0;
			new_va2->buffer[ni * 2]     = (i16)math_floor(cap_su->buffer[c] / cnt);
			new_va2->buffer[ni * 2 + 1] = (i16)math_floor(cap_sv->buffer[c] / cnt);
		}
	}

	i32          num_he = num_tris * 3;
	i32_array_t *he_vlo = i32_array_create(num_he);
	i32_array_t *he_vhi = i32_array_create(num_he);
	i32_array_t *he_tri = i32_array_create(num_he);
	i32_array_t *he_cor = i32_array_create(num_he);

	for (i32 t = 0; t < num_tris; ++t) {
		for (i32 k = 0; k < 3; ++k) {
			i32 ca            = compact_id->buffer[inda->buffer[t * 3 + k]];
			i32 cb            = compact_id->buffer[inda->buffer[t * 3 + (k + 1) % 3]];
			i32 h             = t * 3 + k;
			he_vlo->buffer[h] = ca < cb ? ca : cb;
			he_vhi->buffer[h] = ca < cb ? cb : ca;
			he_tri->buffer[h] = t;
			he_cor->buffer[h] = k;
		}
	}

	i32_array_t *he_order = i32_array_create(num_he);
	for (i32 i = 0; i < num_he; ++i)
		he_order->buffer[i] = i;
	_cc_he_vlo = he_vlo->buffer;
	_cc_he_vhi = he_vhi->buffer;
	i32_array_sort(he_order, &_util_mesh_subdivide_sort);

	u32_array_t *new_inda = u32_array_create_from_raw((u32[]){}, 0);

	for (i32 t = 0; t < num_tris; ++t) {
		u32_array_push(new_inda, (u32)(inner_base + t * 3));
		u32_array_push(new_inda, (u32)(inner_base + t * 3 + 1));
		u32_array_push(new_inda, (u32)(inner_base + t * 3 + 2));
	}

	for (i32 i = 0; i < num_he;) {
		i32 j = i + 1;
		while (j < num_he && he_vlo->buffer[he_order->buffer[j]] == he_vlo->buffer[he_order->buffer[i]] &&
		       he_vhi->buffer[he_order->buffer[j]] == he_vhi->buffer[he_order->buffer[i]])
			++j;

		if (j - i >= 2) {
			i32 h0   = he_order->buffer[i];
			i32 h1   = he_order->buffer[i + 1];
			i32 cvlo = he_vlo->buffer[h0];
			i32 cvhi = he_vhi->buffer[h0];

			i32 hf, hr;
			if (compact_id->buffer[(i32)inda->buffer[he_tri->buffer[h0] * 3 + he_cor->buffer[h0]]] == cvlo) {
				hf = h0;
				hr = h1;
			}
			else {
				hf = h1;
				hr = h0;
			}
			i32 tf = he_tri->buffer[hf], kf = he_cor->buffer[hf];
			i32 tr2 = he_tri->buffer[hr], kr2 = he_cor->buffer[hr];

			i32 s0_vlo = inner_base + tf * 3 + kf;
			i32 s0_vhi = inner_base + tf * 3 + (kf + 1) % 3;

			i32 s1_vhi = inner_base + tr2 * 3 + kr2;
			i32 s1_vlo = inner_base + tr2 * 3 + (kr2 + 1) % 3;

			i32 cap_vlo = cap_base + cvlo;
			i32 cap_vhi = cap_base + cvhi;

			u32_array_push(new_inda, (u32)s0_vlo);
			u32_array_push(new_inda, (u32)s1_vhi);
			u32_array_push(new_inda, (u32)s0_vhi);
			u32_array_push(new_inda, (u32)s0_vlo);
			u32_array_push(new_inda, (u32)s1_vlo);
			u32_array_push(new_inda, (u32)s1_vhi);

			u32_array_push(new_inda, (u32)cap_vlo);
			u32_array_push(new_inda, (u32)s1_vlo);
			u32_array_push(new_inda, (u32)s0_vlo);

			u32_array_push(new_inda, (u32)cap_vhi);
			u32_array_push(new_inda, (u32)s0_vhi);
			u32_array_push(new_inda, (u32)s1_vhi);
		}

		i = j;
	}

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
	util_mesh_calc_normals(true);
	util_mesh_uv_unwrap();
}

void util_mesh_subdivide() {
	mesh_object_t *o         = g_project->_->paint_objects->buffer[0];
	mesh_data_t   *g         = o->data;
	i16_array_t   *va0       = g->vertex_arrays->buffer[0]->values;
	i16_array_t   *va2       = g->vertex_arrays->buffer[2]->values;
	u32_array_t   *inda      = g->index_array;
	i32            num_verts = math_floor(va0->length / 4.0);
	i32            num_tris  = math_floor(inda->length / 3.0);

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
		new_va0->buffer[i * 4 + 3] = 0;
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
		new_va0->buffer[epi * 4 + 3] = 0;
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
	util_mesh_calc_normals(true);
	util_mesh_uv_unwrap();
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
