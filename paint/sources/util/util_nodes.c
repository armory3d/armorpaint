
#include "../global.h"

void ui_nodes_get_linked_nodes(ui_node_t_array_t *linked_nodes, ui_node_t *n, ui_node_canvas_t *c) {
	if (array_index_of(linked_nodes, n) == -1 && (n->flags & UI_NODE_FLAG_PREVIEW)) {
		any_array_push(linked_nodes, n);
	}
	for (i32 i = 0; i < c->links->length; ++i) {
		ui_node_link_t *l = c->links->buffer[i];
		if (l->from_id == n->id) {
			ui_node_t *nn = ui_get_node(c->nodes, l->to_id);
			ui_nodes_get_linked_nodes(linked_nodes, nn, c);
		}
	}
}

bool ui_nodes_contains_node_group_recursive(node_group_t *group, char *group_name) {
	if (string_equals(group->canvas->name, group_name)) {
		return true;
	}
	for (i32 i = 0; i < group->canvas->nodes->length; ++i) {
		ui_node_t *n = group->canvas->nodes->buffer[i];
		if (string_equals(n->type, "GROUP")) {
			node_group_t *g = project_get_material_group_by_name(n->name);
			if (g != NULL && ui_nodes_contains_node_group_recursive(g, group_name)) {
				return true;
			}
		}
	}
	return false;
}

bool ui_nodes_can_place_group(char *group_name) {
	// Prevent Recursive node groups
	// The group to place must not contain the current group or a group that contains the current group
	if (ui_nodes_group_stack->length > 0) {
		for (i32 i = 0; i < ui_nodes_group_stack->length; ++i) {
			node_group_t *g = ui_nodes_group_stack->buffer[i];
			if (ui_nodes_contains_node_group_recursive(project_get_material_group_by_name(group_name), g->canvas->name))
				return false;
		}
	}
	// Group was deleted / renamed
	bool group_exists = false;
	for (i32 i = 0; i < g_project->_->material_groups->length; ++i) {
		node_group_t *group = g_project->_->material_groups->buffer[i];
		if (string_equals(group_name, group->canvas->name)) {
			group_exists = true;
		}
	}
	if (!group_exists) {
		return false;
	}
	return true;
}

gpu_texture_t *ui_nodes_get_node_preview_image(ui_node_t *n) {
	if (n == NULL) {
		return NULL;
	}
	gpu_texture_t *img = NULL;
	if (string_equals(n->type, "LAYER") || string_equals(n->type, "LAYER_MASK")) {
		i32 id = n->buttons->buffer[0]->default_value->buffer[0];
		if (id < g_project->_->layers->length) {
			img = g_project->_->layers->buffer[id]->texpaint;
		}
	}
	else if (string_equals(n->type, "MATERIAL")) {
		i32 id = n->buttons->buffer[0]->default_value->buffer[0];
		if (id < g_project->_->materials->length) {
			img = g_project->_->materials->buffer[id]->image;
		}
	}
	else if (string_equals(n->type, "OUTPUT_MATERIAL_PBR")) {
		img = g_context->material->image;
	}
	else if (string_equals(n->type, "brush_output_node")) {
		img = g_context->brush->image;
	}
	else if (string_equals(n->type, "TEX_IMAGE") && parser_material_get_input_link(n->inputs->buffer[0]) == NULL) {
		i32 i = n->buttons->buffer[0]->default_value->buffer[0];
		if (i <= 9000) { // 9999 - Texture deleted
			char *filepath    = parser_material_enum_data(base_combo_enum_texts(n->type)->buffer[i]);
			i32   asset_index = -1;
			for (i32 i = 0; i < g_project->_->assets->length; ++i) {
				if (string_equals(g_project->_->assets->buffer[i]->file, filepath)) {
					asset_index = i;
					break;
				}
			}
			if (asset_index > -1) {
				img = project_get_image(g_project->_->assets->buffer[asset_index]);
			}
		}
	}
	else if (string_equals(n->type, "TEX_BAKE")) {
		char            *rt_name = string("bake_texture_node_%d", n->id);
		render_target_t *rt      = any_map_get(render_path_render_targets, rt_name);
		if (rt != NULL) {
			img = rt->_image;
		}
	}
	else if (starts_with(n->type, "NEURAL_") && n->outputs->length > 0) {
		i32 socket = i32_imap_get(g_context->node_preview_socket_map, n->id);
		if (socket < 0 || socket >= n->outputs->length) {
			socket = 0;
		}
		img = any_imap_get(neural_node_results, n->outputs->buffer[socket]->id);
	}
	else if (ui_nodes_canvas_type == CANVAS_TYPE_MATERIAL) {
		img = any_imap_get(g_context->node_preview_map, n->id);
	}
	return img;
}

