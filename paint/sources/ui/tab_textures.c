
#include "../global.h"

gpu_texture_t *_tab_textures_draw_img;
char          *_tab_textures_draw_path;
asset_t       *_tab_textures_draw_asset;
i32            _tab_textures_draw_i;
bool           _tab_textures_draw_is_packed;
i32            tab_textures_drag_pos = -1;

void tab_textures_draw_set_as_envmap(void *_) {
	import_envmap_run(_tab_textures_draw_asset->file, _tab_textures_draw_img);
}

void tab_textures_draw_to_layer(void *_) {
	layers_create_image_layer(_tab_textures_draw_asset);
}

void tab_textures_draw_to_mask(void *_) {
	layers_create_image_mask(_tab_textures_draw_asset);
}

void tab_textures_draw_export_on_next_frame2(gpu_texture_t *target) {
	char *path = _tab_textures_draw_path;
	char *f    = ui_files_filename;
	if (string_equals(f, "")) {
		f = string_copy(tr("untitled"));
	}
	if (!ends_with(f, ".png")) {
		f = string("%s.png", f);
	}
	buffer_t *buf = gpu_get_texture_pixels(target);
	iron_write_png(string("%s%s%s", path, PATH_SEP, f), buf, target->width, target->height, 0);
	gpu_delete_texture(target);
}

void tab_textures_draw_export_on_next_frame(void *_) {
	gpu_texture_t *img    = _tab_textures_draw_img;
	gpu_texture_t *target = gpu_create_render_target(img->width, img->height, GPU_TEXTURE_FORMAT_RGBA32);
	draw_begin(target, false, 0);
	draw_set_pipeline(pipes_copy);
	draw_scaled_image(img, 0, 0, target->width, target->height);
	draw_set_pipeline(NULL);
	draw_end();
	sys_notify_on_next_frame(&tab_textures_draw_export_on_next_frame2, target);
}

void tab_textures_draw_export(char *path) {
	gc_unroot(_tab_textures_draw_path);
	_tab_textures_draw_path = string_copy(path);
	gc_root(_tab_textures_draw_path);
	sys_notify_on_next_frame(&tab_textures_draw_export_on_next_frame, NULL);
}

void tab_textures_update_texture_pointers(ui_node_t_array_t *nodes, i32 index) {
	for (i32 i = 0; i < nodes->length; ++i) {
		ui_node_t *n = nodes->buffer[i];
		if (string_equals(n->type, "TEX_IMAGE")) {
			if (n->buttons->buffer[0]->default_value->buffer[0] == index) {
				n->buttons->buffer[0]->default_value->buffer[0] = 9999; // Texture deleted, use pink now
			}
			else if (n->buttons->buffer[0]->default_value->buffer[0] > index) {
				n->buttons->buffer[0]->default_value->buffer[0]--; // Offset by deleted texture
			}
		}
	}
}

void tab_textures_delete_texture_on_next_frame(void *_) {
	make_material_parse_paint_material(true);
	util_render_make_material_preview();
	ui_base_hwnds->buffer[TAB_AREA_SIDEBAR1]->redraws = 2;
}

void tab_textures_delete_texture(asset_t *asset) {
	i32 index = array_index_of(g_project->_->assets, asset);
	if (g_project->_->assets->length > 1) {
		g_context->texture = g_project->_->assets->buffer[index == g_project->_->assets->length - 1 ? index - 1 : index + 1];
	}
	ui_base_hwnds->buffer[TAB_AREA_STATUS]->redraws = 2;

	if (g_context->tool == TOOL_TYPE_COLORID && index == g_context->colorid) {
		ui_header_handle->redraws  = 2;
		g_context->ddirty          = 2;
		g_context->colorid_picked  = false;
		ui_toolbar_handle->redraws = 1;
	}

	if (data_get_texture(asset->file) == scene_world->_->envmap) {
		project_set_default_envmap();
	}

	if (g_project->packed_assets != NULL) {
		for (i32 i = 0; i < g_project->packed_assets->length; ++i) {
			packed_asset_t *pa = g_project->packed_assets->buffer[i];
			if (string_equals(pa->name, asset->file)) {
				array_splice(g_project->packed_assets, i, 1);
				break;
			}
		}
	}

	data_delete_texture(asset->file);
	array_splice(g_project->_->assets, index, 1);
	sys_notify_on_next_frame(&tab_textures_delete_texture_on_next_frame, NULL);

	for (i32 i = 0; i < g_project->_->materials->length; ++i) {
		slot_material_t *m = g_project->_->materials->buffer[i];
		tab_textures_update_texture_pointers(m->canvas->nodes, index);
	}

	for (i32 i = 0; i < g_project->_->brushes->length; ++i) {
		slot_brush_t *b = g_project->_->brushes->buffer[i];
		tab_textures_update_texture_pointers(b->canvas->nodes, index);
	}
}

