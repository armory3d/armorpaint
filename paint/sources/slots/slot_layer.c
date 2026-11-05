
#include "../global.h"

bool slot_layer_defer_alloc = false;

void slot_layer_alloc_textures(slot_layer_t *raw) {
	if (raw->texpaint != NULL) {
		return;
	}

	char *ext    = raw->ext;
	char *format = base_bits == TEXTURE_BITS_BITS8 ? "RGBA32" : base_bits == TEXTURE_BITS_BITS16 ? "RGBA64" : "RGBA128";

	{
		render_target_t *t = render_target_create();
		t->name            = string("texpaint%s", ext);
		t->width           = config_get_texture_res_x();
		t->height          = config_get_texture_res_y();
		t->format          = string_copy(format);
		raw->texpaint      = render_path_create_render_target(t)->_image;
	}
	{
		render_target_t *t = render_target_create();
		t->name            = string("texpaint_nor%s", ext);
		t->width           = config_get_texture_res_x();
		t->height          = config_get_texture_res_y();
		t->format          = string_copy(format);
		raw->texpaint_nor  = render_path_create_render_target(t)->_image;
	}
	{
		render_target_t *t = render_target_create();
		t->name            = string("texpaint_pack%s", ext);
		t->width           = config_get_texture_res_x();
		t->height          = config_get_texture_res_y();
		t->format          = string_copy(format);
		raw->texpaint_pack = render_path_create_render_target(t)->_image;
	}
}

static bool slot_layer_is_unique_name(slot_layer_t *self, char *name) {
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		if (l != self && string_equals(l->name, name)) {
			return false;
		}
	}
	return true;
}

char *slot_layer_unique_name(slot_layer_t *self, char *name) {
	if (slot_layer_is_unique_name(self, name)) {
		return name;
	}
	char *base;
	i32   i   = strings_split_number_ext(name, &base);
	char *res = string_tmp("%s%s", base, strings_number_ext(++i));
	while (!slot_layer_is_unique_name(self, res)) {
		res = string_tmp("%s%s", base, strings_number_ext(++i));
	}
	return res;
}

slot_layer_t *slot_layer_create_undo(char *ext) {
	slot_layer_defer_alloc = true;
	slot_layer_t *l        = slot_layer_create(ext, LAYER_SLOT_TYPE_LAYER, NULL);
	slot_layer_defer_alloc = false;
	return l;
}

slot_layer_t *slot_layer_create(char *ext, layer_slot_type_t type, slot_layer_t *parent) {
	slot_layer_t *raw       = ALLOC_INIT(slot_layer_t, {0});
	raw->id                 = 0;
	raw->ext                = "";
	raw->visible            = true;
	raw->mask_opacity       = 1.0; // Opacity mask
	raw->show_panel         = true;
	raw->blending           = BLEND_TYPE_MIX;
	raw->object_mask        = 0;
	raw->scale              = 1.0;
	raw->angle              = 0.0;
	raw->uv_type            = UV_TYPE_UVMAP;
	raw->paint_base         = true;
	raw->paint_opac         = true;
	raw->paint_occ          = true;
	raw->paint_rough        = true;
	raw->paint_met          = true;
	raw->paint_nor          = true;
	raw->paint_nor_blend    = true;
	raw->paint_height       = true;
	raw->paint_height_blend = true;
	raw->paint_emis         = true;
	raw->paint_subs         = true;
	raw->decal_mat          = mat4_identity(); // Decal layer

	if (string_equals(ext, "")) {
		raw->id = 0;
		for (i32 i = 0; i < g_project->_->layers->length; ++i) {
			slot_layer_t *l = g_project->_->layers->buffer[i];
			if (l->id >= raw->id) {
				raw->id = l->id + 1;
			}
		}
		slot_layer_t_array_t *saved = history_merge_saved_layers();
		for (i32 i = 0; i < saved->length; ++i) {
			if (saved->buffer[i]->id >= raw->id) {
				raw->id = saved->buffer[i]->id + 1;
			}
		}
		ext = string("%d", raw->id);
	}
	raw->ext    = string_copy(ext);
	raw->parent = parent;

	if (type == LAYER_SLOT_TYPE_GROUP) {
		i32 id    = (raw->id + 1);
		raw->name = string("Group %d", id);
	}
	else if (type == LAYER_SLOT_TYPE_LAYER) {
		i32 id    = (raw->id + 1);
		raw->name = string("Layer %d", id);

		if (!slot_layer_defer_alloc) {
			slot_layer_alloc_textures(raw);
		}

		raw->texpaint_preview = gpu_create_render_target(util_render_layer_preview_size, util_render_layer_preview_size, GPU_TEXTURE_FORMAT_RGBA32);

		if (slot_layer_is_filter(raw)) {
			raw->name = string("Filter %d", id);
		}
	}

	else { // Mask
		i32 id        = (raw->id + 1);
		raw->name     = string("Mask %d", id);
		char *format  = "RGBA32"; // Full bits for undo support, R8 is used
		raw->blending = BLEND_TYPE_MIX;

		{
			render_target_t *t = render_target_create();
			t->name            = string("texpaint%s", ext);
			t->width           = config_get_texture_res_x();
			t->height          = config_get_texture_res_y();
			t->format          = string_copy(format);
			raw->texpaint      = render_path_create_render_target(t)->_image;
		}

		raw->texpaint_preview = gpu_create_render_target(util_render_layer_preview_size, util_render_layer_preview_size, GPU_TEXTURE_FORMAT_RGBA32);
	}

	raw->name = string_copy(slot_layer_unique_name(raw, raw->name));
	return raw;
}

