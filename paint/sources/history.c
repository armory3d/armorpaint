
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
	case HISTORY_ACTION_LAYER_VISIBLE:
		return tr("Layer Visibility");
	case HISTORY_ACTION_LAYER_NAME:
		return tr("Rename Layer");
	case HISTORY_ACTION_LAYER_OBJECT:
		return tr("Layer Object");
	case HISTORY_ACTION_LAYER_SCALE:
		return tr("Layer UV Scale");
	case HISTORY_ACTION_LAYER_ANGLE:
		return tr("Layer Angle");
	case HISTORY_ACTION_LAYER_UV_TYPE:
		return tr("Layer UV Type");
	case HISTORY_ACTION_NEW_BRUSH:
		return tr("New Brush");
	case HISTORY_ACTION_DELETE_BRUSH:
		return tr("Delete Brush");
	case HISTORY_ACTION_DUPLICATE_BRUSH:
		return tr("Duplicate Brush");
	case HISTORY_ACTION_NEW_SWATCH:
		return tr("New Swatch");
	case HISTORY_ACTION_DELETE_SWATCH:
		return tr("Delete Swatch");
	case HISTORY_ACTION_DUPLICATE_SWATCH:
		return tr("Duplicate Swatch");
	case HISTORY_ACTION_REPLACE_SWATCHES:
		return tr("Replace Swatches");
	case HISTORY_ACTION_EDIT_SWATCH:
		return tr("Edit Swatch");
	case HISTORY_ACTION_OBJECT_TRANSFORM:
		return tr("Transform Object");
	}
	return tr("Paint");
}

void history_undo_invert_mask(history_step_t *step) {
	g_context->layer = g_project->_->layers->buffer[step->layer];
	slot_layer_invert_mask(g_context->layer);
}

static void history_step_free(history_step_t *step) {
	for (i32 i = 0; step->merge_layers != NULL && i < step->merge_layers->length; ++i) {
		slot_layer_unload(step->merge_layers->buffer[i]);
	}
	free(step);
}

slot_layer_t_array_t *history_merge_saved_layers() {
	i32 count = 0;
	for (i32 i = 0; history_steps != NULL && i < history_steps->length; ++i) {
		slot_layer_t_array_t *layers = history_steps->buffer[i]->merge_layers;
		count += layers != NULL ? layers->length : 0;
	}
	slot_layer_t_array_t *saved = any_array_create_from_raw_tmp(NULL, count);
	i32                   pos   = 0;
	for (i32 i = 0; history_steps != NULL && i < history_steps->length; ++i) {
		slot_layer_t_array_t *layers = history_steps->buffer[i]->merge_layers;
		for (i32 j = 0; layers != NULL && j < layers->length; ++j) {
			saved->buffer[pos++] = layers->buffer[j];
		}
	}
	return saved;
}

static void history_swap_merge(history_step_t *step, bool undo) {
	slot_layer_t_array_t *layers   = g_project->_->layers;
	slot_layer_t_array_t *saved    = step->merge_layers;
	i32_array_t          *parents  = step->merge_parents;
	i32                   count    = layers->length - (step->merge_length - saved->length);
	slot_layer_t_array_t *pointers = tab_layers_init_layer_map();

	step->merge_layers  = any_array_create_from_raw((void *[]){}, 0);
	step->merge_parents = i32_array_create(0);
	step->merge_length  = layers->length;
	for (i32 i = 0; i < count; ++i) {
		slot_layer_t *l = layers->buffer[step->merge_start + i];
		any_array_push(step->merge_layers, l);
		i32_array_push(step->merge_parents, l->parent != NULL ? array_index_of(layers, l->parent) : -1);
	}
	for (i32 i = 0; i < count; ++i) {
		array_remove(layers, step->merge_layers->buffer[i]);
	}
	for (i32 i = 0; i < saved->length; ++i) {
		array_insert(layers, step->merge_start + i, saved->buffer[i]);
	}
	for (i32 i = 0; i < saved->length; ++i) {
		saved->buffer[i]->parent = parents->buffer[i] >= 0 ? layers->buffer[parents->buffer[i]] : NULL;
	}

	if (step->action == HISTORY_ACTION_DELETE_LAYER) {
		tab_layers_remap_all_layer_pointers(pointers);
		for (i32 i = 0; i < saved->length; ++i) {
			tab_stages_add_layer(saved->buffer[i]->name);
		}
	}

	i32 top = step->merge_start + saved->length - 1;
	context_set_layer(layers->buffer[undo ? step->layer : top >= 0 ? top : 0]);
	g_context->layers_preview_dirty = true;
	tab_timeline_sync();
}

