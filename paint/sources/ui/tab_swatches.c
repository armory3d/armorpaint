
#include "../global.h"

gpu_texture_t *_tab_swatches_empty;
i32            tab_swatches_drag_pos = -1;
i32            _tab_swatches_draw_i;
swatch_color_t tab_swatches_edit_prev;

gpu_texture_t *tab_swatches_empty_get() {
	if (_tab_swatches_empty == NULL) {
		u8_array_t *b       = u8_array_create(4);
		b->buffer[0]        = 255;
		b->buffer[1]        = 255;
		b->buffer[2]        = 255;
		b->buffer[3]        = 255;
		_tab_swatches_empty = gpu_create_texture_from_bytes(b, 1, 1, GPU_TEXTURE_FORMAT_RGBA32);
		array_free(b);
		free(b);
	}
	return _tab_swatches_empty;
}

void tab_swatches_delete_swatch(swatch_color_t *swatch) {
	i32 i = array_index_of(g_project->swatches, swatch);
	history_delete_swatch(swatch);
	g_context->swatch = g_project->swatches->buffer[i == g_project->swatches->length - 1 ? i - 1 : i + 1];
	array_splice(g_project->swatches, i, 1);
	ui_base_hwnds->buffer[TAB_AREA_STATUS]->redraws = 2;
}

void tab_swatches_draw_menu() {
	i32 i = _tab_swatches_draw_i;

	if (ui_menu_button(tr("Duplicate"), "ctrl+d", ICON_DUPLICATE)) {
		g_context->swatch = project_clone_swatch(g_context->swatch);
		any_array_push(g_project->swatches, g_context->swatch);
		history_duplicate_swatch();
	}
#if defined(IRON_WINDOWS) || defined(IRON_LINUX) || defined(IRON_MACOS)
	else if (ui_menu_button(tr("Copy Hex Code"), "", ICON_HASH)) {
		i32 color = g_context->swatch->base;
		color     = color_set_ab(color, g_context->swatch->opacity * 255);
		u32 val   = color;
		iron_copy_to_clipboard(i32_to_string(val));
	}
#endif
	else if (g_project->swatches->length > 1 && ui_menu_button(tr("Delete"), "delete", ICON_DELETE)) {
		tab_swatches_delete_swatch(g_project->swatches->buffer[i]);
	}
	else if (ui_menu_button(tr("Create Material"), "", ICON_SPHERE)) {
		tab_materials_accept_swatch_drop(g_project->swatches->buffer[i]);
	}
	else if (ui_menu_button(tr("Create Color Layer"), "", ICON_LAYERS)) {
		i32 color = g_project->swatches->buffer[i]->base;
		color     = color_set_ab(color, g_project->swatches->buffer[i]->opacity * 255);
		layers_create_color_layer(color, g_project->swatches->buffer[i]->occlusion, g_project->swatches->buffer[i]->roughness,
		                          g_project->swatches->buffer[i]->metallic, -1);
	}
}

void tab_swatches_draw_color_picker_callback(swatch_color_t *color) {
	i32 i = _tab_swatches_draw_i;
	if (i >= g_project->swatches->length) {
		return;
	}
	history_edit_swatch(i, g_project->swatches->buffer[i]);
	g_project->swatches->buffer[i] = project_clone_swatch(color);
	g_context->swatch              = g_project->swatches->buffer[i];
}

void tab_swatches_draw_color_picker() {
	g_context->color_picker_previous_tool = g_context->tool;
	context_select_tool(TOOL_TYPE_PICKER);
	g_context->color_picker_callback = &tab_swatches_draw_color_picker_callback;
}

static bool tab_swatches_color_equals(swatch_color_t *a, swatch_color_t *b) {
	return a->base == b->base && a->opacity == b->opacity && a->occlusion == b->occlusion && a->roughness == b->roughness && a->metallic == b->metallic &&
	       a->normal == b->normal && a->emission == b->emission && a->height == b->height && a->subsurface == b->subsurface;
}

