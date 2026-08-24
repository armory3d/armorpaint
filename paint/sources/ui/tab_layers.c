
#include "../global.h"

i32   tab_layers_layer_name_edit   = -1;
char *tab_layers_layer_name_prev   = NULL;
bool  tab_layers_show_context_menu = false;
bool  tab_layers_mini;

bool         tab_layers_search_show   = false;
bool         tab_layers_search_focus  = false;
ui_handle_t *tab_layers_search_handle = NULL;

void tab_layers_button_2d_view() {
	if (ui_button(tr("2D View"), UI_ALIGN_CENTER, "")) {
		ui_base_show_2d_view(VIEW_2D_TYPE_LAYER);
	}
	else if (g_ui->is_hovered) {
		ui_tooltip(string_tmp("%s (%s)", tr("Show 2D View"), (char *)any_map_get(g_keymap, "toggle_2d_view")));
	}
}

void tab_layers_repaint_text_layer(slot_layer_t *l) {
	util_layer_repaint_path(l);
	g_context->layer_preview_dirty  = true;
	g_context->layers_preview_dirty = true;
}

void tab_layers_set_drag_layer(slot_layer_t *layer, f32 off_x, f32 off_y) {
	base_drag_off_x      = off_x;
	base_drag_off_y      = off_y;
	base_drag_layer      = layer;
	g_context->drag_dest = array_index_of(g_project->_->layers, layer);
}

void tab_layers_handle_layer_icon_state(slot_layer_t *l, i32 i, ui_state_t state, f32 uix, f32 uiy) {
	gpu_texture_t *texpaint_preview = l->texpaint_preview;
	tab_layers_show_context_menu    = false;

	// Layer preview tooltip
	if (g_ui->is_hovered && texpaint_preview != NULL) {
		if (slot_layer_is_mask(l)) {
			tab_layers_make_mask_preview_rgba32(l);
			ui_tooltip_image(g_context->mask_preview_rgba32, 0);
		}
		else {
			ui_tooltip_image(texpaint_preview, 0);
		}
		if (i < 9) {
			i32 i1 = (i + 1);
			ui_tooltip(string_tmp("%s - (%s %d)", l->name, (char *)any_map_get(g_keymap, "select_layer"), i1));
		}
		else {
			ui_tooltip(l->name);
		}
	}

	// Show context menu
	if (g_ui->is_hovered && g_ui->input_released_r) {
		context_set_layer(l);
		tab_layers_show_context_menu = true;
	}

	if (state == UI_STATE_STARTED) {
		context_set_layer(l);
		tab_layers_set_drag_layer(g_context->layer, -(mouse_x - uix - g_ui->_window_x - 3), -(mouse_y - uiy - g_ui->_window_y + 1));
	}
	else if (state == UI_STATE_RELEASED) {
		if (sys_time() - g_context->select_time < 0.2) {
			ui_base_show_2d_view(VIEW_2D_TYPE_LAYER);
		}
		if (sys_time() - g_context->select_time > 0.2) {
			g_context->select_time = sys_time();
		}
		if (l->fill_material != NULL) {
			context_set_material(l->fill_material);
		}
		else if (l->path_material != NULL) {
			context_set_material(l->path_material);
		}
	}
}

ui_state_t tab_layers_draw_layer_icon(slot_layer_t *l, i32 i, f32 uix, f32 uiy, bool mini) {
	gpu_texture_t *icons  = resource_get("icons.k");
	i32            icon_h = (UI_ELEMENT_H() - (mini ? 2 : 3)) * 2;

	if (mini && UI_SCALE() > 1) {
		g_ui->_x -= 1 * UI_SCALE();
	}

	if (l->parent != NULL) {
		g_ui->_x += (icon_h - icon_h * 0.9) / 2.0;
		icon_h *= 0.9;
		if (l->parent->parent != NULL) {
			g_ui->_x += (icon_h - icon_h * 0.9) / 2.0;
			icon_h *= 0.9;
		}
	}

	if (!slot_layer_is_group(l)) {
		gpu_texture_t *texpaint_preview = l->texpaint_preview;

		gpu_texture_t *icon;
		if (l->fill_material != NULL) {
			icon = g_config->window_scale > 1 ? l->fill_material->image : l->fill_material->image_icon;
		}
		else if (l->path_material != NULL) {
			icon = g_config->window_scale > 1 ? l->path_material->image : l->path_material->image_icon;
		}
		else {
			icon = texpaint_preview;
		}

		if (l->fill_material == NULL && l->path_material == NULL) {
			// Checker
			rect_t *r  = resource_tile50(icons, ICON_CHECKER);
			f32     _x = g_ui->_x;
			f32     _y = g_ui->_y;
			f32     _w = g_ui->_w;
			ui_sub_image(icons, 0xffffffff, icon_h, r->x, r->y, r->w, r->h);
			g_ui->_x = _x;
			g_ui->_y = _y;
			g_ui->_w = _w;
		}
		if (l->fill_material == NULL && slot_layer_is_mask(l)) {
			draw_set_pipeline(ui_view2d_pipe);
			gpu_set_int(ui_view2d_channel_loc, 1);
		}

		ui_state_t state = ui_image(icon, 0xffffffff, icon_h);

		if (l->fill_material == NULL && slot_layer_is_mask(l)) {
			draw_set_pipeline(NULL);
		}

		// Draw layer numbers when selecting a layer via keyboard shortcut
		bool is_typing = g_ui->is_typing;
		if (!is_typing) {
			if (i < 9 && keymap_shortcut(any_map_get(g_keymap, "select_layer"), SHORTCUT_TYPE_DOWN)) {
				char *number = i32_to_string(i + 1);
				i32   width  = draw_string_width(g_font, g_ui->font_size, number) + 10;
				i32   height = draw_font_height(g_font, g_ui->font_size);
				draw_set_color(g_theme->TEXT_COL);
				draw_filled_rect(uix, uiy, width, height);
				draw_set_color(g_theme->BUTTON_COL);
				draw_string(number, uix + 5, uiy);
			}
		}

		return state;
	}
	else { // Group
		rect_t *folder_closed = resource_tile50(icons, ICON_FOLDER_FULL);
		rect_t *folder_open   = resource_tile50(icons, ICON_FOLDER_OPEN);
		rect_t *folder        = l->show_panel ? folder_open : folder_closed;
		return ui_sub_image(icons, base_darker(g_theme->LABEL_COL, 0x00202020), icon_h, folder->x, folder->y, folder->w, folder->h);
	}
}

void tab_layers_draw_layer_slot_mini(slot_layer_t *l, i32 i) {
	f32        uix   = g_ui->_x;
	f32        uiy   = g_ui->_y;
	ui_state_t state = tab_layers_draw_layer_icon(l, i, uix, uiy, true);
	tab_layers_handle_layer_icon_state(l, i, state, uix, uiy);
	g_ui->_x = uix;
	g_ui->_y = uiy + g_theme->ELEMENT_H * 2 * UI_SCALE();
}

