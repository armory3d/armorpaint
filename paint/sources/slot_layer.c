
#include "global.h"

slot_layer_t *slot_layer_create(char *ext, layer_slot_type_t type, slot_layer_t *parent) {
	slot_layer_t *raw       = GC_ALLOC_INIT(slot_layer_t, {0});
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
		ext = string("%d", raw->id);
	}
	raw->ext    = string_copy(ext);
	raw->parent = parent;

	if (type == LAYER_SLOT_TYPE_GROUP) {
		i32 id    = (raw->id + 1);
		raw->name = string("Group %d", id);
	}
	else if (type == LAYER_SLOT_TYPE_LAYER) {
		i32 id       = (raw->id + 1);
		raw->name    = string("Layer %d", id);
		char *format = base_bits_handle->i == TEXTURE_BITS_BITS8 ? "RGBA32" : base_bits_handle->i == TEXTURE_BITS_BITS16 ? "RGBA64" : "RGBA128";

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

	return raw;
}

void slot_layer_delete(slot_layer_t *raw) {
	slot_layer_unload(raw);

	if (slot_layer_is_layer(raw)) {
		slot_layer_t_array_t *masks = slot_layer_get_masks(raw, false); // Prevents deleting group masks
		if (masks != NULL) {
			for (i32 i = 0; i < masks->length; ++i) {
				slot_layer_t *m = masks->buffer[i];
				slot_layer_delete(m);
			}
		}
	}
	else if (slot_layer_is_group(raw)) {
		slot_layer_t_array_t *children = slot_layer_get_children(raw);
		if (children != NULL) {
			for (i32 i = 0; i < children->length; ++i) {
				slot_layer_t *c = children->buffer[i];
				slot_layer_delete(c);
			}
		}
		slot_layer_t_array_t *masks = slot_layer_get_masks(raw, true);
		if (masks != NULL) {
			for (i32 i = 0; i < masks->length; ++i) {
				slot_layer_t *m = masks->buffer[i];
				slot_layer_delete(m);
			}
		}
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

slot_layer_t *slot_layer_duplicate(slot_layer_t *raw) {
	slot_layer_t_array_t *layers = g_project->_->layers;
	i32                   i      = array_index_of(layers, raw) + 1;
	slot_layer_t         *l      = slot_layer_create("",
                                        slot_layer_is_layer(raw)  ? LAYER_SLOT_TYPE_LAYER
	                                                 : slot_layer_is_mask(raw) ? LAYER_SLOT_TYPE_MASK
	                                                                           : LAYER_SLOT_TYPE_GROUP,
	                                                 raw->parent);
	array_insert(layers, i, l);

	if (slot_layer_is_layer(raw)) {
		draw_begin(l->texpaint, false, 0);
		draw_set_pipeline(pipes_copy);
		draw_image(raw->texpaint, 0, 0);
		draw_set_pipeline(NULL);
		draw_end();

		if (l->texpaint_nor != NULL) {
			draw_begin(l->texpaint_nor, false, 0);
			draw_set_pipeline(pipes_copy);
			draw_image(raw->texpaint_nor, 0, 0);
			draw_set_pipeline(NULL);
			draw_end();
		}

		if (l->texpaint_pack != NULL) {
			draw_begin(l->texpaint_pack, false, 0);
			draw_set_pipeline(pipes_copy);
			draw_image(raw->texpaint_pack, 0, 0);
			draw_set_pipeline(NULL);
			draw_end();
		}
	}
	else if (slot_layer_is_mask(raw)) {
		draw_begin(l->texpaint, false, 0);
		draw_set_pipeline(pipes_copy8);
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
		l->path_material      = raw->path_material;
	}

	return l;
}

void slot_layer_resize_and_set_bits(slot_layer_t *raw) {
	i32        res_x = config_get_texture_res_x();
	i32        res_y = config_get_texture_res_y();
	any_map_t *rts   = render_path_render_targets;

	if (slot_layer_is_layer(raw)) {
		gpu_texture_format_t format = base_bits_handle->i == TEXTURE_BITS_BITS8    ? GPU_TEXTURE_FORMAT_RGBA32
		                              : base_bits_handle->i == TEXTURE_BITS_BITS16 ? GPU_TEXTURE_FORMAT_RGBA64
		                                                                           : GPU_TEXTURE_FORMAT_RGBA128;

		gpu_pipeline_t *pipe = format == GPU_TEXTURE_FORMAT_RGBA32 ? pipes_copy : format == GPU_TEXTURE_FORMAT_RGBA64 ? pipes_copy64 : pipes_copy128;

		gpu_texture_t *_texpaint = raw->texpaint;
		raw->texpaint            = gpu_create_render_target(res_x, res_y, format);
		draw_begin(raw->texpaint, false, 0);
		draw_set_pipeline(pipe);
		draw_scaled_image(_texpaint, 0, 0, res_x, res_y);
		draw_set_pipeline(NULL);
		draw_end();

		gpu_texture_t *_texpaint_nor = raw->texpaint_nor;
		if (raw->texpaint_nor != NULL) {
			raw->texpaint_nor = gpu_create_render_target(res_x, res_y, format);
			draw_begin(raw->texpaint_nor, false, 0);
			draw_set_pipeline(pipe);
			draw_scaled_image(_texpaint_nor, 0, 0, res_x, res_y);
			draw_set_pipeline(NULL);
			draw_end();
		}

		gpu_texture_t *_texpaint_pack = raw->texpaint_pack;
		if (raw->texpaint_pack != NULL) {
			raw->texpaint_pack = gpu_create_render_target(res_x, res_y, format);
			draw_begin(raw->texpaint_pack, false, 0);
			draw_set_pipeline(pipe);
			draw_scaled_image(_texpaint_pack, 0, 0, res_x, res_y);
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
		draw_scaled_image(_texpaint, 0, 0, res_x, res_y);
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
	g_context->layer_preview_dirty                    = true;
	ui_base_hwnds->buffer[TAB_AREA_SIDEBAR0]->redraws = 2;
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
		util_layer_update_path();
	}
	make_material_parse_paint_material(true);
	g_context->layer_preview_dirty                    = true;
	ui_base_hwnds->buffer[TAB_AREA_SIDEBAR0]->redraws = 2;
}

bool slot_layer_is_visible(slot_layer_t *raw) {
	return raw->visible && (raw->parent == NULL || raw->parent->visible);
}

slot_layer_t_array_t *slot_layer_get_children(slot_layer_t *raw) {
	slot_layer_t_array_t *children = NULL; // Child layers of a group
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		if (l->parent == raw && slot_layer_is_layer(l)) {
			if (children == NULL) {
				children = any_array_create_from_raw((void *[]){}, 0);
			}
			any_array_push(children, l);
		}
	}
	return children;
}

slot_layer_t_array_t *slot_layer_get_recursive_children(slot_layer_t *raw) {
	slot_layer_t_array_t *children = NULL;
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		if (l->parent == raw) { // Child layers and group masks
			if (children == NULL) {
				children = any_array_create_from_raw((void *[]){}, 0);
			}
			any_array_push(children, l);
		}
		if (l->parent != NULL && l->parent->parent == raw) { // Layer masks
			if (children == NULL) {
				children = any_array_create_from_raw((void *[]){}, 0);
			}
			any_array_push(children, l);
		}
	}
	return children;
}

slot_layer_t_array_t *slot_layer_get_masks(slot_layer_t *raw, bool include_group_masks) {
	if (slot_layer_is_mask(raw)) {
		return NULL;
	}

	slot_layer_t_array_t *children = NULL;
	// Child masks of a layer
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		if (l->parent == raw && slot_layer_is_mask(l)) {
			if (children == NULL) {
				children = any_array_create_from_raw((void *[]){}, 0);
			}
			any_array_push(children, l);
		}
	}
	// Child masks of a parent group
	if (include_group_masks) {
		if (raw->parent != NULL && slot_layer_is_group(raw->parent)) {
			for (i32 i = 0; i < g_project->_->layers->length; ++i) {
				slot_layer_t *l = g_project->_->layers->buffer[i];
				if (l->parent == raw->parent && slot_layer_is_mask(l)) {
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

bool slot_layer_has_masks(slot_layer_t *raw, bool include_group_masks) {
	// Layer mask
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		if (l->parent == raw && slot_layer_is_mask(l)) {
			return true;
		}
	}

	// Group mask
	if (include_group_masks && raw->parent != NULL && slot_layer_is_group(raw->parent)) {
		for (i32 i = 0; i < g_project->_->layers->length; ++i) {
			slot_layer_t *l = g_project->_->layers->buffer[i];
			if (l->parent == raw->parent && slot_layer_is_mask(l)) {
				return true;
			}
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

f32 slot_layer_get_opacity(slot_layer_t *raw) {
	f32 f = raw->mask_opacity;
	if (slot_layer_is_layer(raw) && raw->parent != NULL) {
		f *= raw->parent->mask_opacity;
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

slot_layer_t *slot_layer_get_containing_group(slot_layer_t *raw) {
	if (raw->parent != NULL && slot_layer_is_group(raw->parent)) {
		return raw->parent;
	}
	else if (raw->parent != NULL && raw->parent->parent != NULL && slot_layer_is_group(raw->parent->parent)) {
		return raw->parent->parent;
	}
	else {
		return NULL;
	}
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

bool slot_layer_is_in_group(slot_layer_t *raw) {
	return raw->parent != NULL && (slot_layer_is_group(raw->parent) || (raw->parent->parent != NULL && slot_layer_is_group(raw->parent->parent)));
}

bool slot_layer_can_move(slot_layer_t *raw, i32 to) {
	i32 old_index = array_index_of(g_project->_->layers, raw);

	i32 delta = to - old_index; // If delta > 0 the layer is moved up, otherwise down
	if (to < 0 || to > g_project->_->layers->length - 1 || delta == 0) {
		return false;
	}

	// If the layer is moved up, all layers between the old position and the new one move one down
	// The layers above the new position stay where they are
	// If the new position is on top or on bottom no upper resp. lower layer exists
	slot_layer_t *new_upper_layer =
	    delta > 0 ? (to < g_project->_->layers->length - 1 ? g_project->_->layers->buffer[to + 1] : NULL) : g_project->_->layers->buffer[to];

	// Group or layer is collapsed so we check below and update the upper layer
	if (new_upper_layer != NULL && !new_upper_layer->show_panel) {
		slot_layer_t_array_t *children = slot_layer_get_recursive_children(new_upper_layer);
		to -= children != NULL ? children->length : 0;
		delta           = to - old_index;
		new_upper_layer = delta > 0 ? (to < g_project->_->layers->length - 1 ? g_project->_->layers->buffer[to + 1] : NULL) : g_project->_->layers->buffer[to];
	}

	slot_layer_t *new_lower_layer = delta > 0 ? g_project->_->layers->buffer[to] : (to > 0 ? g_project->_->layers->buffer[to - 1] : NULL);

	if (slot_layer_is_mask(raw)) {
		// Masks can not be on top
		if (new_upper_layer == NULL) {
			return false;
		}
		// Masks should not be placed below a collapsed group - this condition can be savely removed
		if (slot_layer_is_in_group(new_upper_layer) && !slot_layer_get_containing_group(new_upper_layer)->show_panel) {
			return false;
		}
		// Masks should not be placed below a collapsed layer - this condition can be savely removed
		if (slot_layer_is_mask(new_upper_layer) && !new_upper_layer->parent->show_panel) {
			return false;
		}
	}

	if (slot_layer_is_filter(raw)) {
		// Filters can not be on top
		if (new_upper_layer == NULL) {
			return false;
		}
		// Filters should not be placed below a collapsed group
		if (slot_layer_is_in_group(new_upper_layer) && !slot_layer_get_containing_group(new_upper_layer)->show_panel) {
			return false;
		}
		// Filters should not be placed below a collapsed layer
		if ((slot_layer_is_filter(new_upper_layer) || slot_layer_is_mask(new_upper_layer)) && !new_upper_layer->parent->show_panel) {
			return false;
		}
	}

	if (slot_layer_is_layer(raw) && !slot_layer_is_filter(raw)) {
		// Layers can not be moved directly below its own mask(s)
		if (new_upper_layer != NULL && slot_layer_is_mask(new_upper_layer) && new_upper_layer->parent == raw) {
			return false;
		}
		// Layers can not be moved directly below its own filter(s)
		if (new_upper_layer != NULL && slot_layer_is_filter(new_upper_layer) && new_upper_layer->parent == raw) {
			return false;
		}
		// Layers can not be placed above a mask as the mask would be reparented
		if (new_lower_layer != NULL && slot_layer_is_mask(new_lower_layer)) {
			return false;
		}
		// Layers can not be placed above a filter as the filter would be reparented
		if (new_lower_layer != NULL && slot_layer_is_filter(new_lower_layer)) {
			return false;
		}
	}

	// Currently groups can not be nested - thus valid positions for groups are:
	if (slot_layer_is_group(raw)) {
		// At the top
		if (new_upper_layer == NULL) {
			return true;
		}
		// NOT below its own children
		if (slot_layer_get_containing_group(new_upper_layer) == raw) {
			return false;
		}
		// At the bottom
		if (new_lower_layer == NULL) {
			return true;
		}
		// Above a group
		if (slot_layer_is_group(new_lower_layer)) {
			return true;
		}
		// Above a non-grouped layer
		if (slot_layer_is_layer(new_lower_layer) && !slot_layer_is_in_group(new_lower_layer)) {
			return true;
		}
		else {
			return false;
		}
	}

	return true;
}

void slot_layer_move(slot_layer_t *raw, i32 to) {
	if (!slot_layer_can_move(raw, to)) {
		return;
	}

	i32_map_t    *pointers  = tab_layers_init_layer_map();
	i32           old_index = array_index_of(g_project->_->layers, raw);
	i32           delta     = to - old_index;
	slot_layer_t *new_upper_layer =
	    delta > 0 ? (to < g_project->_->layers->length - 1 ? g_project->_->layers->buffer[to + 1] : NULL) : g_project->_->layers->buffer[to];

	// Group or layer is collapsed so we check below and update the upper layer
	if (new_upper_layer != NULL && !new_upper_layer->show_panel) {
		slot_layer_t_array_t *children = slot_layer_get_recursive_children(new_upper_layer);
		to -= children != NULL ? children->length : 0;
		delta           = to - old_index;
		new_upper_layer = delta > 0 ? (to < g_project->_->layers->length - 1 ? g_project->_->layers->buffer[to + 1] : NULL) : g_project->_->layers->buffer[to];
	}

	context_set_layer(raw);
	history_order_layers(to);
	ui_base_hwnds->buffer[TAB_AREA_SIDEBAR0]->redraws = 2;

	array_remove(g_project->_->layers, raw);
	array_insert(g_project->_->layers, to, raw);

	if (slot_layer_is_filter(raw)) {
		// Precondition new_upper_layer != NULL, ensured in can_move
		if (slot_layer_is_filter(new_upper_layer) || slot_layer_is_mask(new_upper_layer)) {
			raw->parent = new_upper_layer->parent;
		}
		else if (slot_layer_is_layer(new_upper_layer) || slot_layer_is_group(new_upper_layer)) {
			raw->parent = new_upper_layer;
		}
	}
	else if (slot_layer_is_layer(raw)) {
		slot_layer_t *old_parent = raw->parent;

		if (new_upper_layer == NULL) {
			raw->parent = NULL; // Placed on top
		}
		else if (slot_layer_is_in_group(new_upper_layer) && !slot_layer_get_containing_group(new_upper_layer)->show_panel) {
			raw->parent = NULL; // Placed below a collapsed group
		}
		else if (slot_layer_is_filter(new_upper_layer)) {
			raw->parent = new_upper_layer->parent->parent; // Placed below a filter, use the same level as the filter's parent
		}
		else if (slot_layer_is_layer(new_upper_layer)) {
			raw->parent = new_upper_layer->parent; // Placed below a layer, use the same parent
		}
		else if (slot_layer_is_group(new_upper_layer)) {
			raw->parent = new_upper_layer; // Placed as top layer in a group
		}
		else if (slot_layer_is_group_mask(new_upper_layer)) {
			raw->parent = new_upper_layer->parent; // Placed in a group below the lowest group mask
		}
		else if (slot_layer_is_layer_mask(new_upper_layer)) {
			raw->parent = slot_layer_get_containing_group(new_upper_layer); // Either the group the mask belongs to or NULL
		}

		// Layers can have masks as children
		// These have to be moved, too
		slot_layer_t_array_t *layer_masks = slot_layer_get_masks(raw, false);
		if (layer_masks != NULL) {
			for (i32 idx = 0; idx < layer_masks->length; ++idx) {
				slot_layer_t *mask = layer_masks->buffer[idx];
				array_remove(g_project->_->layers, mask);
				// If the masks are moved down each step increases the index below the layer by one.
				array_insert(g_project->_->layers, delta > 0 ? old_index + delta - 1 : old_index + delta + idx, mask);
			}
		}

		// Layers can have filters as children
		// These have to be moved, too
		slot_layer_t_array_t *layer_filters = slot_layer_get_filters(raw, false);
		if (layer_filters != NULL) {
			i32 masks_count = layer_masks != NULL ? layer_masks->length : 0;
			for (i32 idx = 0; idx < layer_filters->length; ++idx) {
				slot_layer_t *filter = layer_filters->buffer[idx];
				array_remove(g_project->_->layers, filter);
				array_insert(g_project->_->layers, delta > 0 ? old_index + delta - 1 : old_index + delta + masks_count + idx, filter);
			}
		}

		// The layer is the last layer in the group, remove it
		// Notice that this might remove group masks
		if (old_parent != NULL && slot_layer_get_children(old_parent) == NULL) {
			slot_layer_delete(old_parent);
		}
	}
	else if (slot_layer_is_mask(raw)) {
		// Precondition new_upper_layer != NULL, ensured in can_move
		if (slot_layer_is_filter(new_upper_layer) || slot_layer_is_mask(new_upper_layer)) {
			raw->parent = new_upper_layer->parent;
		}
		else if (slot_layer_is_layer(new_upper_layer) || slot_layer_is_group(new_upper_layer)) {
			raw->parent = new_upper_layer;
		}
	}
	else if (slot_layer_is_group(raw)) {
		slot_layer_t_array_t *children = slot_layer_get_recursive_children(raw);
		if (children != NULL) {
			for (i32 idx = 0; idx < children->length; ++idx) {
				slot_layer_t *child = children->buffer[idx];
				array_remove(g_project->_->layers, child);
				// If the children are moved down each step increases the index below the layer by one
				array_insert(g_project->_->layers, delta > 0 ? old_index + delta - 1 : old_index + delta + idx, child);
			}
		}
	}

	for (i32 i = 0; i < g_project->_->materials->length; ++i) {
		slot_material_t *m = g_project->_->materials->buffer[i];
		tab_layers_remap_layer_pointers(m->canvas->nodes, tab_layers_fill_layer_map(pointers));
	}
}