void slot_layer_delete(slot_layer_t *raw) {
	slot_layer_unload(raw);

	slot_layer_t_array_t *children = slot_layer_get_direct_children(raw);
	for (i32 i = 0; i < children->length; ++i) {
		slot_layer_delete(children->buffer[i]);
	}

	i32 lpos = array_index_of(g_project->_->layers, raw);
	array_remove(g_project->_->layers, raw);
	tab_timeline_sync();
	// Undo can remove base layer and then restore it from undo layers
	if (g_project->_->layers->length > 0) {
		context_set_layer(g_project->_->layers->buffer[lpos > 0 ? lpos - 1 : 0]);
	}

	// Do not remove empty groups if the last layer is deleted as this prevents redo from working properly
}

void slot_layer_unload(slot_layer_t *raw) {
	if (slot_layer_is_group(raw)) {
		if (raw->texpaint_preview != NULL) {
			gpu_delete_texture(raw->texpaint_preview);
			raw->texpaint_preview = NULL;
		}
		return;
	}

	gpu_texture_t *_texpaint         = raw->texpaint;
	gpu_texture_t *_texpaint_nor     = raw->texpaint_nor;
	gpu_texture_t *_texpaint_pack    = raw->texpaint_pack;
	gpu_texture_t *_texpaint_preview = raw->texpaint_preview;

	gpu_delete_texture(_texpaint);
	if (_texpaint_nor != NULL) {
		gpu_delete_texture(_texpaint_nor);
	}
	if (_texpaint_pack != NULL) {
		gpu_delete_texture(_texpaint_pack);
	}
	if (_texpaint_preview != NULL) {
		gpu_delete_texture(_texpaint_preview);
	}
	if (raw->texpaint_sculpt != NULL) {
		gpu_delete_texture(raw->texpaint_sculpt);
		raw->texpaint_sculpt = NULL;
		map_delete(render_path_render_targets, string("texpaint_sculpt%s", raw->ext));
	}

	map_delete(render_path_render_targets, string("texpaint%s", raw->ext));
	if (slot_layer_is_layer(raw)) {
		map_delete(render_path_render_targets, string("texpaint_nor%s", raw->ext));
		map_delete(render_path_render_targets, string("texpaint_pack%s", raw->ext));
	}
}