void tab_layers_delete_layer(slot_layer_t *l) {
	i32_map_t *pointers = tab_layers_init_layer_map();

	if (slot_layer_is_layer(l) && slot_layer_has_masks(l, false)) {
		slot_layer_t_array_t *masks = slot_layer_get_masks(l, false);
		for (i32 i = 0; i < masks->length; ++i) {
			slot_layer_t *m  = masks->buffer[i];
			g_context->layer = m;
			history_delete_layer();
			slot_layer_delete(m);
		}
	}
	if (slot_layer_is_group(l)) {
		slot_layer_t_array_t *children = slot_layer_get_children(l);
		for (i32 i = 0; i < children->length; ++i) {
			slot_layer_t *c = children->buffer[i];
			if (slot_layer_has_masks(c, false)) {
				slot_layer_t_array_t *masks = slot_layer_get_masks(c, false);
				for (i32 i = 0; i < masks->length; ++i) {
					slot_layer_t *m  = masks->buffer[i];
					g_context->layer = m;
					history_delete_layer();
					slot_layer_delete(m);
				}
			}
			g_context->layer = c;
			history_delete_layer();
			slot_layer_delete(c);
		}
		if (slot_layer_has_masks(l, true)) {
			for (i32 i = 0; i < slot_layer_get_masks(l, true)->length; ++i) {
				slot_layer_t *m  = slot_layer_get_masks(l, true)->buffer[i];
				g_context->layer = m;
				history_delete_layer();
				slot_layer_delete(m);
			}
		}
	}

	g_context->layer = l;
	history_delete_layer();
	slot_layer_delete(l);

	if (slot_layer_is_mask(l)) {
		g_context->layer = l->parent;
		layers_update_fill_layers();
	}

	// Remove empty group
	if (slot_layer_is_in_group(l) && slot_layer_get_children(slot_layer_get_containing_group(l)) == NULL) {
		slot_layer_t *g = slot_layer_get_containing_group(l);
		// Maybe some group masks are left
		if (slot_layer_has_masks(g, true)) {
			for (i32 i = 0; i < slot_layer_get_masks(g, true)->length; ++i) {
				slot_layer_t *m  = slot_layer_get_masks(g, true)->buffer[i];
				g_context->layer = m;
				history_delete_layer();
				slot_layer_delete(m);
			}
		}
		g_context->layer = l->parent;
		history_delete_layer();
		slot_layer_delete(l->parent);
	}
	g_context->ddirty = 2;
	for (i32 i = 0; i < g_project->_->materials->length; ++i) {
		slot_material_t *m = g_project->_->materials->buffer[i];
		tab_layers_remap_layer_pointers(m->canvas->nodes, tab_layers_fill_layer_map(pointers));
	}
	tab_stages_prune();
}

void tab_layers_draw_layer_slot_full_delete_layer(void *_) {
	tab_layers_delete_layer(g_context->layer);
}

void tab_layers_combo_object_layer_clear(slot_layer_t *l) {
	g_context->material = l->fill_material;
	slot_layer_clear(l, 0x00000000, NULL, 1.0, layers_default_rough, 0.0);
	layers_update_fill_layers();
}

ui_handle_t *tab_layers_combo_object(slot_layer_t *l, bool label) {
	string_array_t *ar = any_array_create_from_raw(
	    (void *[]){
	        tr("Shared"),
	    },
	    1);
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_object_t *p = g_project->_->paint_objects->buffer[i];
		any_array_push(ar, p->base->name);
	}
	string_array_t *atlases = project_get_used_atlases();
	if (atlases != NULL) {
		for (i32 i = 0; i < atlases->length; ++i) {
			char *a = atlases->buffer[i];
			any_array_push(ar, a);
		}
	}
	ui_handle_t *object_handle = ui_nest(ui_handle(__ID__), l->id);
	object_handle->i           = l->object_mask;
	i32 prev_object_mask       = l->object_mask;
	l->object_mask             = ui_combo(object_handle, ar, tr("Object"), label, UI_ALIGN_LEFT, true);
	array_free(ar);
	free(ar);
	if (object_handle->changed) {
		i32 new_object_mask = l->object_mask;
		l->object_mask      = prev_object_mask;
		context_set_layer(l);
		history_layer_object();
		l->object_mask = new_object_mask;
		make_material_parse_mesh_material();
		if (l->fill_material != NULL) {
			sys_notify_on_next_frame(&tab_layers_combo_object_layer_clear, l);
		}
		else {
			layers_set_object_mask();
		}
	}
	return object_handle;
}

ui_handle_t *tab_layers_combo_blending(slot_layer_t *l, bool label) {
	ui_handle_t *blending_handle   = ui_nest(ui_handle(__ID__), l->id);
	blending_handle->i             = l->blending;
	string_array_t *blending_combo = any_array_create_from_raw_tmp(
	    (void *[]){
	        tr("Mix"),
	        tr("Darken"),
	        tr("Multiply"),
	        tr("Burn"),
	        tr("Lighten"),
	        tr("Screen"),
	        tr("Dodge"),
	        tr("Add"),
	        tr("Overlay"),
	        tr("Soft Light"),
	        tr("Linear Light"),
	        tr("Difference"),
	        tr("Subtract"),
	        tr("Divide"),
	        tr("Hue"),
	        tr("Saturation"),
	        tr("Color"),
	        tr("Value"),
	    },
	    18);
	ui_combo(blending_handle, blending_combo, tr("Blending"), label, UI_ALIGN_LEFT, true);
	if (blending_handle->changed) {
		context_set_layer(l);
		history_layer_blending();
		l->blending = blending_handle->i;
		make_material_parse_mesh_material();
	}
	return blending_handle;
}

void tab_layers_layer_toggle_visible(slot_layer_t *l) {
	history_layer_visible(l);
	l->visible              = !l->visible;
	ui_view2d_hwnd->redraws = 2;
	make_material_parse_mesh_material();
}

bool tab_layers_can_delete(slot_layer_t *l) {
	i32 num_layers = 0;

	if (slot_layer_is_mask(l)) {
		return true;
	}

	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *slot = g_project->_->layers->buffer[i];
		if (slot_layer_is_layer(slot)) {
			++num_layers;
		}
	}

	// All layers are in one group
	if (slot_layer_is_group(l) && slot_layer_get_children(l)->length == num_layers) {
		return false;
	}

	// Do not delete last layer
	return num_layers > 1;
}

void tab_layers_draw_layer_context_menu_duplicate(void *_) {
	slot_layer_t *l = tab_layers_l;
	context_set_layer(l);
	history_duplicate_layer();
	layers_duplicate_layer(l);
}

bool tab_layers_has_children(slot_layer_t *l) {
	return slot_layer_is_group(l) || (slot_layer_is_layer(l) && slot_layer_get_masks(l, false) != NULL) ||
	       (slot_layer_is_layer(l) && slot_layer_get_filters(l, false));
}

