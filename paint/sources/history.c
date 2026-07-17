
#include "global.h"

char *history_action_to_string(history_action_t action) {
	switch (action) {
	case HISTORY_ACTION_PAINT:
		return tr("Paint");
	case HISTORY_ACTION_EDIT_NODES:
		return tr("Edit Nodes");
	case HISTORY_ACTION_NEW_LAYER:
		return tr("New Layer");
	case HISTORY_ACTION_NEW_BLACK_MASK:
		return tr("New Black Mask");
	case HISTORY_ACTION_NEW_WHITE_MASK:
		return tr("New White Mask");
	case HISTORY_ACTION_NEW_FILL_MASK:
		return tr("New Fill Mask");
	case HISTORY_ACTION_NEW_GROUP:
		return tr("New Group");
	case HISTORY_ACTION_DELETE_LAYER:
		return tr("Delete Layer");
	case HISTORY_ACTION_CLEAR_LAYER:
		return tr("Clear Layer");
	case HISTORY_ACTION_DUPLICATE_LAYER:
		return tr("Duplicate Layer");
	case HISTORY_ACTION_ORDER_LAYERS:
		return tr("Order Layers");
	case HISTORY_ACTION_MERGE_LAYERS:
		return tr("Merge Layers");
	case HISTORY_ACTION_APPLY_MASK:
		return tr("Apply Mask");
	case HISTORY_ACTION_INVERT_MASK:
		return tr("Invert Mask");
	case HISTORY_ACTION_APPLY_FILTER:
		return tr("Apply Filter");
	case HISTORY_ACTION_TO_FILL_LAYER:
		return tr("To Fill Layer");
	case HISTORY_ACTION_TO_FILL_MASK:
		return tr("To Fill Mask");
	case HISTORY_ACTION_TO_PAINT_LAYER:
		return tr("To Paint Layer");
	case HISTORY_ACTION_TO_PAINT_MASK:
		return tr("To Paint Mask");
	case HISTORY_ACTION_LAYER_OPACITY:
		return tr("Layer Opacity");
	case HISTORY_ACTION_LAYER_BLENDING:
		return tr("Layer Blending");
	case HISTORY_ACTION_DELETE_NODE_GROUP:
		return tr("Delete Node Group");
	case HISTORY_ACTION_NEW_MATERIAL:
		return tr("New Material");
	case HISTORY_ACTION_DELETE_MATERIAL:
		return tr("Delete Material");
	case HISTORY_ACTION_DUPLICATE_MATERIAL:
		return tr("Duplicate Material");
	}
	return tr("Paint");
}

void history_undo_invert_mask(history_step_t *step) {
	g_context->layer = g_project->_->layers->buffer[step->layer];
	slot_layer_invert_mask(g_context->layer);
}

void history_undo_delete_layer_group(void *_) {
	i32 active = history_steps->length - 1 - history_redos;
	// 1. Undo deleting group masks
	i32 n = 1;
	while (active - n >= 0 && history_steps->buffer[active - n]->layer_type == LAYER_SLOT_TYPE_MASK) {
		history_undo();
		++n;
	}
	// 2. Undo a mask to have a non empty group
	history_undo();
}

void history_undo_delete_layer_masks(void *_) {
	while (true) {
		i32 active = history_steps->length - 1 - history_redos;
		if (active < 0) {
			return;
		}
		history_step_t *step = history_steps->buffer[active];
		if (step->action != HISTORY_ACTION_DELETE_LAYER || step->layer_type != LAYER_SLOT_TYPE_MASK) {
			return;
		}
		history_undo();
	}
}

ui_node_canvas_t *history_get_canvas(history_step_t *step) {
	if (step->canvas_group == -1) {
		return g_project->_->materials->buffer[step->material]->canvas;
	}
	else {
		return g_project->_->material_groups->buffer[step->canvas_group]->canvas;
	}
}

void history_set_canvas(history_step_t *step, ui_node_canvas_t *canvas) {
	if (step->canvas_group == -1) {
		g_project->_->materials->buffer[step->material]->canvas = canvas;
	}
	else {
		g_project->_->material_groups->buffer[step->canvas_group]->canvas = canvas;
	}
}

void history_swap_canvas(history_step_t *step) {
	if (step->canvas_type == 0) {
		ui_node_canvas_t *_canvas = history_get_canvas(step);
		history_set_canvas(step, step->canvas);
		step->canvas        = _canvas;
		g_context->material = g_project->_->materials->buffer[step->material];
	}
	else {
		ui_node_canvas_t *_canvas                          = g_project->_->brushes->buffer[step->brush]->canvas;
		g_project->_->brushes->buffer[step->brush]->canvas = step->canvas;
		step->canvas                                       = _canvas;
		g_context->brush                                   = g_project->_->brushes->buffer[step->brush];
	}

	ui_nodes_t *nodes                = ui_nodes_get_nodes();
	nodes->nodes_selected_id->length = 0;

	ui_nodes_canvas_changed();
	ui_nodes_hwnd->redraws = 2;
}