void tab_textures_draw_context_menu() {
	if (ui_menu_button(tr("Export"), "", ICON_EXPORT)) {
		ui_files_show("png", true, false, &tab_textures_draw_export);
	}
	if (ui_menu_button(tr("Reimport"), "", ICON_SYNC)) {
		project_reimport_texture(_tab_textures_draw_asset);
	}
	if (ui_menu_button(tr("To Layer"), "", ICON_LAYER)) {
		sys_notify_on_next_frame(&tab_textures_draw_to_layer, NULL);
	}
	if (ui_menu_button(tr("To Mask"), "", ICON_MASK)) {
		sys_notify_on_next_frame(&tab_textures_draw_to_mask, NULL);
	}
	if (ui_menu_button(tr("Set as Envmap"), "", ICON_LANDSCAPE)) {
		sys_notify_on_next_frame(&tab_textures_draw_set_as_envmap, NULL);
	}
	if (ui_menu_button(tr("Set as Color ID Map"), "", ICON_COLOR_ID)) {
		g_context->colorid         = _tab_textures_draw_i;
		g_context->colorid_picked  = false;
		ui_toolbar_handle->redraws = 1;
		if (g_context->tool == TOOL_TYPE_COLORID) {
			ui_header_handle->redraws = 2;
			g_context->ddirty         = 2;
		}
	}
	if (ui_menu_button(tr("Delete"), "delete", ICON_DELETE)) {
		tab_textures_delete_texture(_tab_textures_draw_asset);
	}
	if (!_tab_textures_draw_is_packed && ui_menu_button(tr("Open Containing Directory..."), "", ICON_FOLDER_OPEN)) {
		file_start(substring(_tab_textures_draw_asset->file, 0, string_last_index_of(_tab_textures_draw_asset->file, PATH_SEP)));
	}
	if (!_tab_textures_draw_is_packed && ui_menu_button(tr("Open in Browser"), "", ICON_NONE)) {
		tab_browser_show_directory(substring(_tab_textures_draw_asset->file, 0, string_last_index_of(_tab_textures_draw_asset->file, PATH_SEP)));
	}
}

void tab_textures_draw_import(char *path) {
	import_asset_run(path, -1.0, -1.0, true, false, NULL);
	ui_base_hwnds->buffer[TAB_AREA_STATUS]->redraws = 2;
}

