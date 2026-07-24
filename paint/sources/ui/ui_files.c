
#include "../global.h"

typedef struct draw_cloud_icon_data {
	char               *f;
	struct gpu_texture *image;
} draw_cloud_icon_data_t;

typedef struct ui_files_make_icon {
	struct gpu_texture *image;
	char               *shandle;
	i32                 w;
} ui_files_make_icon_t;

char           *ui_files_last_search     = "";
string_array_t *ui_files_files           = NULL;
any_map_t      *ui_files_icon_map        = NULL;
any_map_t      *ui_files_icon_file_map   = NULL;
i32             ui_files_selected        = -1;
i32             ui_files_num_cols        = 1;
char           *ui_files_select_pending  = NULL;
bool            ui_files_show_extensions = false;
bool            ui_files_offline         = false;
ui_handle_t    *_ui_files_file_browser_handle;

void ui_files_release_keys() {
	// File dialog may prevent firing key up events
	keyboard_up_listener(KEY_CODE_SHIFT);
	keyboard_up_listener(KEY_CODE_CONTROL);
#ifdef IRON_MACOS
	keyboard_up_listener(KEY_CODE_META);
#endif
}

void ui_files_show(char *filters, bool is_save, bool open_multiple, void (*files_done)(char *)) {
	if (is_save) {
		gc_unroot(ui_files_path);
		ui_files_path = string_copy(iron_save_dialog(filters, ""));
		gc_root(ui_files_path);
		if (ui_files_path != NULL) {
			char *sep2 = string("%s%s", PATH_SEP, PATH_SEP);
			while (string_index_of(ui_files_path, sep2) >= 0) {
				gc_unroot(ui_files_path);
				ui_files_path = string_copy(string_replace_all(ui_files_path, sep2, PATH_SEP));
				gc_root(ui_files_path);
			}
			gc_unroot(ui_files_path);
			ui_files_path = string_copy(string_replace_all(ui_files_path, "\r", ""));
			gc_root(ui_files_path);
			gc_unroot(ui_files_filename);
			ui_files_filename = string_copy(substring(ui_files_path, string_last_index_of(ui_files_path, PATH_SEP) + 1, string_length(ui_files_path)));
			gc_root(ui_files_filename);
			gc_unroot(ui_files_path);
			ui_files_path = string_copy(substring(ui_files_path, 0, string_last_index_of(ui_files_path, PATH_SEP)));
			gc_root(ui_files_path);
			files_done(ui_files_path);
		}
	}
	else {
		string_array_t *paths = iron_open_dialog(filters, "", open_multiple);
		if (paths != NULL) {
			for (i32 i = 0; i < paths->length; ++i) {
				char *path = paths->buffer[i];
				char *sep2 = string("%s%s", PATH_SEP, PATH_SEP);

				while (string_index_of(path, sep2) >= 0) {
					path = string_copy(string_replace_all(path, sep2, PATH_SEP));
				}
				path = string_copy(string_replace_all(path, "\r", ""));
				gc_unroot(ui_files_filename);
				ui_files_filename = string_copy(substring(path, string_last_index_of(path, PATH_SEP) + 1, string_length(path)));
				gc_root(ui_files_filename);
				files_done(path);
			}
		}
	}

	ui_files_release_keys();
}

draw_cloud_icon_data_t *make_draw_cloud_icon_data(char *f, gpu_texture_t *image) {
	draw_cloud_icon_data_t *data = GC_ALLOC_INIT(draw_cloud_icon_data_t, {.f = f, .image = image});
	return data;
}

void ui_files_file_browser_on_cache_cloud_done_on_next_frame(draw_cloud_icon_data_t *data) {
	gpu_texture_t *icon = gpu_create_render_target(data->image->width, data->image->height, GPU_TEXTURE_FORMAT_RGBA32);
	if (ends_with(data->f, ".arm")) { // Used for material sphere alpha cutout
		draw_begin(icon, false, 0);
		draw_image(g_project->_->materials->buffer[0]->image, 0, 0);
	}
	else {
		draw_begin(icon, true, 0xffffffff);
	}
	draw_set_pipeline(pipes_copy_rgb);
	draw_image(data->image, 0, 0);
	draw_set_pipeline(NULL);
	draw_end();
	any_map_set(ui_files_icon_map, string("%s%s%s", _ui_files_file_browser_handle->text, PATH_SEP, data->f), icon);
	ui_base_hwnds->buffer[TAB_AREA_STATUS]->redraws = 3;
}