bool ui_nodes_has_group(ui_node_canvas_t *c) {
	for (i32 i = 0; i < c->nodes->length; ++i) {
		ui_node_t *n = c->nodes->buffer[i];
		if (string_equals(n->type, "GROUP")) {
			return true;
		}
	}
	return false;
}

ui_node_canvas_t *ui_nodes_get_group(ui_node_canvas_t_array_t *canvases, char *name) {
	for (i32 i = 0; i < canvases->length; ++i) {
		ui_node_canvas_t *c = canvases->buffer[i];
		if (string_equals(c->name, name)) {
			return c;
		}
	}
	return NULL;
}

void ui_nodes_traverse_group(ui_node_canvas_t_array_t *mgroups, ui_node_canvas_t *c) {
	for (i32 i = 0; i < c->nodes->length; ++i) {
		ui_node_t *n = c->nodes->buffer[i];
		if (string_equals(n->type, "GROUP")) {
			if (ui_nodes_get_group(mgroups, n->name) == NULL) {
				ui_node_canvas_t_array_t *canvases = any_array_create_from_raw((void *[]){}, 0);
				for (i32 i = 0; i < g_project->_->material_groups->length; ++i) {
					node_group_t *g = g_project->_->material_groups->buffer[i];
					any_array_push(canvases, g->canvas);
				}
				ui_node_canvas_t *group = ui_nodes_get_group(canvases, n->name);
				any_array_push(mgroups, util_clone_canvas(group));
				ui_nodes_traverse_group(mgroups, group);
			}
		}
	}
}

i32 ui_nodes_get_node_x() {
	return math_floor((mouse_x - ui_nodes_wx - UI_NODES_PAN_X()) / (float)UI_NODES_SCALE());
}

i32 ui_nodes_get_node_y() {
	return math_floor((mouse_y - ui_nodes_wy - UI_NODES_PAN_Y()) / (float)UI_NODES_SCALE());
}

bool ui_nodes_is_tab_selected() {
	return ui_nodes_htab->i > 0 && ui_nodes_htab->i % 2 == 1 && // [tab0, tab1, x, tab2, x, +]
	       ui_nodes_tabs->length >= ui_nodes_htab->i / 2.0;
}

i32 ui_nodes_tab_index() {
	return (int)(ui_nodes_htab->i / 2.0);
}

ui_node_canvas_t *ui_nodes_get_canvas(bool groups) {
	if (ui_nodes_canvas_type == CANVAS_TYPE_MATERIAL) {
		if (groups && ui_nodes_group_stack->length > 0) {
			return ui_nodes_group_stack->buffer[ui_nodes_group_stack->length - 1]->canvas;
		}
		else if (ui_nodes_is_tab_selected()) {
			return ui_nodes_tabs->buffer[ui_nodes_tab_index()]->canvas;
		}
		else {
			return g_context->material->canvas;
		}
	}
	else {
		return g_context->brush->canvas;
	}
}

ui_nodes_t *ui_nodes_get_nodes() {
	if (ui_nodes_canvas_type == CANVAS_TYPE_MATERIAL) {
		if (ui_nodes_group_stack->length > 0) {
			return ui_nodes_group_stack->buffer[ui_nodes_group_stack->length - 1]->nodes;
		}
		else if (ui_nodes_is_tab_selected()) {
			return ui_nodes_tabs->buffer[ui_nodes_tab_index()]->nodes;
		}
		else {
			return g_context->material->nodes;
		}
	}
	else {
		return g_context->brush->nodes;
	}
}

void ui_nodes_canvas_changed() {
	ui_nodes_recompile_mat       = true;
	ui_nodes_recompile_mat_final = true;
}