void tab_layers_draw_layer_slot_full(slot_layer_t *l, i32 i) {
	i32 step   = g_theme->ELEMENT_H;
	f32 center = (step / 2.0) * UI_SCALE();
	f32 uiw    = g_ui->_w;
	f32 uix    = g_ui->_x;
	f32 uiy    = g_ui->_y;

	bool has_children = tab_layers_has_children(l);

	// Draw eye icon
	f32_array_t *row = f32_array_create_from_raw_tmp(
	    (f32[]){
	        0.08,
	    },
	    1);
	ui_row(row);
	gpu_texture_t *icons = resource_get("icons.k");
	rect_t        *r     = resource_tile18(icons, l->visible ? ICON18_EYE_ON : ICON18_EYE_OFF);
	g_ui->_x             = uix + 4;
	g_ui->_y             = uiy + 3 + center;
	i32  col             = g_theme->HOVER_COL + 0x00282828;
	bool parent_hidden   = l->parent != NULL && (!l->parent->visible || (l->parent->parent != NULL && !l->parent->parent->visible));
	if (parent_hidden) {
		col -= 0x99000000;
	}
	if (ui_sub_image(icons, col, 18 * UI_SCALE(), r->x, r->y, r->w, r->h) == UI_STATE_RELEASED) {
		tab_layers_layer_toggle_visible(l);
	}

	// Nested offset
	f32 offx = 0.0;
	if (l->parent != NULL) {
		offx = 14 * UI_SCALE();
		if (l->parent->parent != NULL) {
			offx += 14 * UI_SCALE();
		}
	}

	// Layer icon
	i32 icon_h       = (UI_ELEMENT_H() - 3) * 2;
	g_ui->_x         = uix + uiw * 0.08 + offx;
	g_ui->_y         = uiy + 3;
	g_ui->_w         = math_max(uiw * 0.16, icon_h);
	ui_state_t state = tab_layers_draw_layer_icon(l, i, uix, uiy, false);
	tab_layers_handle_layer_icon_state(l, i, state, uix, uiy);

	// Draw layer name
	bool has_blending = !slot_layer_is_group(l) && !slot_layer_is_mask(l) && !slot_layer_is_filter(l);
	f32  name_x       = math_max(uix + uiw * 0.25 + offx, uix + uiw * 0.08 + offx + icon_h + 4 * UI_SCALE());
	f32  name_right   = has_blending ? uix + uiw * 0.60 : (has_children ? uix + uiw * 0.90 : uix + uiw); // Blending combo / panel / window edge
	g_ui->_x          = name_x;
	g_ui->_y          = uiy + center;
	g_ui->_w          = name_right - name_x;
	if (tab_layers_layer_name_edit == l->id) {
		tab_layers_layer_name_handle->text = string_copy(l->name);
		char *new_name                     = string_copy(ui_text_input(tab_layers_layer_name_handle, "", UI_ALIGN_LEFT, true, false));
		tab_stages_rename_layer(l->name, new_name);
		if (l->path_text && slot_layer_is_path(l) && !string_equals(l->name, new_name)) {
			sys_notify_on_next_frame(&tab_layers_repaint_text_layer, l);
		}
		l->name = new_name;
		if (g_ui->text_selected_handle != tab_layers_layer_name_handle) {
			tab_layers_layer_name_edit = -1;
			if (tab_layers_layer_name_prev != NULL && !string_equals(tab_layers_layer_name_prev, l->name)) {
				history_layer_name(l, tab_layers_layer_name_prev);
			}
		}
	}
	else {
		if (g_ui->enabled && g_ui->input_enabled && g_ui->combo_selected_handle == NULL && g_ui->input_x > g_ui->_window_x + g_ui->_x &&
		    g_ui->input_x < g_ui->_window_x + uiw && g_ui->input_y > g_ui->_window_y + g_ui->_y - center &&
		    g_ui->input_y < g_ui->_window_y + g_ui->_y - center + (step * UI_SCALE()) * 2) {
			if (g_ui->input_started) {
				context_set_layer(l);
				tab_layers_set_drag_layer(g_context->layer, -(mouse_x - uix - g_ui->_window_x - 3), -(mouse_y - uiy - g_ui->_window_y + 1));
			}
			else if (g_ui->input_released_r) {
				context_set_layer(l);
				tab_layers_show_context_menu = true;
			}
		}

		ui_state_t state = ui_text(l->name, UI_ALIGN_LEFT, 0x00000000);
		if (state == UI_STATE_RELEASED) {
			if (sys_time() - g_context->select_time < 0.2) {
				tab_layers_layer_name_edit         = l->id;
				tab_layers_layer_name_prev         = string_copy(l->name);
				tab_layers_layer_name_handle->text = string_copy(l->name);
				ui_start_text_edit(tab_layers_layer_name_handle, UI_ALIGN_LEFT);
			}
			g_context->select_time = sys_time();
		}

		bool in_focus = g_ui->input_x > g_ui->_window_x && g_ui->input_x < g_ui->_window_x + g_ui->_window_w && g_ui->input_y > g_ui->_window_y &&
		                g_ui->input_y < g_ui->_window_y + g_ui->_window_h;
		if (in_focus && !g_ui->is_typing && g_ui->is_delete_down && tab_layers_can_delete(g_context->layer)) {
			g_ui->is_delete_down = false;
			sys_notify_on_next_frame(&tab_layers_draw_layer_slot_full_delete_layer, NULL);
		}
		if (in_focus && g_ui->is_ctrl_down && g_ui->is_key_pressed && g_ui->key_code == KEY_CODE_D) {
			g_ui->is_key_pressed = false;
			tab_layers_l         = g_context->layer;
			sys_notify_on_next_frame(&tab_layers_draw_layer_context_menu_duplicate, NULL);
		}
	}

	// Blending combo
	if (!slot_layer_is_group(l) && !slot_layer_is_mask(l) && !slot_layer_is_filter(l) && l->texpaint_sculpt == NULL) {
		g_ui->_x = uix + uiw * 0.60;
		g_ui->_y = uiy;
		g_ui->_w = uiw * 0.30;
		tab_layers_combo_blending(l, false);
	}

	// Object combo
	if (!slot_layer_is_group(l) && !slot_layer_is_mask(l) && !slot_layer_is_filter(l)) {
		g_ui->_x = uix + uiw * 0.60;
		g_ui->_y = l->texpaint_sculpt != NULL ? uiy + center : uiy + center * 2;
		g_ui->_w = uiw * 0.30;
		tab_layers_combo_object(l, false);
	}

	// Panel
	if (has_children) {
		g_ui->_x                 = uix + uiw * 0.90;
		g_ui->_y                 = uiy + center;
		g_ui->_w                 = uiw * 0.15;
		ui_handle_t *layer_panel = ui_nest(ui_handle(__ID__), l->id);
		layer_panel->b           = l->show_panel;
		l->show_panel            = ui_panel(layer_panel, "", false, false, true);
	}

	g_ui->_x = uix;
	g_ui->_y = uiy + step * 2 * UI_SCALE();
	g_ui->_w = uiw;
}

void tab_layers_draw_layer_highlight(slot_layer_t *l, bool mini) {
	i32 step = g_theme->ELEMENT_H;

	// Separator line
	ui_fill(0, 0, (g_ui->_w / (float)UI_SCALE() - 2), 1 * UI_SCALE(), g_theme->SEPARATOR_COL);

	// Highlight selected
	if (g_context->layer == l) {
		if (mini) {
			ui_rect(1, -step * 2, g_ui->_w / (float)UI_SCALE() - 1, step * 2 + (mini ? -1 : 1), g_theme->HIGHLIGHT_COL, 3);
		}
		else {
			ui_rect(1, -step * 2 - 1, g_ui->_w / (float)UI_SCALE() - 2, step * 2 + (mini ? -2 : 1), g_theme->HIGHLIGHT_COL, 2);
		}
	}
}