static void history_swap_order(history_step_t *step) {
	slot_layer_t_array_t *pointers = tab_layers_init_layer_map();
	slot_layer_t         *l        = g_project->_->layers->buffer[step->prev_order];
	i32                   parent   = l->parent != NULL ? array_index_of(g_project->_->layers, l->parent) : -1;
	slot_layer_relocate(l, step->layer);
	l->parent          = step->layer_parent >= 0 ? g_project->_->layers->buffer[step->layer_parent] : NULL;
	step->layer_parent = parent;
	i32 prev_order     = step->prev_order;
	step->prev_order   = step->layer;
	step->layer        = prev_order;
	tab_layers_remap_all_layer_pointers(pointers);
	context_set_layer(l);
	make_material_parse_mesh_material();
}

void history_update_fill_layer(slot_layer_t *l) {
	context_set_material(l->fill_material);
	context_set_layer(l);
	layers_update_fill_layers();
}

void history_layer_object_clear(slot_layer_t *l) {
	g_context->material = l->fill_material;
	slot_layer_clear(l, 0x00000000, NULL, 1.0, layers_default_rough, 0.0);
	layers_update_fill_layers();
}

void history_swap_layer_props(history_step_t *step) {
	slot_layer_t *l = g_project->_->layers->buffer[step->layer];

	if (step->action == HISTORY_ACTION_LAYER_VISIBLE) {
		context_set_layer(l);
		l->visible = !l->visible;
		make_material_parse_mesh_material();
		if (ui_view2d_show) {
			ui_view2d_hwnd->redraws = 2;
		}
	}
	else if (step->action == HISTORY_ACTION_LAYER_NAME) {
		char *t = l->name;
		tab_stages_rename_layer(l->name, step->layer_name);
		l->name          = step->layer_name;
		step->layer_name = t;
		context_set_layer(l);
	}
	else if (step->action == HISTORY_ACTION_LAYER_OBJECT) {
		i32 t              = l->object_mask;
		l->object_mask     = step->layer_object;
		step->layer_object = t;
		context_set_layer(l);
		make_material_parse_mesh_material();
		if (l->fill_material != NULL) {
			sys_notify_on_next_frame(&history_layer_object_clear, l);
		}
		else {
			layers_set_object_mask();
		}
	}
	else if (step->action == HISTORY_ACTION_LAYER_SCALE) {
		f32 t             = l->scale;
		l->scale          = step->layer_scale;
		step->layer_scale = t;
		sys_notify_on_next_frame(&history_update_fill_layer, l);
	}
	else if (step->action == HISTORY_ACTION_LAYER_ANGLE) {
		f32 t             = l->angle;
		l->angle          = step->layer_angle;
		step->layer_angle = t;
		context_set_material(l->fill_material);
		context_set_layer(l);
		make_material_parse_paint_material(true);
		sys_notify_on_next_frame(&history_update_fill_layer, l);
	}
	else if (step->action == HISTORY_ACTION_LAYER_UV_TYPE) {
		i32 t               = l->uv_type;
		l->uv_type          = step->layer_uv_type;
		step->layer_uv_type = t;
		context_set_material(l->fill_material);
		context_set_layer(l);
		make_material_parse_paint_material(true);
		sys_notify_on_next_frame(&history_update_fill_layer, l);
	}
}

void history_new_brush_slot(history_step_t *step) {
	slot_brush_t *b = slot_brush_create(NULL);
	array_insert(g_project->_->brushes, step->brush, b);
	b->canvas = step->canvas;
	context_set_brush(b);
	ui_nodes_canvas_changed();
	ui_nodes_hwnd->redraws = 2;
	ui_dock_redraw(TAB_AREA_SIDEBAR1);
	util_render_make_brush_preview();
}

void history_delete_brush_slot(history_step_t *step) {
	step->canvas = g_project->_->brushes->buffer[step->brush]->canvas;
	if (g_project->_->brushes->length > 1) {
		context_select_brush(step->brush == g_project->_->brushes->length - 1 ? step->brush - 1 : step->brush + 1);
	}
	array_splice(g_project->_->brushes, step->brush, 1);
	ui_nodes_canvas_changed();
	ui_nodes_hwnd->redraws = 2;
	ui_dock_redraw(TAB_AREA_SIDEBAR1);
}