void history_undo() {
	if (history_undos > 0) {
		i32             active = history_steps->length - 1 - history_redos;
		history_step_t *step   = history_steps->buffer[active];

		if (step->action == HISTORY_ACTION_EDIT_NODES) {
			history_swap_canvas(step);
		}
		else if (step->action == HISTORY_ACTION_NEW_LAYER || step->action == HISTORY_ACTION_NEW_BLACK_MASK || step->action == HISTORY_ACTION_NEW_WHITE_MASK ||
		         step->action == HISTORY_ACTION_NEW_FILL_MASK) {
			g_context->layer = g_project->_->layers->buffer[step->layer];
			slot_layer_delete(g_context->layer);
			g_context->layer = g_project->_->layers->buffer[step->layer > 0 ? step->layer - 1 : 0];
		}
		else if (step->action == HISTORY_ACTION_NEW_GROUP) {
			g_context->layer = g_project->_->layers->buffer[step->layer];
			// The layer below is the only layer in the group. Its layer masks are automatically unparented, too.
			g_project->_->layers->buffer[step->layer - 1]->parent = NULL;
			slot_layer_delete(g_context->layer);
			g_context->layer = g_project->_->layers->buffer[step->layer > 0 ? step->layer - 1 : 0];
		}
		else if (step->action == HISTORY_ACTION_DELETE_LAYER) {
			slot_layer_t *parent = step->layer_parent > 0 ? g_project->_->layers->buffer[step->layer_parent - 1] : NULL;
			slot_layer_t *l      = slot_layer_create("", step->layer_type, parent);
			array_insert(g_project->_->layers, step->layer, l);
			context_set_layer(l);
			history_undo_i    = history_undo_i - 1 < 0 ? g_config->undo_steps - 1 : history_undo_i - 1;
			slot_layer_t *lay = history_undo_layers->buffer[history_undo_i];
			slot_layer_swap(l, lay);
			l->mask_opacity = step->layer_opacity;
			l->blending     = step->layer_blending;
			l->object_mask  = step->layer_object;
			make_material_parse_mesh_material();

			// Undo at least second time in order to avoid empty groups
			if (step->layer_type == LAYER_SLOT_TYPE_GROUP) {
				sys_notify_on_next_frame(&history_undo_delete_layer_group, NULL);
			}
			else if (step->layer_type == LAYER_SLOT_TYPE_LAYER && active > 0 &&
			         history_steps->buffer[active - 1]->action == HISTORY_ACTION_DELETE_LAYER &&
			         history_steps->buffer[active - 1]->layer_type == LAYER_SLOT_TYPE_MASK) {
				sys_notify_on_next_frame(&history_undo_delete_layer_masks, NULL);
			}
		}
		else if (step->action == HISTORY_ACTION_CLEAR_LAYER) {
			history_undo_i    = history_undo_i - 1 < 0 ? g_config->undo_steps - 1 : history_undo_i - 1;
			slot_layer_t *lay = history_undo_layers->buffer[history_undo_i];
			slot_layer_swap(g_context->layer, lay);
			g_context->layer_preview_dirty = true;
		}
		else if (step->action == HISTORY_ACTION_DUPLICATE_LAYER) {
			slot_layer_t_array_t *children = slot_layer_get_recursive_children(g_project->_->layers->buffer[step->layer]);
			i32                   position = step->layer + 1;
			if (children != NULL) {
				position += children->length;
			}

			g_context->layer = g_project->_->layers->buffer[position];
			slot_layer_delete(g_context->layer);
		}
		else if (step->action == HISTORY_ACTION_ORDER_LAYERS) {
			slot_layer_t *target                           = g_project->_->layers->buffer[step->prev_order];
			g_project->_->layers->buffer[step->prev_order] = g_project->_->layers->buffer[step->layer];
			g_project->_->layers->buffer[step->layer]      = target;
		}
		else if (step->action == HISTORY_ACTION_MERGE_LAYERS) {
			g_context->layer = g_project->_->layers->buffer[step->layer];
			slot_layer_delete(g_context->layer);

			slot_layer_t *parent = step->layer_parent > 0 ? g_project->_->layers->buffer[step->layer_parent - 2] : NULL;
			slot_layer_t *l      = slot_layer_create("", step->layer_type, parent);
			array_insert(g_project->_->layers, step->layer, l);
			context_set_layer(l);

			history_undo_i    = history_undo_i - 1 < 0 ? g_config->undo_steps - 1 : history_undo_i - 1;
			slot_layer_t *lay = history_undo_layers->buffer[history_undo_i];
			slot_layer_swap(g_context->layer, lay);

			l = slot_layer_create("", step->layer_type, parent);
			array_insert(g_project->_->layers, step->layer + 1, l);
			context_set_layer(l);

			history_undo_i = history_undo_i - 1 < 0 ? g_config->undo_steps - 1 : history_undo_i - 1;
			lay            = history_undo_layers->buffer[history_undo_i];
			slot_layer_swap(g_context->layer, lay);

			g_context->layer->mask_opacity  = step->layer_opacity;
			g_context->layer->blending      = step->layer_blending;
			g_context->layer->object_mask   = step->layer_object;
			g_context->layers_preview_dirty = true;
			make_material_parse_mesh_material();
		}
		else if (step->action == HISTORY_ACTION_APPLY_MASK) {
			// First restore the layer(s)
			i32           mask_pos      = step->layer;
			slot_layer_t *current_layer = NULL;
			// The layer at the old mask position is a mask, i.e. the layer had multiple masks before.
			if (slot_layer_is_mask(g_project->_->layers->buffer[mask_pos])) {
				current_layer = g_project->_->layers->buffer[mask_pos]->parent;
			}
			else if (slot_layer_is_layer(g_project->_->layers->buffer[mask_pos]) || slot_layer_is_group(g_project->_->layers->buffer[mask_pos])) {
				current_layer = g_project->_->layers->buffer[mask_pos];
			}

			slot_layer_t_array_t *layers_to_restore;
			if (slot_layer_is_group(current_layer)) {
				layers_to_restore = slot_layer_get_children(current_layer);
			}
			else {
				layers_to_restore = any_array_create_from_raw(
				    (void *[]){
				        current_layer,
				    },
				    1);
			}
			array_reverse(layers_to_restore);

			for (i32 i = 0; i < layers_to_restore->length; ++i) {
				slot_layer_t *layer = layers_to_restore->buffer[i];
				// Replace the current layer's content with the old one
				g_context->layer        = layer;
				history_undo_i          = history_undo_i - 1 < 0 ? g_config->undo_steps - 1 : history_undo_i - 1;
				slot_layer_t *old_layer = history_undo_layers->buffer[history_undo_i];
				slot_layer_swap(g_context->layer, old_layer);
			}

			// Now restore the applied mask
			history_undo_i     = history_undo_i - 1 < 0 ? g_config->undo_steps - 1 : history_undo_i - 1;
			slot_layer_t *mask = history_undo_layers->buffer[history_undo_i];
			layers_new_mask(false, current_layer, mask_pos);
			slot_layer_swap(g_context->layer, mask);
			g_context->layers_preview_dirty = true;
			context_set_layer(g_context->layer);
		}
		else if (step->action == HISTORY_ACTION_INVERT_MASK) {
			sys_notify_on_next_frame(&history_undo_invert_mask, step);
		}
		else if (step->action == HISTORY_ACTION_APPLY_FILTER) {
			history_undo_i    = history_undo_i - 1 < 0 ? g_config->undo_steps - 1 : history_undo_i - 1;
			slot_layer_t *lay = history_undo_layers->buffer[history_undo_i];
			context_set_layer(g_project->_->layers->buffer[step->layer]);
			slot_layer_swap(g_context->layer, lay);
			layers_new_mask(false, g_context->layer, -1);
			slot_layer_swap(g_context->layer, lay);
			g_context->layer_preview_dirty = true;
		}
		else if (step->action == HISTORY_ACTION_TO_FILL_LAYER || step->action == HISTORY_ACTION_TO_FILL_MASK) {
			slot_layer_to_paint_layer(g_context->layer);
			history_undo_i    = history_undo_i - 1 < 0 ? g_config->undo_steps - 1 : history_undo_i - 1;
			slot_layer_t *lay = history_undo_layers->buffer[history_undo_i];
			slot_layer_swap(g_context->layer, lay);
		}
		else if (step->action == HISTORY_ACTION_TO_PAINT_LAYER || step->action == HISTORY_ACTION_TO_PAINT_MASK) {
			history_undo_i    = history_undo_i - 1 < 0 ? g_config->undo_steps - 1 : history_undo_i - 1;
			slot_layer_t *lay = history_undo_layers->buffer[history_undo_i];
			slot_layer_swap(g_context->layer, lay);
			g_context->layer->fill_material = g_project->_->materials->buffer[step->material];
		}
		else if (step->action == HISTORY_ACTION_LAYER_OPACITY) {
			context_set_layer(g_project->_->layers->buffer[step->layer]);
			f32 t                          = g_context->layer->mask_opacity;
			g_context->layer->mask_opacity = step->layer_opacity;
			step->layer_opacity            = t;
			make_material_parse_mesh_material();
		}
		else if (step->action == HISTORY_ACTION_LAYER_BLENDING) {
			context_set_layer(g_project->_->layers->buffer[step->layer]);
			blend_type_t t             = g_context->layer->blending;
			g_context->layer->blending = step->layer_blending;
			step->layer_blending       = t;
			make_material_parse_mesh_material();
		}
		else if (step->action == HISTORY_ACTION_DELETE_NODE_GROUP) {
			node_group_t *ng = GC_ALLOC_INIT(node_group_t, {.canvas = NULL, .nodes = ui_nodes_create()});
			array_insert(g_project->_->material_groups, step->canvas_group, ng);
			history_swap_canvas(step);
		}
		else if (step->action == HISTORY_ACTION_NEW_MATERIAL) {
			g_context->material = g_project->_->materials->buffer[step->material];
			step->canvas        = g_context->material->canvas;
			slot_material_delete(g_context->material);
		}
		else if (step->action == HISTORY_ACTION_DELETE_MATERIAL) {
			g_context->material = slot_material_create(g_project->_->materials->buffer[0]->data, NULL);
			array_insert(g_project->_->materials, step->material, g_context->material);
			g_context->material->canvas = step->canvas;
			ui_nodes_canvas_changed();
			ui_nodes_hwnd->redraws = 2;
		}
		else if (step->action == HISTORY_ACTION_DUPLICATE_MATERIAL) {
			g_context->material = g_project->_->materials->buffer[step->material];
			step->canvas        = g_context->material->canvas;
			slot_material_delete(g_context->material);
		}
		else { // Paint operation
			history_undo_i    = history_undo_i - 1 < 0 ? g_config->undo_steps - 1 : history_undo_i - 1;
			slot_layer_t *lay = history_undo_layers->buffer[history_undo_i];
			context_select_paint_object(g_project->_->paint_objects->buffer[step->object]);
			context_set_layer(g_project->_->layers->buffer[step->layer]);
			slot_layer_swap(g_context->layer, lay);
			g_context->layer_preview_dirty = true;
		}

		history_undos--;
		history_redos++;
		g_context->rtdirty = 1;
		g_context->ddirty  = 2;

		ui_base_hwnds->buffer[TAB_AREA_SIDEBAR0]->redraws = 2;
		ui_base_hwnds->buffer[TAB_AREA_SIDEBAR1]->redraws = 2;
		if (ui_view2d_show) {
			ui_view2d_hwnd->redraws = 2;
		}

		if (g_config->touch_ui) {
			// Refresh undo & redo buttons
			ui_menubar_menu_handle->redraws = 2;
		}
	}
}