void ui_nodes_push_undo(ui_node_canvas_t *last_canvas) {
	if (last_canvas == NULL) {
		last_canvas = ui_nodes_get_canvas(true);
	}
	i32 canvas_group = -1;
	if (ui_nodes_group_stack->length > 0) {
		canvas_group = array_index_of(g_project->_->material_groups, ui_nodes_group_stack->buffer[ui_nodes_group_stack->length - 1]);
	}
	ui_base_hwnds->buffer[TAB_AREA_SIDEBAR0]->redraws = 2;
	history_edit_nodes(last_canvas, ui_nodes_canvas_type, canvas_group);
}

void ui_viewnodes_on_node_remove(ui_node_t *n) {
	gpu_texture_t *img = any_imap_get(g_context->node_preview_map, n->id);
	if (img != NULL) {
		gpu_delete_texture(img);
		imap_delete(g_context->node_preview_map, n->id);
	}
}

void ui_viewnodes_on_node_changed(ui_node_t *n) {
	gc_unroot(ui_nodes_node_changed);
	ui_nodes_node_changed = n;
	gc_root(ui_nodes_node_changed);
}

void ui_nodes_capture_output() {
	ui_nodes_t       *ui_nodes = ui_nodes_get_nodes();
	ui_node_canvas_t *c        = ui_nodes_get_canvas(true);
	ui_node_t        *sel      = ui_get_node(c->nodes, ui_nodes->nodes_selected_id->buffer[0]);

	bool bgra = false;
#ifdef IRON_BGRA
	bgra = string_equals(sel->type, "TEX_IMAGE") || starts_with(sel->type, "NEURAL_");
#endif

	util_texture_capture_output(ui_nodes_get_node_preview_image(sel), "node_preview", bgra);
}

void ui_viewnodes_on_canvas_capture_output(void *_) {
	ui_nodes_capture_output();
}

void ui_viewnodes_on_canvas_released_duplicate(void *_) {
	ui_nodes_hwnd->redraws   = 2;
	ui_is_copy               = true;
	ui_is_paste              = true;
	ui_nodes_is_node_menu_op = true;
}

void ui_viewnodes_on_canvas_delete_on_next_frame(void *_) {
	ui_nodes_hwnd->redraws   = 2;
	g_ui->is_delete_down     = true;
	ui_nodes_is_node_menu_op = true;
}

void ui_viewnodes_on_canvas_delete(void *_) {
	sys_notify_on_end_frame(&ui_viewnodes_on_canvas_delete_on_next_frame, NULL);
}

void ui_viewnodes_on_canvas_paste(void *_) {
	ui_nodes_hwnd->redraws   = 2;
	ui_is_paste              = true;
	ui_nodes_is_node_menu_op = true;
}

void ui_viewnodes_on_canvas_copy(void *_) {
	ui_is_copy               = true;
	ui_nodes_is_node_menu_op = true;
}

void ui_viewnodes_on_canvas_cut(void *_) {
	ui_nodes_hwnd->redraws   = 2;
	ui_is_copy               = true;
	ui_is_cut                = true;
	ui_nodes_is_node_menu_op = true;
}

void ui_nodes_recompile() {
	if (ui_nodes_recompile_mat) {
		if (ui_nodes_canvas_type == CANVAS_TYPE_BRUSH) {
			make_material_parse_brush();
			util_render_make_brush_preview();
			if (slot_layer_is_path(g_context->layer)) {
				util_layer_repaint_path(g_context->layer);
			}
			ui_base_hwnds->buffer[TAB_AREA_SIDEBAR1]->redraws = 2;
		}
		else {
			slot_material_t *_material = g_context->material;
			if (ui_nodes_is_tab_selected()) {
				g_context->material = ui_nodes_tabs->buffer[ui_nodes_tab_index()];
			}
			if (layers_is_fill_material()) {
				layers_update_fill_layers();
			}
			else if (layers_is_path_material()) {
				layers_update_path_layers();
			}
			else {
				util_render_make_material_preview();
			}

			g_context->material = _material;

			if (ui_view2d_show && ui_view2d_type == VIEW_2D_TYPE_NODE) {
				ui_view2d_hwnd->redraws = 2;
			}
		}

		ui_base_hwnds->buffer[TAB_AREA_SIDEBAR1]->redraws = 2;
		if (g_context->split_view) {
			g_context->ddirty = 2;
		}

		ui_nodes_recompile_mat = false;
	}
	else if (ui_nodes_recompile_mat_final) {
		make_material_parse_paint_material(true);

		if (ui_nodes_canvas_type == CANVAS_TYPE_MATERIAL) {
			if (layers_is_fill_material()) {
				layers_update_fill_layers();
				util_render_make_material_preview();
			}
			if (layers_is_path_material()) {
				layers_update_path_layers();
				util_render_make_material_preview();
			}
		}

		bool decal = context_is_decal();
		if (decal) {
			util_render_make_decal_preview();
		}

		ui_base_hwnds->buffer[TAB_AREA_SIDEBAR0]->redraws = 2;
		ui_nodes_recompile_mat_final                      = false;
	}
}