void history_new_swatch_slot(history_step_t *step) {
	array_insert(g_project->swatches, step->swatch, step->swatch_color);
	g_context->swatch = step->swatch_color;
	ui_dock_redraw(TAB_AREA_STATUS);
}

void history_delete_swatch_slot(history_step_t *step) {
	step->swatch_color = g_project->swatches->buffer[step->swatch];
	if (g_project->swatches->length > 1) {
		g_context->swatch = g_project->swatches->buffer[step->swatch == g_project->swatches->length - 1 ? step->swatch - 1 : step->swatch + 1];
	}
	array_splice(g_project->swatches, step->swatch, 1);
	ui_dock_redraw(TAB_AREA_STATUS);
}

void history_swap_swatches(history_step_t *step) {
	swatch_color_t_array_t *t = g_project->swatches;
	g_project->swatches       = step->swatch_colors;
	step->swatch_colors       = t;
	if (g_project->swatches->length > 0) {
		g_context->swatch = g_project->swatches->buffer[0];
	}
	ui_dock_redraw(TAB_AREA_STATUS);
}

void history_swap_swatch(history_step_t *step) {
	swatch_color_t *s       = g_project->swatches->buffer[step->swatch];
	swatch_color_t  t       = *s;
	*s                      = *step->swatch_color;
	*step->swatch_color     = t;
	g_context->swatch       = s;
	g_context->picked_color = util_clone_swatch_color(s);
	ui_dock_redraw(TAB_AREA_STATUS);
	ui_header_handle->redraws = 2;
}

void history_swap_object_transform(history_step_t *step) {
	object_t    *o = g_project->_->paint_objects->buffer[step->object]->base;
	transform_t *t = o->transform;

	vec4_t loc         = t->loc;
	quat_t rot         = t->rot;
	vec4_t scale       = t->scale;
	t->loc             = step->object_loc;
	t->rot             = step->object_rot;
	t->scale           = step->object_scale;
	step->object_loc   = loc;
	step->object_rot   = rot;
	step->object_scale = scale;

	transform_build_matrix(t);
	transform_compute_dim(t);

	physics_body_t *pb = o->_->body;
	if (pb != NULL) {
		physics_body_sync_transform(pb);
	}
	util_mesh_transform_changed();
	context_select_paint_object(g_project->_->paint_objects->buffer[step->object]);
	ui_header_handle->redraws = 2;
}

static node_group_t_array_t *history_groups(history_step_t *step) {
	return step->canvas_type == CANVAS_TYPE_MESH ? g_project->_->mesh_groups : g_project->_->material_groups;
}

ui_node_canvas_t *history_get_canvas(history_step_t *step) {
	if (step->canvas_group == -1) {
		return g_project->_->materials->buffer[step->material]->canvas;
	}
	else {
		return history_groups(step)->buffer[step->canvas_group]->canvas;
	}
}

void history_set_canvas(history_step_t *step, ui_node_canvas_t *canvas) {
	if (step->canvas_group == -1) {
		g_project->_->materials->buffer[step->material]->canvas = canvas;
	}
	else {
		history_groups(step)->buffer[step->canvas_group]->canvas = canvas;
	}
}

