
#include "../global.h"

i32 _tab_brushes_draw_i;
i32 tab_brushes_drag_pos = -1;

void tab_brushes_draw_make_brush_preview(void *_) {
	i32           i      = _tab_brushes_draw_i;
	slot_brush_t *_brush = g_context->brush;
	g_context->brush     = g_project->_->brushes->buffer[i];
	make_material_parse_brush();
	brush_output_node_parse_inputs();
	util_render_make_brush_preview();
	g_context->brush = _brush;
}

void tab_brushes_draw_duplicate(void *_) {
	i32 i            = _tab_brushes_draw_i;
	g_context->brush = slot_brush_create(NULL);
	any_array_push(g_project->_->brushes, g_context->brush);
	void *cloned             = util_clone_canvas(g_project->_->brushes->buffer[i]->canvas);
	g_context->brush->canvas = cloned;
	context_set_brush(g_context->brush);
	util_render_make_brush_preview();
	history_duplicate_brush();
}

void tab_brushes_delete_brush(slot_brush_t *b) {
	i32 i            = array_index_of(g_project->_->brushes, b);
	g_context->brush = b;
	history_delete_brush();
	context_select_brush(i == g_project->_->brushes->length - 1 ? i - 1 : i + 1);
	array_splice(g_project->_->brushes, i, 1);
	ui_base_hwnds->buffer[1]->redraws = 2;
}

void tab_brushes_draw_context_menu() {
	i32 i = _tab_brushes_draw_i;
	// let b: slot_brush_t = brushes[i];

	if (ui_menu_button(tr("Export"), "", ICON_EXPORT)) {
		context_select_brush(i);
		box_export_show_brush();
	}

	if (ui_menu_button(tr("Duplicate"), "ctrl+d", ICON_DUPLICATE)) {
		sys_notify_on_next_frame(&tab_brushes_draw_duplicate, NULL);
	}

	if (g_project->_->brushes->length > 1 && ui_menu_button(tr("Delete"), "delete", ICON_DELETE)) {
		tab_brushes_delete_brush(g_project->_->brushes->buffer[i]);
	}
}