void tab_textures_draw(ui_handle_t *htab) {

	if (ui_tab(htab, tr("Textures"), false, -1, false) && g_ui->_window_h > ui_statusbar_default_h * UI_SCALE()) {

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
			ui_files_show(string_array_join(path_texture_formats(), ","), false, true, &tab_textures_draw_import);
		}
		if (g_ui->is_hovered) {
			ui_tooltip(string("%s (%s)", tr("Import texture file"), (char *)any_map_get(g_keymap, "file_import_assets")));
		}
		if (ui_icon_button(tr("2D View"), ICON_WINDOW, UI_ALIGN_CENTER)) {
			ui_base_show_2d_view(VIEW_2D_TYPE_ASSET);
		}

		hsearch->text = string_copy(ui_text_input(hsearch, tr("Search"), UI_ALIGN_LEFT, true, true));
		if (g_ui->is_hovered) {
			ui_tooltip(string("%s\n%s", tr("ctrl+f to search"), tr("esc to cancel")));
		}
		if (g_ui->is_ctrl_down && g_ui->is_key_pressed && g_ui->key_code == KEY_CODE_F) {
			ui_start_text_edit(hsearch, UI_ALIGN_LEFT);
		}
		if (!string_equals(hsearch->text, "") && (ui_button(tr("X"), UI_ALIGN_CENTER, "") || g_ui->is_escape_down)) {
			hsearch->text = "";
		}

		ui_end_sticky();

		char *search = to_lower_case(hsearch->text);

		if (g_project->_->assets->length > 0) {

			i32 slotw = math_floor(52 * UI_SCALE());
			i32 num   = math_floor(g_ui->_window_w / (float)slotw);
			if (num == 0) {
				return;
			}

			// Indices of the assets passing the search filter
			i32_array_t *filtered = i32_array_create_from_raw((i32[]){}, 0);
			for (i32 i = 0; i < g_project->_->assets->length; ++i) {
				if (string_equals(search, "") || string_index_of(to_lower_case(g_project->_->assets->buffer[i]->name), search) >= 0) {
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

					asset_t       *asset = g_project->_->assets->buffer[i];
					gpu_texture_t *img   = project_get_image(asset);
					if (img == NULL) {
						render_target_t *empty_rt = any_map_get(render_path_render_targets, "empty_black");
						img                       = empty_rt->_image;
					}
					uix    = g_ui->_x;
					uiy    = g_ui->_y;
					i32 sw = img->height < img->width ? img->height : 0;

					if (base_drag_asset != NULL && tab_textures_drag_pos == i) {
						ui_fill(-1, -2, 2, imgw_val + 4, g_theme->HIGHLIGHT_COL);
					}

					ui_state_t _state = ui_sub_image(img, 0xffffffff, slotw, 0, 0, sw, sw);

					// Reordering
					if (_state == UI_STATE_HOVERED && base_drag_asset != NULL && string_equals(search, "")) {
						tab_textures_drag_pos = (mouse_x > uix + g_ui->_window_x + imgw_val / 2.0) ? i + 1 : i;
						drag_pos_set          = true;
					}

					if (_state == UI_STATE_STARTED && g_ui->input_y > g_ui->_window_y) {
						base_drag_off_x = -(mouse_x - uix - g_ui->_window_x - 3);
						base_drag_off_y = -(mouse_y - uiy - g_ui->_window_y + 1);
						gc_unroot(base_drag_asset);
						base_drag_asset = asset;
						gc_root(base_drag_asset);
						g_context->texture = asset;
						if (sys_time() - g_context->select_time < 0.2) {
							ui_base_show_2d_view(VIEW_2D_TYPE_ASSET);
						}
						g_context->select_time  = sys_time();
						ui_view2d_hwnd->redraws = 2;
					}

					if (asset == g_context->texture) {
						f32 _uix = g_ui->_x;
						f32 _uiy = g_ui->_y;
						g_ui->_x = uix;
						g_ui->_y = uiy;
						i32 off  = i % 2 == 1 ? 1 : 0;
						i32 w    = 50;
						ui_fill(0, 0, w + 3, 2, g_theme->HIGHLIGHT_COL);
						ui_fill(0, w - off + 2, w + 3, 2 + off, g_theme->HIGHLIGHT_COL);
						ui_fill(0, 0, 2, w + 3, g_theme->HIGHLIGHT_COL);
						ui_fill(w + 2, 0, 2, w + 4, g_theme->HIGHLIGHT_COL);
						g_ui->_x = _uix;
						g_ui->_y = _uiy;
					}

					bool is_packed = g_project->packed_assets != NULL && project_packed_asset_exists(g_project->packed_assets, asset->file);

					if (g_ui->is_hovered) {
						ui_tooltip_image(img, 256);
						char *tooltip = asset->name;
						if (is_packed) {
							tooltip = string("%s %s", tooltip, tr("(packed)"));
						}
#ifdef WITH_BC7
						if (img->format == GPU_TEXTURE_FORMAT_RGBA32_BC7) {
							tooltip = string("%s %s", tooltip, tr("(compressed)"));
						}
#endif
						ui_tooltip(tooltip);
					}

					if (g_ui->is_hovered && g_ui->input_released_r) {
						g_context->texture = asset;

						gc_unroot(_tab_textures_draw_img);
						_tab_textures_draw_img = img;
						gc_root(_tab_textures_draw_img);
						gc_unroot(_tab_textures_draw_asset);
						_tab_textures_draw_asset = asset;
						gc_root(_tab_textures_draw_asset);
						_tab_textures_draw_i         = i;
						_tab_textures_draw_is_packed = is_packed;
						ui_menu_draw(&tab_textures_draw_context_menu, -1, -1);
					}

					if (g_config->show_asset_names) {
						g_ui->_x = uix;
						g_ui->_y += slotw * 0.9;
						ui_text(g_project->_->assets->buffer[i]->name, UI_ALIGN_CENTER, 0x00000000);
						if (g_ui->is_hovered) {
							ui_tooltip(g_project->_->assets->buffer[i]->name);
						}
						g_ui->_y -= slotw * 0.9;
						if (fi == filtered->length - 1) {
							g_ui->_y += j == num - 1 ? imgw : imgw + UI_ELEMENT_H() + UI_ELEMENT_OFFSET();
						}
					}
				}
			}

			if (base_drag_asset != NULL && tab_textures_drag_pos == g_project->_->assets->length) {
				g_ui->_x = uix;
				g_ui->_y = uiy;
				ui_fill(imgw_val + 1, -2, 2, imgw_val + 4, g_theme->HIGHLIGHT_COL);
			}

			if (!drag_pos_set) {
				tab_textures_drag_pos = -1;
			}
		}
		else {
			gpu_texture_t *img = resource_get("icons.k");
			rect_t        *r   = resource_tile50(img, ICON_DROP);
			ui_sub_image(img, g_theme->BUTTON_COL, r->h, r->x, r->y, r->w, r->h);
			if (g_ui->is_hovered) {
				ui_tooltip(tr("Drag and drop files here"));
			}
		}

		bool in_focus = g_ui->input_x > g_ui->_window_x && g_ui->input_x < g_ui->_window_x + g_ui->_window_w && g_ui->input_y > g_ui->_window_y &&
		                g_ui->input_y < g_ui->_window_y + g_ui->_window_h;
		if (in_focus && g_ui->is_delete_down && g_project->_->assets->length > 0 && array_index_of(g_project->_->assets, g_context->texture) >= 0) {
			g_ui->is_delete_down = false;
			tab_textures_delete_texture(g_context->texture);
		}
		if (in_focus && g_project->_->assets->length > 0) {
			i32 i = array_index_of(g_project->_->assets, g_context->texture);
			if (g_ui->is_key_pressed && g_ui->key_code == KEY_CODE_UP) {
				if (i > 0) {
					g_context->texture = g_project->_->assets->buffer[i - 1];
				}
			}
			if (g_ui->is_key_pressed && g_ui->key_code == KEY_CODE_DOWN) {
				if (i < g_project->_->assets->length - 1) {
					g_context->texture = g_project->_->assets->buffer[i + 1];
				}
			}
		}
	}
}