void slot_layer_swap(slot_layer_t *raw, slot_layer_t *other) {
	if ((slot_layer_is_layer(raw) || slot_layer_is_mask(raw)) && (slot_layer_is_layer(other) || slot_layer_is_mask(other))) {
		render_target_t *rt0     = any_map_get(render_path_render_targets, string("texpaint%s", raw->ext));
		render_target_t *rt1     = any_map_get(render_path_render_targets, string("texpaint%s", other->ext));
		rt0->_image              = other->texpaint;
		rt1->_image              = raw->texpaint;
		gpu_texture_t *_texpaint = raw->texpaint;
		raw->texpaint            = other->texpaint;
		other->texpaint          = _texpaint;

		gpu_texture_t *_texpaint_preview = raw->texpaint_preview;
		raw->texpaint_preview            = other->texpaint_preview;
		other->texpaint_preview          = _texpaint_preview;
	}

	if (raw->texpaint_sculpt != NULL && other->texpaint_sculpt != NULL) {
		render_target_t *srt0           = any_map_get(render_path_render_targets, string("texpaint_sculpt%s", raw->ext));
		render_target_t *srt1           = any_map_get(render_path_render_targets, string("texpaint_sculpt%s", other->ext));
		srt0->_image                    = other->texpaint_sculpt;
		srt1->_image                    = raw->texpaint_sculpt;
		gpu_texture_t *_texpaint_sculpt = raw->texpaint_sculpt;
		raw->texpaint_sculpt            = other->texpaint_sculpt;
		other->texpaint_sculpt          = _texpaint_sculpt;
	}

	if (slot_layer_is_layer(raw) && slot_layer_is_layer(other)) {
		render_target_t *nor0         = any_map_get(render_path_render_targets, string("texpaint_nor%s", raw->ext));
		nor0->_image                  = other->texpaint_nor;
		render_target_t *pack0        = any_map_get(render_path_render_targets, string("texpaint_pack%s", raw->ext));
		pack0->_image                 = other->texpaint_pack;
		render_target_t *nor1         = any_map_get(render_path_render_targets, string("texpaint_nor%s", other->ext));
		nor1->_image                  = raw->texpaint_nor;
		render_target_t *pack1        = any_map_get(render_path_render_targets, string("texpaint_pack%s", other->ext));
		pack1->_image                 = raw->texpaint_pack;
		gpu_texture_t *_texpaint_nor  = raw->texpaint_nor;
		gpu_texture_t *_texpaint_pack = raw->texpaint_pack;
		raw->texpaint_nor             = other->texpaint_nor;
		raw->texpaint_pack            = other->texpaint_pack;
		other->texpaint_nor           = _texpaint_nor;
		other->texpaint_pack          = _texpaint_pack;
	}
}

void slot_layer_clear(slot_layer_t *raw, i32 base_color, gpu_texture_t *base_image, f32 occlusion, f32 roughness, f32 metallic) {
	// Base
	_gpu_begin(raw->texpaint, NULL, NULL, GPU_CLEAR_COLOR, base_color, 0.0);
	gpu_end();
	if (base_image != NULL) {
		draw_begin(raw->texpaint, false, 0);
		draw_scaled_image(base_image, 0, 0, raw->texpaint->width, raw->texpaint->height);
		draw_end();
	}

	if (slot_layer_is_layer(raw)) {
		// Nor
		_gpu_begin(raw->texpaint_nor, NULL, NULL, GPU_CLEAR_COLOR, color_from_floats(0.5, 0.5, 1.0, 0.0), 0.0);
		gpu_end();
		// Occ, rough, met
		_gpu_begin(raw->texpaint_pack, NULL, NULL, GPU_CLEAR_COLOR, color_from_floats(occlusion, roughness, metallic, 0.0), 0.0);
		gpu_end();
	}

	if (raw->texpaint_sculpt != NULL) {
		sculpt_import_mesh_pack_to_texture(raw->texpaint_sculpt);
	}

	g_context->layer_preview_dirty = true;
	g_context->ddirty              = 3;
	g_context->rtdirty             = 1;
}

void slot_layer_invert_mask(slot_layer_t *raw) {
	gpu_texture_t *inverted = gpu_create_render_target(raw->texpaint->width, raw->texpaint->height, GPU_TEXTURE_FORMAT_RGBA32);
	draw_begin(inverted, false, 0);
	draw_set_pipeline(pipes_invert_mask);
	draw_image(raw->texpaint, 0, 0);
	draw_set_pipeline(NULL);
	draw_end();
	gpu_texture_t *_texpaint = raw->texpaint;
	gpu_delete_texture(_texpaint);
	render_target_t *rt = any_map_get(render_path_render_targets, string("texpaint%d", raw->id));
	raw->texpaint = rt->_image     = inverted;
	g_context->layer_preview_dirty = true;
	g_context->ddirty              = 3;
	g_context->rtdirty             = 1;
}

