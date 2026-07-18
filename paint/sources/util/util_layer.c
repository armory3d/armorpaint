
#include "../global.h"

#if defined(IRON_ANDROID) || defined(IRON_IOS)
i32 layers_max_layers = 18;
#else
i32 layers_max_layers = 255;
#endif

uv_type_t _layers_uv_type;
mat4_t    _layers_decal_mat;
i32       _layers_position;
i32       _layers_base_color;
f32       _layers_occlusion;
f32       _layers_roughness;
f32       _layers_metallic;

void util_layer_update_preview() {
	if (g_context->layers_preview_dirty) {
		g_context->layers_preview_dirty = false;
		g_context->layer_preview_dirty  = false;
		g_context->mask_preview_last    = NULL;
		// Update all layer previews
		for (i32 i = 0; i < g_project->_->layers->length; ++i) {
			slot_layer_t *l = g_project->_->layers->buffer[i];
			if (slot_layer_is_group(l)) {
				continue;
			}

			gpu_texture_t *target = l->texpaint_preview;
			if (target == NULL) {
				continue;
			}

			gpu_texture_t *source = l->texpaint;
			draw_begin(target, true, 0x00000000);
			// draw_set_pipeline(l.is_mask() ? pipes_copy8 : pipes_copy);
			draw_set_pipeline(pipes_copy); // texpaint_preview is always RGBA32 for now
			draw_scaled_image(source, 0, 0, target->width, target->height);
			draw_set_pipeline(NULL);
			draw_end();
		}
		ui_base_hwnds->buffer[TAB_AREA_SIDEBAR0]->redraws = 2;
	}

	if (g_context->layer != NULL && g_context->layer_preview_dirty && !slot_layer_is_group(g_context->layer)) {
		g_context->layer_preview_dirty = false;
		g_context->mask_preview_last   = NULL;
		// Update layer preview
		slot_layer_t  *l      = g_context->layer;
		gpu_texture_t *target = l->texpaint_preview;
		if (target != NULL) {
			gpu_texture_t *source = l->texpaint;
			draw_begin(target, true, 0x00000000);
			// draw_set_pipeline(raw.layer.is_mask() ? pipes_copy8 : pipes_copy);
			draw_set_pipeline(pipes_copy); // texpaint_preview is always RGBA32 for now
			draw_scaled_image(source, 0, 0, target->width, target->height);
			draw_set_pipeline(NULL);
			draw_end();
			ui_base_hwnds->buffer[TAB_AREA_SIDEBAR0]->redraws = 2;
		}
	}
}

void layers_init() {
	slot_layer_clear(g_project->_->layers->buffer[0], color_from_floats(layers_default_base, layers_default_base, layers_default_base, 1.0), NULL, 1.0,
	                 layers_default_rough, 0.0);
}

void layers_resize() {
	if (config_get_texture_res_x() >= 16384 || config_get_texture_res_y() >= 16384) { // Save memory for >=16k
		g_config->undo_steps = 1;
		while (history_undo_layers->length > g_config->undo_steps) {
			slot_layer_t *l = array_pop(history_undo_layers);
			sys_notify_on_next_frame(&slot_layer_unload, l);
		}
	}
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		slot_layer_resize_and_set_bits(l);
	}
	for (i32 i = 0; i < history_undo_layers->length; ++i) {
		slot_layer_t *l = history_undo_layers->buffer[i];
		slot_layer_resize_and_set_bits(l);
	}

	any_map_t *rts = render_path_render_targets;

	render_target_t *blend0           = any_map_get(rts, "texpaint_blend0");
	gpu_texture_t   *_texpaint_blend0 = blend0->_image;
	gpu_delete_texture(_texpaint_blend0);
	blend0->width  = config_get_texture_res_x();
	blend0->height = config_get_texture_res_y();
	blend0->_image = gpu_create_render_target(config_get_texture_res_x(), config_get_texture_res_y(), GPU_TEXTURE_FORMAT_R8);

	render_target_t *blend1           = any_map_get(rts, "texpaint_blend1");
	gpu_texture_t   *_texpaint_blend1 = blend1->_image;
	gpu_delete_texture(_texpaint_blend1);
	blend1->width  = config_get_texture_res_x();
	blend1->height = config_get_texture_res_y();
	blend1->_image = gpu_create_render_target(config_get_texture_res_x(), config_get_texture_res_y(), GPU_TEXTURE_FORMAT_R8);

	g_context->brush_blend_dirty = true;

	render_target_t *blur = any_map_get(rts, "texpaint_blur");
	if (blur != NULL) {
		gpu_texture_t *_texpaint_blur = blur->_image;
		gpu_delete_texture(_texpaint_blur);
		f32 size_x   = math_floor(config_get_texture_res_x() * 0.95);
		f32 size_y   = math_floor(config_get_texture_res_y() * 0.95);
		blur->width  = size_x;
		blur->height = size_y;
		blur->_image = gpu_create_render_target(size_x, size_y, GPU_TEXTURE_FORMAT_RGBA32);
	}
	if (render_path_paint_live_layer != NULL) {
		slot_layer_resize_and_set_bits(render_path_paint_live_layer);
	}

	render_target_t *sculpt_ref = any_map_get(rts, "texpaint_sculpt_ref");
	if (sculpt_ref != NULL) {
		gpu_delete_texture(sculpt_ref->_image);
		sculpt_ref->width  = config_get_texture_res_x();
		sculpt_ref->height = config_get_texture_res_y();
		sculpt_ref->_image = gpu_create_render_target(config_get_texture_res_x(), config_get_texture_res_y(), GPU_TEXTURE_FORMAT_RGBA128);
	}

	render_target_t *sculpt_base = any_map_get(rts, "texpaint_sculpt_base");
	if (sculpt_base != NULL) {
		gpu_delete_texture(sculpt_base->_image);
		sculpt_base->width  = config_get_texture_res_x();
		sculpt_base->height = config_get_texture_res_y();
		sculpt_base->_image = gpu_create_render_target(config_get_texture_res_x(), config_get_texture_res_y(), GPU_TEXTURE_FORMAT_RGBA128);
	}

	render_path_raytrace_ready = false; // Rebuild baketex
	g_context->ddirty          = 2;
}

void layers_set_bits() {
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		slot_layer_resize_and_set_bits(l);
	}
	for (i32 i = 0; i < history_undo_layers->length; ++i) {
		slot_layer_t *l = history_undo_layers->buffer[i];
		slot_layer_resize_and_set_bits(l);
	}
}