void ui_files_file_browser_on_cache_cloud_done(char *abs) {
	if (abs != NULL) {
		gpu_texture_t *image = data_get_texture(string_copy(abs));
		if (image != NULL) {
#ifdef IRON_WINDOWS
			abs = string_copy(string_replace_all(abs, "\\", "/"));
#endif
			char *icon_file = substring(abs, string_last_index_of(abs, "/") + 1, string_length(abs));
			char *f         = any_map_get(ui_files_icon_file_map, icon_file);
			if (f != NULL) {
				draw_cloud_icon_data_t *data = make_draw_cloud_icon_data(f, image);
				sys_notify_on_next_frame(&ui_files_file_browser_on_cache_cloud_done_on_next_frame, data);
			}
		}
	}
	else {
		ui_files_offline = true;
	}
}

void ui_files_file_browser_on_init_cloud_done() {
	ui_base_hwnds->buffer[TAB_AREA_STATUS]->redraws = 3;
	tab_browser_refresh                             = true;
}

static void ui_files_clear_icon_map(void) {
	if (ui_files_icon_map == NULL) {
		return;
	}
	gpu_texture_t   *icons_tex = resource_get("icons.k");
	render_target_t *rt        = any_map_get(render_path_render_targets, "empty_black");
	gpu_texture_t   *empty     = rt->_image;
	for (i32 i = 0; i < (i32)ui_files_icon_map->keys->capacity; ++i) {
		gpu_texture_t *tex = ui_files_icon_map->values->buffer[i];
		if (tex != NULL && tex != icons_tex && tex != empty) {
			gpu_delete_texture(tex);
		}
	}
	gc_unroot(ui_files_icon_map);
	ui_files_icon_map = NULL;

	if (ui_files_icon_file_map != NULL) {
		char dest[512];
		if (path_is_protected()) {
			strcpy(dest, iron_internal_save_path());
		}
		else {
			strcpy(dest, iron_internal_files_location());
			strcat(dest, PATH_SEP);
		}

		for (i32 i = 0; i < (i32)ui_files_icon_file_map->keys->capacity; ++i) {
			char *icon_file = ui_files_icon_file_map->keys->buffer[i];
			if (icon_file != NULL) {
				char *path = string("%s%s%s%s", dest, ui_files_last_path, PATH_SEP, icon_file);
				data_delete_texture(path);
			}
		}
	}
	gc_unroot(ui_files_icon_file_map);
	ui_files_icon_file_map = NULL;
}

void ui_files_make_icon(ui_files_make_icon_t *args) {
	i32            w     = args->w;
	gpu_texture_t *image = args->image;
	i32            sw    = image->width > image->height ? w : math_floor(1.0 * image->width / (float)image->height * w);
	i32            sh    = image->width > image->height ? math_floor(1.0 * image->height / (float)image->width * w) : w;
	gpu_texture_t *icon  = gpu_create_render_target(sw, sh, GPU_TEXTURE_FORMAT_RGBA32);
	draw_begin(icon, true, 0xffffffff);
	draw_set_pipeline(pipes_copy_rgb);
	draw_scaled_image(image, 0, 0, sw, sh);
	draw_set_pipeline(NULL);
	draw_end();
	any_map_set(ui_files_icon_map, args->shandle, icon);
	ui_base_hwnds->buffer[TAB_AREA_STATUS]->redraws = 3;

	bool found = false;
	for (i32 i = 0; i < g_project->_->assets->length; ++i) {
		asset_t *a = g_project->_->assets->buffer[i];
		if (string_equals(a->file, args->shandle)) {
			found = true;
			break;
		}
	}

	if (!found) {
		data_delete_texture(args->shandle); // The big image is not needed anymore
	}
}