void tab_brushes_draw(ui_handle_t *htab) {
	if (ui_tab(htab, tr("Brushes"), false, -1, false)) {
		ui_begin_sticky();
		f32_array_t *row = f32_array_create_from_raw_tmp(
		    (f32[]){
		        -70,
		        -70,
		        -70,
		    },
		    3);
		ui_row(row);
		if (ui_icon_button(tr("New"), ICON_PLUS, UI_ALIGN_CENTER)) {
			g_context->brush = slot_brush_create(NULL);
			any_array_push(g_project->_->brushes, g_context->brush);
			make_material_parse_brush();
			history_new_brush();
			ui_nodes_hwnd->redraws = 2;
		}
		if (ui_icon_button(tr("Import"), ICON_IMPORT, UI_ALIGN_CENTER)) {
			project_import_brush();
		}
		if (ui_button(tr("Nodes"), UI_ALIGN_CENTER, "")) {
			ui_base_show_brush_nodes();
		}
		ui_end_sticky();
		ui_separator(3, false);

		i32 slotw = math_floor(51 * UI_SCALE());
		i32 num   = math_floor(g_ui->_window_w / (float)slotw);
		if (num == 0) {
			return;
		}

		bool drag_pos_set = false;
		f32  uix          = 0.0;
		f32  uiy          = 0.0;
		i32  imgw_val     = math_floor(50 * UI_SCALE());

		for (i32 row = 0; row < math_floor(math_ceil(g_project->_->brushes->length / (float)num)); ++row) {
			i32          mult = g_config->show_asset_names ? 2 : 1;
			f32_array_t *ar   = f32_array_create_from_raw((f32[]){}, 0);
			for (i32 i = 0; i < num * mult; ++i) {
				f32_array_push(ar, 1 / (float)num);
			}
			ui_row(ar);

			g_ui->_x += 2;
			f32 off = g_config->show_asset_names ? UI_ELEMENT_OFFSET() * 10.0 : 6;
			if (row > 0) {
				g_ui->_y += off;
			}

			for (i32 j = 0; j < num; ++j) {
				i32 imgw = math_floor(50 * UI_SCALE());
				i32 i    = j + row * num;
				if (i >= g_project->_->brushes->length) {
					ui_end_element_of_size(imgw);
					if (g_config->show_asset_names) {
						ui_end_element_of_size(0);
					}
					continue;
				}
				gpu_texture_t *img      = UI_SCALE() > 1 ? g_project->_->brushes->buffer[i]->image : g_project->_->brushes->buffer[i]->image_icon;
				gpu_texture_t *img_full = g_project->_->brushes->buffer[i]->image;

				if (g_context->brush == g_project->_->brushes->buffer[i]) {
					// ui_fill(1, -2, img.width + 3, img.height + 3, ui.ops.theme.HIGHLIGHT_COL); // TODO
					i32 off = row % 2 == 1 ? 1 : 0;
					i32 w   = 50;
					if (g_config->window_scale > 1) {
						w += math_floor(g_config->window_scale * 2);
					}
					ui_fill(-1, -2, w + 3, 2, g_theme->HIGHLIGHT_COL);
					ui_fill(-1, w - off, w + 3, 2 + off, g_theme->HIGHLIGHT_COL);
					ui_fill(-1, -2, 2, w + 3, g_theme->HIGHLIGHT_COL);
					ui_fill(w + 1, -2, 2, w + 4, g_theme->HIGHLIGHT_COL);
				}

				uix      = g_ui->_x;
				uiy      = g_ui->_y;
				i32 tile = UI_SCALE() > 1 ? 100 : 50;

				if (base_drag_brush != NULL && tab_brushes_drag_pos == i) {
					ui_fill(-1, -2, 2, imgw_val + 4, g_theme->HIGHLIGHT_COL);
				}

				ui_state_t state = g_project->_->brushes->buffer[i]->preview_ready
				                       ? ui_image(img, 0xffffffff, -1.0)
				                       : ui_sub_image(resource_get("icons.k"), -1, -1.0, tile * 5, tile, tile, tile);

				if (state == UI_STATE_HOVERED && base_drag_brush != NULL) {
					tab_brushes_drag_pos = (mouse_x > uix + g_ui->_window_x + imgw_val / 2.0) ? i + 1 : i;
					drag_pos_set         = true;
				}

				if (state == UI_STATE_STARTED) {
					if (g_context->brush != g_project->_->brushes->buffer[i]) {
						context_select_brush(i);
					}
					base_drag_off_x = -(mouse_x - uix - g_ui->_window_x - 3);
					base_drag_off_y = -(mouse_y - uiy - g_ui->_window_y + 1);
					base_drag_brush = g_context->brush;
					if (sys_time() - g_context->select_time < 0.2) {
						ui_base_show_brush_nodes();
						base_drag_brush  = NULL;
						base_is_dragging = false;
					}
					g_context->select_time = sys_time();
				}
				if (g_ui->is_hovered && g_ui->input_released_r) {
					context_select_brush(i);

					_tab_brushes_draw_i = i;

					ui_menu_draw(&tab_brushes_draw_context_menu, -1, -1);
				}

				if (g_ui->is_hovered) {
					if (img_full == NULL) {
						_tab_brushes_draw_i = i;
						sys_notify_on_next_frame(&tab_brushes_draw_make_brush_preview, NULL);
					}
					else {
						ui_tooltip_image(img_full, 0);
						ui_tooltip(g_project->_->brushes->buffer[i]->canvas->name);
					}
				}

				if (g_config->show_asset_names) {
					g_ui->_x = uix;
					g_ui->_y += slotw * 0.9;
					ui_text(g_project->_->brushes->buffer[i]->canvas->name, UI_ALIGN_CENTER, 0x00000000);
					if (g_ui->is_hovered) {
						ui_tooltip(g_project->_->brushes->buffer[i]->canvas->name);
					}
					g_ui->_y -= slotw * 0.9;
					if (i == g_project->_->brushes->length - 1) {
						g_ui->_y += j == num - 1 ? imgw : imgw + UI_ELEMENT_H() + UI_ELEMENT_OFFSET();
					}
				}
			}

			g_ui->_y += 6;
		}

		if (base_drag_brush != NULL && tab_brushes_drag_pos == g_project->_->brushes->length) {
			g_ui->_x = uix;
			g_ui->_y = uiy;
			ui_fill(imgw_val + 1, -2, 2, imgw_val + 4, g_theme->HIGHLIGHT_COL);
		}

		if (!drag_pos_set) {
			tab_brushes_drag_pos = -1;
		}

		bool in_focus = g_ui->input_x > g_ui->_window_x && g_ui->input_x < g_ui->_window_x + g_ui->_window_w && g_ui->input_y > g_ui->_window_y &&
		                g_ui->input_y < g_ui->_window_y + g_ui->_window_h;
		if (in_focus && g_ui->is_delete_down && g_project->_->brushes->length > 1) {
			g_ui->is_delete_down = false;
			tab_brushes_delete_brush(g_context->brush);
		}
		if (in_focus && g_ui->is_ctrl_down && g_ui->is_key_pressed && g_ui->key_code == KEY_CODE_D) {
			_tab_brushes_draw_i = array_index_of(g_project->_->brushes, g_context->brush);
			sys_notify_on_next_frame(&tab_brushes_draw_duplicate, NULL);
		}
		if (in_focus) {
			i32 i = array_index_of(g_project->_->brushes, g_context->brush);
			if (g_ui->is_key_pressed && g_ui->key_code == KEY_CODE_UP) {
				if (i > 0) {
					context_select_brush(i - 1);
				}
			}
			if (g_ui->is_key_pressed && g_ui->key_code == KEY_CODE_DOWN) {
				if (i < g_project->_->brushes->length - 1) {
					context_select_brush(i + 1);
				}
			}
		}
	}
}

void tab_brushes_accept_brush_drop(slot_brush_t *brush) {
	if (tab_brushes_drag_pos == -1) {
		return;
	}

	i32 brush_pos = array_index_of(g_project->_->brushes, brush);
	if (brush_pos != -1 && math_abs(brush_pos - tab_brushes_drag_pos) > 0) {
		array_remove(g_project->_->brushes, brush);
		i32 new_pos = tab_brushes_drag_pos - brush_pos > 0 ? tab_brushes_drag_pos - 1 : tab_brushes_drag_pos;
		array_insert(g_project->_->brushes, new_pos, brush);
	}
}