void tab_swatches_draw_edit_menu() {
	g_ui->changed  = false;
	ui_handle_t *h = ui_handle(__ID__);
	h->color       = g_context->swatch->base;

	g_context->swatch->base = ui_color_wheel(h, false, -1, 11 * g_theme->ELEMENT_H * UI_SCALE(), true, &tab_swatches_draw_color_picker, NULL);

	ui_handle_t *hopacity      = ui_handle(__ID__);
	hopacity->f                = g_context->swatch->opacity;
	g_context->swatch->opacity = ui_slider(hopacity, "Opacity", 0, 1, true, 100.0, true, UI_ALIGN_RIGHT, true);

	if (g_config->workflow == WORKFLOW_PBR) {
		ui_handle_t *hocclusion      = ui_handle(__ID__);
		hocclusion->f                = g_context->swatch->occlusion;
		g_context->swatch->occlusion = ui_slider(hocclusion, "Occlusion", 0, 1, true, 100.0, true, UI_ALIGN_RIGHT, true);
		ui_handle_t *hroughness      = ui_handle(__ID__);
		hroughness->f                = g_context->swatch->roughness;
		g_context->swatch->roughness = ui_slider(hroughness, "Roughness", 0, 1, true, 100.0, true, UI_ALIGN_RIGHT, true);
		ui_handle_t *hmetallic       = ui_handle(__ID__);
		hmetallic->f                 = g_context->swatch->metallic;
		g_context->swatch->metallic  = ui_slider(hmetallic, "Metallic", 0, 1, true, 100.0, true, UI_ALIGN_RIGHT, true);
		ui_handle_t *hheight         = ui_handle(__ID__);
		hheight->f                   = g_context->swatch->height;
		g_context->swatch->height    = ui_slider(hheight, "Height", 0, 1, true, 100.0, true, UI_ALIGN_RIGHT, true);
	}

	if (g_ui->changed || g_ui->is_typing) {
		ui_menu_keep_open = true;
	}
	if (g_ui->input_released) {
		g_context->swatch         = g_context->swatch; // Trigger material preview update
		g_context->picked_color   = util_clone_swatch_color(g_context->swatch);
		ui_header_handle->redraws = 2;
	}

	i32 index = array_index_of(g_project->swatches, g_context->swatch);
	if (index != -1 && !g_ui->input_down && !g_ui->is_typing && !tab_swatches_color_equals(&tab_swatches_edit_prev, g_context->swatch)) {
		history_edit_swatch(index, &tab_swatches_edit_prev);
		tab_swatches_edit_prev = *g_context->swatch;
	}
}

void tab_swatches_draw_import() {
	if (ui_menu_button(tr("Replace Existing"), "", ICON_NONE)) {
		project_import_swatches(true);
		g_context->swatch = g_project->swatches->buffer[0];
	}
	if (ui_menu_button(tr("Append"), "", ICON_NONE)) {
		project_import_swatches(false);
	}
}