void tab_layers_draw_layer_context_menu_export_on_file_picked(char *path) {
	slot_layer_t *l = tab_layers_l;
	char         *f = ui_files_filename;
	if (string_equals(f, "")) {
		f = string_copy(tr("untitled"));
	}
	if (!ends_with(f, ".png")) {
		f = string("%s.png", f);
	}
	iron_write_png(string("%s%s%s", path, PATH_SEP, f), gpu_get_texture_pixels(l->texpaint), l->texpaint->width, l->texpaint->height, 3); // RRR1
}

void tab_layers_draw_layer_context_menu_delete(void *_) {
	tab_layers_delete_layer(g_context->layer);
}

void tab_layers_draw_layer_context_menu_to_paint_layer(void *_) {
	slot_layer_t *l = tab_layers_l;

	if (slot_layer_is_filter(l)) {
		i32 pi = array_index_of(g_project->_->layers, l->parent);
		array_remove(g_project->_->layers, l);
		array_insert(g_project->_->layers, pi, l);
		l->parent = NULL;
	}

	slot_layer_is_layer(l) ? history_to_paint_layer() : history_to_paint_mask();
	slot_layer_to_paint_layer(l);
}

void tab_layers_draw_layer_context_menu_to_fill_layer(void *_) {
	slot_layer_t *l = tab_layers_l;
	slot_layer_is_layer(l) ? history_to_fill_layer() : history_to_fill_mask();
	slot_layer_to_fill_layer(l);
}

bool tab_layers_can_merge_down(slot_layer_t *l) {
	i32 index = array_index_of(g_project->_->layers, l);
	// Lowest layer
	if (index == 0) {
		return false;
	}
	// Lowest layer that has masks
	if (slot_layer_is_layer(l) && slot_layer_is_mask(g_project->_->layers->buffer[0]) && g_project->_->layers->buffer[0]->parent == l) {
		return false;
	}
	// The lowest toplevel layer is a group
	if (slot_layer_is_group(l) && slot_layer_is_in_group(g_project->_->layers->buffer[0]) &&
	    slot_layer_get_containing_group(g_project->_->layers->buffer[0]) == l) {
		return false;
	}
	// Masks must be merged down to masks
	if (slot_layer_is_mask(l) && !slot_layer_is_mask(g_project->_->layers->buffer[index - 1])) {
		return false;
	}
	return true;
}

void tab_layers_draw_layer_context_menu_update_fill_layers(void *_) {
	layers_update_fill_layers();
}

void tab_layers_draw_layer_context_menu_set_bits(void *_) {
	layers_set_bits();
}

void tab_layers_draw_layer_context_menu_merge_down(void *_) {
	slot_layer_t *l = tab_layers_l;
	context_set_layer(l);
	history_merge_layers();
	layers_merge_down();
	if (g_context->layer->fill_material != NULL)
		slot_layer_to_paint_layer(g_context->layer);
}

void tab_layers_draw_layer_context_menu_merge_group(void *_) {
	slot_layer_t *l = tab_layers_l;
	layers_merge_group(l);
}

void tab_layers_draw_layer_context_menu_apply(void *_) {
	slot_layer_t *l  = tab_layers_l;
	g_context->layer = l;
	history_apply_mask();
	slot_layer_apply_mask(l);
	context_set_layer(l->parent);
	make_material_parse_mesh_material();
	g_context->layers_preview_dirty = true;
}

void tab_layers_draw_layer_context_menu_apply_sculpt(void *_) {
	slot_layer_t *l  = tab_layers_l;
	g_context->layer = l;
	slot_layer_apply_sculpt(l);
}

void tab_layers_draw_layer_context_menu_invert(void *_) {
	slot_layer_t *l = tab_layers_l;
	context_set_layer(l);
	history_invert_mask();
	slot_layer_invert_mask(l);
}

void tab_layers_draw_layer_context_menu_clear(void *_) {
	slot_layer_t *l = tab_layers_l;
	if (!slot_layer_is_group(l)) {
		history_clear_layer();
		slot_layer_clear(l, slot_layer_is_mask(l) ? 0xffffffff : 0x00000000, NULL, 1.0, layers_default_rough, 0.0);
		util_layer_clear_path_points(l);
	}
	else {
		for (i32 i = 0; i < slot_layer_get_children(l)->length; ++i) {
			slot_layer_t *c  = slot_layer_get_children(l)->buffer[i];
			g_context->layer = c;
			history_clear_layer();
			slot_layer_clear(c, slot_layer_is_mask(c) ? 0xffffffff : 0x00000000, NULL, 1.0, layers_default_rough, 0.0);
		}
		g_context->layers_preview_dirty = true;
		g_context->layer                = l;
	}
}