void history_swap_canvas(history_step_t *step) {
	if (step->canvas_type == 0) {
		ui_node_canvas_t *_canvas = history_get_canvas(step);
		history_set_canvas(step, step->canvas);
		step->canvas        = _canvas;
		g_context->material = g_project->_->materials->buffer[step->material];
	}
	else if (step->canvas_type == CANVAS_TYPE_MESH && step->canvas_group >= 0) {
		node_group_t     *g       = g_project->_->mesh_groups->buffer[step->canvas_group];
		ui_node_canvas_t *_canvas = history_get_canvas(step);
		history_set_canvas(step, step->canvas);
		step->canvas                        = _canvas;
		g->nodes->nodes_selected_id->length = 0;
		nodes_mesh_update_all();
		ui_nodes_hwnd->redraws = 2;
		return;
	}
	else if (step->canvas_type == CANVAS_TYPE_MESH) {
		if (step->object < 0 || step->object >= g_project->_->paint_objects->length) {
			return;
		}
		mesh_object_t    *o                    = g_project->_->paint_objects->buffer[step->object];
		mesh_nodes_t     *slot                 = nodes_mesh_get(o, true);
		ui_node_canvas_t *_canvas              = slot->canvas;
		slot->canvas                           = step->canvas;
		step->canvas                           = _canvas;
		g_context->paint_object                = o;
		slot->nodes->nodes_selected_id->length = 0;
		nodes_mesh_update(o, false);
		ui_nodes_hwnd->redraws = 2;
		return;
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
			g_project->_->layers->buffer[step->layer - 1]->parent = g_context->layer->parent;
			slot_layer_delete(g_context->layer);
			g_context->layer = g_project->_->layers->buffer[step->layer > 0 ? step->layer - 1 : 0];
		}
		else if (step->action == HISTORY_ACTION_DELETE_LAYER) {
			history_swap_merge(step, true);
		}
		else if (step->action == HISTORY_ACTION_CLEAR_LAYER) {
			history_undo_i    = history_undo_i - 1 < 0 ? g_config->undo_steps - 1 : history_undo_i - 1;
			slot_layer_t *lay = history_undo_slot(history_undo_i);
			slot_layer_swap(g_context->layer, lay);
			g_context->layer_preview_dirty = true;
		}
		else if (step->action == HISTORY_ACTION_DUPLICATE_LAYER) {
			i32 position     = step->layer + 1 + slot_layer_count_descendants(g_project->_->layers->buffer[step->layer]);
			g_context->layer = g_project->_->layers->buffer[position];
			slot_layer_delete(g_context->layer);
		}
		else if (step->action == HISTORY_ACTION_ORDER_LAYERS) {
			history_swap_order(step);
		}
		else if (step->action == HISTORY_ACTION_MERGE_LAYERS) {
			history_swap_merge(step, true);
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
				slot_layer_t *old_layer = history_undo_slot(history_undo_i);
				slot_layer_swap(g_context->layer, old_layer);
			}

			// Now restore the applied mask
			history_undo_i     = history_undo_i - 1 < 0 ? g_config->undo_steps - 1 : history_undo_i - 1;
			slot_layer_t *mask = history_undo_slot(history_undo_i);
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
			slot_layer_t *lay = history_undo_slot(history_undo_i);
			context_set_layer(g_project->_->layers->buffer[step->layer]);
			slot_layer_swap(g_context->layer, lay);
			layers_new_mask(false, g_context->layer, -1);
			slot_layer_swap(g_context->layer, lay);
			g_context->layer_preview_dirty = true;
		}
		else if (step->action == HISTORY_ACTION_TO_FILL_LAYER || step->action == HISTORY_ACTION_TO_FILL_MASK) {
			slot_layer_to_paint_layer(g_context->layer);
			history_undo_i    = history_undo_i - 1 < 0 ? g_config->undo_steps - 1 : history_undo_i - 1;
			slot_layer_t *lay = history_undo_slot(history_undo_i);
			slot_layer_swap(g_context->layer, lay);
		}
		else if (step->action == HISTORY_ACTION_TO_PAINT_LAYER || step->action == HISTORY_ACTION_TO_PAINT_MASK) {
			history_undo_i    = history_undo_i - 1 < 0 ? g_config->undo_steps - 1 : history_undo_i - 1;
			slot_layer_t *lay = history_undo_slot(history_undo_i);
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
		else if (step->action == HISTORY_ACTION_LAYER_VISIBLE || step->action == HISTORY_ACTION_LAYER_NAME || step->action == HISTORY_ACTION_LAYER_OBJECT ||
		         step->action == HISTORY_ACTION_LAYER_SCALE || step->action == HISTORY_ACTION_LAYER_ANGLE || step->action == HISTORY_ACTION_LAYER_UV_TYPE) {
			history_swap_layer_props(step);
		}
		else if (step->action == HISTORY_ACTION_NEW_BRUSH || step->action == HISTORY_ACTION_DUPLICATE_BRUSH) {
			history_delete_brush_slot(step);
		}
		else if (step->action == HISTORY_ACTION_DELETE_BRUSH) {
			history_new_brush_slot(step);
		}
		else if (step->action == HISTORY_ACTION_NEW_SWATCH || step->action == HISTORY_ACTION_DUPLICATE_SWATCH) {
			history_delete_swatch_slot(step);
		}
		else if (step->action == HISTORY_ACTION_DELETE_SWATCH) {
			history_new_swatch_slot(step);
		}
		else if (step->action == HISTORY_ACTION_REPLACE_SWATCHES) {
			history_swap_swatches(step);
		}
		else if (step->action == HISTORY_ACTION_EDIT_SWATCH) {
			history_swap_swatch(step);
		}
		else if (step->action == HISTORY_ACTION_OBJECT_TRANSFORM) {
			history_swap_object_transform(step);
		}
		else if (step->action == HISTORY_ACTION_DELETE_NODE_GROUP) {
			node_group_t *ng = ALLOC_INIT(node_group_t, {.canvas = NULL, .nodes = ui_nodes_create()});
			array_insert(history_groups(step), step->canvas_group, ng);
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
			slot_layer_t *lay = history_undo_slot(history_undo_i);
			context_select_paint_object(g_project->_->paint_objects->buffer[step->object]);
			context_set_layer(g_project->_->layers->buffer[step->layer]);
			slot_layer_swap(g_context->layer, lay);
			g_context->layer_preview_dirty = true;
		}

		history_undos--;
		history_redos++;
		if (step->with_prev) {
			history_undo();
		}
		g_context->rtdirty = 1;
		g_context->ddirty  = 2;

		ui_dock_redraw(TAB_AREA_SIDEBAR0);
		ui_dock_redraw(TAB_AREA_SIDEBAR1);
		if (ui_view2d_show) {
			ui_view2d_hwnd->redraws = 2;
		}

		if (g_config->touch_ui) {
			// Refresh undo & redo buttons
			ui_menubar_menu_handle->redraws = 2;
		}
	}
	nodes_mesh_request_refresh(); // Layer masks used by mesh nodes
	base_redraw_status();
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