void layers_apply_mask(slot_layer_t *l, slot_layer_t *m) {
	if (!slot_layer_is_layer(l) || !slot_layer_is_mask(m)) {
		return;
	}

	layers_make_temp_img();

	// Copy layer to temp
	draw_begin(layers_temp_image, false, 0);
	draw_set_pipeline(pipes_copy);
	draw_image(l->texpaint, 0, 0);
	draw_set_pipeline(NULL);
	draw_end();

	// Apply mask
	_gpu_begin(l->texpaint, NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
	gpu_set_pipeline(pipes_apply_mask);
	gpu_set_texture(pipes_tex0_mask, layers_temp_image);
	gpu_set_texture(pipes_texa_mask, m->texpaint);
	gpu_set_float(pipes_opac_apply_mask, m->mask_opacity);
	gpu_set_vertex_buffer(const_data_screen_aligned_vb);
	gpu_set_index_buffer(const_data_screen_aligned_ib);
	gpu_draw();
	gpu_end();
}

void slot_layer_apply_mask(slot_layer_t *raw) {
	if (raw->parent->fill_material != NULL) {
		slot_layer_to_paint_layer(raw->parent);
	}
	if (slot_layer_is_group(raw->parent)) {
		for (i32 i = 0; i < slot_layer_get_children(raw->parent)->length; ++i) {
			slot_layer_t *c = slot_layer_get_children(raw->parent)->buffer[i];
			layers_apply_mask(c, raw);
		}
	}
	else {
		layers_apply_mask(raw->parent, raw);
	}
	slot_layer_delete(raw);
}

static slot_layer_t *slot_layer_copy(slot_layer_t *raw) {
	slot_layer_t *l = slot_layer_create("",
	                                    slot_layer_is_layer(raw)  ? LAYER_SLOT_TYPE_LAYER
	                                    : slot_layer_is_mask(raw) ? LAYER_SLOT_TYPE_MASK
	                                                              : LAYER_SLOT_TYPE_GROUP,
	                                    raw->parent);

	if (slot_layer_is_layer(raw)) {
		gpu_pipeline_t *pipe = base_bits == TEXTURE_BITS_BITS8 ? pipes_copy : base_bits == TEXTURE_BITS_BITS16 ? pipes_copy64 : pipes_copy128;
		draw_begin(l->texpaint, false, 0);
		draw_set_pipeline(pipe);
		draw_image(raw->texpaint, 0, 0);
		draw_set_pipeline(NULL);
		draw_end();

		if (l->texpaint_nor != NULL) {
			draw_begin(l->texpaint_nor, false, 0);
			draw_set_pipeline(pipe);
			draw_image(raw->texpaint_nor, 0, 0);
			draw_set_pipeline(NULL);
			draw_end();
		}

		if (l->texpaint_pack != NULL) {
			draw_begin(l->texpaint_pack, false, 0);
			draw_set_pipeline(pipe);
			draw_image(raw->texpaint_pack, 0, 0);
			draw_set_pipeline(NULL);
			draw_end();
		}
	}
	else if (slot_layer_is_mask(raw)) {
		draw_begin(l->texpaint, false, 0);
		draw_set_pipeline(pipes_copy); // Masks are RGBA32
		draw_image(raw->texpaint, 0, 0);
		draw_set_pipeline(NULL);
		draw_end();
	}

	if (l->texpaint_preview != NULL) {
		draw_begin(l->texpaint_preview, true, 0x00000000);
		draw_set_pipeline(pipes_copy);
		draw_scaled_image(raw->texpaint_preview, 0, 0, raw->texpaint_preview->width, raw->texpaint_preview->height);
		draw_set_pipeline(NULL);
		draw_end();
	}

	l->visible            = raw->visible;
	l->mask_opacity       = raw->mask_opacity;
	l->fill_material      = raw->fill_material;
	l->object_mask        = raw->object_mask;
	l->blending           = raw->blending;
	l->uv_type            = raw->uv_type;
	l->decal_mat          = raw->decal_mat;
	l->scale              = raw->scale;
	l->angle              = raw->angle;
	l->paint_base         = raw->paint_base;
	l->paint_opac         = raw->paint_opac;
	l->paint_occ          = raw->paint_occ;
	l->paint_rough        = raw->paint_rough;
	l->paint_met          = raw->paint_met;
	l->paint_nor          = raw->paint_nor;
	l->paint_nor_blend    = raw->paint_nor_blend;
	l->paint_height       = raw->paint_height;
	l->paint_height_blend = raw->paint_height_blend;
	l->paint_emis         = raw->paint_emis;
	l->paint_subs         = raw->paint_subs;

	if (raw->path_points != NULL) {
		l->path_points        = f32_array_create_from_raw(raw->path_points->buffer, raw->path_points->length);
		l->path_points_world  = f32_array_create_from_raw(raw->path_points_world->buffer, raw->path_points_world->length);
		l->path_points_camera = f32_array_create_from_raw(raw->path_points_camera->buffer, raw->path_points_camera->length);
		l->path_points_parent = i32_array_create_from_raw(raw->path_points_parent->buffer, raw->path_points_parent->length);
		l->path_tool          = raw->path_tool;
		l->path_curved        = raw->path_curved;
		l->path_text          = raw->path_text;
		l->path_material      = raw->path_material;
	}

	return l;
}

slot_layer_t *slot_layer_duplicate(slot_layer_t *raw) {
	slot_layer_t *l = slot_layer_copy(raw);
	array_insert(g_project->_->layers, array_index_of(g_project->_->layers, raw) + 1, l);
	tab_stages_add_layer(l->name);
	return l;
}

slot_layer_t *slot_layer_clone(slot_layer_t *raw) {
	slot_layer_t *l = slot_layer_copy(raw);
	l->name         = raw->name;
	l->show_panel   = raw->show_panel;
	l->uv_map       = raw->uv_map;
	if (raw->texpaint_sculpt != NULL) {
		render_target_t *t = render_target_create();
		t->name            = string("texpaint_sculpt%s", l->ext);
		t->width           = raw->texpaint_sculpt->width;
		t->height          = raw->texpaint_sculpt->height;
		t->format          = "RGBA128";
		l->texpaint_sculpt = render_path_create_render_target(t)->_image;
		draw_begin(l->texpaint_sculpt, false, 0);
		draw_set_pipeline(pipes_copy128);
		draw_image(raw->texpaint_sculpt, 0, 0);
		draw_set_pipeline(NULL);
		draw_end();
	}
	return l;
}

static void slot_layer_draw_resized(gpu_texture_t *src, i32 res_x, i32 res_y) {
	if (layers_crop) {
		draw_scaled_sub_image(src, layers_crop_x, layers_crop_y, res_x, res_y, 0, 0, res_x, res_y);
	}
	else {
		draw_scaled_image(src, 0, 0, res_x, res_y);
	}
}

void slot_layer_resize_and_set_bits(slot_layer_t *raw) {
	i32        res_x = config_get_texture_res_x();
	i32        res_y = config_get_texture_res_y();
	any_map_t *rts   = render_path_render_targets;

	if (slot_layer_is_layer(raw)) {
		gpu_texture_format_t format = base_bits == TEXTURE_BITS_BITS8    ? GPU_TEXTURE_FORMAT_RGBA32
		                              : base_bits == TEXTURE_BITS_BITS16 ? GPU_TEXTURE_FORMAT_RGBA64
		                                                                 : GPU_TEXTURE_FORMAT_RGBA128;

		gpu_pipeline_t *pipe = format == GPU_TEXTURE_FORMAT_RGBA32 ? pipes_copy : format == GPU_TEXTURE_FORMAT_RGBA64 ? pipes_copy64 : pipes_copy128;

		gpu_texture_t *_texpaint = raw->texpaint;
		raw->texpaint            = gpu_create_render_target(res_x, res_y, format);
		draw_begin(raw->texpaint, false, 0);
		draw_set_pipeline(pipe);
		slot_layer_draw_resized(_texpaint, res_x, res_y);
		draw_set_pipeline(NULL);
		draw_end();

		gpu_texture_t *_texpaint_nor = raw->texpaint_nor;
		if (raw->texpaint_nor != NULL) {
			raw->texpaint_nor = gpu_create_render_target(res_x, res_y, format);
			draw_begin(raw->texpaint_nor, false, 0);
			draw_set_pipeline(pipe);
			slot_layer_draw_resized(_texpaint_nor, res_x, res_y);
			draw_set_pipeline(NULL);
			draw_end();
		}

		gpu_texture_t *_texpaint_pack = raw->texpaint_pack;
		if (raw->texpaint_pack != NULL) {
			raw->texpaint_pack = gpu_create_render_target(res_x, res_y, format);
			draw_begin(raw->texpaint_pack, false, 0);
			draw_set_pipeline(pipe);
			slot_layer_draw_resized(_texpaint_pack, res_x, res_y);
			draw_set_pipeline(NULL);
			draw_end();
		}

		gpu_delete_texture(_texpaint);
		if (_texpaint_nor != NULL) {
			gpu_delete_texture(_texpaint_nor);
		}
		if (_texpaint_pack != NULL) {
			gpu_delete_texture(_texpaint_pack);
		}

		render_target_t *rt = any_map_get(rts, string("texpaint%s", raw->ext));
		rt->_image          = raw->texpaint;

		if (raw->texpaint_nor != NULL) {
			render_target_t *rt_nor = any_map_get(rts, string("texpaint_nor%s", raw->ext));
			rt_nor->_image          = raw->texpaint_nor;
		}

		if (raw->texpaint_pack != NULL) {
			render_target_t *rt_pack = any_map_get(rts, string("texpaint_pack%s", raw->ext));
			rt_pack->_image          = raw->texpaint_pack;
		}
	}
	else if (slot_layer_is_mask(raw)) {
		gpu_texture_t *_texpaint = raw->texpaint;
		raw->texpaint            = gpu_create_render_target(res_x, res_y, GPU_TEXTURE_FORMAT_RGBA32);

		draw_begin(raw->texpaint, false, 0);
		draw_set_pipeline(pipes_copy8);
		slot_layer_draw_resized(_texpaint, res_x, res_y);
		draw_set_pipeline(NULL);
		draw_end();

		gpu_delete_texture(_texpaint);

		render_target_t *rt = any_map_get(rts, string("texpaint%s", raw->ext));
		rt->_image          = raw->texpaint;
	}

	// Clear and repack the base mesh
	if (raw->texpaint_sculpt != NULL) {
		gpu_texture_t *_texpaint_sculpt = raw->texpaint_sculpt;
		raw->texpaint_sculpt            = gpu_create_render_target(res_x, res_y, GPU_TEXTURE_FORMAT_RGBA128);
		sculpt_import_mesh_pack_to_texture(raw->texpaint_sculpt);
		gpu_delete_texture(_texpaint_sculpt);

		render_target_t *srt = any_map_get(rts, string("texpaint_sculpt%s", raw->ext));
		srt->width           = res_x;
		srt->height          = res_y;
		srt->_image          = raw->texpaint_sculpt;
	}
}

void slot_layer_to_fill_layer_on_next_frame(void *_) {
	make_material_parse_paint_material(true);
	g_context->layer_preview_dirty = true;
	ui_dock_redraw(TAB_AREA_SIDEBAR0);
}

void slot_layer_to_fill_layer(slot_layer_t *raw) {
	context_set_layer(raw);
	raw->fill_material = g_context->material;
	layers_update_fill_layer(true);
	sys_notify_on_next_frame(&slot_layer_to_fill_layer_on_next_frame, NULL);
}

void slot_layer_to_paint_layer(slot_layer_t *raw) {
	context_set_layer(raw);
	raw->fill_material = NULL;
	if (raw->path_material != NULL) {
		raw->path_material = NULL;
		raw->path_points   = NULL;
		raw->path_text     = false;
		util_layer_update_path();
	}
	make_material_parse_paint_material(true);
	g_context->layer_preview_dirty = true;
	ui_dock_redraw(TAB_AREA_SIDEBAR0);
}

bool slot_layer_is_visible(slot_layer_t *raw) {
	for (slot_layer_t *p = raw; p != NULL; p = p->parent) {
		if (!p->visible) {
			return false;
		}
	}
	return true;
}

bool slot_layer_shows(slot_layer_t *raw) {
	if (!slot_layer_is_group(raw)) {
		return raw->visible;
	}
	slot_layer_t_array_t *children = slot_layer_get_children(raw);
	for (i32 i = 0; children != NULL && i < children->length; ++i) {
		if (slot_layer_is_visible(children->buffer[i])) {
			return true;
		}
	}
	return false;
}

bool slot_layer_is_descendant(slot_layer_t *raw, slot_layer_t *ancestor) {
	for (slot_layer_t *p = raw->parent; p != NULL; p = p->parent) {
		if (p == ancestor) {
			return true;
		}
	}
	return false;
}

i32 slot_layer_count_descendants(slot_layer_t *raw) {
	i32 count = 0;
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		if (slot_layer_is_descendant(g_project->_->layers->buffer[i], raw)) {
			++count;
		}
	}
	return count;
}