void tab_layers_draw_layer_context_menu_draw() {
	slot_layer_t *l    = tab_layers_l;
	bool          mini = tab_layers_mini;

	if (mini) {
		ui_handle_t *visible_handle = ui_handle(__ID__);
		visible_handle->b           = l->visible;
		ui_check(visible_handle, tr("Visible"), "");
		if (visible_handle->changed) {
			tab_layers_layer_toggle_visible(l);
			ui_menu_keep_open = true;
		}

		if (!slot_layer_is_group(l) && !slot_layer_is_mask(l) && !slot_layer_is_filter(l) && l->texpaint_sculpt == NULL) {
			if (tab_layers_combo_blending(l, true)->changed) {
				ui_menu_keep_open = true;
			}
		}
		if (slot_layer_is_layer(l) && !slot_layer_is_filter(l)) {
			if (tab_layers_combo_object(l, true)->changed) {
				ui_menu_keep_open = true;
			}
		}
	}

	if (ui_menu_button(tr("Export"), "", ICON_EXPORT)) {
		if (slot_layer_is_mask(l)) {
			ui_files_show("png", true, false, &tab_layers_draw_layer_context_menu_export_on_file_picked);
		}
		else {
			g_context->layers_export = EXPORT_MODE_SELECTED;
			box_export_show_textures();
		}
	}

	if (!slot_layer_is_group(l)) {
		char *to_fill_string  = slot_layer_is_layer(l) ? tr("To Fill Layer") : tr("To Fill Mask");
		char *to_paint_string = slot_layer_is_layer(l) ? tr("To Paint Layer") : tr("To Paint Mask");
		bool  fill_layer      = l->fill_material != NULL || l->path_material != NULL;

		if (!fill_layer && ui_menu_button(to_fill_string, "", ICON_SPHERE)) {
			sys_notify_on_next_frame(&tab_layers_draw_layer_context_menu_to_fill_layer, NULL);
		}
		if (fill_layer && ui_menu_button(to_paint_string, "", ICON_PAINT)) {
			sys_notify_on_next_frame(&tab_layers_draw_layer_context_menu_to_paint_layer, NULL);
		}
	}

	if (slot_layer_is_layer(l) && l->texpaint_sculpt != NULL && ui_menu_button(tr("Apply Sculpt"), "", ICON_CHECK)) {
		sys_notify_on_next_frame(&tab_layers_draw_layer_context_menu_apply_sculpt, NULL);
	}

	g_ui->enabled = tab_layers_can_delete(l);
	if (ui_menu_button(tr("Delete"), "delete", ICON_DELETE)) {
		sys_notify_on_next_frame(&tab_layers_draw_layer_context_menu_delete, NULL);
	}
	g_ui->enabled = true;

	if (l->fill_material == NULL && ui_menu_button(tr("Clear"), "", ICON_ERASE)) {
		context_set_layer(l);
		sys_notify_on_next_frame(&tab_layers_draw_layer_context_menu_clear, NULL);
	}
	if (slot_layer_is_mask(l) && l->fill_material == NULL && ui_menu_button(tr("Invert"), "", ICON_INVERT)) {
		sys_notify_on_next_frame(&tab_layers_draw_layer_context_menu_invert, NULL);
	}
	if (slot_layer_is_mask(l) && ui_menu_button(tr("Apply"), "", ICON_CHECK)) {
		sys_notify_on_next_frame(&tab_layers_draw_layer_context_menu_apply, NULL);
	}
	if (slot_layer_is_group(l) && ui_menu_button(tr("Merge Group"), "", ICON_NONE)) {
		sys_notify_on_next_frame(&tab_layers_draw_layer_context_menu_merge_group, NULL);
	}

	g_ui->enabled = tab_layers_can_merge_down(l);
	if (ui_menu_button(tr("Merge Down"), "", ICON_ARROW_DOWN)) {
		sys_notify_on_next_frame(&tab_layers_draw_layer_context_menu_merge_down, NULL);
	}
	g_ui->enabled = true;

	if (ui_menu_button(tr("Duplicate"), "ctrl+d", ICON_DUPLICATE)) {
		sys_notify_on_next_frame(&tab_layers_draw_layer_context_menu_duplicate, NULL);
	}

	ui_menu_align();
	ui_handle_t *layer_opac_handle = ui_nest(ui_handle(__ID__), l->id);
	layer_opac_handle->f           = l->mask_opacity;
	ui_slider(layer_opac_handle, tr("Opacity"), 0.0, 1.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
	if (layer_opac_handle->changed) {
		if (g_ui->input_started) {
			history_layer_opacity();
		}
		l->mask_opacity = layer_opac_handle->f;
		make_material_parse_mesh_material();
		ui_menu_keep_open = true;
	}

	if (!slot_layer_is_group(l)) {
		ui_menu_align();
		string_array_t *ar = any_array_create_from_raw(
		    (void *[]){
		        "2048",
		        "4096",
		        "8192",
		        "16384",
		        tr("Custom"),
		    },
		    5);
		ui_combo(base_res_handle, ar, tr("Resolution"), true, UI_ALIGN_LEFT, true);
		if (base_res_handle->changed) {
			ui_menu_show_first = true;
			config_set_texture_res(base_res_handle->i);
			layers_on_resized();
		}

		if (base_res_handle->i == TEXTURE_RES_CUSTOM) {
			static bool res_was_changed = false;

			ui_menu_align();
			ui_slider(base_res_x_handle, tr("Width"), 1, 16384, false, 1, true, UI_ALIGN_RIGHT, true);
			ui_menu_align();
			ui_slider(base_res_y_handle, tr("Height"), 1, 16384, false, 1, true, UI_ALIGN_RIGHT, true);

			if (base_res_x_handle->changed || base_res_y_handle->changed) {
				ui_menu_keep_open = true;
			}
			if (res_was_changed && !g_ui->input_down) {
				res_was_changed = false;
				layers_on_resized();
			}

			if (base_res_x_handle->changed || base_res_y_handle->changed) {
				res_was_changed = true;
			}
		}

		ui_menu_align();
		ui_handle_t *huv     = ui_handle(__ID__);
		huv->i               = l->uv_map;
		string_array_t *aruv = any_array_create_from_raw(
		    (void *[]){
		        "uv0",
		    },
		    1);
		if (mesh_data_get_vertex_array(g_context->paint_object->data, "tex1") != NULL) {
			any_array_push(aruv, "uv1");
		}
		ui_combo(huv, aruv, tr("UV Map"), true, UI_ALIGN_LEFT, true);
		l->uv_map = huv->i;
		if (huv->changed) {
			make_material_parse_paint_material(true);
			make_material_parse_mesh_material();
			ui_menu_keep_open = true;
		}

#if defined(IRON_ANDROID) || defined(IRON_IOS)
// let bits_items: string[] = ["8"];
// ui_inline_radio(base_bits_handle, bits_items, ui_align_t.LEFT);
#else
		ui_menu_separator();
		ui_menu_align();
		ui_menu_label(tr("Bits"), NULL);
		ui_menu_align();
		string_array_t *bits_items = any_array_create_from_raw(
		    (void *[]){
		        "8",
		        "16",
		        "32",
		    },
		    3);
		ui_inline_radio(base_bits_handle, bits_items, UI_ALIGN_LEFT);
#endif
		if (base_bits_handle->changed) {
			sys_notify_on_next_frame(&tab_layers_draw_layer_context_menu_set_bits, NULL);
			make_material_parse_paint_material(true);
			ui_menu_keep_open = true;
		}
	}
	if (l->fill_material != NULL) {
		ui_menu_align();
		ui_handle_t *scale_handle = ui_nest(ui_handle(__ID__), l->id);
		scale_handle->f           = l->scale;
		f32 prev_scale            = l->scale;
		l->scale                  = ui_slider(scale_handle, tr("UV Scale"), 0.0, 5.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
		if (scale_handle->changed) {
			context_set_material(l->fill_material);
			context_set_layer(l);
			if (g_ui->input_started) {
				f32 new_scale = l->scale;
				l->scale      = prev_scale;
				history_layer_scale();
				l->scale = new_scale;
			}
			sys_notify_on_next_frame(&tab_layers_draw_layer_context_menu_update_fill_layers, NULL);
			ui_menu_keep_open = true;
		}

		ui_menu_align();
		ui_handle_t *angle_handle = ui_nest(ui_handle(__ID__), l->id);
		angle_handle->f           = l->angle;
		f32 prev_angle            = l->angle;
		l->angle                  = ui_slider(angle_handle, tr("Angle"), 0.0, 360, true, 1, true, UI_ALIGN_RIGHT, true);
		if (angle_handle->changed) {
			context_set_material(l->fill_material);
			context_set_layer(l);
			if (g_ui->input_started) {
				f32 new_angle = l->angle;
				l->angle      = prev_angle;
				history_layer_angle();
				l->angle = new_angle;
			}
			make_material_parse_paint_material(true);
			sys_notify_on_next_frame(&tab_layers_draw_layer_context_menu_update_fill_layers, NULL);
			ui_menu_keep_open = true;
		}

		ui_menu_align();
		ui_handle_t *uv_type_handle   = ui_nest(ui_handle(__ID__), l->id);
		uv_type_handle->i             = l->uv_type;
		string_array_t *uv_type_items = any_array_create_from_raw(
		    (void *[]){
		        tr("UV Map"),
		        tr("Triplanar"),
		        tr("Project"),
		    },
		    3);
		i32 prev_uv_type = l->uv_type;
		l->uv_type       = ui_inline_radio(uv_type_handle, uv_type_items, UI_ALIGN_LEFT);
		if (uv_type_handle->changed) {
			i32 new_uv_type = l->uv_type;
			l->uv_type      = prev_uv_type;
			context_set_material(l->fill_material);
			context_set_layer(l);
			history_layer_uv_type();
			l->uv_type = new_uv_type;
			make_material_parse_paint_material(true);
			sys_notify_on_next_frame(&tab_layers_draw_layer_context_menu_update_fill_layers, NULL);
			ui_menu_keep_open = true;
		}
	}

	if (!slot_layer_is_group(l)) {
		ui_handle_t *base_handle         = ui_nest(ui_handle(__ID__), l->id);
		ui_handle_t *opac_handle         = ui_nest(ui_handle(__ID__), l->id);
		ui_handle_t *nor_handle          = ui_nest(ui_handle(__ID__), l->id);
		ui_handle_t *nor_blend_handle    = ui_nest(ui_handle(__ID__), l->id);
		ui_handle_t *occ_handle          = ui_nest(ui_handle(__ID__), l->id);
		ui_handle_t *rough_handle        = ui_nest(ui_handle(__ID__), l->id);
		ui_handle_t *met_handle          = ui_nest(ui_handle(__ID__), l->id);
		ui_handle_t *height_handle       = ui_nest(ui_handle(__ID__), l->id);
		ui_handle_t *height_blend_handle = ui_nest(ui_handle(__ID__), l->id);
		ui_handle_t *emis_handle         = ui_nest(ui_handle(__ID__), l->id);
		ui_handle_t *subs_handle         = ui_nest(ui_handle(__ID__), l->id);
		base_handle->b                   = l->paint_base;
		opac_handle->b                   = l->paint_opac;
		nor_handle->b                    = l->paint_nor;
		nor_blend_handle->b              = l->paint_nor_blend;
		occ_handle->b                    = l->paint_occ;
		rough_handle->b                  = l->paint_rough;
		met_handle->b                    = l->paint_met;
		height_handle->b                 = l->paint_height;
		height_blend_handle->b           = l->paint_height_blend;
		emis_handle->b                   = l->paint_emis;
		subs_handle->b                   = l->paint_subs;

		ui_menu_separator();
		ui_menu_align();
		ui_menu_label(tr("Channels"), NULL);
		ui_menu_align();
		ui_row2();
		l->paint_base = ui_check(base_handle, tr("Base Color"), "");
		l->paint_opac = ui_check(opac_handle, tr("Opacity"), "");

		if (g_config->workflow == WORKFLOW_PBR) {
			ui_row2();
			l->paint_nor       = ui_check(nor_handle, tr("Normal"), "");
			l->paint_nor_blend = ui_check(nor_blend_handle, tr("Normal Blend"), "");
			ui_row2();
			l->paint_rough = ui_check(rough_handle, tr("Roughness"), "");
			l->paint_met   = ui_check(met_handle, tr("Metallic"), "");
			ui_row2();
			l->paint_height       = ui_check(height_handle, tr("Height"), "");
			l->paint_height_blend = ui_check(height_blend_handle, tr("Height Blend"), "");
			ui_row2();
			l->paint_emis = ui_check(emis_handle, tr("Emission"), "");
			l->paint_subs = ui_check(subs_handle, tr("Subsurface"), "");
			l->paint_occ  = ui_check(occ_handle, tr("Occlusion"), "");
		}

		if (base_handle->changed || opac_handle->changed || nor_handle->changed || nor_blend_handle->changed || occ_handle->changed || rough_handle->changed ||
		    met_handle->changed || height_handle->changed || height_blend_handle->changed || emis_handle->changed || subs_handle->changed) {
			make_material_parse_mesh_material();
			ui_menu_keep_open = true;
		}
	}
}

void tab_layers_draw_layer_context_menu(slot_layer_t *l, bool mini) {
	tab_layers_l    = l;
	tab_layers_mini = mini;

	ui_menu_draw(&tab_layers_draw_layer_context_menu_draw, -1, -1);
}

static bool tab_layers_slot_hidden(slot_layer_t *l) {
	stage_t *stage = tab_stages_get_stage();
	if (stage != NULL && string_array_index_of(stage->layers, l->name) < 0) {
		return true;
	}

	if (g_context->layer_filter > 0 && slot_layer_get_object_mask(l) > 0 && slot_layer_get_object_mask(l) != g_context->layer_filter) {
		return true;
	}

	bool is_sculpt_layer = l->texpaint_sculpt != NULL;
	if (g_config->workflow == WORKFLOW_SCULPT && !is_sculpt_layer && !slot_layer_is_group(l) && !slot_layer_is_mask(l)) {
		return true;
	}
	if (g_config->workflow != WORKFLOW_SCULPT && is_sculpt_layer) {
		return true;
	}

	if (l->parent != NULL && !l->parent->show_panel) { // Group closed
		return true;
	}
	if (l->parent != NULL && l->parent->parent != NULL && !l->parent->parent->show_panel) {
		return true;
	}

	if (tab_layers_search_show && tab_layers_search_handle != NULL) {
		char *search = tab_layers_search_handle->text;
		if (search != NULL && !string_equals(search, "") && string_index_of(to_lower_case(l->name), to_lower_case(search)) < 0) {
			return true;
		}
	}

	return false;
}

void tab_layers_draw_layer_slot(slot_layer_t *l, i32 i, bool mini) {
	if (tab_layers_slot_hidden(l)) {
		return;
	}

	i32 step   = g_theme->ELEMENT_H;
	f32 checkw = (g_ui->_window_w / 100.0 * 8) / (float)UI_SCALE();

	// Highlight drag destination
	f32 absy = g_ui->_window_y + g_ui->_y;
	if (base_is_dragging && base_drag_layer != NULL && context_in_layers()) {
		if (mouse_y > absy + step && mouse_y < absy + step * 3) {
			bool down                          = array_index_of(g_project->_->layers, base_drag_layer) >= i;
			g_context->drag_dest               = down ? i : i - 1;
			slot_layer_t_array_t *ls           = g_project->_->layers;
			i32                   dest         = g_context->drag_dest;
			bool                  to_group     = down ? dest > 0 && ls->buffer[dest - 1]->parent != NULL && ls->buffer[dest - 1]->parent->show_panel
			                                          : dest < ls->length && ls->buffer[dest]->parent != NULL && ls->buffer[dest]->parent->show_panel;
			bool                  nested_group = slot_layer_is_group(base_drag_layer) && to_group;
			if (!nested_group) {
				if (slot_layer_can_move(g_context->layer, g_context->drag_dest)) {
					ui_fill(checkw, step * 2, (g_ui->_window_w / (float)UI_SCALE() - 2) - checkw, 2 * UI_SCALE(), g_theme->HIGHLIGHT_COL);
				}
			}
		}
		else if (i == g_project->_->layers->length - 1 && mouse_y < absy + step) {
			g_context->drag_dest = g_project->_->layers->length - 1;
			if (slot_layer_can_move(g_context->layer, g_context->drag_dest)) {
				ui_fill(checkw, 0, (g_ui->_window_w / (float)UI_SCALE() - 2) - checkw, 2 * UI_SCALE(), g_theme->HIGHLIGHT_COL);
			}
		}
	}
	if (base_is_dragging && (base_drag_material != NULL || base_drag_swatch != NULL) && context_in_layers()) {
		if (mouse_y > absy + step && mouse_y < absy + step * 3) {
			g_context->drag_dest = i;
			if (tab_layers_can_drop_new_layer(i)) {
				ui_fill(checkw, 2 * step, (g_ui->_window_w / (float)UI_SCALE() - 2) - checkw, 2 * UI_SCALE(), g_theme->HIGHLIGHT_COL);
			}
		}
		else if (i == g_project->_->layers->length - 1 && mouse_y < absy + step) {
			g_context->drag_dest = g_project->_->layers->length;
			if (tab_layers_can_drop_new_layer(g_project->_->layers->length)) {
				ui_fill(checkw, 0, (g_ui->_window_w / (float)UI_SCALE() - 2) - checkw, 2 * UI_SCALE(), g_theme->HIGHLIGHT_COL);
			}
		}
	}
	mini ? tab_layers_draw_layer_slot_mini(l, i) : tab_layers_draw_layer_slot_full(l, i);

	tab_layers_draw_layer_highlight(l, mini);

	if (tab_layers_show_context_menu) {
		tab_layers_draw_layer_context_menu(l, mini);
	}
}

void tab_layers_draw_slots(bool mini) {
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		if (i >= g_project->_->layers->length) {
			break; // Layer was deleted
		}
		i32           j = g_project->_->layers->length - 1 - i;
		slot_layer_t *l = g_project->_->layers->buffer[j];
		tab_layers_draw_layer_slot(l, j, mini);
	}
}

void tab_layers_highlight_odd_lines() {
	i32 step   = g_theme->ELEMENT_H * 2;
	i32 full_h = g_ui->_window_h - ui_base_hwnds->buffer[0]->scroll_offset;
	for (i32 i = 0; i < math_floor(full_h / (float)step); ++i) {
		if (i % 2 == 0) {
			ui_fill(0, i * step, (g_ui->_w / (float)UI_SCALE() - 2), step, base_darker(g_theme->WINDOW_BG_COL, 0x00040404));
		}
	}
}

void tab_layers_button_new_to_fill_layer(slot_layer_t *m) {
	slot_layer_to_fill_layer(m);
}

void tab_layers_button_new_layer_clear(slot_layer_t *m) {
	slot_layer_clear(m, 0xffffffff, NULL, 1.0, layers_default_rough, 0.0);
}

void tab_layers_button_new_update_fill_layers(void *_) {
	layers_update_fill_layers();
}

void tab_layers_button_new_black_mask(slot_layer_t *m) {
	slot_layer_clear(m, 0x00000000, NULL, 1.0, layers_default_rough, 0.0);
}

void tab_layers_button_new_sculpt_layer(void *_) {
	sculpt_layers_create_sculpt_layer();
}

void tab_layers_button_new_menu() {
	slot_layer_t *l = g_context->layer;
	if (g_config->workflow == WORKFLOW_SCULPT) {
		if (ui_menu_button(tr("Sculpt Layer"), "", ICON_PAINT)) {
			sys_notify_on_next_frame(&tab_layers_button_new_sculpt_layer, NULL);
		}
	}
	else {
		if (ui_menu_button(tr("Paint Layer"), "", ICON_PAINT)) {
			layers_new_layer(true, -1, NULL);
			history_new_layer();
		}
	}
	if (ui_menu_button(tr("Fill Layer"), "", ICON_SPHERE)) {
		layers_create_fill_layer(UV_TYPE_UVMAP, mat4_nan(), -1);
	}
	if (ui_menu_button(tr("Decal"), "", ICON_DECAL)) {
		layers_create_fill_layer(UV_TYPE_PROJECT, mat4_nan(), -1);
	}
	if (ui_menu_button(tr("Text"), "", ICON_FONT)) {
		layers_new_text_layer();
		history_new_layer();
	}
	if (ui_menu_button(tr("Path"), "", ICON_PATH)) {
		layers_new_path_layer(false);
		history_new_layer();
	}
	if (ui_menu_button(tr("Curve"), "", ICON_CURVE)) {
		layers_new_path_layer(true);
		history_new_layer();
	}
	if (ui_menu_button(tr("Black Mask"), "", ICON_MASK)) {
		if (slot_layer_is_mask(l) || slot_layer_is_filter(l)) {
			context_set_layer(l->parent);
		}
		l = g_context->layer;

		i32_map_t    *pointers = tab_layers_init_layer_map();
		slot_layer_t *m        = layers_new_mask(false, l, -1);
		for (i32 i = 0; i < g_project->_->materials->length; ++i) {
			slot_material_t *mat = g_project->_->materials->buffer[i];
			tab_layers_remap_layer_pointers(mat->canvas->nodes, tab_layers_fill_layer_map(pointers));
		}
		sys_notify_on_next_frame(&tab_layers_button_new_black_mask, m);
		g_context->layer_preview_dirty = true;
		history_new_black_mask();
		sys_notify_on_next_frame(&tab_layers_button_new_update_fill_layers, NULL);
	}
	if (ui_menu_button(tr("White Mask"), "", ICON_MASK_WHITE)) {
		if (slot_layer_is_mask(l) || slot_layer_is_filter(l)) {
			context_set_layer(l->parent);
		}
		l = g_context->layer;

		i32_map_t    *pointers = tab_layers_init_layer_map();
		slot_layer_t *m        = layers_new_mask(false, l, -1);
		for (i32 i = 0; i < g_project->_->materials->length; ++i) {
			slot_material_t *mat = g_project->_->materials->buffer[i];
			tab_layers_remap_layer_pointers(mat->canvas->nodes, tab_layers_fill_layer_map(pointers));
		}
		sys_notify_on_next_frame(&tab_layers_button_new_layer_clear, m);
		g_context->layer_preview_dirty = true;
		history_new_white_mask();
		sys_notify_on_next_frame(&tab_layers_button_new_update_fill_layers, NULL);
	}
	if (ui_menu_button(tr("Fill Mask"), "", ICON_MASK_FILL)) {
		if (slot_layer_is_mask(l) || slot_layer_is_filter(l)) {
			context_set_layer(l->parent);
		}
		l = g_context->layer;

		i32_map_t    *pointers = tab_layers_init_layer_map();
		slot_layer_t *m        = layers_new_mask(false, l, -1);
		for (i32 i = 0; i < g_project->_->materials->length; ++i) {
			slot_material_t *mat = g_project->_->materials->buffer[i];
			tab_layers_remap_layer_pointers(mat->canvas->nodes, tab_layers_fill_layer_map(pointers));
		}
		sys_notify_on_next_frame(&tab_layers_button_new_to_fill_layer, m);
		g_context->layer_preview_dirty = true;
		history_new_fill_mask();
		sys_notify_on_next_frame(&tab_layers_button_new_update_fill_layers, NULL);
	}
	if (ui_menu_button(tr("Filter"), "", ICON_FILTER)) {
		if (slot_layer_is_mask(l) || slot_layer_is_filter(l)) {
			context_set_layer(l->parent);
		}
		layers_create_filter();
	}
	g_ui->enabled = !slot_layer_is_group(g_context->layer) && !slot_layer_is_in_group(g_context->layer);
	if (ui_menu_button(tr("Group"), "", ICON_FOLDER)) {
		if (slot_layer_is_group(l) || slot_layer_is_in_group(l)) {
			return;
		}

		if (slot_layer_is_layer_mask(l)) {
			l = l->parent;
		}

		i32_map_t    *pointers = tab_layers_init_layer_map();
		slot_layer_t *group    = layers_new_group();
		context_set_layer(l);
		array_remove(g_project->_->layers, group);
		array_insert(g_project->_->layers, array_index_of(g_project->_->layers, l) + 1, group);
		l->parent = group;
		for (i32 i = 0; i < g_project->_->materials->length; ++i) {
			slot_material_t *m = g_project->_->materials->buffer[i];
			tab_layers_remap_layer_pointers(m->canvas->nodes, tab_layers_fill_layer_map(pointers));
		}
		context_set_layer(group);
		history_new_group();
	}
	g_ui->enabled = true;
}

void tab_layers_button_new(char *text) {
	if (ui_icon_button(text, ICON_PLUS, UI_ALIGN_CENTER)) {
		ui_menu_draw(&tab_layers_button_new_menu, -1, -1);
	}
}

void tab_layers_combo_filter() {
	string_array_t *ar = any_array_create_from_raw(
	    (void *[]){
	        tr("All"),
	    },
	    1);
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_object_t *p = g_project->_->paint_objects->buffer[i];
		any_array_push(ar, p->base->name);
	}
	string_array_t *atlases = project_get_used_atlases();
	if (atlases != NULL) {
		for (i32 i = 0; i < atlases->length; ++i) {
			char *a = atlases->buffer[i];
			any_array_push(ar, a);
		}
	}
	ui_handle_t *filter_handle = ui_handle(__ID__);
	filter_handle->i           = g_context->layer_filter;
	g_context->layer_filter    = ui_combo(filter_handle, ar, tr("Filter"), false, UI_ALIGN_LEFT, true);
	array_free(ar);
	free(ar);
	if (filter_handle->changed) {
		tab_layers_apply_filter(g_context->layer_filter);
	}
}

