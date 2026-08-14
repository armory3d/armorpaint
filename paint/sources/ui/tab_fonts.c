
#include "../global.h"

i32 _tab_fonts_draw_i;
i32 tab_fonts_drag_pos = -1;

void tab_fonts_draw_make_font_preview(void *_) {
	i32          i     = _tab_fonts_draw_i;
	slot_font_t *_font = g_context->font;
	g_context->font    = g_project->_->fonts->buffer[i];
	util_render_make_font_preview();
	g_context->font = _font;
}

void tab_fonts_delete_font_on_next_frame(slot_font_t *font) {
	i32 i = array_index_of(g_project->_->fonts, font);
	context_select_font(i == g_project->_->fonts->length - 1 ? i - 1 : i + 1);
	data_delete_font(g_project->_->fonts->buffer[i]->file);
	array_splice(g_project->_->fonts, i, 1);
}

void tab_fonts_delete_font(slot_font_t *font) {
	sys_notify_on_next_frame(&tab_fonts_delete_font_on_next_frame, font);
	ui_base_hwnds->buffer[2]->redraws = 2;
}

void tab_fonts_draw_context_menu_draw() {
	i32 i = _tab_fonts_draw_i;
	if (g_project->_->fonts->length > 1 && ui_menu_button(tr("Delete"), "delete", ICON_DELETE) && !string_equals(g_project->_->fonts->buffer[i]->file, "")) {
		tab_fonts_delete_font(g_project->_->fonts->buffer[i]);
	}
}

void tab_fonts_draw_context_menu(void *_) {
	context_select_font(_tab_fonts_draw_i);
	ui_menu_draw(&tab_fonts_draw_context_menu_draw, -1, -1);
}

void tab_fonts_draw_select_font(void *_) {
	i32 i = _tab_fonts_draw_i;
	context_select_font(i);
}