void layers_make_temp_img() {
	slot_layer_t *l = g_project->_->layers->buffer[0];

	if (layers_temp_image != NULL && (layers_temp_image->width != l->texpaint->width || layers_temp_image->height != l->texpaint->height ||
	                                  layers_temp_image->format != l->texpaint->format)) {
		render_target_t *_temptex0 = any_map_get(render_path_render_targets, "temptex0");
		gpu_delete_texture(_temptex0->_image);
		map_delete(render_path_render_targets, "temptex0");
		gc_unroot(layers_temp_image);
		layers_temp_image = NULL;
	}

	if (layers_temp_image == NULL) {
		char            *format = base_bits_handle->i == TEXTURE_BITS_BITS8 ? "RGBA32" : base_bits_handle->i == TEXTURE_BITS_BITS16 ? "RGBA64" : "RGBA128";
		render_target_t *t      = render_target_create();
		t->name                 = "temptex0";
		t->width                = l->texpaint->width;
		t->height               = l->texpaint->height;
		t->format               = string_copy(format);
		render_target_t *rt     = render_path_create_render_target(t);
		gc_unroot(layers_temp_image);
		layers_temp_image = rt->_image;
		gc_root(layers_temp_image);
	}
}

void layers_make_temp_mask_img() {
	if (pipes_temp_mask_image != NULL &&
	    (pipes_temp_mask_image->width != config_get_texture_res_x() || pipes_temp_mask_image->height != config_get_texture_res_y())) {
		gpu_texture_t *_temp_mask_image = pipes_temp_mask_image;
		gpu_delete_texture(_temp_mask_image);
		gc_unroot(pipes_temp_mask_image);
		pipes_temp_mask_image = NULL;
	}

	if (pipes_temp_mask_image == NULL) {
		gc_unroot(pipes_temp_mask_image);
		// pipes_temp_mask_image = gpu_create_render_target(config_get_texture_res_x(), config_get_texture_res_y(), GPU_TEXTURE_FORMAT_R8);
		pipes_temp_mask_image = gpu_create_render_target(config_get_texture_res_x(), config_get_texture_res_y(), GPU_TEXTURE_FORMAT_RGBA32);
		gc_root(pipes_temp_mask_image);
	}
}

void layers_make_export_img() {
	slot_layer_t *l = g_project->_->layers->buffer[0];
	if (layers_expa != NULL &&
	    (layers_expa->width != l->texpaint->width || layers_expa->height != l->texpaint->height || layers_expa->format != l->texpaint->format)) {
		gpu_texture_t *_expa = layers_expa;
		gpu_texture_t *_expb = layers_expb;
		gpu_texture_t *_expc = layers_expc;
		gpu_delete_texture(_expa);
		gpu_delete_texture(_expb);
		gpu_delete_texture(_expc);
		gc_unroot(layers_expa);
		layers_expa = NULL;
		gc_unroot(layers_expb);
		layers_expb = NULL;
		gc_unroot(layers_expc);
		layers_expc = NULL;
		map_delete(render_path_render_targets, "expa");
		map_delete(render_path_render_targets, "expb");
		map_delete(render_path_render_targets, "expc");
	}
	if (layers_expa == NULL) {
		char *format = base_bits_handle->i == TEXTURE_BITS_BITS8 ? "RGBA32" : base_bits_handle->i == TEXTURE_BITS_BITS16 ? "RGBA64" : "RGBA128";
		{
			render_target_t *t  = render_target_create();
			t->name             = "expa";
			t->width            = l->texpaint->width;
			t->height           = l->texpaint->height;
			t->format           = string_copy(format);
			render_target_t *rt = render_path_create_render_target(t);
			gc_unroot(layers_expa);
			layers_expa = rt->_image;
			gc_root(layers_expa);
		}
		{
			render_target_t *t  = render_target_create();
			t->name             = "expb";
			t->width            = l->texpaint->width;
			t->height           = l->texpaint->height;
			t->format           = string_copy(format);
			render_target_t *rt = render_path_create_render_target(t);
			gc_unroot(layers_expb);
			layers_expb = rt->_image;
			gc_root(layers_expb);
		}
		{
			render_target_t *t  = render_target_create();
			t->name             = "expc";
			t->width            = l->texpaint->width;
			t->height           = l->texpaint->height;
			t->format           = string_copy(format);
			render_target_t *rt = render_path_create_render_target(t);
			gc_unroot(layers_expc);
			layers_expc = rt->_image;
			gc_root(layers_expc);
		}
	}
}

void layers_commands_merge_pack(gpu_pipeline_t *pipe, gpu_texture_t *i0, gpu_texture_t *i1, gpu_texture_t *i1pack, f32 i1mask_opacity, gpu_texture_t *i1texmask,
                                i32 i1blending) {
	_gpu_begin(i0, NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
	gpu_set_pipeline(pipe);
	gpu_set_texture(pipes_tex0, i1);
	gpu_set_texture(pipes_tex1, i1pack);
	gpu_set_texture(pipes_texmask, i1texmask);
	gpu_set_texture(pipes_texa, layers_temp_image);
	gpu_set_float(pipes_opac, i1mask_opacity);
	gpu_set_float(pipes_tex1w, i1pack->width);
	gpu_set_int(pipes_blending, i1blending);
	gpu_set_vertex_buffer(const_data_screen_aligned_vb);
	gpu_set_index_buffer(const_data_screen_aligned_ib);
	gpu_draw();
	gpu_end();
}

bool layers_is_fill_material() {
	if (g_context->tool == TOOL_TYPE_MATERIAL) {
		return true;
	}

	slot_material_t *m = g_context->material;
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		if (l->fill_material == m) {
			return true;
		}
	}
	return false;
}