slot_layer_t_array_t *slot_layer_get_children(slot_layer_t *raw) {
	slot_layer_t_array_t *children = NULL; // Layers inside a group, at any depth
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		if (slot_layer_is_layer(l) && !slot_layer_is_filter(l) && slot_layer_is_descendant(l, raw)) {
			if (children == NULL) {
				children = any_array_create_from_raw((void *[]){}, 0);
			}
			any_array_push(children, l);
		}
	}
	return children;
}

slot_layer_t_array_t *slot_layer_get_direct_children(slot_layer_t *raw) {
	slot_layer_t_array_t *children = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		if (l->parent == raw) {
			any_array_push(children, l);
		}
	}
	return children;
}

static bool slot_layer_mask_applies(slot_layer_t *raw, slot_layer_t *l, bool include_group_masks) {
	return slot_layer_is_mask(l) && (l->parent == raw || (include_group_masks && !slot_layer_is_filter(raw) && slot_layer_is_descendant(raw, l->parent)));
}

slot_layer_t_array_t *slot_layer_get_masks(slot_layer_t *raw, bool include_group_masks) {
	if (slot_layer_is_mask(raw)) {
		return NULL;
	}

	i32 count = 0;
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		if (slot_layer_mask_applies(raw, g_project->_->layers->buffer[i], include_group_masks)) {
			count++;
		}
	}
	if (count == 0) {
		return NULL;
	}

	slot_layer_t_array_t *children = any_array_create_from_raw_tmp(NULL, count);
	i32                   pos      = 0;
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		if (slot_layer_mask_applies(raw, l, include_group_masks)) {
			children->buffer[pos++] = l;
		}
	}
	return children;
}