void history_redo_invert_mask(history_step_t *step) {
	g_context->layer = g_project->_->layers->buffer[step->layer];
	slot_layer_invert_mask(g_context->layer);
}

void history_redo_apply_mask(void *_) {
	slot_layer_apply_mask(g_context->layer);
	context_set_layer(g_context->layer);
	g_context->layers_preview_dirty = true;
}

void history_redo_merge_layers2(void *_) {
	layers_merge_down();
}

void history_copy_to_undo(i32 from_id, i32 to_id, bool is_mask) {
	char *to_id_s   = i32_to_string(to_id);
	char *from_id_s = i32_to_string(from_id);
	if (is_mask) {
		render_path_set_target(string("texpaint_undo%s", to_id_s), NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
		render_path_bind_target(string("texpaint%s", from_id_s), "tex");
		// render_path_draw_shader("Scene/copy_pass/copyR8_pass");
		render_path_draw_shader("Scene/copy_pass/copy_pass");
	}
	else if (g_context->layer->texpaint_sculpt != NULL) {
		render_path_set_target(string("texpaint_sculpt_undo%s", to_id_s), NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
		render_path_bind_target(string("texpaint_sculpt%s", from_id_s), "tex");
		render_path_draw_shader("Scene/copy_pass/copyRGBA128_pass");
	}
	else {
		string_array_t *additional = any_array_create_from_raw(
		    (void *[]){
		        string("texpaint_nor_undo%s", to_id_s),
		        string("texpaint_pack_undo%s", to_id_s),
		    },
		    2);
		render_path_set_target(string("texpaint_undo%s", to_id_s), additional, NULL, GPU_CLEAR_NONE, 0, 0.0);
		render_path_bind_target(string("texpaint%s", from_id_s), "tex0");
		render_path_bind_target(string("texpaint_nor%s", from_id_s), "tex1");
		render_path_bind_target(string("texpaint_pack%s", from_id_s), "tex2");

		gpu_texture_format_t format = base_bits_handle->i == TEXTURE_BITS_BITS8    ? GPU_TEXTURE_FORMAT_RGBA32
		                              : base_bits_handle->i == TEXTURE_BITS_BITS16 ? GPU_TEXTURE_FORMAT_RGBA64
		                                                                           : GPU_TEXTURE_FORMAT_RGBA128;

		char *pipe = format == GPU_TEXTURE_FORMAT_RGBA32   ? "copy_mrt3_pass"
		             : format == GPU_TEXTURE_FORMAT_RGBA64 ? "copy_mrt3RGBA64_pass"
		                                                   : "copy_mrt3RGBA128_pass";
		render_path_draw_shader(string("Scene/copy_mrt3_pass/%s", pipe));
	}
	history_undo_i = (history_undo_i + 1) % g_config->undo_steps;
}

void history_copy_merging_layers() {
	slot_layer_t *lay = g_context->layer;
	history_copy_to_undo(lay->id, history_undo_i, slot_layer_is_mask(g_context->layer));

	i32 below = array_index_of(g_project->_->layers, lay) - 1;
	lay       = g_project->_->layers->buffer[below];
	history_copy_to_undo(lay->id, history_undo_i, slot_layer_is_mask(g_context->layer));
}

void history_copy_merging_layers2(slot_layer_t_array_t *layers) {
	for (i32 i = 0; i < layers->length; ++i) {
		slot_layer_t *layer = layers->buffer[i];
		history_copy_to_undo(layer->id, history_undo_i, slot_layer_is_mask(layer));
	}
}

void history_redo_merge_layers(void *_) {
	history_copy_merging_layers();
}

void history_redo_duplicate_layer(void *_) {
	layers_duplicate_layer(g_context->layer);
}

void history_redo_delete_layer(void *_) {
	i32 active = history_steps->length - history_redos;
	i32 n      = 1;
	while (active + n < history_steps->length && history_steps->buffer[active + n]->layer_type == LAYER_SLOT_TYPE_MASK) {
		++n;
	}
	for (i32 i = 0; i < n; ++i) {
		history_redo();
	}
}

void history_redo_new_fill_mask(slot_layer_t *l) {
	slot_layer_to_fill_layer(l);
}

void history_redo_new_white_mask(slot_layer_t *l) {
	slot_layer_clear(l, 0xffffffff, NULL, 1.0, layers_default_rough, 0.0);
}

void history_redo_new_black_mask(slot_layer_t *l) {
	slot_layer_clear(l, 0x00000000, NULL, 1.0, layers_default_rough, 0.0);
}

void history_swap_active() {
	slot_layer_t *undo_layer = history_undo_layers->buffer[history_undo_i];
	slot_layer_swap(undo_layer, g_context->layer);
	history_undo_i = (history_undo_i + 1) % g_config->undo_steps;
}

void history_redo() {
	if (history_redos > 0) {
		i32             active = history_steps->length - history_redos;
		history_step_t *step   = history_steps->buffer[active];

		if (step->action == HISTORY_ACTION_EDIT_NODES) {
			history_swap_canvas(step);
		}
		else if (step->action == HISTORY_ACTION_NEW_LAYER || step->action == HISTORY_ACTION_NEW_BLACK_MASK || step->action == HISTORY_ACTION_NEW_WHITE_MASK ||
		         step->action == HISTORY_ACTION_NEW_FILL_MASK) {
			slot_layer_t *parent = step->layer_parent > 0 ? g_project->_->layers->buffer[step->layer_parent - 1] : NULL;
			slot_layer_t *l      = slot_layer_create("", step->layer_type, parent);
			array_insert(g_project->_->layers, step->layer, l);
			if (step->action == HISTORY_ACTION_NEW_BLACK_MASK) {
				sys_notify_on_next_frame(&history_redo_new_black_mask, l);
			}
			else if (step->action == HISTORY_ACTION_NEW_WHITE_MASK) {
				sys_notify_on_next_frame(&history_redo_new_white_mask, l);
			}
			else if (step->action == HISTORY_ACTION_NEW_FILL_MASK) {
				g_context->material = g_project->_->materials->buffer[step->material];
				sys_notify_on_next_frame(&history_redo_new_fill_mask, l);
			}
			g_context->layer_preview_dirty = true;
			context_set_layer(l);
		}
		else if (step->action == HISTORY_ACTION_NEW_GROUP) {
			slot_layer_t *l     = g_project->_->layers->buffer[step->layer - 1];
			slot_layer_t *group = layers_new_group();
			array_remove(g_project->_->layers, group);
			array_insert(g_project->_->layers, step->layer, group);
			l->parent = group;
			context_set_layer(group);
		}
		else if (step->action == HISTORY_ACTION_DELETE_LAYER) {
			g_context->layer = g_project->_->layers->buffer[step->layer];
			history_swap_active();
			slot_layer_delete(g_context->layer);

			// Redoing the last delete would result in an empty group
			// Redo deleting all group masks + the group itself
			if (step->layer_type == LAYER_SLOT_TYPE_LAYER && history_steps->length >= active + 2 &&
			    (history_steps->buffer[active + 1]->layer_type == LAYER_SLOT_TYPE_GROUP ||
			     history_steps->buffer[active + 1]->layer_type == LAYER_SLOT_TYPE_MASK)) {
				sys_notify_on_next_frame(&history_redo_delete_layer, NULL);
			}
		}
		else if (step->action == HISTORY_ACTION_CLEAR_LAYER) {
			g_context->layer = g_project->_->layers->buffer[step->layer];
			history_swap_active();
			slot_layer_clear(g_context->layer, 0x00000000, NULL, 1.0, layers_default_rough, 0.0);
			g_context->layer_preview_dirty = true;
		}
		else if (step->action == HISTORY_ACTION_DUPLICATE_LAYER) {
			g_context->layer = g_project->_->layers->buffer[step->layer];
			sys_notify_on_next_frame(&history_redo_duplicate_layer, NULL);
		}
		else if (step->action == HISTORY_ACTION_ORDER_LAYERS) {
			slot_layer_t *target                           = g_project->_->layers->buffer[step->prev_order];
			g_project->_->layers->buffer[step->prev_order] = g_project->_->layers->buffer[step->layer];
			g_project->_->layers->buffer[step->layer]      = target;
		}
		else if (step->action == HISTORY_ACTION_MERGE_LAYERS) {
			g_context->layer = g_project->_->layers->buffer[step->layer + 1];
			sys_notify_on_next_frame(&history_redo_merge_layers, NULL);
			sys_notify_on_next_frame(&history_redo_merge_layers2, NULL);
		}
		else if (step->action == HISTORY_ACTION_APPLY_MASK) {
			g_context->layer = g_project->_->layers->buffer[step->layer];
			if (slot_layer_is_group_mask(g_context->layer)) {
				slot_layer_t         *group  = g_context->layer->parent;
				slot_layer_t_array_t *layers = slot_layer_get_children(group);
				array_insert(layers, 0, g_context->layer);
				history_copy_merging_layers2(layers);
			}
			else {
				slot_layer_t_array_t *layers = any_array_create_from_raw(
				    (void *[]){
				        g_context->layer,
				        g_context->layer->parent,
				    },
				    2);
				history_copy_merging_layers2(layers);
			}

			sys_notify_on_next_frame(&history_redo_apply_mask, NULL);
		}
		else if (step->action == HISTORY_ACTION_INVERT_MASK) {
			sys_notify_on_next_frame(&history_redo_invert_mask, step);
		}
		else if (step->action == HISTORY_ACTION_APPLY_FILTER) {
			slot_layer_t *lay = history_undo_layers->buffer[history_undo_i];
			context_set_layer(g_project->_->layers->buffer[step->layer]);
			slot_layer_swap(g_context->layer, lay);
			layers_new_mask(false, lay, -1);
			slot_layer_swap(g_context->layer, lay);
			g_context->layer_preview_dirty = true;
			history_undo_i                 = (history_undo_i + 1) % g_config->undo_steps;
		}
		else if (step->action == HISTORY_ACTION_TO_FILL_LAYER || step->action == HISTORY_ACTION_TO_FILL_MASK) {
			slot_layer_t *lay = history_undo_layers->buffer[history_undo_i];
			slot_layer_swap(g_context->layer, lay);
			g_context->layer->fill_material = g_project->_->materials->buffer[step->material];
			history_undo_i                  = (history_undo_i + 1) % g_config->undo_steps;
		}
		else if (step->action == HISTORY_ACTION_TO_PAINT_LAYER || step->action == HISTORY_ACTION_TO_PAINT_MASK) {
			slot_layer_to_paint_layer(g_context->layer);
			slot_layer_t *lay = history_undo_layers->buffer[history_undo_i];
			slot_layer_swap(g_context->layer, lay);
			history_undo_i = (history_undo_i + 1) % g_config->undo_steps;
		}
		else if (step->action == HISTORY_ACTION_LAYER_OPACITY) {
			context_set_layer(g_project->_->layers->buffer[step->layer]);
			f32 t                          = g_context->layer->mask_opacity;
			g_context->layer->mask_opacity = step->layer_opacity;
			step->layer_opacity            = t;
			make_material_parse_mesh_material();
		}
		else if (step->action == HISTORY_ACTION_LAYER_BLENDING) {
			context_set_layer(g_project->_->layers->buffer[step->layer]);
			blend_type_t t             = g_context->layer->blending;
			g_context->layer->blending = step->layer_blending;
			step->layer_blending       = t;
			make_material_parse_mesh_material();
		}
		else if (step->action == HISTORY_ACTION_DELETE_NODE_GROUP) {
			history_swap_canvas(step);
			array_remove(g_project->_->material_groups, g_project->_->material_groups->buffer[step->canvas_group]);
		}
		else if (step->action == HISTORY_ACTION_NEW_MATERIAL) {
			g_context->material = slot_material_create(g_project->_->materials->buffer[0]->data, NULL);
			array_insert(g_project->_->materials, step->material, g_context->material);
			g_context->material->canvas = step->canvas;
			ui_nodes_canvas_changed();
			ui_nodes_hwnd->redraws = 2;
		}
		else if (step->action == HISTORY_ACTION_DELETE_MATERIAL) {
			g_context->material = g_project->_->materials->buffer[step->material];
			step->canvas        = g_context->material->canvas;
			slot_material_delete(g_context->material);
		}
		else if (step->action == HISTORY_ACTION_DUPLICATE_MATERIAL) {
			g_context->material = slot_material_create(g_project->_->materials->buffer[0]->data, NULL);
			array_insert(g_project->_->materials, step->material, g_context->material);
			g_context->material->canvas = step->canvas;
			ui_nodes_canvas_changed();
			ui_nodes_hwnd->redraws = 2;
		}
		else { // Paint operation
			slot_layer_t *lay = history_undo_layers->buffer[history_undo_i];
			context_select_paint_object(g_project->_->paint_objects->buffer[step->object]);
			context_set_layer(g_project->_->layers->buffer[step->layer]);
			slot_layer_swap(g_context->layer, lay);
			g_context->layer_preview_dirty = true;
			history_undo_i                 = (history_undo_i + 1) % g_config->undo_steps;
		}

		history_undos++;
		history_redos--;
		g_context->rtdirty = 1;
		g_context->ddirty  = 2;

		ui_base_hwnds->buffer[TAB_AREA_SIDEBAR0]->redraws = 2;
		ui_base_hwnds->buffer[TAB_AREA_SIDEBAR1]->redraws = 2;
		if (ui_view2d_show) {
			ui_view2d_hwnd->redraws = 2;
		}

		if (g_config->touch_ui) {
			// Refresh undo & redo buttons
			ui_menubar_menu_handle->redraws = 2;
		}
	}
}

void history_reset() {
	gc_unroot(history_steps);
	history_steps = any_array_create_from_raw(
	    (void *[]){
	        GC_ALLOC_INIT(history_step_t,
	                      {.name = tr("New"), .layer = 0, .layer_type = LAYER_SLOT_TYPE_LAYER, .layer_parent = -1, .object = 0, .material = 0, .brush = 0}),
	    },
	    1);
	gc_root(history_steps);
	history_undos  = 0;
	history_redos  = 0;
	history_undo_i = 0;
}

history_step_t *history_push(history_action_t action) {
	char *name = history_action_to_string(action);
#if defined(IRON_WINDOWS) || defined(IRON_LINUX) || defined(IRON_MACOS)
	char *filename = string_equals(g_project->_->filepath, "") ? ui_files_filename
	                                                           : substring(g_project->_->filepath, string_last_index_of(g_project->_->filepath, PATH_SEP) + 1,
	                                                                       string_length(g_project->_->filepath) - 4);
	sys_title_set(string("%s* - %s", filename, manifest_title));
#endif

	if (g_config->touch_ui) {
		// Refresh undo & redo buttons
		ui_menubar_menu_handle->redraws = 2;
	}

	if (history_undos < g_config->undo_steps) {
		history_undos++;
	}
	if (history_redos > 0) {
		for (i32 i = 0; i < history_redos; ++i) {
			array_pop(history_steps);
		}
		history_redos = 0;
	}

	i32 opos = array_index_of(g_project->_->paint_objects, g_context->paint_object);
	i32 lpos = array_index_of(g_project->_->layers, g_context->layer);
	i32 mpos = array_index_of(g_project->_->materials, g_context->material);
	i32 bpos = array_index_of(g_project->_->brushes, g_context->brush);

	history_step_t *step =
	    GC_ALLOC_INIT(history_step_t, {.name           = name,
	                                   .action         = action,
	                                   .layer          = lpos,
	                                   .layer_type     = slot_layer_is_mask(g_context->layer)    ? LAYER_SLOT_TYPE_MASK
	                                                     : slot_layer_is_group(g_context->layer) ? LAYER_SLOT_TYPE_GROUP
	                                                                                             : LAYER_SLOT_TYPE_LAYER,
	                                   .layer_parent   = g_context->layer->parent == NULL ? -1 : array_index_of(g_project->_->layers, g_context->layer->parent),
	                                   .object         = opos,
	                                   .material       = mpos,
	                                   .brush          = bpos,
	                                   .layer_opacity  = g_context->layer->mask_opacity,
	                                   .layer_object   = g_context->layer->object_mask,
	                                   .layer_blending = g_context->layer->blending});

	any_array_push(history_steps, step);

	while (history_steps->length > g_config->undo_steps + 1) {
		array_shift(history_steps);
	}
	return history_steps->buffer[history_steps->length - 1];
}

void history_edit_nodes(ui_node_canvas_t *canvas, i32 canvas_type, i32 canvas_group) {
	history_step_t *step = history_push(HISTORY_ACTION_EDIT_NODES);
	step->canvas_group   = canvas_group;
	step->canvas_type    = canvas_type;
	step->canvas         = util_clone_canvas(canvas);
}

void history_paint() {
	bool is_mask = slot_layer_is_mask(g_context->layer);
	history_copy_to_undo(g_context->layer->id, history_undo_i, is_mask);

	history_push_undo    = false;
	history_step_t *step = history_push(HISTORY_ACTION_PAINT);
	step->name           = tr(ui_toolbar_tool_names->buffer[g_context->tool]);
}

void history_new_layer() {
	history_push(HISTORY_ACTION_NEW_LAYER);
}

void history_new_black_mask() {
	history_push(HISTORY_ACTION_NEW_BLACK_MASK);
}

void history_new_white_mask() {
	history_push(HISTORY_ACTION_NEW_WHITE_MASK);
}

void history_new_fill_mask() {
	history_push(HISTORY_ACTION_NEW_FILL_MASK);
}

void history_new_group() {
	history_push(HISTORY_ACTION_NEW_GROUP);
}

void history_duplicate_layer() {
	history_push(HISTORY_ACTION_DUPLICATE_LAYER);
}

void history_delete_layer() {
	history_swap_active();
	history_push(HISTORY_ACTION_DELETE_LAYER);
}

void history_clear_layer() {
	history_swap_active();
	history_push(HISTORY_ACTION_CLEAR_LAYER);
}

void history_order_layers(i32 prev_order) {
	history_step_t *step = history_push(HISTORY_ACTION_ORDER_LAYERS);
	step->prev_order     = prev_order;
}

void history_merge_layers() {
	history_copy_merging_layers();

	history_step_t *step = history_push(HISTORY_ACTION_MERGE_LAYERS);
	step->layer -= 1; // Merge down
	if (slot_layer_has_masks(g_context->layer, true)) {
		step->layer -= slot_layer_get_masks(g_context->layer, true)->length;
	}
	array_shift(history_steps); // Merge consumes 2 steps
	history_undos--;
	// TODO: use undo layer in app_merge_down to save memory
}

void history_apply_mask() {
	if (slot_layer_is_group_mask(g_context->layer)) {
		slot_layer_t         *group  = g_context->layer->parent;
		slot_layer_t_array_t *layers = slot_layer_get_children(group);
		array_insert(layers, 0, g_context->layer);
		history_copy_merging_layers2(layers);
	}
	else {
		slot_layer_t_array_t *layers = any_array_create_from_raw(
		    (void *[]){
		        g_context->layer,
		        g_context->layer->parent,
		    },
		    2);
		history_copy_merging_layers2(layers);
	}
	history_push(HISTORY_ACTION_APPLY_MASK);
}

void history_invert_mask() {
	history_push(HISTORY_ACTION_INVERT_MASK);
}

void history_to_fill_layer() {
	history_copy_to_undo(g_context->layer->id, history_undo_i, false);
	history_push(HISTORY_ACTION_TO_FILL_LAYER);
}

void history_to_fill_mask() {
	history_copy_to_undo(g_context->layer->id, history_undo_i, true);
	history_push(HISTORY_ACTION_TO_FILL_MASK);
}

void history_to_paint_layer() {
	history_copy_to_undo(g_context->layer->id, history_undo_i, false);
	history_push(HISTORY_ACTION_TO_PAINT_LAYER);
}

void history_to_paint_mask() {
	history_copy_to_undo(g_context->layer->id, history_undo_i, true);
	history_push(HISTORY_ACTION_TO_PAINT_MASK);
}

void history_layer_opacity() {
	history_push(HISTORY_ACTION_LAYER_OPACITY);
}

void history_layer_blending() {
	history_push(HISTORY_ACTION_LAYER_BLENDING);
}

void history_new_material() {
	history_step_t *step = history_push(HISTORY_ACTION_NEW_MATERIAL);
	step->canvas_type    = 0;
	step->canvas         = util_clone_canvas(g_context->material->canvas);
}

void history_delete_material() {
	history_step_t *step = history_push(HISTORY_ACTION_DELETE_MATERIAL);
	step->canvas_type    = 0;
	step->canvas         = util_clone_canvas(g_context->material->canvas);
}

void history_duplicate_material() {
	history_step_t *step = history_push(HISTORY_ACTION_DUPLICATE_MATERIAL);
	step->canvas_type    = 0;
	step->canvas         = util_clone_canvas(g_context->material->canvas);
}

void history_delete_material_group(node_group_t *group) {
	history_step_t *step = history_push(HISTORY_ACTION_DELETE_NODE_GROUP);
	step->canvas_type    = CANVAS_TYPE_MATERIAL;
	step->canvas_group   = array_index_of(g_project->_->material_groups, group);
	step->canvas         = util_clone_canvas(group->canvas);
}