slot_layer_t *history_undo_slot(i32 i) {
	slot_layer_t *l = history_undo_layers->buffer[i];
	slot_layer_alloc_textures(l);
	return l;
}

void history_copy_to_undo(i32 from_id, i32 to_id, bool is_mask) {
	char *to_id_s   = i32_to_string(to_id);
	char *from_id_s = i32_to_string(from_id);
	if (history_undo_layers != NULL && to_id < history_undo_layers->length) {
		history_undo_slot(to_id);
	}
	if (is_mask) {
		render_path_set_target(string_tmp("texpaint_undo%s", to_id_s), NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
		render_path_bind_target(string("texpaint%s", from_id_s), "tex");
		// render_path_draw_shader("Scene/copy_pass/copyR8_pass");
		render_path_draw_shader("Scene/copy_pass/copy_pass");
	}
	else if (g_context->layer->texpaint_sculpt != NULL) {
		render_path_set_target(string_tmp("texpaint_sculpt_undo%s", to_id_s), NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
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
		render_path_set_target(string_tmp("texpaint_undo%s", to_id_s), additional, NULL, GPU_CLEAR_NONE, 0, 0.0);
		render_path_bind_target(string("texpaint%s", from_id_s), "tex0");
		render_path_bind_target(string("texpaint_nor%s", from_id_s), "tex1");
		render_path_bind_target(string("texpaint_pack%s", from_id_s), "tex2");

		gpu_texture_format_t format = base_bits == TEXTURE_BITS_BITS8    ? GPU_TEXTURE_FORMAT_RGBA32
		                              : base_bits == TEXTURE_BITS_BITS16 ? GPU_TEXTURE_FORMAT_RGBA64
		                                                                 : GPU_TEXTURE_FORMAT_RGBA128;

		char *pipe = format == GPU_TEXTURE_FORMAT_RGBA32   ? "copy_mrt3_pass"
		             : format == GPU_TEXTURE_FORMAT_RGBA64 ? "copy_mrt3RGBA64_pass"
		                                                   : "copy_mrt3RGBA128_pass";
		render_path_draw_shader(string_tmp("Scene/copy_mrt3_pass/%s", pipe));
	}
	history_undo_i = (history_undo_i + 1) % g_config->undo_steps;
}

void history_copy_merging_layers2(slot_layer_t_array_t *layers) {
	for (i32 i = 0; i < layers->length; ++i) {
		slot_layer_t *layer = layers->buffer[i];
		history_copy_to_undo(layer->id, history_undo_i, slot_layer_is_mask(layer));
	}
}

void history_redo_duplicate_layer(void *_) {
	layers_duplicate_layer(g_context->layer);
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
	slot_layer_t *undo_layer = history_undo_slot(history_undo_i);
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
			group->parent = l->parent;
			l->parent     = group;
			context_set_layer(group);
		}
		else if (step->action == HISTORY_ACTION_DELETE_LAYER) {
			history_swap_merge(step, false);
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
			history_swap_order(step);
		}
		else if (step->action == HISTORY_ACTION_MERGE_LAYERS) {
			history_swap_merge(step, false);
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
			slot_layer_t *lay = history_undo_slot(history_undo_i);
			context_set_layer(g_project->_->layers->buffer[step->layer]);
			slot_layer_swap(g_context->layer, lay);
			layers_new_mask(false, lay, -1);
			slot_layer_swap(g_context->layer, lay);
			g_context->layer_preview_dirty = true;
			history_undo_i                 = (history_undo_i + 1) % g_config->undo_steps;
		}
		else if (step->action == HISTORY_ACTION_TO_FILL_LAYER || step->action == HISTORY_ACTION_TO_FILL_MASK) {
			slot_layer_t *lay = history_undo_slot(history_undo_i);
			slot_layer_swap(g_context->layer, lay);
			g_context->layer->fill_material = g_project->_->materials->buffer[step->material];
			history_undo_i                  = (history_undo_i + 1) % g_config->undo_steps;
		}
		else if (step->action == HISTORY_ACTION_TO_PAINT_LAYER || step->action == HISTORY_ACTION_TO_PAINT_MASK) {
			slot_layer_to_paint_layer(g_context->layer);
			slot_layer_t *lay = history_undo_slot(history_undo_i);
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
		else if (step->action == HISTORY_ACTION_LAYER_VISIBLE || step->action == HISTORY_ACTION_LAYER_NAME || step->action == HISTORY_ACTION_LAYER_OBJECT ||
		         step->action == HISTORY_ACTION_LAYER_SCALE || step->action == HISTORY_ACTION_LAYER_ANGLE || step->action == HISTORY_ACTION_LAYER_UV_TYPE) {
			history_swap_layer_props(step);
		}
		else if (step->action == HISTORY_ACTION_NEW_BRUSH || step->action == HISTORY_ACTION_DUPLICATE_BRUSH) {
			history_new_brush_slot(step);
		}
		else if (step->action == HISTORY_ACTION_DELETE_BRUSH) {
			history_delete_brush_slot(step);
		}
		else if (step->action == HISTORY_ACTION_NEW_SWATCH || step->action == HISTORY_ACTION_DUPLICATE_SWATCH) {
			history_new_swatch_slot(step);
		}
		else if (step->action == HISTORY_ACTION_DELETE_SWATCH) {
			history_delete_swatch_slot(step);
		}
		else if (step->action == HISTORY_ACTION_REPLACE_SWATCHES) {
			history_swap_swatches(step);
		}
		else if (step->action == HISTORY_ACTION_EDIT_SWATCH) {
			history_swap_swatch(step);
		}
		else if (step->action == HISTORY_ACTION_OBJECT_TRANSFORM) {
			history_swap_object_transform(step);
		}
		else if (step->action == HISTORY_ACTION_DELETE_NODE_GROUP) {
			history_swap_canvas(step);
			array_remove(history_groups(step), history_groups(step)->buffer[step->canvas_group]);
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
			slot_layer_t *lay = history_undo_slot(history_undo_i);
			context_select_paint_object(g_project->_->paint_objects->buffer[step->object]);
			context_set_layer(g_project->_->layers->buffer[step->layer]);
			slot_layer_swap(g_context->layer, lay);
			g_context->layer_preview_dirty = true;
			history_undo_i                 = (history_undo_i + 1) % g_config->undo_steps;
		}

		history_undos++;
		history_redos--;
		if (history_redos > 0 && history_steps->buffer[history_steps->length - history_redos]->with_prev) {
			history_redo();
		}
		g_context->rtdirty = 1;
		g_context->ddirty  = 2;

		ui_dock_redraw(TAB_AREA_SIDEBAR0);
		ui_dock_redraw(TAB_AREA_SIDEBAR1);
		if (ui_view2d_show) {
			ui_view2d_hwnd->redraws = 2;
		}

		if (g_config->touch_ui) {
			// Refresh undo & redo buttons
			ui_menubar_menu_handle->redraws = 2;
		}
	}
	nodes_mesh_request_refresh(); // Layer masks used by mesh nodes
	base_redraw_status();
}

void history_reset() {
	if (history_steps != NULL) {
		for (i32 i = 0; i < history_steps->length; ++i) {
			history_step_free(history_steps->buffer[i]);
		}
		array_free(history_steps);
		free(history_steps);
	}
	history_steps = any_array_create_from_raw(
	    (void *[]){
	        ALLOC_INIT(history_step_t,
	                   {.name = tr("New"), .layer = 0, .layer_type = LAYER_SLOT_TYPE_LAYER, .layer_parent = -1, .object = 0, .material = 0, .brush = 0}),
	    },
	    1);
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
			history_step_free(array_pop(history_steps));
		}
		history_redos = 0;
	}

	i32 opos = array_index_of(g_project->_->paint_objects, g_context->paint_object);
	i32 lpos = array_index_of(g_project->_->layers, g_context->layer);
	i32 mpos = array_index_of(g_project->_->materials, g_context->material);
	i32 bpos = array_index_of(g_project->_->brushes, g_context->brush);

	history_step_t *step =
	    ALLOC_INIT(history_step_t, {.name           = name,
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
		history_step_free(array_shift(history_steps));
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

	// Erasing the bottom layer makes the mesh see-through
	if (g_context->tool == TOOL_TYPE_ERASER && g_context->layer == g_project->_->layers->buffer[0]) {
		layers_enable_translucency();
	}
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

static slot_layer_t *history_emptied_root(slot_layer_t *l) {
	slot_layer_t *top = l;
	if (slot_layer_is_mask(l) || slot_layer_is_filter(l)) {
		return top;
	}
	while (top->parent != NULL) {
		slot_layer_t_array_t *children = slot_layer_get_children(top->parent);
		for (i32 i = 0; children != NULL && i < children->length; ++i) {
			slot_layer_t *c = children->buffer[i];
			if (c != top && !slot_layer_is_descendant(c, top)) {
				return top;
			}
		}
		top = top->parent;
	}
	return top;
}

history_step_t *history_delete_layer(slot_layer_t *l) {
	slot_layer_t_array_t *layers = g_project->_->layers;
	slot_layer_t         *top    = history_emptied_root(l);
	i32                   end    = array_index_of(layers, top);
	i32                   start  = end - slot_layer_count_descendants(top);
	history_step_t       *step   = history_push(HISTORY_ACTION_DELETE_LAYER);
	step->merge_layers           = any_array_create_from_raw((void *[]){}, 0);
	step->merge_parents          = i32_array_create(0);
	step->merge_start            = start;
	step->merge_length           = layers->length;
	for (i32 i = start; i <= end; ++i) {
		slot_layer_t *c = layers->buffer[i];
		any_array_push(step->merge_layers, c);
		i32_array_push(step->merge_parents, c->parent != NULL ? array_index_of(layers, c->parent) : -1);
	}
	for (i32 i = 0; i < step->merge_layers->length; ++i) {
		array_remove(layers, step->merge_layers->buffer[i]);
	}
	tab_timeline_sync();
	context_set_layer(layers->buffer[start > 0 ? start - 1 : 0]);
	return step;
}

void history_clear_layer() {
	history_swap_active();
	history_push(HISTORY_ACTION_CLEAR_LAYER);
}

void history_order_layers(i32 prev_order) {
	history_step_t *step = history_push(HISTORY_ACTION_ORDER_LAYERS);
	step->prev_order     = prev_order;
}

static void history_merge_capture(slot_layer_t *top, i32 start) {
	slot_layer_t_array_t *layers = g_project->_->layers;
	history_step_t       *step   = history_push(HISTORY_ACTION_MERGE_LAYERS);
	i32                   end    = array_index_of(layers, top);
	step->merge_layers           = any_array_create_from_raw((void *[]){}, 0);
	step->merge_parents          = i32_array_create(0);
	step->merge_start            = start;
	step->merge_length           = layers->length;
	for (i32 i = start; i <= end; ++i) {
		slot_layer_t *l = layers->buffer[i];
		any_array_push(step->merge_layers, slot_layer_clone(l));
		i32_array_push(step->merge_parents, l->parent != NULL ? array_index_of(layers, l->parent) : -1);
	}
}

void history_merge_layers() {
	slot_layer_t_array_t *layers = g_project->_->layers;
	slot_layer_t         *l1     = g_context->layer;
	slot_layer_t         *l0     = layers->buffer[array_index_of(layers, l1) - slot_layer_count_descendants(l1) - 1];
	history_merge_capture(l1, array_index_of(layers, l0) - slot_layer_count_descendants(l0));
}

void history_merge_group(slot_layer_t *l) {
	slot_layer_t *top = slot_layer_shows(l) ? l : history_emptied_root(l);
	history_merge_capture(top, array_index_of(g_project->_->layers, top) - slot_layer_count_descendants(top));
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

void history_layer_visible(slot_layer_t *l) {
	slot_layer_t *prev = g_context->layer;
	g_context->layer   = l;
	history_push(HISTORY_ACTION_LAYER_VISIBLE);
	g_context->layer = prev;
}

void history_layer_name(slot_layer_t *l, char *prev_name) {
	slot_layer_t *prev   = g_context->layer;
	g_context->layer     = l;
	history_step_t *step = history_push(HISTORY_ACTION_LAYER_NAME);
	step->layer_name     = prev_name;
	g_context->layer     = prev;
}

void history_layer_object() {
	history_push(HISTORY_ACTION_LAYER_OBJECT);
}

void history_layer_scale() {
	history_step_t *step = history_push(HISTORY_ACTION_LAYER_SCALE);
	step->layer_scale    = g_context->layer->scale;
}

void history_layer_angle() {
	history_step_t *step = history_push(HISTORY_ACTION_LAYER_ANGLE);
	step->layer_angle    = g_context->layer->angle;
}

void history_layer_uv_type() {
	history_step_t *step = history_push(HISTORY_ACTION_LAYER_UV_TYPE);
	step->layer_uv_type  = g_context->layer->uv_type;
}

void history_new_brush() {
	history_step_t *step = history_push(HISTORY_ACTION_NEW_BRUSH);
	step->canvas_type    = CANVAS_TYPE_BRUSH;
	step->canvas         = util_clone_canvas(g_context->brush->canvas);
}

void history_delete_brush() {
	history_step_t *step = history_push(HISTORY_ACTION_DELETE_BRUSH);
	step->canvas_type    = CANVAS_TYPE_BRUSH;
	step->canvas         = util_clone_canvas(g_context->brush->canvas);
}

void history_duplicate_brush() {
	history_step_t *step = history_push(HISTORY_ACTION_DUPLICATE_BRUSH);
	step->canvas_type    = CANVAS_TYPE_BRUSH;
	step->canvas         = util_clone_canvas(g_context->brush->canvas);
}

void history_object_transform(mesh_object_t *o, vec4_t loc, quat_t rot, vec4_t scale) {
	if (array_index_of(g_project->_->paint_objects, o) == -1) {
		return;
	}
	mesh_object_t *prev     = g_context->paint_object;
	g_context->paint_object = o;
	history_step_t *step    = history_push(HISTORY_ACTION_OBJECT_TRANSFORM);
	g_context->paint_object = prev;

	step->object_loc   = loc;
	step->object_rot   = rot;
	step->object_scale = scale;
}

void history_new_swatch() {
	history_step_t *step = history_push(HISTORY_ACTION_NEW_SWATCH);
	step->swatch         = array_index_of(g_project->swatches, g_context->swatch);
	step->swatch_color   = g_context->swatch;
}

void history_duplicate_swatch() {
	history_step_t *step = history_push(HISTORY_ACTION_DUPLICATE_SWATCH);
	step->swatch         = array_index_of(g_project->swatches, g_context->swatch);
	step->swatch_color   = g_context->swatch;
}

void history_delete_swatch(swatch_color_t *swatch) {
	history_step_t *step = history_push(HISTORY_ACTION_DELETE_SWATCH);
	step->swatch         = array_index_of(g_project->swatches, swatch);
	step->swatch_color   = swatch;
}

void history_replace_swatches(char *name) {
	history_step_t *step = history_push(HISTORY_ACTION_REPLACE_SWATCHES);
	step->name           = name;
	step->swatch_colors  = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < g_project->swatches->length; ++i) {
		any_array_push(step->swatch_colors, g_project->swatches->buffer[i]);
	}
}

void history_edit_swatch(i32 index, swatch_color_t *prev) {
	history_step_t *step = history_push(HISTORY_ACTION_EDIT_SWATCH);
	step->swatch         = index;
	step->swatch_color   = util_clone_swatch_color(prev);
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
	step->canvas_type    = ui_nodes_canvas_type == CANVAS_TYPE_MESH ? CANVAS_TYPE_MESH : CANVAS_TYPE_MATERIAL;
	step->canvas_group   = array_index_of(history_groups(step), group);
	step->canvas         = util_clone_canvas(group->canvas);
}