bool slot_layer_has_masks(slot_layer_t *raw, bool include_group_masks) {
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		if (slot_layer_mask_applies(raw, g_project->_->layers->buffer[i], include_group_masks)) {
			return true;
		}
	}
	return false;
}

slot_layer_t_array_t *slot_layer_get_filters(slot_layer_t *raw, bool include_group_filters) {
	if (slot_layer_is_filter(raw)) {
		return NULL;
	}

	slot_layer_t_array_t *children = NULL;
	// Child filters of a layer
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		if (l->parent == raw && slot_layer_is_filter(l)) {
			if (children == NULL) {
				children = any_array_create_from_raw((void *[]){}, 0);
			}
			any_array_push(children, l);
		}
	}
	// Child filters of a parent group
	if (include_group_filters) {
		if (raw->parent != NULL && slot_layer_is_group(raw->parent)) {
			for (i32 i = 0; i < g_project->_->layers->length; ++i) {
				slot_layer_t *l = g_project->_->layers->buffer[i];
				if (l->parent == raw->parent && slot_layer_is_filter(l)) {
					if (children == NULL) {
						children = any_array_create_from_raw((void *[]){}, 0);
					}
					any_array_push(children, l);
				}
			}
		}
	}
	return children;
}

bool slot_layer_has_visible_filter(slot_layer_t *raw) {
	slot_layer_t_array_t *filters = slot_layer_get_filters(raw, false);
	if (filters == NULL) {
		return false;
	}
	for (i32 i = 0; i < filters->length; ++i) {
		if (slot_layer_is_visible(filters->buffer[i])) {
			return true;
		}
	}
	return false;
}