void layers_update_fill_layers() {
	if (bake_texture_node_baking)
		return;

	slot_layer_t  *_layer     = g_context->layer;
	tool_type_t    _tool      = g_context->tool;
	i32            _fill_type = g_context->fill_type;
	gpu_texture_t *current    = NULL;

	if (g_context->tool == TOOL_TYPE_MATERIAL) {
		if (render_path_paint_live_layer == NULL) {
			gc_unroot(render_path_paint_live_layer);
			render_path_paint_live_layer = slot_layer_create("_live", LAYER_SLOT_TYPE_LAYER, NULL);
			gc_root(render_path_paint_live_layer);
		}

		current     = _draw_current;
		bool in_use = gpu_in_use;
		if (in_use)
			draw_end();

		g_context->tool      = TOOL_TYPE_FILL;
		g_context->fill_type = FILL_TYPE_OBJECT;
		render_path_paint_set_plane_mesh();
		make_material_parse_paint_material(false);
		g_context->pdirty = 1;
		render_path_paint_use_live_layer(true);
		render_path_paint_commands_paint(false);
		render_path_paint_dilate(true, true);
		render_path_paint_use_live_layer(false);
		g_context->tool      = _tool;
		g_context->fill_type = _fill_type;
		g_context->pdirty    = 0;
		g_context->rdirty    = 2;
		render_path_paint_restore_plane_mesh();
		make_material_parse_paint_material(true);
		ui_view2d_hwnd->redraws = 2;

		if (in_use)
			draw_begin(current, false, 0);
		return;
	}

	bool has_fill_layer = false;
	bool has_fill_mask  = false;
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		if (slot_layer_is_layer(l) && l->fill_material == g_context->material) {
			has_fill_layer = true;
		}
	}
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		if (slot_layer_is_mask(l) && l->fill_material == g_context->material) {
			has_fill_mask = true;
		}
	}

	if (has_fill_layer || has_fill_mask) {
		current     = _draw_current;
		bool in_use = gpu_in_use;
		if (in_use)
			draw_end();
		g_context->pdirty    = 1;
		g_context->tool      = TOOL_TYPE_FILL;
		g_context->fill_type = FILL_TYPE_OBJECT;

		if (has_fill_layer) {
			bool first = true;
			for (i32 i = 0; i < g_project->_->layers->length; ++i) {
				slot_layer_t *l = g_project->_->layers->buffer[i];
				if (slot_layer_is_layer(l) && l->fill_material == g_context->material) {
					g_context->layer = l;
					if (first) {
						first = false;
						make_material_parse_paint_material(false);
					}
					layers_set_object_mask();
					if (l->texpaint_sculpt != NULL) {
						i32 tid = l->id;
						i32 hid = history_undo_i - 1 < 0 ? g_config->undo_steps - 1 : history_undo_i - 1;
						sculpt_import_mesh_pack_to_texture(l->texpaint_sculpt);
						render_path_set_target(string("texpaint_sculpt_undo%d", hid), NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
						render_path_bind_target(string("texpaint_sculpt%d", tid), "tex");
						render_path_draw_shader("Scene/copy_pass/copyRGBA128_pass");
						render_path_sculpt_commands();
					}
					else {
						slot_layer_clear(l, 0x00000000, NULL, 1.0, layers_default_rough, 0.0);
						render_path_paint_commands_paint(false);
						render_path_paint_dilate(true, true);
					}
				}
			}
		}
		if (has_fill_mask) {
			bool first = true;
			for (i32 i = 0; i < g_project->_->layers->length; ++i) {
				slot_layer_t *l = g_project->_->layers->buffer[i];
				if (slot_layer_is_mask(l) && l->fill_material == g_context->material) {
					g_context->layer = l;
					if (first) {
						first = false;
						make_material_parse_paint_material(false);
					}
					layers_set_object_mask();
					slot_layer_clear(l, 0x00000000, NULL, 1.0, layers_default_rough, 0.0);
					render_path_paint_commands_paint(false);
					render_path_paint_dilate(true, true);
				}
			}
		}
		g_context->pdirty               = 0;
		g_context->ddirty               = 2;
		g_context->rdirty               = 2;
		g_context->rtdirty              = 1;
		g_context->layers_preview_dirty = true;
		if (in_use)
			draw_begin(current, false, 0);
		g_context->layer = _layer;
		layers_set_object_mask();
		g_context->tool      = _tool;
		g_context->fill_type = _fill_type;
		make_material_parse_paint_material(false);
	}
}

bool layers_is_path_material() {
	slot_material_t *m = g_context->material;
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		if (slot_layer_is_path(l) && l->path_material == m) {
			return true;
		}
	}
	return false;
}

void layers_update_path_layers() {
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		if (slot_layer_is_path(l) && l->path_material == g_context->material) {
			util_layer_repaint_path(l);
		}
	}
}

void layers_update_fill_layer(bool parse_paint) {
	gpu_texture_t *current = _draw_current;
	bool           in_use  = gpu_in_use;
	if (in_use)
		draw_end();

	tool_type_t _tool      = g_context->tool;
	i32         _fill_type = g_context->fill_type;
	g_context->tool        = TOOL_TYPE_FILL;
	g_context->fill_type   = FILL_TYPE_OBJECT;
	g_context->pdirty      = 1;

	if (g_context->layer->texpaint_sculpt != NULL) {
		i32 tid = g_context->layer->id;
		i32 hid = history_undo_i - 1 < 0 ? g_config->undo_steps - 1 : history_undo_i - 1;
		sculpt_import_mesh_pack_to_texture(g_context->layer->texpaint_sculpt);
		render_path_set_target(string("texpaint_sculpt_undo%d", hid), NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
		render_path_bind_target(string("texpaint_sculpt%d", tid), "tex");
		render_path_draw_shader("Scene/copy_pass/copyRGBA128_pass");
		if (parse_paint) {
			make_material_parse_paint_material(false);
		}
		render_path_sculpt_commands();
	}
	else {
		slot_layer_clear(g_context->layer, 0x00000000, NULL, 1.0, layers_default_rough, 0.0);
		if (parse_paint) {
			make_material_parse_paint_material(false);
		}
		render_path_paint_commands_paint(false);
		render_path_paint_dilate(true, true);
	}

	g_context->rdirty    = 2;
	g_context->tool      = _tool;
	g_context->fill_type = _fill_type;
	if (in_use)
		draw_begin(current, false, 0);
}

void layers_update_linked_layers() {
	slot_material_t *_material  = g_context->material;
	bool             any_linked = false;
	for (i32 i = 0; i < g_project->_->materials->length; ++i) {
		slot_material_t *m          = g_project->_->materials->buffer[i];
		bool             has_linked = false;
		for (i32 j = 0; j < m->canvas->nodes->length; ++j) {
			ui_node_t *node = m->canvas->nodes->buffer[j];
			if (string_equals(node->type, "LAYER") || string_equals(node->type, "LAYER_MASK")) {
				has_linked = true;
				break;
			}
		}
		if (!has_linked) {
			continue;
		}
		any_linked          = true;
		g_context->material = m;
		layers_update_fill_layers();
	}
	g_context->material = _material;
	if (any_linked) {
		make_material_parse_paint_material(false);
	}
}

void layers_set_object_mask() {
	string_array_t *ar = any_array_create_from_raw(
	    (void *[]){
	        tr("None"),
	    },
	    1);
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_object_t *p = g_project->_->paint_objects->buffer[i];
		any_array_push(ar, p->base->name);
	}

	i32 mask = context_object_mask_used() ? slot_layer_get_object_mask(g_context->layer) : 0;
	if (context_layer_filter_used()) {
		mask = g_context->layer_filter;
	}
	if (mask > 0) {
		if (g_context->merged_object != NULL) {
			g_context->merged_object->base->visible = false;
		}
		mesh_object_t *o = g_project->_->paint_objects->buffer[0];
		for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
			mesh_object_t *p         = g_project->_->paint_objects->buffer[i];
			char          *mask_name = ar->buffer[mask];
			if (string_equals(p->base->name, mask_name)) {
				o = p;
				break;
			}
		}
		context_select_paint_object(o);
	}
	else {
		i32 atlas_mask = g_context->layer_filter > g_project->_->paint_objects->length ? g_context->layer_filter : slot_layer_get_object_mask(g_context->layer);
		bool is_atlas  = atlas_mask > g_project->_->paint_objects->length;
		if (g_context->merged_object == NULL || is_atlas || g_context->merged_object_is_atlas) {
			mesh_object_t_array_t *visibles = is_atlas ? project_get_atlas_objects(atlas_mask) : NULL;
			util_mesh_merge(visibles);
		}
		context_select_paint_object(context_main_object());
		g_context->paint_object->skip_context   = "paint";
		g_context->merged_object->base->visible = true;
	}
	util_uv_dilatemap_cached = false;
}