ui_node_t *ui_nodes_make_group_node(ui_node_canvas_t *group_canvas, ui_nodes_t *nodes, ui_node_canvas_t *canvas) {
	ui_node_t_array_t *category = nodes_material_list->buffer[5];
	ui_node_t         *n        = category->buffer[0];
	ui_node_t         *node     = util_clone_canvas_node(n);
	node->name                  = string_copy(group_canvas->name);
	node->id                    = ui_next_node_id(canvas->nodes);
	node->x                     = ui_nodes_get_node_x();
	node->y                     = ui_nodes_get_node_y();
	ui_node_t *group_input      = NULL;
	ui_node_t *group_output     = NULL;
	for (i32 i = 0; i < g_project->_->material_groups->length; ++i) {
		node_group_t *g     = g_project->_->material_groups->buffer[i];
		char         *cname = g->canvas->name;
		if (string_equals(cname, node->name)) {
			for (i32 i = 0; i < g->canvas->nodes->length; ++i) {
				ui_node_t *n = g->canvas->nodes->buffer[i];
				if (string_equals(n->type, "GROUP_INPUT")) {
					group_input = n;
				}
				else if (string_equals(n->type, "GROUP_OUTPUT")) {
					group_output = n;
				}
			}
			break;
		}
	}
	if (group_input != NULL && group_output != NULL) {
		for (i32 i = 0; i < group_input->outputs->length; ++i) {
			ui_node_socket_t *soc = group_input->outputs->buffer[i];
			any_array_push(node->inputs, nodes_material_create_socket(nodes, node, soc->name, soc->type, canvas, soc->min, soc->max, soc->default_value));
		}
		for (i32 i = 0; i < group_output->inputs->length; ++i) {
			ui_node_socket_t *soc = group_output->inputs->buffer[i];
			any_array_push(node->outputs, nodes_material_create_socket(nodes, node, soc->name, soc->type, canvas, soc->min, soc->max, soc->default_value));
		}
	}
	return node;
}

void ui_nodes_make_node_preview(ui_node_t *node) {
	g_context->node_preview_name = string_copy(node->name);

	if (string_equals(node->type, "LAYER") || string_equals(node->type, "LAYER_MASK") || string_equals(node->type, "MATERIAL") ||
	    string_equals(node->type, "OUTPUT_MATERIAL_PBR")) {
		return;
	}

	if (ui_nodes_canvas_type == CANVAS_TYPE_BRUSH) {
		return;
	}

	ui_node_canvas_t  *current_canvas = ui_nodes_get_canvas(true);
	ui_node_t_array_t *nodes          = current_canvas->nodes;
	if (array_index_of(nodes, node) == -1) {
		return;
	}

	gpu_texture_t *img = any_imap_get(g_context->node_preview_map, node->id);
	if (img == NULL) {
		img = gpu_create_render_target(util_render_node_preview_size, util_render_node_preview_size, GPU_TEXTURE_FORMAT_RGBA32);
		any_imap_set(g_context->node_preview_map, node->id, img);
	}

	ui_node_canvas_t  *group_canvas  = NULL;
	ui_node_t_array_t *group_parents = NULL;
	if (ui_nodes_group_stack->length > 0) {
		group_canvas            = ui_nodes_group_stack->buffer[ui_nodes_group_stack->length - 1]->canvas;
		group_parents           = any_array_create_from_raw((void *[]){}, 0);
		ui_node_canvas_t *outer = ui_nodes_get_canvas(false);
		for (i32 i = 0; i < ui_nodes_group_stack->length; ++i) {
			char      *gname      = ui_nodes_group_stack->buffer[i]->canvas->name;
			ui_node_t *group_node = NULL;
			for (i32 j = 0; j < outer->nodes->length; ++j) {
				ui_node_t *n = outer->nodes->buffer[j];
				if (string_equals(n->type, "GROUP") && string_equals(n->name, gname)) {
					group_node = n;
					break;
				}
			}
			if (group_node != NULL) {
				any_array_push(group_parents, group_node);
			}
			outer = ui_nodes_group_stack->buffer[i]->canvas;
		}
	}

	ui_nodes_hwnd->redraws = 2;
	util_render_make_node_preview(current_canvas, node, img, group_canvas, group_parents);
}