void tab_fonts_draw(ui_handle_t *htab) {
	if (ui_tab(htab, tr("Fonts"), false, -1, false) && g_ui->_window_h > ui_statusbar_default_h * UI_SCALE()) {

		ui_begin_sticky();

		ui_handle_t *hsearch = ui_handle(__ID__);

		f32_array_t *row = string_equals(hsearch->text, "") ? f32_array_create_from_raw(
		                                                          (f32[]){
		                                                              -100,
		                                                              -100,
		                                                              -200,
		                                                          },
		                                                          3)
		                                                    : f32_array_create_from_raw(
		                                                          (f32[]){
		                                                              -100,
		                                                              -100,
		                                                              -200,
		                                                              -40,
		                                                          },
		                                                          4);
		ui_row(row);

		if (ui_icon_button(tr("Import"), ICON_IMPORT, UI_ALIGN_CENTER)) {
			project_import_asset("ttf,ttc,otf", true);
		}
		if (g_ui->is_hovered) {
			ui_tooltip(tr("Import font file"));
		}

		if (ui_icon_button(tr("2D View"), ICON_WINDOW, UI_ALIGN_CENTER)) {
			ui_base_show_2d_view(VIEW_2D_TYPE_FONT);
		}

		hsearch->text = string_copy(ui_text_input(hsearch, tr("Search"), UI_ALIGN_LEFT, true, true));
		if (g_ui->is_ctrl_down && g_ui->is_key_pressed && g_ui->key_code == KEY_CODE_F) {
			ui_start_text_edit(hsearch, UI_ALIGN_LEFT);
		}
		if (!string_equals(hsearch->text, "") && (ui_button(tr("X"), UI_ALIGN_CENTER, "") || g_ui->is_escape_down)) {
			hsearch->text = "";
		}

		ui_end_sticky();
		ui_separator(3, false);

		char *search = to_lower_case(hsearch->text);

		i32 slotw = math_floor(51 * UI_SCALE());
		i32 num   = math_floor(g_ui->_window_w / (float)slotw);
		if (num == 0) {
			return;
		}

		i32_array_t *filtered = i32_array_create_from_raw((i32[]){}, 0);
		for (i32 i = 0; i < g_project->_->fonts->length; ++i) {
			if (string_equals(search, "") || string_index_of(to_lower_case(g_project->_->fonts->buffer[i]->name), search) >= 0) {
				i32_array_push(filtered, i);
			}
		}

		bool drag_pos_set = false;
		f32  uix          = 0.0;
		f32  uiy          = 0.0;
		i32  imgw_val     = math_floor(50 * UI_SCALE());

		for (i32 row = 0; row < math_floor(math_ceil(filtered->length / (float)num)); ++row) {
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
				i32 fi   = j + row * num;
				if (fi >= filtered->length) {
					ui_end_element_of_size(imgw);
					if (g_config->show_asset_names) {
						ui_end_element_of_size(0);
					}
					continue;
				}
				i32 i = filtered->buffer[fi];

				gpu_texture_t *img = g_project->_->fonts->buffer[i]->image;

				if (g_context->font == g_project->_->fonts->buffer[i]) {
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

				if (base_drag_font != NULL && tab_fonts_drag_pos == i) {
					ui_fill(-1, -2, 2, imgw_val + 4, g_theme->HIGHLIGHT_COL);
				}

				ui_state_t state = UI_STATE_IDLE;
				if (g_project->_->fonts->buffer[i]->preview_ready) {
					state = ui_image(img, 0xffffffff, -1.0);
				}
				else {
					state = ui_sub_image(resource_get("icons.k"), -1, -1.0, tile * 6, tile, tile, tile);
				}

				if (state == UI_STATE_HOVERED && base_drag_font != NULL && string_equals(search, "")) {
					tab_fonts_drag_pos = (mouse_x > uix + g_ui->_window_x + imgw_val / 2.0) ? i + 1 : i;
					drag_pos_set       = true;
				}

				if (state == UI_STATE_STARTED) {
					if (g_context->font != g_project->_->fonts->buffer[i]) {
						_tab_fonts_draw_i = i;
						sys_notify_on_next_frame(&tab_fonts_draw_select_font, NULL);
					}
					base_drag_off_x = -(mouse_x - uix - g_ui->_window_x - 3);
					base_drag_off_y = -(mouse_y - uiy - g_ui->_window_y + 1);
					gc_unroot(base_drag_font);
					base_drag_font = g_project->_->fonts->buffer[i];
					gc_root(base_drag_font);
					if (sys_time() - g_context->select_time < 0.2) {
						ui_base_show_2d_view(VIEW_2D_TYPE_FONT);
						gc_unroot(base_drag_font);
						base_drag_font   = NULL;
						base_is_dragging = false;
					}
					g_context->select_time = sys_time();
				}
				if (g_ui->is_hovered && g_ui->input_released_r) {
					_tab_fonts_draw_i = i;
					sys_notify_on_next_frame(&tab_fonts_draw_context_menu, NULL);
				}
				if (g_ui->is_hovered) {
					if (img == NULL) {
						_tab_fonts_draw_i = i;
						sys_notify_on_next_frame(&tab_fonts_draw_make_font_preview, NULL);
					}
					else {
						ui_tooltip_image(img, 0);
						ui_tooltip(g_project->_->fonts->buffer[i]->name);
					}
				}

				if (g_config->show_asset_names) {
					g_ui->_x = uix;
					g_ui->_y += slotw * 0.9;
					ui_text(g_project->_->fonts->buffer[i]->name, UI_ALIGN_CENTER, 0x00000000);
					if (g_ui->is_hovered) {
						ui_tooltip(g_project->_->fonts->buffer[i]->name);
					}
					g_ui->_y -= slotw * 0.9;
					if (fi == filtered->length - 1) {
						g_ui->_y += j == num - 1 ? imgw : imgw + UI_ELEMENT_H() + UI_ELEMENT_OFFSET();
					}
				}
			}

			g_ui->_y += 6;
		}

		if (base_drag_font != NULL && tab_fonts_drag_pos == g_project->_->fonts->length) {
			g_ui->_x = uix;
			g_ui->_y = uiy;
			ui_fill(imgw_val + 1, -2, 2, imgw_val + 4, g_theme->HIGHLIGHT_COL);
		}

		if (!drag_pos_set) {
			tab_fonts_drag_pos = -1;
		}

		bool in_focus = g_ui->input_x > g_ui->_window_x && g_ui->input_x < g_ui->_window_x + g_ui->_window_w && g_ui->input_y > g_ui->_window_y &&
		                g_ui->input_y < g_ui->_window_y + g_ui->_window_h;
		if (in_focus && g_ui->is_delete_down && g_project->_->fonts->length > 1 && !string_equals(g_context->font->file, "")) {
			g_ui->is_delete_down = false;
			tab_fonts_delete_font(g_context->font);
		}
		if (in_focus) {
			i32 i = array_index_of(g_project->_->fonts, g_context->font);
			if (g_ui->is_key_pressed && g_ui->key_code == KEY_CODE_UP) {
				if (i > 0) {
					_tab_fonts_draw_i = i - 1;
					sys_notify_on_next_frame(&tab_fonts_draw_select_font, NULL);
				}
			}
			if (g_ui->is_key_pressed && g_ui->key_code == KEY_CODE_DOWN) {
				if (i < g_project->_->fonts->length - 1) {
					_tab_fonts_draw_i = i + 1;
					sys_notify_on_next_frame(&tab_fonts_draw_select_font, NULL);
				}
			}
		}
	}
}

void tab_fonts_accept_font_drop(slot_font_t *font) {
	if (tab_fonts_drag_pos == -1) {
		return;
	}

	i32 font_pos = array_index_of(g_project->_->fonts, font);
	if (font_pos != -1 && math_abs(font_pos - tab_fonts_drag_pos) > 0) {
		array_remove(g_project->_->fonts, font);
		i32 new_pos = tab_fonts_drag_pos - font_pos > 0 ? tab_fonts_drag_pos - 1 : tab_fonts_drag_pos;
		array_insert(g_project->_->fonts, new_pos, font);
	}
}