void tab_layers_apply_filter(i32 filter) {
	g_context->layer_filter = filter;
	char *filter_name       = NULL;
	if (filter > 0 && filter <= g_project->_->paint_objects->length) {
		filter_name = g_project->_->paint_objects->buffer[filter - 1]->base->name;
	}
	else if (filter > g_project->_->paint_objects->length) {
		string_array_t *atlases = project_get_used_atlases();
		if (atlases != NULL) {
			filter_name = atlases->buffer[filter - g_project->_->paint_objects->length - 1];
		}
	}
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_object_t *p = g_project->_->paint_objects->buffer[i];
		p->base->visible = filter == 0 || (filter_name != NULL && string_equals(p->base->name, filter_name)) || project_is_atlas_object(p);
	}
	if (filter == 0 && g_context->merged_object_is_atlas) {
		util_mesh_merge(NULL);
	}
	else if (filter > g_project->_->paint_objects->length) {
		util_mesh_merge(util_mesh_get_visible());
	}
	layers_set_object_mask();
	util_uv_uvmap_cached       = false;
	g_context->ddirty          = 2;
	render_path_raytrace_ready = false;
}

void layers_new_layer_clear(slot_layer_t *l) {
	slot_layer_clear(l, 0x00000000, NULL, 1.0, layers_default_rough, 0.0);
}

slot_layer_t *layers_new_layer(bool clear, i32 position, slot_layer_t *parent) {
	if (g_project->_->layers->length > layers_max_layers) {
		return NULL;
	}

	slot_layer_t *l = slot_layer_create("", LAYER_SLOT_TYPE_LAYER, parent);
	l->object_mask  = g_context->layer_filter;

	if (position == -1 && slot_layer_is_filter(l)) {
		position = array_index_of(g_project->_->layers, parent);
	}

	if (position == -1) {
		if (slot_layer_is_mask(g_context->layer) || slot_layer_is_filter(g_context->layer))
			context_set_layer(g_context->layer->parent);
		array_insert(g_project->_->layers, array_index_of(g_project->_->layers, g_context->layer) + 1, l);
	}
	else {
		array_insert(g_project->_->layers, position, l);
	}

	context_set_layer(l);

	if (parent == NULL) {
		i32 li = array_index_of(g_project->_->layers, g_context->layer);
		if (li > 0) {
			slot_layer_t *below = g_project->_->layers->buffer[li - 1];
			if (slot_layer_is_layer(below)) {
				g_context->layer->parent = below->parent;
			}
		}
	}

	if (clear) {
		sys_notify_on_next_frame(&layers_new_layer_clear, l);
	}

	stage_t *stage = tab_stages_get_stage();
	if (stage != NULL && string_array_index_of(stage->layers, l->name) < 0) {
		string_array_push(stage->layers, l->name);
	}

	g_context->layer_preview_dirty = true;
	return l;
}

void layers_new_mask_clear(slot_layer_t *l) {
	slot_layer_clear(l, 0x00000000, NULL, 1.0, layers_default_rough, 0.0);
}

slot_layer_t *layers_new_mask(bool clear, slot_layer_t *parent, i32 position) {
	if (g_project->_->layers->length > layers_max_layers) {
		return NULL;
	}

	slot_layer_t *l = slot_layer_create("", LAYER_SLOT_TYPE_MASK, parent);
	if (position == -1) {
		position = array_index_of(g_project->_->layers, parent);
	}
	array_insert(g_project->_->layers, position, l);
	context_set_layer(l);
	if (clear) {
		sys_notify_on_next_frame(&layers_new_mask_clear, l);
	}
	g_context->layer_preview_dirty = true;
	return l;
}

slot_layer_t *layers_new_group() {
	if (g_project->_->layers->length > layers_max_layers) {
		return NULL;
	}

	slot_layer_t *l = slot_layer_create("", LAYER_SLOT_TYPE_GROUP, NULL);
	any_array_push(g_project->_->layers, l);
	context_set_layer(l);
	return l;
}

static slot_layer_t *_layers_path_sculpt_layer = NULL;

void layers_new_path_layer_sculpt_init(void *_) {
	sculpt_init();
	sculpt_init_sculpt_texture(_layers_path_sculpt_layer);
}

slot_layer_t *layers_new_path_layer(bool curved) {
	slot_layer_t *l = layers_new_layer(true, -1, NULL);
	if (l == NULL) {
		return NULL;
	}
	l->path_points        = f32_array_create(0);
	l->path_points_world  = f32_array_create(0);
	l->path_points_camera = f32_array_create(0);
	l->path_points_parent = i32_array_create(0);
	l->path_tool          = -1;
	l->path_curved        = curved;
	l->path_material      = g_context->material;
	l->name               = string(curved ? "Curve %d" : "Path %d", l->id + 1);

	if (g_config->workflow == WORKFLOW_SCULPT) {
		_layers_path_sculpt_layer = l;
		sys_notify_on_next_frame(&layers_new_path_layer_sculpt_init, NULL);
	}

	return l;
}