char *ui_files_file_browser(ui_handle_t *handle, bool drag_files, char *search, bool refresh, void (*context_menu)(char *)) {
	gpu_texture_t *icons       = resource_get("icons.k");
	rect_t        *folder      = resource_tile50(icons, ICON_FOLDER_FULL);
	rect_t        *file        = resource_tile50(icons, ICON_FILE);
	rect_t        *cube        = resource_tile50(icons, ICON_CUBE);
	rect_t        *downloading = resource_tile50(icons, ICON_DOWNLOADING);
	bool           is_cloud    = starts_with(handle->text, "cloud");

	if (is_cloud && file_cloud == NULL) {
		file_init_cloud(&ui_files_file_browser_on_init_cloud_done, g_config->server);
	}
	if (is_cloud && file_read_directory("cloud")->length == 0) {
		return handle->text;
	}

#ifdef IRON_IOS
	char *document_directory = iron_save_dialog("", "");
	document_directory       = string_copy(substring(document_directory, 0, string_length(document_directory) - 8)); // Strip /"untitled"
#endif

	if (string_equals(handle->text, "")) {
		handle->text = string_copy(ui_files_default_path);
	}

	if (!string_equals(handle->text, ui_files_last_path)) {
		ui_files_clear_icon_map();
	}

	if (!string_equals(handle->text, ui_files_last_path) || !string_equals(search, ui_files_last_search) || refresh) {
		gc_unroot(ui_files_files);
		ui_files_files = any_array_create_from_raw((void *[]){}, 0);
		gc_root(ui_files_files);

		char *dir_path = handle->text;
#ifdef IRON_IOS
		if (!is_cloud) {
			dir_path = string("%s%s", document_directory, dir_path);
		}
#endif
		string_array_t *files_all = file_read_directory(dir_path);

		for (i32 i = 0; i < files_all->length; ++i) {
			char *f      = files_all->buffer[i];
			bool  is_dir = iron_is_directory(path_join(dir_path, f));
			if (string_equals(f, "") || string_equals(char_at(f, 0), ".")) {
				continue; // Skip hidden
			}
			if (!is_cloud && string_index_of(f, ".") > 0 && !is_dir && !path_is_known(f)) {
				continue; // Skip unknown extensions
			}
			if (!is_cloud && !is_dir && string_index_of(f, ".") == -1) {
				continue; // Skip files with no extension
			}
			if (is_cloud && string_index_of(f, "_icon.") >= 0) {
				continue; // Skip thumbnails
			}
			if (string_index_of(to_lower_case(f), to_lower_case(search)) < 0) {
				continue; // Search filter
			}
			any_array_push(ui_files_files, f);
		}
	}

	gc_unroot(ui_files_last_path);
	ui_files_last_path = string_copy(handle->text);
	gc_root(ui_files_last_path);
	gc_unroot(ui_files_last_search);
	ui_files_last_search = string_copy(search);
	gc_root(ui_files_last_search);
	handle->changed = false;

	if (ui_files_select_pending != NULL) {
		ui_files_selected = -1;
		for (i32 i = 0; i < ui_files_files->length; ++i) {
			if (string_equals(ui_files_files->buffer[i], ui_files_select_pending)) {
				ui_files_selected = i;
				break;
			}
		}
		gc_unroot(ui_files_select_pending);
		ui_files_select_pending = NULL;
	}

	i32 slotw = math_floor(70 * UI_SCALE());
	i32 num   = math_floor(g_ui->_w / (float)slotw);
	if (num == 0) {
		return handle->text;
	}
	ui_files_num_cols = num;

	g_ui->_y += 4; // Don't cut off the border around selected materials
	// Directory contents
	for (i32 row = 0; row < math_floor(math_ceil(ui_files_files->length / (float)num)); ++row) {
		f32_array_t *ar = f32_array_create_from_raw((f32[]){}, 0);
		for (i32 i = 0; i < num * 2; ++i) {
			f32_array_push(ar, 1 / (float)num);
		}
		ui_row(ar);
		if (row > 0) {
			g_ui->_y += UI_ELEMENT_OFFSET() * 14.0;
		}

		for (i32 j = 0; j < num; ++j) {
			i32 i = j + row * num;
			if (i >= ui_files_files->length) {
				ui_end_element_of_size(slotw);
				ui_end_element_of_size(slotw);
				continue;
			}

			char *f  = ui_files_files->buffer[i];
			f32   _x = g_ui->_x;
			bool  is_folder;
			if (is_cloud) {
				is_folder = string_index_of(f, ".") == -1;
			}
			else {
				is_folder = iron_is_directory(path_join(handle->text, f));
			}

			rect_t *rect = is_folder ? folder : file;
			if (rect == file && is_cloud) {
				rect = downloading;
			}
			else if (!is_folder && path_is_mesh(f)) {
				rect = cube;
			}

			i32 col = rect == file ? g_theme->LABEL_COL : base_darker(g_theme->LABEL_COL, 0x00202020);
			if (ui_files_selected == i)
				col = g_theme->HIGHLIGHT_COL;

			f32 off = g_ui->_w / 2.0 - 25 * UI_SCALE();
			g_ui->_x += off;

			f32            uix     = g_ui->_x;
			f32            uiy     = g_ui->_y;
			ui_state_t     state   = UI_STATE_IDLE;
			bool           generic = true;
			gpu_texture_t *icon    = NULL;

			if (is_cloud && !ui_files_offline) {
				if (ui_files_icon_map == NULL) {
					ui_files_icon_map = any_map_create();
					gc_root(ui_files_icon_map);
				}
				if (ui_files_icon_file_map == NULL) {
					ui_files_icon_file_map = any_map_create();
					gc_root(ui_files_icon_file_map);
				}
				icon = any_map_get(ui_files_icon_map, string("%s%s%s", handle->text, PATH_SEP, f));
				if (icon == NULL) {
					i32 dot = string_last_index_of(f, ".");
					if (dot > -1) {
						string_array_t *files_all = file_read_directory(handle->text);
						char           *icon_file = string("%s_icon.jpg", substring(f, 0, dot));
						if (string_array_index_of(files_all, icon_file) < 0) {
							icon_file = string("%s_icon.png", substring(f, 0, dot));
						}
						if (string_array_index_of(files_all, icon_file) >= 0) {
							any_map_set(ui_files_icon_map, string("%s%s%s", handle->text, PATH_SEP, f), icons);

							gc_unroot(_ui_files_file_browser_handle);
							_ui_files_file_browser_handle = handle;
							gc_root(_ui_files_file_browser_handle);
							any_map_set(ui_files_icon_file_map, icon_file, f);

							file_cache_cloud(string("%s%s%s", handle->text, PATH_SEP, icon_file), &ui_files_file_browser_on_cache_cloud_done, g_config->server);
						}
					}
				}
				if (icon != NULL && icon != icons) {
					i32 w = 50;
					if (i == ui_files_selected) {
						ui_fill(-2, -2, w + 4, 2, g_theme->HIGHLIGHT_COL);
						ui_fill(-2, w + 2, w + 4, 2, g_theme->HIGHLIGHT_COL);
						ui_fill(-2, 0, 2, w + 4, g_theme->HIGHLIGHT_COL);
						ui_fill(w + 2, -2, 2, w + 6, g_theme->HIGHLIGHT_COL);
					}
					state = ui_image(icon, 0xffffffff, w * UI_SCALE());
					if (g_ui->is_hovered) {
						ui_tooltip_image(icon, 0);
						ui_tooltip(f);
					}
					generic = false;
				}
			}

			if (!is_folder && ends_with(f, ".arm") && !is_cloud) {
				if (ui_files_icon_map == NULL) {
					ui_files_icon_map = any_map_create();
					gc_root(ui_files_icon_map);
				}
				char *key = string("%s%s%s", handle->text, PATH_SEP, f);
				icon      = any_map_get(ui_files_icon_map, key);
				if (icon == NULL) {
					char *blob_path = key;

#ifdef IRON_IOS
					blob_path = string("%s%s", document_directory, blob_path);
#endif

					buffer_t  *buffer = iron_load_blob(blob_path);
					project_t *raw;

#ifdef IRON_WINDOWS
					bool is_cloud_cache = string_index_of(handle->text, "\\cloud\\") >= 0;
#else
					bool is_cloud_cache = string_index_of(handle->text, "/cloud/") >= 0;
#endif

					if (import_arm_is_old(buffer) && !is_cloud_cache) {
						raw = import_arm_from_old(buffer);
					}
					else if (import_arm_has_version(buffer)) {
						raw = armpack_decode(buffer);
					}

					if (raw->material_icons != NULL) {
						buffer_t *bytes_icon = raw->material_icons->buffer[0];
#ifdef IRON_BGRA
						buffer_t *buf = buffer_bgra64_swap(lz4_decode(bytes_icon, 256 * 256 * 8));
#else
						buffer_t *buf = lz4_decode(bytes_icon, 256 * 256 * 8);
#endif
						icon = gpu_create_texture_from_bytes(buf, 256, 256, GPU_TEXTURE_FORMAT_RGBA64);
					}
					else if (raw->mesh_icons != NULL) {
						buffer_t *bytes_icon = raw->mesh_icons->buffer[0];
						icon                 = gpu_create_texture_from_bytes(lz4_decode(bytes_icon, 256 * 256 * 4), 256, 256, GPU_TEXTURE_FORMAT_RGBA32);
					}
					else if (raw->brush_icons != NULL) {
						buffer_t *bytes_icon = raw->brush_icons->buffer[0];
						icon                 = gpu_create_texture_from_bytes(lz4_decode(bytes_icon, 256 * 256 * 4), 256, 256, GPU_TEXTURE_FORMAT_RGBA32);
					}
					if (icon == NULL) {
						render_target_t *rt = any_map_get(render_path_render_targets, "empty_black");
						icon                = rt->_image;
					}

					any_map_set(ui_files_icon_map, key, icon);
				}
				if (icon != NULL) {
					i32 w = 50;
					if (i == ui_files_selected) {
						ui_fill(-2, -2, w + 4, 2, g_theme->HIGHLIGHT_COL);
						ui_fill(-2, w + 2, w + 4, 2, g_theme->HIGHLIGHT_COL);
						ui_fill(-2, 0, 2, w + 4, g_theme->HIGHLIGHT_COL);
						ui_fill(w + 2, -2, 2, w + 6, g_theme->HIGHLIGHT_COL);
					}
					state = ui_image(icon, 0xffffffff, w * UI_SCALE());
					if (g_ui->is_hovered) {
						ui_tooltip_image(icon, 0);
						ui_tooltip(f);
					}
					generic = false;
				}
			}

			if (!is_folder && path_is_texture(f) && !is_cloud) {
				i32 w = 50;
				if (ui_files_icon_map == NULL) {
					gc_unroot(ui_files_icon_map);
					ui_files_icon_map = any_map_create();
					gc_root(ui_files_icon_map);
				}
				char *shandle = string("%s%s%s", handle->text, PATH_SEP, f);
#ifdef IRON_IOS
				shandle = string("%s%s", document_directory, shandle);
#endif
				icon = any_map_get(ui_files_icon_map, shandle);
				if (icon == NULL) {
					render_target_t *rt    = any_map_get(render_path_render_targets, "empty_black");
					gpu_texture_t   *empty = rt->_image;
					any_map_set(ui_files_icon_map, shandle, empty);
					gpu_texture_t *image = data_get_texture(shandle);

					if (image != NULL) {
						ui_files_make_icon_t *args = GC_ALLOC_INIT(ui_files_make_icon_t, {.image = image, .shandle = shandle, .w = w});
						sys_notify_on_next_frame(ui_files_make_icon, args);
					}
				}
				if (icon != NULL) {
					if (i == ui_files_selected) {
						ui_fill(-2, -2, w + 4, 2, g_theme->HIGHLIGHT_COL);
						ui_fill(-2, w + 2, w + 4, 2, g_theme->HIGHLIGHT_COL);
						ui_fill(-2, 0, 2, w + 4, g_theme->HIGHLIGHT_COL);
						ui_fill(w + 2, -2, 2, w + 6, g_theme->HIGHLIGHT_COL);
					}
					state   = ui_image(icon, 0xffffffff, icon->height * UI_SCALE());
					generic = false;
				}
			}

			if (generic) {
				state = ui_sub_image(icons, col, 50 * UI_SCALE(), rect->x, rect->y, rect->w, rect->h);
			}

			if (g_ui->is_hovered && g_ui->input_released_r && context_menu != NULL) {
				context_menu(string("%s%s%s", handle->text, PATH_SEP, f));
			}

			if (state == UI_STATE_STARTED) {
				if (drag_files) {
					base_drag_off_x = -(mouse_x - uix - g_ui->_window_x - 3);
					base_drag_off_y = -(mouse_y - uiy - g_ui->_window_y + 1);
					gc_unroot(base_drag_file);
					base_drag_file = string_copy(handle->text);
					gc_root(base_drag_file);
#ifdef IRON_IOS
					if (!is_cloud) {
						gc_unroot(base_drag_file);
						base_drag_file = string("%s%s", document_directory, base_drag_file);
						gc_root(base_drag_file);
					}
#endif
					if (!string_equals(char_at(base_drag_file, string_length(base_drag_file) - 1), PATH_SEP)) {
						gc_unroot(base_drag_file);
						base_drag_file = string("%s%s", base_drag_file, PATH_SEP);
						gc_root(base_drag_file);
					}
					gc_unroot(base_drag_file);
					base_drag_file = string("%s%s", base_drag_file, f);
					gc_root(base_drag_file);
					gc_unroot(base_drag_file_icon);
					base_drag_file_icon = icon;
					gc_root(base_drag_file_icon);
				}

				ui_files_selected = i;
				if (sys_time() - g_context->select_time < 0.2) {
					gc_unroot(base_drag_file);
					base_drag_file = NULL;
					gc_unroot(base_drag_file_icon);
					base_drag_file_icon = NULL;
					base_is_dragging    = false;
					handle->changed = g_ui->changed = true;
					if (!string_equals(char_at(handle->text, string_length(handle->text) - 1), PATH_SEP)) {
						handle->text = string("%s%s", handle->text, PATH_SEP);
					}
					handle->text      = string("%s%s", handle->text, f);
					ui_files_selected = -1;
				}
				g_context->select_time = sys_time();
			}

			// Label
			g_ui->_x = _x;
			g_ui->_y += slotw * 0.75;
			char *label0 = (is_folder || ui_files_show_extensions || string_index_of(f, ".") <= 0) ? f : substring(f, 0, string_last_index_of(f, "."));
			char *label1 = "";
			while (string_length(label0) > 0 && draw_string_width(g_font, g_ui->font_size, label0) > g_ui->_w - 6) { // 2 line split
				label1 = string("%s%s", char_at(label0, string_length(label0) - 1), label1);
				label0 = string_copy(substring(label0, 0, string_length(label0) - 1));
			}
			if (!string_equals(label1, "")) {
				g_ui->current_ratio--;
			}
			ui_text(label0, UI_ALIGN_CENTER, 0x00000000);
			if (g_ui->is_hovered) {
				ui_tooltip(string("%s%s", label0, label1));
			}
			if (!string_equals(label1, "")) { // Second line
				g_ui->_x = _x;
				g_ui->_y += draw_font_height(g_font, g_ui->font_size);
				ui_text(label1, UI_ALIGN_CENTER, 0x00000000);
				if (g_ui->is_hovered) {
					ui_tooltip(string("%s%s", label0, label1));
				}
				g_ui->_y -= draw_font_height(g_font, g_ui->font_size);
			}

			g_ui->_y -= slotw * 0.75;

			if (handle->changed) {
				break;
			}
		}
		if (handle->changed) {
			break;
		}
	}
	g_ui->_y += slotw * 0.8;
	return handle->text;
}