f32 slot_layer_get_opacity(slot_layer_t *raw) {
	f32 f = raw->mask_opacity;
	if (slot_layer_is_layer(raw)) {
		for (slot_layer_t *p = raw->parent; p != NULL; p = p->parent) {
			f *= p->mask_opacity;
		}
	}
	return f;
}

i32 slot_layer_get_object_mask(slot_layer_t *raw) {
	return (slot_layer_is_mask(raw) || slot_layer_is_filter(raw)) ? raw->parent->object_mask : raw->object_mask;
}

i32 slot_layer_get_blending(slot_layer_t *raw) {
	return slot_layer_is_filter(raw) ? raw->parent->blending : raw->blending;
}

bool slot_layer_is_layer(slot_layer_t *raw) {
	return raw->texpaint != NULL && raw->texpaint_nor != NULL;
}

bool slot_layer_is_path(slot_layer_t *raw) {
	return raw->path_points != NULL;
}

bool slot_layer_is_group(slot_layer_t *raw) {
	return raw->texpaint == NULL;
}

bool slot_layer_is_filter(slot_layer_t *raw) {
	return raw->texpaint != NULL && raw->texpaint_nor != NULL && raw->parent != NULL && slot_layer_is_layer(raw->parent);
}

bool slot_layer_is_mask(slot_layer_t *raw) {
	return raw->texpaint != NULL && raw->texpaint_nor == NULL;
}