void layers_create_fill_layer_on_next_frame(void *_) {
	slot_layer_t *l = layers_new_layer(false, _layers_position, NULL);
	history_new_layer();
	l->uv_type = _layers_uv_type;
	if (!mat4_isnan(_layers_decal_mat)) {
		l->decal_mat = _layers_decal_mat;
	}
	l->object_mask = g_context->layer_filter;

	if (g_config->workflow == WORKFLOW_SCULPT) {
		sculpt_init();
		sculpt_init_sculpt_texture(l);
	}

	history_to_fill_layer();
	slot_layer_to_fill_layer(l);
}

void layers_create_fill_layer(uv_type_t uv_type, mat4_t decal_mat, i32 position) {
	// if (g_context->tool == TOOL_TYPE_CURSOR) {
	// 	return;
	// }

	_layers_uv_type   = uv_type;
	_layers_decal_mat = decal_mat;
	_layers_position  = position;
	sys_notify_on_next_frame(&layers_create_fill_layer_on_next_frame, NULL);
}

void tab_materials_button_new_on_next_frame(void *_);

void layers_create_filter_on_next_frame(void *_) {
	if (slot_layer_get_filters(g_context->layer, true) != NULL) {
		return;
	}

	tab_materials_button_new_on_next_frame(NULL);

	slot_layer_t *l = layers_new_layer(false, -1, g_context->layer);
	history_new_layer();
	history_to_fill_layer();
	slot_layer_to_fill_layer(l);

	// Filter material
	g_context->material->canvas->name = string_copy(g_context->layer->name);

	ui_nodes_t       *nodes  = g_context->material->nodes;
	ui_node_canvas_t *canvas = g_context->material->canvas;
	ui_node_t        *nout   = NULL;
	for (i32 i = 0; i < canvas->nodes->length; ++i) {
		ui_node_t *n = canvas->nodes->buffer[i];
		if (string_equals(n->type, "OUTPUT_MATERIAL_PBR")) {
			nout = n;
			break;
		}
	}
	for (i32 i = 0; i < canvas->nodes->length; ++i) {
		ui_node_t *n = canvas->nodes->buffer[i];
		if (string_equals(n->name, "Color")) {
			ui_remove_node(n, canvas);
			break;
		}
	}

	ui_node_t *n                         = nodes_material_create_node("LAYER", NULL);
	n->buttons->buffer[0]->default_value = f32_array_create_x(array_index_of(g_project->_->layers, l->parent));
	n->x                                 = -50;
	n->y                                 = 100;

	for (int i = 0; i < 9; ++i) {
		ui_node_link_t *l =
		    GC_ALLOC_INIT(ui_node_link_t, {.id = ui_next_link_id(canvas->links), .from_id = n->id, .from_socket = i, .to_id = nout->id, .to_socket = i});
		any_array_push(canvas->links, l);
	}

	layers_update_fill_layer(true);
	util_render_make_material_preview();
}

void layers_create_filter() {
	sys_notify_on_next_frame(&layers_create_filter_on_next_frame, NULL);
}

void layers_create_image_mask(asset_t *asset) {
	slot_layer_t *l = g_context->layer;
	if (slot_layer_is_mask(l) || slot_layer_is_group(l)) {
		return;
	}

	history_new_layer();
	slot_layer_t *m = layers_new_mask(false, l, -1);
	slot_layer_clear(m, 0x00000000, project_get_image(asset), 1.0, layers_default_rough, 0.0);
	g_context->layer_preview_dirty = true;
}

void layers_create_image_layer(asset_t *asset) {
	history_new_layer();
	slot_layer_t *m = layers_new_layer(false, -1, NULL);
	slot_layer_clear(m, 0x00000000, project_get_image(asset), 1.0, layers_default_rough, 0.0);
	g_context->layer_preview_dirty = true;
}

void layers_create_color_layer_on_next_frame(void *_) {
	slot_layer_t *l = layers_new_layer(false, _layers_position, NULL);
	history_new_layer();
	l->uv_type     = UV_TYPE_UVMAP;
	l->object_mask = g_context->layer_filter;
	slot_layer_clear(l, _layers_base_color, NULL, _layers_occlusion, _layers_roughness, _layers_metallic);
}

void layers_create_color_layer(i32 base_color, f32 occlusion, f32 roughness, f32 metallic, i32 position) {
	_layers_base_color = base_color;
	_layers_occlusion  = occlusion;
	_layers_roughness  = roughness;
	_layers_metallic   = metallic;
	_layers_position   = position;

	sys_notify_on_next_frame(&layers_create_color_layer_on_next_frame, NULL);
}

void layers_duplicate_layer(slot_layer_t *l) {
	if (!slot_layer_is_group(l)) {
		slot_layer_t *new_layer = slot_layer_duplicate(l);
		context_set_layer(new_layer);
		slot_layer_t_array_t *masks = slot_layer_get_masks(l, false);
		if (masks != NULL) {
			for (i32 i = 0; i < masks->length; ++i) {
				slot_layer_t *m = masks->buffer[i];
				m               = slot_layer_duplicate(m);
				m->parent       = new_layer;
				array_remove(g_project->_->layers, m);
				array_insert(g_project->_->layers, array_index_of(g_project->_->layers, new_layer), m);
			}
		}
		context_set_layer(new_layer);
	}
	else {
		slot_layer_t *new_group = layers_new_group();
		array_remove(g_project->_->layers, new_group);
		array_insert(g_project->_->layers, array_index_of(g_project->_->layers, l) + 1, new_group);
		// group.show_panel = true;
		for (i32 i = 0; i < slot_layer_get_children(l)->length; ++i) {
			slot_layer_t         *c         = slot_layer_get_children(l)->buffer[i];
			slot_layer_t_array_t *masks     = slot_layer_get_masks(c, false);
			slot_layer_t         *new_layer = slot_layer_duplicate(c);
			new_layer->parent               = new_group;
			array_remove(g_project->_->layers, new_layer);
			array_insert(g_project->_->layers, array_index_of(g_project->_->layers, new_group), new_layer);
			if (masks != NULL) {
				for (i32 i = 0; i < masks->length; ++i) {
					slot_layer_t *m        = masks->buffer[i];
					slot_layer_t *new_mask = slot_layer_duplicate(m);
					new_mask->parent       = new_layer;
					array_remove(g_project->_->layers, new_mask);
					array_insert(g_project->_->layers, array_index_of(g_project->_->layers, new_layer), new_mask);
				}
			}
		}
		slot_layer_t_array_t *group_masks = slot_layer_get_masks(l, true);
		if (group_masks != NULL) {
			for (i32 i = 0; i < group_masks->length; ++i) {
				slot_layer_t *m        = group_masks->buffer[i];
				slot_layer_t *new_mask = slot_layer_duplicate(m);
				new_mask->parent       = new_group;
				array_remove(g_project->_->layers, new_mask);
				array_insert(g_project->_->layers, array_index_of(g_project->_->layers, new_group), new_mask);
			}
		}
		context_set_layer(new_group);
	}
}