void ui_files_enter_selected(ui_handle_t *handle) {
	if (ui_files_files == NULL || ui_files_selected < 0 || ui_files_selected >= ui_files_files->length) {
		return;
	}
	char *f = ui_files_files->buffer[ui_files_selected];
	if (!string_equals(char_at(handle->text, string_length(handle->text) - 1), PATH_SEP)) {
		handle->text = string("%s%s", handle->text, PATH_SEP);
	}
	handle->text      = string("%s%s", handle->text, f);
	handle->changed   = true;
	ui_files_selected = -1;
}

void ui_files_navigate(i32 dx, i32 dy) {
	if (ui_files_files == NULL || ui_files_files->length == 0) {
		return;
	}
	if (ui_files_selected < 0) {
		ui_files_selected = 0;
		return;
	}
	i32 next = ui_files_selected + dx + dy * ui_files_num_cols;
	if (next >= 0 && next < ui_files_files->length) {
		ui_files_selected = next;
	}
}

void ui_files_go_up(ui_handle_t *handle) {
	i32 sep = string_last_index_of(handle->text, PATH_SEP);
	gc_unroot(ui_files_select_pending);
	ui_files_select_pending = string_copy(substring(handle->text, sep + 1, string_length(handle->text)));
	gc_root(ui_files_select_pending);
	handle->text = string_copy(substring(handle->text, 0, sep));
	// Drive root
	if (string_length(handle->text) == 2 && string_equals(char_at(handle->text, 1), ":")) {
		handle->text = string("%s%s", handle->text, PATH_SEP);
	}
}