void tab_textures_remap_node_indices(ui_node_t_array_t *nodes, i32 old_pos, i32 new_pos) {
	for (i32 i = 0; i < nodes->length; ++i) {
		ui_node_t *n = nodes->buffer[i];
		if (string_equals(n->type, "TEX_IMAGE")) {
			i32 idx = n->buttons->buffer[0]->default_value->buffer[0];
			if (idx == old_pos) {
				n->buttons->buffer[0]->default_value->buffer[0] = new_pos;
			}
			else if (new_pos > old_pos && idx > old_pos && idx <= new_pos) {
				n->buttons->buffer[0]->default_value->buffer[0] = idx - 1;
			}
			else if (new_pos < old_pos && idx >= new_pos && idx < old_pos) {
				n->buttons->buffer[0]->default_value->buffer[0] = idx + 1;
			}
		}
	}
}

void tab_textures_accept_asset_drop(asset_t *asset) {
	if (tab_textures_drag_pos == -1) {
		return;
	}

	i32 asset_pos = array_index_of(g_project->_->assets, asset);
	if (asset_pos != -1 && math_abs(asset_pos - tab_textures_drag_pos) > 0) {
		array_remove(g_project->_->assets, asset);
		i32 new_pos = tab_textures_drag_pos - asset_pos > 0 ? tab_textures_drag_pos - 1 : tab_textures_drag_pos;
		array_insert(g_project->_->assets, new_pos, asset);

		for (i32 i = 0; i < g_project->_->materials->length; ++i) {
			tab_textures_remap_node_indices(g_project->_->materials->buffer[i]->canvas->nodes, asset_pos, new_pos);
		}
		for (i32 i = 0; i < g_project->_->brushes->length; ++i) {
			tab_textures_remap_node_indices(g_project->_->brushes->buffer[i]->canvas->nodes, asset_pos, new_pos);
		}
		if (g_context->colorid == asset_pos) {
			g_context->colorid = new_pos;
		}
		else if (new_pos > asset_pos && g_context->colorid > asset_pos && g_context->colorid <= new_pos) {
			g_context->colorid--;
		}
		else if (new_pos < asset_pos && g_context->colorid >= new_pos && g_context->colorid < asset_pos) {
			g_context->colorid++;
		}
	}
}