void layers_apply_masks(slot_layer_t *l) {
	slot_layer_t_array_t *masks = slot_layer_get_masks(l, true);

	if (masks != NULL) {
		for (i32 i = 0; i < masks->length - 1; ++i) {
			layers_merge_layer(masks->buffer[i + 1], masks->buffer[i], false);
			slot_layer_delete(masks->buffer[i]);
		}
		slot_layer_apply_mask(masks->buffer[masks->length - 1]);
		g_context->layer_preview_dirty = true;
	}
}

void layers_merge_down() {
	slot_layer_t *l1 = g_context->layer;

	if (slot_layer_is_group(l1)) {
		l1 = layers_merge_group(l1);
	}
	else if (slot_layer_has_masks(l1, true)) { // It is a layer
		layers_apply_masks(l1);
		context_set_layer(l1);
	}

	slot_layer_t *l0 = g_project->_->layers->buffer[array_index_of(g_project->_->layers, l1) - 1];

	if (slot_layer_is_group(l0)) {
		l0 = layers_merge_group(l0);
	}
	else if (slot_layer_has_masks(l0, true)) { // It is a layer
		layers_apply_masks(l0);
		context_set_layer(l0);
	}

	layers_merge_layer(l0, l1, false);
	slot_layer_delete(l1);
	context_set_layer(l0);
	g_context->layer_preview_dirty = true;
}

slot_layer_t *layers_merge_group(slot_layer_t *l) {
	if (!slot_layer_is_group(l)) {
		return NULL;
	}

	slot_layer_t_array_t *children = slot_layer_get_children(l);

	if (children->length == 1 && slot_layer_has_masks(children->buffer[0], false)) {
		layers_apply_masks(children->buffer[0]);
	}

	for (i32 i = 0; i < children->length - 1; ++i) {
		context_set_layer(children->buffer[children->length - 1 - i]);
		history_merge_layers();
		layers_merge_down();
	}

	// Now apply the group masks
	slot_layer_t_array_t *masks = slot_layer_get_masks(l, true);
	if (masks != NULL) {
		for (i32 i = 0; i < masks->length - 1; ++i) {
			layers_merge_layer(masks->buffer[i + 1], masks->buffer[i], false);
			slot_layer_delete(masks->buffer[i]);
		}
		layers_apply_mask(children->buffer[0], masks->buffer[masks->length - 1]);
	}

	children->buffer[0]->parent = NULL;
	children->buffer[0]->name   = l->name;
	if (children->buffer[0]->fill_material != NULL) {
		slot_layer_to_paint_layer(children->buffer[0]);
	}
	slot_layer_delete(l);
	return children->buffer[0];
}