void tab_swatches_draw(ui_handle_t *htab) {
	if (ui_tab(htab, tr("Swatches"), false, -1, false) && g_ui->_window_h > ui_statusbar_default_h * UI_SCALE()) {

		ui_begin_sticky();
		f32_array_t *row = f32_array_create_from_raw_tmp(
		    (f32[]){
		        -100,
		        -100,
		        -100,
		        -100,
		        -100,
		    },
		    5);
		ui_row(row);

		if (ui_icon_button(tr("New"), ICON_PLUS, UI_ALIGN_CENTER)) {
			g_context->swatch = project_make_swatch(0xffffffff);
			any_array_push(g_project->swatches, g_context->swatch);
			history_new_swatch();
		}
		if (g_ui->is_hovered) {
			ui_tooltip(tr("Add new swatch"));
		}

		if (ui_icon_button(tr("Import"), ICON_IMPORT, UI_ALIGN_CENTER)) {
			ui_menu_draw(&tab_swatches_draw_import, -1, -1);
		}
		if (g_ui->is_hovered) {
			ui_tooltip(tr("Import swatches"));
		}

		if (ui_icon_button(tr("Export"), ICON_EXPORT, UI_ALIGN_CENTER)) {
			project_export_swatches();
		}
		if (g_ui->is_hovered) {
			ui_tooltip(tr("Export swatches"));
		}

		if (ui_icon_button(tr("Clear"), ICON_ERASE, UI_ALIGN_CENTER)) {
			history_replace_swatches(tr("Clear Swatches"));
			g_context->swatch   = project_make_swatch(0xffffffff);
			g_project->swatches = any_array_create_from_raw(
			    (void *[]){
			        g_context->swatch,
			    },
			    1);
		}

		if (ui_icon_button(tr("Restore"), ICON_REPLAY, UI_ALIGN_CENTER)) {
			history_replace_swatches(tr("Restore Swatches"));
			project_set_default_swatches();
			g_context->swatch = g_project->swatches->buffer[0];
		}
		if (g_ui->is_hovered) {
			ui_tooltip(tr("Restore default swatches"));
		}

		ui_end_sticky();
		ui_separator(3, false);

		i32 slotw = math_floor(26 * UI_SCALE());
		i32 num   = math_floor(g_ui->_w / (float)(slotw + 3));
		if (num == 0) {
			return;
		}
		bool drag_pos_set = false;

		f32 uix = 0.0;
		f32 uiy = 0.0;
		for (i32 row = 0; row < math_floor(math_ceil(g_project->swatches->length / (float)num)); ++row) {
			f32_array_t *ar = f32_array_create_from_raw((f32[]){}, 0);
			for (i32 i = 0; i < num; ++i) {
				f32_array_push(ar, 1 / (float)num);
			}
			ui_row(ar);

			g_ui->_x += 2;
			if (row > 0) {
				g_ui->_y += 6;
			}

			for (i32 j = 0; j < num; ++j) {
				i32 i = j + row * num;
				if (i >= g_project->swatches->length) {
					ui_end_element_of_size(slotw);
					continue;
				}

				if (g_context->swatch == g_project->swatches->buffer[i]) {
					i32 w = 32;
					ui_fill(-2, -2, w, w, g_theme->HIGHLIGHT_COL);
				}

				uix = g_ui->_x;
				uiy = g_ui->_y;

				// Draw the drag position indicator
				if (base_drag_swatch != NULL && tab_swatches_drag_pos == i) {
					ui_fill(-1, -2, 2, 32, g_theme->HIGHLIGHT_COL);
				}

				ui_state_t state = ui_image(tab_swatches_empty_get(), g_project->swatches->buffer[i]->base, slotw);

				if (state == UI_STATE_STARTED) {
					g_context->swatch         = g_project->swatches->buffer[i];
					base_drag_off_x           = -(mouse_x - uix - g_ui->_window_x);
					base_drag_off_y           = -(mouse_y - uiy - g_ui->_window_y + 1);
					base_drag_swatch          = g_context->swatch;
					g_context->picked_color   = util_clone_swatch_color(g_context->swatch);
					ui_header_handle->redraws = 2;
				}
				else if (state == UI_STATE_HOVERED) {
					tab_swatches_drag_pos = (mouse_x > uix + g_ui->_window_x + slotw / 2.0)
					                            ? i + 1
					                            : i; // Switch to the next position if the mouse crosses the swatch rectangle center
					drag_pos_set          = true;
				}
				else if (state == UI_STATE_RELEASED) {
					if (sys_time() - g_context->select_time < 0.2) {
						_tab_swatches_draw_i   = i;
						tab_swatches_edit_prev = *g_project->swatches->buffer[i];
						ui_menu_draw(&tab_swatches_draw_edit_menu, -1, -1);
					}

					g_context->select_time = sys_time();
				}
				if (g_ui->is_hovered && g_ui->input_released_r) {
					g_context->swatch = g_project->swatches->buffer[i];

					_tab_swatches_draw_i = i;

					ui_menu_draw(&tab_swatches_draw_menu, -1, -1);
				}
				if (g_ui->is_hovered) {
					i32 color = g_project->swatches->buffer[i]->base;
					color     = color_set_ab(color, g_project->swatches->buffer[i]->opacity * 255);
					u32 val   = color;
					ui_tooltip(string_tmp("#%s", i32_to_string_hex(val)));
				}
			}
		}

		// Draw the rightmost line next to the last swatch
		if (base_drag_swatch != NULL && tab_swatches_drag_pos == g_project->swatches->length) {
			g_ui->_x = uix; // Reset the position because otherwise it would start in the row below
			g_ui->_y = uiy;
			ui_fill(28, -2, 2, 32, g_theme->HIGHLIGHT_COL);
		}

		// Currently there is no valid drag_position so reset it
		if (!drag_pos_set) {
			tab_swatches_drag_pos = -1;
		}

		bool in_focus = g_ui->input_x > g_ui->_window_x && g_ui->input_x < g_ui->_window_x + g_ui->_window_w && g_ui->input_y > g_ui->_window_y &&
		                g_ui->input_y < g_ui->_window_y + g_ui->_window_h;
		if (in_focus && g_ui->is_delete_down && g_project->swatches->length > 1) {
			g_ui->is_delete_down = false;
			tab_swatches_delete_swatch(g_context->swatch);
		}
		if (in_focus && g_ui->is_ctrl_down && g_ui->is_key_pressed && g_ui->key_code == KEY_CODE_D) {
			g_context->swatch = project_clone_swatch(g_context->swatch);
			any_array_push(g_project->swatches, g_context->swatch);
		}
		if (in_focus) {
			i32 i = array_index_of(g_project->swatches, g_context->swatch);
			if (g_ui->is_key_pressed && g_ui->key_code == KEY_CODE_UP) {
				if (i > 0) {
					g_context->swatch = g_project->swatches->buffer[i - 1];
				}
			}
			if (g_ui->is_key_pressed && g_ui->key_code == KEY_CODE_DOWN) {
				if (i < g_project->swatches->length - 1) {
					g_context->swatch = g_project->swatches->buffer[i + 1];
				}
			}
		}
	}
}

void tab_swatches_accept_swatch_drop(swatch_color_t *swatch) {
	// No valid position available
	if (tab_swatches_drag_pos == -1) {
		return;
	}

	i32 swatch_pos = array_index_of(g_project->swatches, swatch);
	// A new swatch from color picker
	if (swatch_pos == -1) {
		array_insert(g_project->swatches, tab_swatches_drag_pos, swatch);
	}
	else if (math_abs(swatch_pos - tab_swatches_drag_pos) > 0) { // Existing swatch is reordered
		array_remove(g_project->swatches, swatch);
		// If the new position is after the old one, decrease by one because the swatch has been deleted
		i32 new_pos = tab_swatches_drag_pos - swatch_pos > 0 ? tab_swatches_drag_pos - 1 : tab_swatches_drag_pos;
		array_insert(g_project->swatches, new_pos, swatch);
	}
}