void tab_layers_draw_mini(ui_handle_t *htab) {
	ui_set_hovered_tab_name(tr("Layers"));

	i32 _ELEMENT_H     = g_theme->ELEMENT_H;
	g_theme->ELEMENT_H = math_floor(ui_sidebar_w_mini / 2.0 / (float)UI_SCALE());

	ui_begin_sticky();
	ui_separator(5, true);

	tab_layers_combo_filter();
	tab_layers_button_2d_view();
	tab_layers_button_new("");

	ui_end_sticky();
	g_ui->_y += 2;

	tab_layers_highlight_odd_lines();
	tab_layers_draw_slots(true);

	g_theme->ELEMENT_H = _ELEMENT_H;
}

static void tab_layers_scroll_to_layer(slot_layer_t *l) {
	i32 row = 0;
	for (i32 i = g_project->_->layers->length - 1; i >= 0; --i) {
		slot_layer_t *c = g_project->_->layers->buffer[i];
		if (tab_layers_slot_hidden(c)) {
			continue;
		}
		if (c == l) {
			break;
		}
		++row;
	}

	// Scroll the row into view
	f32 slot_h = g_theme->ELEMENT_H * 2 * UI_SCALE();
	f32 top    = g_ui->window_header_h + g_ui->current_window->scroll_offset + 2 + row * slot_h;
	if (top < g_ui->window_header_h) {
		g_ui->current_window->scroll_offset += g_ui->window_header_h - top;
	}
	else if (top + slot_h > g_ui->_window_h) {
		g_ui->current_window->scroll_offset -= top + slot_h - g_ui->_window_h;
	}
}