void layers_merge_layer(slot_layer_t *l0, slot_layer_t *l1, bool use_mask) {
	if (!l1->visible || slot_layer_is_group(l1)) {
		return;
	}

	layers_make_temp_img();

	draw_begin(layers_temp_image, false, 0); // Copy to temp
	draw_set_pipeline(pipes_copy);
	draw_image(l0->texpaint, 0, 0);
	draw_set_pipeline(NULL);
	draw_end();

	render_target_t      *empty_rt = any_map_get(render_path_render_targets, "empty_white");
	gpu_texture_t        *empty    = empty_rt->_image;
	gpu_texture_t        *mask     = empty;
	slot_layer_t_array_t *l1masks  = use_mask ? slot_layer_get_masks(l1, true) : NULL;
	if (l1masks != NULL) {
		// for (let i: i32 = 1; i < l1masks.length - 1; ++i) {
		// 	merge_layer(l1masks[i + 1], l1masks[i]);
		// }
		layers_make_temp_mask_img();
		_gpu_begin(pipes_temp_mask_image, NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
		gpu_set_pipeline(pipes_merge_mask);
		gpu_set_texture(pipes_tex0_merge_mask, l1masks->buffer[0]->texpaint);
		gpu_set_texture(pipes_texa_merge_mask, empty);
		gpu_set_float(pipes_opac_merge_mask, l1masks->buffer[0]->mask_opacity);
		gpu_set_int(pipes_blending_merge_mask, BLEND_TYPE_MIX);
		gpu_set_vertex_buffer(const_data_screen_aligned_vb);
		gpu_set_index_buffer(const_data_screen_aligned_ib);
		gpu_draw();
		gpu_end();
		mask = pipes_temp_mask_image;
	}

	if (slot_layer_is_mask(l1)) {
		_gpu_begin(l0->texpaint, NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
		gpu_set_pipeline(pipes_merge_mask);
		gpu_set_texture(pipes_tex0_merge_mask, l1->texpaint);
		gpu_set_texture(pipes_texa_merge_mask, layers_temp_image);
		gpu_set_float(pipes_opac_merge_mask, slot_layer_get_opacity(l1));
		gpu_set_int(pipes_blending_merge_mask, BLEND_TYPE_MIX);
		gpu_set_vertex_buffer(const_data_screen_aligned_vb);
		gpu_set_index_buffer(const_data_screen_aligned_ib);
		gpu_draw();
		gpu_end();
	}

	if (slot_layer_is_layer(l1)) {
		if (l1->paint_base) {
			_gpu_begin(l0->texpaint, NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
			gpu_set_pipeline(pipes_merge);
			gpu_set_texture(pipes_tex0, l1->texpaint);
			gpu_set_texture(pipes_tex1, empty);
			gpu_set_texture(pipes_texmask, mask);
			gpu_set_texture(pipes_texa, layers_temp_image);
			gpu_set_float(pipes_opac, slot_layer_get_opacity(l1));
			gpu_set_float(pipes_tex1w, empty->width);
			gpu_set_int(pipes_blending, slot_layer_get_blending(l1));
			gpu_set_vertex_buffer(const_data_screen_aligned_vb);
			gpu_set_index_buffer(const_data_screen_aligned_ib);
			gpu_draw();
			gpu_end();
		}

		if (l0->texpaint_nor != NULL) {
			draw_begin(layers_temp_image, false, 0);
			draw_set_pipeline(pipes_copy);
			draw_image(l0->texpaint_nor, 0, 0);
			draw_set_pipeline(NULL);
			draw_end();

			if (l1->paint_nor) {
				_gpu_begin(l0->texpaint_nor, NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
				gpu_set_pipeline(pipes_merge);
				gpu_set_texture(pipes_tex0, l1->texpaint);
				gpu_set_texture(pipes_tex1, l1->texpaint_nor);
				gpu_set_texture(pipes_texmask, mask);
				gpu_set_texture(pipes_texa, layers_temp_image);
				gpu_set_float(pipes_opac, slot_layer_get_opacity(l1));
				gpu_set_float(pipes_tex1w, l1->texpaint_nor->width);
				gpu_set_int(pipes_blending, l1->paint_nor_blend ? 102 : 101);
				gpu_set_vertex_buffer(const_data_screen_aligned_vb);
				gpu_set_index_buffer(const_data_screen_aligned_ib);
				gpu_draw();
				gpu_end();
			}
		}

		if (l0->texpaint_pack != NULL) {
			draw_begin(layers_temp_image, false, 0);
			draw_set_pipeline(pipes_copy);
			draw_image(l0->texpaint_pack, 0, 0);
			draw_set_pipeline(NULL);
			draw_end();

			if (l1->paint_occ || l1->paint_rough || l1->paint_met || l1->paint_height) {
				if (l1->paint_occ && l1->paint_rough && l1->paint_met && l1->paint_height) {
					layers_commands_merge_pack(pipes_merge, l0->texpaint_pack, l1->texpaint, l1->texpaint_pack, slot_layer_get_opacity(l1), mask,
					                           l1->paint_height_blend ? 103 : 101);
				}
				else {
					if (l1->paint_occ) {
						layers_commands_merge_pack(pipes_merge_r, l0->texpaint_pack, l1->texpaint, l1->texpaint_pack, slot_layer_get_opacity(l1), mask, 101);
					}
					if (l1->paint_rough) {
						layers_commands_merge_pack(pipes_merge_g, l0->texpaint_pack, l1->texpaint, l1->texpaint_pack, slot_layer_get_opacity(l1), mask, 101);
					}
					if (l1->paint_met) {
						layers_commands_merge_pack(pipes_merge_b, l0->texpaint_pack, l1->texpaint, l1->texpaint_pack, slot_layer_get_opacity(l1), mask, 101);
					}
				}
			}
		}
	}
}

slot_layer_t *layers_flatten(bool height_to_normal, slot_layer_t_array_t *layers) {
	if (layers == NULL) {
		layers = g_project->_->layers;
	}
	layers_make_temp_img();
	layers_make_export_img();
	render_target_t *empty_rt = any_map_get(render_path_render_targets, "empty_white");
	gpu_texture_t   *empty    = empty_rt->_image;

	// Clear export layer
	_gpu_begin(layers_expa, NULL, NULL, GPU_CLEAR_COLOR, color_from_floats(0.0, 0.0, 0.0, 0.0), 0.0);
	gpu_end();
	_gpu_begin(layers_expb, NULL, NULL, GPU_CLEAR_COLOR, color_from_floats(0.5, 0.5, 1.0, 0.0), 0.0);
	gpu_end();
	_gpu_begin(layers_expc, NULL, NULL, GPU_CLEAR_COLOR, color_from_floats(1.0, 0.0, 0.0, 0.0), 0.0);
	gpu_end();

	// Flatten layers
	for (i32 i = 0; i < layers->length; ++i) {
		slot_layer_t *l1 = layers->buffer[i];
		if (!slot_layer_is_visible(l1)) {
			continue;
		}
		if (!slot_layer_is_layer(l1)) {
			continue;
		}
		if (slot_layer_get_filters(l1, false) != NULL) {
			continue;
		}

		gpu_texture_t        *mask    = empty;
		slot_layer_t_array_t *l1masks = slot_layer_get_masks(l1, true);
		if (l1masks != NULL) {
			layers_make_temp_mask_img();
			draw_begin(pipes_temp_mask_image, GPU_CLEAR_COLOR, 0xffffffff);
			draw_end();
			slot_layer_t *l1 = GC_ALLOC_INIT(slot_layer_t, {.texpaint = pipes_temp_mask_image});
			for (i32 i = 0; i < l1masks->length; ++i) {
				layers_merge_layer(l1, l1masks->buffer[i], false);
			}
			mask = pipes_temp_mask_image;
		}

		if (l1->paint_base) {
			draw_begin(layers_temp_image, false, 0); // Copy to temp
			draw_set_pipeline(pipes_copy);
			draw_image(layers_expa, 0, 0);
			draw_set_pipeline(NULL);
			draw_end();

			// if (g_context->tool == TOOL_TYPE_CURSOR) {
			// 	// Do not multiply basecol by alpha
			// 	draw_begin(layers_expa, false, 0); // Copy to temp
			// 	draw_set_pipeline(pipes_copy);
			// 	draw_image(l1->texpaint, 0, 0);
			// 	draw_set_pipeline(NULL);
			// 	draw_end();
			// }
			// else {
			_gpu_begin(layers_expa, NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
			gpu_set_pipeline(pipes_merge);
			gpu_set_texture(pipes_tex0, l1->texpaint);
			gpu_set_texture(pipes_tex1, empty);
			gpu_set_texture(pipes_texmask, mask);
			gpu_set_texture(pipes_texa, layers_temp_image);
			gpu_set_float(pipes_opac, slot_layer_get_opacity(l1));
			gpu_set_float(pipes_tex1w, empty->width);
			gpu_set_int(pipes_blending, layers->length > 1 ? slot_layer_get_blending(l1) : 0);
			gpu_set_vertex_buffer(const_data_screen_aligned_vb);
			gpu_set_index_buffer(const_data_screen_aligned_ib);
			gpu_draw();
			gpu_end();
			// }
		}

		if (l1->paint_nor) {
			draw_begin(layers_temp_image, false, 0);
			draw_set_pipeline(pipes_copy);
			draw_image(layers_expb, 0, 0);
			draw_set_pipeline(NULL);
			draw_end();

			_gpu_begin(layers_expb, NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
			gpu_set_pipeline(pipes_merge);
			gpu_set_texture(pipes_tex0, l1->texpaint);
			gpu_set_texture(pipes_tex1, l1->texpaint_nor);
			gpu_set_texture(pipes_texmask, mask);
			gpu_set_texture(pipes_texa, layers_temp_image);
			gpu_set_float(pipes_opac, slot_layer_get_opacity(l1));
			gpu_set_float(pipes_tex1w, l1->texpaint_nor->width);
			gpu_set_int(pipes_blending, l1->paint_nor_blend ? 102 : 101);
			gpu_set_vertex_buffer(const_data_screen_aligned_vb);
			gpu_set_index_buffer(const_data_screen_aligned_ib);
			gpu_draw();
			gpu_end();
		}

		if (l1->paint_occ || l1->paint_rough || l1->paint_met || l1->paint_height) {
			draw_begin(layers_temp_image, false, 0);
			draw_set_pipeline(pipes_copy);
			draw_image(layers_expc, 0, 0);
			draw_set_pipeline(NULL);
			draw_end();

			if (l1->paint_occ && l1->paint_rough && l1->paint_met && l1->paint_height) {
				layers_commands_merge_pack(pipes_merge, layers_expc, l1->texpaint, l1->texpaint_pack, slot_layer_get_opacity(l1), mask,
				                           l1->paint_height_blend ? 103 : 101);
			}
			else {
				if (l1->paint_occ) {
					layers_commands_merge_pack(pipes_merge_r, layers_expc, l1->texpaint, l1->texpaint_pack, slot_layer_get_opacity(l1), mask, 101);
				}
				if (l1->paint_rough) {
					layers_commands_merge_pack(pipes_merge_g, layers_expc, l1->texpaint, l1->texpaint_pack, slot_layer_get_opacity(l1), mask, 101);
				}
				if (l1->paint_met) {
					layers_commands_merge_pack(pipes_merge_b, layers_expc, l1->texpaint, l1->texpaint_pack, slot_layer_get_opacity(l1), mask, 101);
				}
			}
		}
	}

	slot_layer_t *l0 = GC_ALLOC_INIT(slot_layer_t, {.texpaint = layers_expa, .texpaint_nor = layers_expb, .texpaint_pack = layers_expc});

	// Merge height map into normal map
	if (height_to_normal && make_material_height_used) {
		draw_begin(layers_temp_image, false, 0);
		draw_set_pipeline(pipes_copy);
		draw_image(l0->texpaint_nor, 0, 0);
		draw_set_pipeline(NULL);
		draw_end();

		_gpu_begin(l0->texpaint_nor, NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
		gpu_set_pipeline(pipes_merge);
		gpu_set_texture(pipes_tex0, layers_temp_image);
		gpu_set_texture(pipes_tex1, l0->texpaint_pack);
		gpu_set_texture(pipes_texmask, empty);
		gpu_set_texture(pipes_texa, empty);
		gpu_set_float(pipes_opac, 1.0);
		gpu_set_float(pipes_tex1w, l0->texpaint_pack->width);
		gpu_set_int(pipes_blending, 104);
		gpu_set_vertex_buffer(const_data_screen_aligned_vb);
		gpu_set_index_buffer(const_data_screen_aligned_ib);
		gpu_draw();
		gpu_end();
	}

	return l0;
}

void layers_on_resized_on_next_frame(void *_) {
	layers_resize();
	slot_layer_t    *_layer    = g_context->layer;
	slot_material_t *_material = g_context->material;
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		if (l->fill_material != NULL) {
			g_context->layer    = l;
			g_context->material = l->fill_material;
			layers_set_object_mask();
			layers_update_fill_layer(true);
		}
	}
	g_context->layer    = _layer;
	g_context->material = _material;
	layers_set_object_mask();
	make_material_parse_paint_material(true);
}

void layers_on_resized() {
	sys_notify_on_next_frame(&layers_on_resized_on_next_frame, NULL);
	gc_unroot(util_uv_uvmap);
	util_uv_uvmap        = NULL;
	util_uv_uvmap_cached = false;
	gc_unroot(util_uv_trianglemap);
	util_uv_trianglemap        = NULL;
	util_uv_trianglemap_cached = false;
	util_uv_dilatemap_cached   = false;
	render_path_raytrace_ready = false;
}

void tab_layers_remap_layer_pointers(ui_node_t_array_t *nodes, i32_imap_t *pointer_map) {
	for (i32 i = 0; i < nodes->length; ++i) {
		ui_node_t *n = nodes->buffer[i];
		if (string_equals(n->type, "LAYER") || string_equals(n->type, "LAYER_MASK")) {
			i32 i = n->buttons->buffer[0]->default_value->buffer[0];
			if (i32_imap_get(pointer_map, i) != -1) {
				n->buttons->buffer[0]->default_value->buffer[0] = i32_imap_get(pointer_map, i);
			}
		}
	}
}

i32_map_t *tab_layers_init_layer_map() {
	i32_map_t *res = any_map_create();
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		i32_map_set(res, g_project->_->layers->buffer[i], i);
	}
	return res;
}

i32_imap_t *tab_layers_fill_layer_map(i32_map_t *map) {
	i32_imap_t     *res  = any_map_create();
	string_array_t *keys = map_keys(map);
	for (i32 i = 0; i < keys->length; ++i) {
		char *l = keys->buffer[i];
		i32_imap_set(res, i32_map_get(map, l), array_index_of(g_project->_->layers, l) > -1 ? array_index_of(g_project->_->layers, l) : 9999);
	}
	return res;
}

void tab_layers_make_mask_preview_rgba32_on_next_frame(void *_) {
	slot_layer_t *l = tab_layers_l;
	draw_begin(g_context->mask_preview_rgba32, true, 0xff000000);
	draw_set_pipeline(ui_view2d_pipe);
	gpu_set_int(ui_view2d_channel_loc, 1);
	draw_image(l->texpaint_preview, 0, 0);
	draw_end();
	draw_set_pipeline(NULL);
}

void tab_layers_make_mask_preview_rgba32(slot_layer_t *l) {
	if (g_context->mask_preview_rgba32 == NULL) {
		g_context->mask_preview_rgba32 = gpu_create_render_target(util_render_layer_preview_size, util_render_layer_preview_size, GPU_TEXTURE_FORMAT_RGBA32);
	}
	// Convert from R8 to RGBA32 for tooltip display
	if (g_context->mask_preview_last != l) {
		g_context->mask_preview_last = l;
		gc_unroot(tab_layers_l);
		tab_layers_l = l;
		gc_root(tab_layers_l);
		sys_notify_on_next_frame(&tab_layers_make_mask_preview_rgba32_on_next_frame, NULL);
	}
}