ui_node_t *ui_nodes_make_node(ui_node_t *n, ui_nodes_t *nodes, ui_node_canvas_t *canvas) {
	ui_node_t *node = GC_ALLOC_INIT(ui_node_t, {0});
	node->id        = ui_next_node_id(canvas->nodes);
	node->name      = string_copy(n->name);
	node->type      = string_copy(n->type);
	node->x         = ui_nodes_get_node_x();
	node->y         = ui_nodes_get_node_y();
	node->color     = n->color;
	node->inputs    = any_array_create_from_raw((void *[]){}, 0);
	node->outputs   = any_array_create_from_raw((void *[]){}, 0);
	node->buttons   = any_array_create_from_raw((void *[]){}, 0);
	node->width     = 0;
	node->flags     = g_config->node_previews ? UI_NODE_FLAG_PREVIEW : UI_NODE_FLAG_NONE;
	i32 count       = 0;
	for (i32 i = 0; i < n->inputs->length; ++i) {
		ui_node_socket_t *soc = GC_ALLOC_INIT(ui_node_socket_t, {0});
		soc->id               = ui_get_socket_id(canvas->nodes) + count;
		count++;
		soc->node_id       = node->id;
		soc->name          = string_copy(n->inputs->buffer[i]->name);
		soc->type          = string_copy(n->inputs->buffer[i]->type);
		soc->color         = n->inputs->buffer[i]->color;
		soc->default_value = f32_array_create_from_array(n->inputs->buffer[i]->default_value);
		soc->min           = n->inputs->buffer[i]->min;
		soc->max           = n->inputs->buffer[i]->max;
		soc->precision     = n->inputs->buffer[i]->precision;
		soc->display       = n->inputs->buffer[i]->display;
		any_array_push(node->inputs, soc);
	}
	for (i32 i = 0; i < n->outputs->length; ++i) {
		ui_node_socket_t *soc = GC_ALLOC_INIT(ui_node_socket_t, {0});
		soc->id               = ui_get_socket_id(canvas->nodes) + count;
		count++;
		soc->node_id       = node->id;
		soc->name          = string_copy(n->outputs->buffer[i]->name);
		soc->type          = string_copy(n->outputs->buffer[i]->type);
		soc->color         = n->outputs->buffer[i]->color;
		soc->default_value = f32_array_create_from_array(n->outputs->buffer[i]->default_value);
		soc->min           = n->outputs->buffer[i]->min;
		soc->max           = n->outputs->buffer[i]->max;
		soc->precision     = n->outputs->buffer[i]->precision;
		soc->display       = n->outputs->buffer[i]->display;
		any_array_push(node->outputs, soc);
	}
	for (i32 i = 0; i < n->buttons->length; ++i) {
		ui_node_button_t *but = GC_ALLOC_INIT(ui_node_button_t, {0});
		but->name             = string_copy(n->buttons->buffer[i]->name);
		but->type             = string_copy(n->buttons->buffer[i]->type);
		but->output           = n->buttons->buffer[i]->output;
		but->default_value    = f32_array_create_from_array(n->buttons->buffer[i]->default_value);
		if (n->buttons->buffer[i]->data != NULL) {
			but->data = u8_array_create_from_array(n->buttons->buffer[i]->data);
		}
		but->min       = n->buttons->buffer[i]->min;
		but->max       = n->buttons->buffer[i]->max;
		but->precision = n->buttons->buffer[i]->precision;
		but->height    = n->buttons->buffer[i]->height;
		any_array_push(node->buttons, but);
	}
	gc_unroot(ui_nodes_node_changed);
	ui_nodes_node_changed = node;
	gc_root(ui_nodes_node_changed);
	return node;
}