void tab_layers_draw_full(ui_handle_t *htab) {
	if (ui_tab(htab, tr("Layers"), false, -1, false)) {
		bool in_window = ui_input_in_rect(g_ui->_window_x, g_ui->_window_y, g_ui->_window_w, g_ui->_window_h);

		if (in_window && g_ui->is_ctrl_down && g_ui->is_key_pressed && g_ui->key_code == KEY_CODE_F) {
			tab_layers_search_show  = true;
			tab_layers_search_focus = true;
			g_ui->is_key_pressed    = false;
			g_ui->key_code          = 0;
		}

		ui_begin_sticky();
		f32_array_t *row = f32_array_create_from_raw_tmp(
		    (f32[]){
		        -70,
		        -70,
		        -140,
		    },
		    3);
		ui_row(row);

		tab_layers_button_new(tr("New"));
		tab_layers_button_2d_view();
		tab_layers_combo_filter();

		tab_layers_search_handle = ui_handle(__ID__);
		if (tab_layers_search_show) {
			bool search_selected           = g_ui->text_selected_handle == tab_layers_search_handle;
			tab_layers_search_handle->text = string_copy(ui_text_input(tab_layers_search_handle, tr("Search"), UI_ALIGN_LEFT, true, true));
			if (tab_layers_search_focus) { // Ctrl+f to open
				tab_layers_search_focus = false;
				ui_start_text_edit(tab_layers_search_handle, UI_ALIGN_LEFT);
				g_ui->cursor_x         = string_length(tab_layers_search_handle->text);
				g_ui->highlight_anchor = 0;
			}
			if ((search_selected || in_window) && g_ui->is_escape_down) {
				tab_layers_search_show         = false;
				tab_layers_search_handle->text = "";
			}
		}

		ui_end_sticky();
		g_ui->_y += 2;

		tab_layers_highlight_odd_lines();
		tab_layers_draw_slots(false);

		if (in_window && !g_ui->is_typing) {
			// Layer selection
			if (g_ui->is_key_pressed && g_ui->key_code == KEY_CODE_UP) {
				i32 i = array_index_of(g_project->_->layers, g_context->layer);
				while (++i < g_project->_->layers->length) {
					slot_layer_t *candidate = g_project->_->layers->buffer[i];
					if (tab_layers_slot_hidden(candidate))
						continue;
					context_set_layer(candidate);
					ui_base_hwnds->buffer[TAB_AREA_SIDEBAR0]->redraws = 2;
					tab_layers_scroll_to_layer(candidate);
					break;
				}
			}
			if (g_ui->is_key_pressed && g_ui->key_code == KEY_CODE_DOWN) {
				i32 i = array_index_of(g_project->_->layers, g_context->layer);
				while (--i >= 0) {
					slot_layer_t *candidate = g_project->_->layers->buffer[i];
					if (tab_layers_slot_hidden(candidate))
						continue;
					context_set_layer(candidate);
					ui_base_hwnds->buffer[TAB_AREA_SIDEBAR0]->redraws = 2;
					tab_layers_scroll_to_layer(candidate);
					break;
				}
			}
			// Open / close group
			slot_layer_t *l            = g_context->layer;
			bool          has_children = tab_layers_has_children(l);
			if (has_children && g_ui->is_key_pressed && g_ui->key_code == KEY_CODE_RETURN) {
				l->show_panel                                     = !l->show_panel;
				ui_base_hwnds->buffer[TAB_AREA_SIDEBAR0]->redraws = 2;
			}
		}
	}
}

void tab_layers_draw(ui_handle_t *htab) {
	bool mini = g_config->layout->buffer[LAYOUT_SIZE_SIDEBAR_W] <= ui_sidebar_w_mini;
	mini ? tab_layers_draw_mini(htab) : tab_layers_draw_full(htab);
}

bool tab_layers_can_drop_new_layer(i32 position) {
	if (position > 0 && position < g_project->_->layers->length && slot_layer_is_mask(g_project->_->layers->buffer[position - 1])) {
		// 1. The layer to insert is inserted in the middle
		// 2. The layer below is a mask, i.e. the layer would have to be a (group) mask, too.
		return false;
	}
	return true;
}