bool slot_layer_is_group_mask(slot_layer_t *raw) {
	return slot_layer_is_mask(raw) && slot_layer_is_group(raw->parent);
}

bool slot_layer_is_layer_mask(slot_layer_t *raw) {
	return slot_layer_is_mask(raw) && slot_layer_is_layer(raw->parent);
}

static bool slot_layer_move_target(slot_layer_t *raw, i32 to, slot_layer_t **parent, i32 *pos) {
	slot_layer_t_array_t *layers = g_project->_->layers;
	i32                   from   = array_index_of(layers, raw);
	if (to < 0 || to > layers->length - 1 || to == from) {
		return false;
	}

	slot_layer_t *upper     = to > from ? (to < layers->length - 1 ? layers->buffer[to + 1] : NULL) : layers->buffer[to];
	slot_layer_t *collapsed = NULL;
	for (slot_layer_t *p = upper; p != NULL; p = p->parent) {
		if (!p->show_panel && slot_layer_count_descendants(p) > 0) {
			collapsed = p;
		}
	}

	if (upper == raw || (upper != NULL && slot_layer_is_descendant(upper, raw))) {
		return false;
	}

	if (slot_layer_is_mask(raw) || slot_layer_is_filter(raw)) {
		if (upper == NULL || collapsed != NULL) {
			return false;
		}
		*parent = slot_layer_is_mask(upper) || slot_layer_is_filter(upper) ? upper->parent : upper;
		*pos    = array_index_of(layers, upper);
		return true;
	}

	if (collapsed != NULL) {
		*parent = collapsed->parent;
		*pos    = array_index_of(layers, collapsed) - slot_layer_count_descendants(collapsed);
	}
	else if (upper == NULL) {
		*parent = NULL;
		*pos    = layers->length;
	}
	else {
		*pos = array_index_of(layers, upper);
		if (slot_layer_is_group(upper)) {
			*parent = upper;
		}
		else if (slot_layer_is_layer_mask(upper) || slot_layer_is_filter(upper)) {
			*parent = upper->parent->parent;
		}
		else {
			*parent = upper->parent;
		}
	}

	i32 start = from - slot_layer_count_descendants(raw);
	if (*pos >= start && *pos <= from + 1 && *parent == raw->parent) {
		return false;
	}

	i32 below = *pos - 1 >= start && *pos - 1 <= from ? start - 1 : *pos - 1;
	return below < 0 || (!slot_layer_is_mask(layers->buffer[below]) && !slot_layer_is_filter(layers->buffer[below]));
}

bool slot_layer_can_move(slot_layer_t *raw, i32 to) {
	slot_layer_t *parent;
	i32           pos;
	return slot_layer_move_target(raw, to, &parent, &pos);
}

void slot_layer_relocate(slot_layer_t *raw, i32 to) {
	slot_layer_t_array_t *layers = g_project->_->layers;
	i32                   n      = slot_layer_count_descendants(raw) + 1;
	i32                   start  = array_index_of(layers, raw) - n + 1;
	slot_layer_t_array_t *range  = any_array_create_from_raw_tmp(NULL, n);
	for (i32 i = 0; i < n; ++i) {
		range->buffer[i] = layers->buffer[start + i];
	}
	for (i32 i = 0; i < n; ++i) {
		array_remove(layers, range->buffer[i]);
	}
	for (i32 i = 0; i < n; ++i) {
		array_insert(layers, to - n + 1 + i, range->buffer[i]);
	}
}

void slot_layer_move(slot_layer_t *raw, i32 to) {
	slot_layer_t *parent;
	i32           pos;
	if (!slot_layer_move_target(raw, to, &parent, &pos)) {
		return;
	}

	slot_layer_t_array_t *pointers   = tab_layers_init_layer_map();
	slot_layer_t         *old_parent = raw->parent;
	i32                   from       = array_index_of(g_project->_->layers, raw);
	i32                   dest       = pos > from ? pos - 1 : pos + slot_layer_count_descendants(raw);

	context_set_layer(raw);
	history_order_layers(dest);
	ui_dock_redraw(TAB_AREA_SIDEBAR0);

	slot_layer_relocate(raw, dest);
	raw->parent = parent;

	if (old_parent != NULL && slot_layer_is_group(old_parent) && slot_layer_get_children(old_parent) == NULL) {
		history_delete_layer(old_parent)->with_prev = true;
	}
	context_set_layer(raw);

	tab_layers_remap_all_layer_pointers(pointers);
}
