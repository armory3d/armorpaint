
#include "../global.h"

any_map_t *box_projects_icon_map = NULL;
char      *_box_projects_path;
char      *_box_projects_icon_path;
i32        _box_projects_i;

void box_projects_tab_menu_on_next_frame(void *_) {
	iron_delete_file(_box_projects_path);
	iron_delete_file(_box_projects_icon_path);
	char *data_path = substring(_box_projects_path, 0, string_length(_box_projects_path) - 4);
	iron_delete_file(data_path);
	string_array_t *recent_projects = g_config->recent_projects;
	array_splice(recent_projects, _box_projects_i, 1);
}

void box_projects_tab_menu() {
	// if (ui_menu_button(tr("Duplicate"), "ctrl+d", icon_t.DUPLICATE)) {}
	if (ui_menu_button(tr("Delete"), "delete", ICON_DELETE)) {
		sys_notify_on_next_frame(&box_projects_tab_menu_on_next_frame, NULL);
	}
}

void box_projects_tab_on_next_frame(char *path) {
	ui_box_hide();
	import_arm_run_project(path);
}

void box_projects_draw_badge() {
	gpu_texture_t *img = data_get_texture("badge.k");
	ui_image(img, 0xffffffff, -1.0);
	ui_end_element();
}

void box_projects_tab() {
	if (ui_tab(box_projects_htab, tr("Projects"), true, -1, false)) {
		ui_begin_sticky();

		ui_separator(UI_ELEMENT_H(), false);

		box_projects_draw_badge();

		if (ui_icon_button(tr("New"), ICON_PLUS, UI_ALIGN_CENTER)) {
			project_new(true);
			ui_box_hide();
			// Pick unique name
			i32   i     = 0;
			i32   j     = 0;
			char *title = string("%s%s", tr("untitled"), i32_to_string(i));
			while (j < g_config->recent_projects->length) {
				char *base = g_config->recent_projects->buffer[j];
				base       = string_copy(substring(base, string_last_index_of(base, PATH_SEP) + 1, string_last_index_of(base, ".")));
				j++;
				if (string_equals(title, base)) {
					i++;
					title = string("%s%s", tr("untitled"), i32_to_string(i));
					j     = 0;
				}
			}
			sys_title_set(title);
		}
		ui_end_sticky();
		ui_separator(3, false);

		i32 slotw = math_floor(150 * UI_SCALE());
		i32 num   = math_floor(iron_window_width() / (float)slotw);
		if (num == 0) {
			return;
		}
		string_array_t *recent_projects  = g_config->recent_projects;
		bool            show_asset_names = true;

		for (i32 row = 0; row < math_ceil(recent_projects->length / (float)num); ++row) {
			i32          mult = show_asset_names ? 2 : 1;
			f32_array_t *ar   = f32_array_create_from_raw((f32[]){}, 0);
			for (i32 i = 0; i < num * mult; ++i) {
				f32_array_push(ar, 1 / (float)num);
			}
			ui_row(ar);

			g_ui->_x += 2;
			f32 off = show_asset_names ? 96 * UI_SCALE() : 16 * UI_SCALE();
			if (row > 0) {
				g_ui->_y += off;
			}

			for (i32 j = 0; j < num; ++j) {
				i32 imgw = math_floor(128 * UI_SCALE());
				i32 i    = j + row * num;
				if (i >= recent_projects->length) {
					ui_end_element_of_size(imgw);
					if (show_asset_names) {
						ui_end_element_of_size(0);
					}
					continue;
				}

				char *path = recent_projects->buffer[i];

#ifdef IRON_IOS
				char *document_directory = iron_save_dialog("", "");
				document_directory       = string_copy(substring(document_directory, 0, string_length(document_directory) - 8)); // Strip /"untitled"
				path                     = string("%s%s", document_directory, path);
#endif

				char *icon_path = string("%s_icon.png", substring(path, 0, string_length(path) - 4));
				if (box_projects_icon_map == NULL) {
					gc_unroot(box_projects_icon_map);
					box_projects_icon_map = any_map_create();
					gc_root(box_projects_icon_map);
				}
				gpu_texture_t *icon = any_map_get(box_projects_icon_map, icon_path);
				if (icon == NULL) {
					gpu_texture_t *image = data_get_texture(icon_path);
					icon                 = image;
					any_map_set(box_projects_icon_map, icon_path, icon);
				}

				i32 uix = g_ui->_x;
				if (icon != NULL) {
					ui_fill(0, 0, 128, 128, g_theme->SEPARATOR_COL);

					i32 state = ui_image(icon, 0xffffffff, 128 * UI_SCALE());
					if (state == UI_STATE_RELEASED) {
						i32 _uix = g_ui->_x;
						g_ui->_x = uix;
						ui_fill(0, 0, 128, 128, 0x66000000);
						g_ui->_x = _uix;
#if defined(IRON_ANDROID) || defined(IRON_IOS)
						console_toast(tr("Opening project"));
#endif
						sys_notify_on_next_frame(&box_projects_tab_on_next_frame, path);
					}

					char *name = substring(path, string_last_index_of(path, PATH_SEP) + 1, string_last_index_of(path, "."));
					if (g_ui->is_hovered && g_ui->input_released_r) {
						gc_unroot(_box_projects_path);
						_box_projects_path = string_copy(path);
						gc_root(_box_projects_path);
						gc_unroot(_box_projects_icon_path);
						_box_projects_icon_path = string_copy(icon_path);
						gc_root(_box_projects_icon_path);
						_box_projects_i = i;
						ui_menu_draw(&box_projects_tab_menu, -1, -1);
					}

					if (show_asset_names) {
						g_ui->_x = uix - (150 - 128) / 2.0;
						g_ui->_y += slotw * 0.9;
						ui_text(name, UI_ALIGN_CENTER, 0x00000000);
						if (g_ui->is_hovered) {
							ui_tooltip(name);
						}
						g_ui->_y -= slotw * 0.9;
						if (i == recent_projects->length - 1) {
							g_ui->_y += j == num - 1 ? imgw : imgw + UI_ELEMENT_H() + UI_ELEMENT_OFFSET();
						}
					}
				}
				else {
					ui_end_element_of_size(0);
					if (show_asset_names) {
						ui_end_element_of_size(0);
					}
					g_ui->_x = uix;
				}
			}

			g_ui->_y += 150;
		}
	}
}

void box_projects_get_started_tab() {
	if (ui_tab(box_projects_htab, tr("Get Started"), true, -1, false)) {

		ui_separator(UI_ELEMENT_H(), false);

		if (ui_icon_button(tr("Manual"), ICON_HELP, UI_ALIGN_CENTER)) {
			iron_load_url("https://armorpaint.org/manual");
		}
		if (ui_icon_button(tr("How To"), ICON_HELP, UI_ALIGN_CENTER)) {
			iron_load_url("https://forums.armorpaint.org/c/how-to");
		}
		if (ui_icon_button(tr("What's New"), ICON_LINK, UI_ALIGN_CENTER)) {
			iron_load_url("https://forums.armorpaint.org/c/news");
		}
	}
}

void box_projects_align_to_fullscreen() {
	ui_box_modalw       = math_floor(iron_window_width() / (float)UI_SCALE());
	ui_box_modalh       = math_floor(iron_window_height() / (float)UI_SCALE());
	i32 appw            = iron_window_width();
	i32 apph            = iron_window_height();
	i32 mw              = appw;
	i32 mh              = apph;
	ui_box_hwnd->drag_x = math_floor(-appw / 2.0 + mw / 2.0);
	ui_box_hwnd->drag_y = math_floor(-apph / 2.0 + mh / 2.0);
}

void box_projects_show_box() {
#if defined(IRON_ANDROID) || defined(IRON_IOS)
	box_projects_align_to_fullscreen();
#endif

#if defined(IRON_ANDROID) || defined(IRON_IOS)
	box_projects_tab();
	box_projects_get_started_tab();
#else
	box_projects_recent_tab();
#endif
}

void box_projects_show() {
	if (box_projects_icon_map != NULL) {
		string_array_t *keys = map_keys(box_projects_icon_map);
		for (i32 i = 0; i < keys->length; ++i) {
			char *handle = keys->buffer[i];
			data_delete_texture(handle);
		}
		gc_unroot(box_projects_icon_map);
		box_projects_icon_map = NULL;
	}

	bool draggable;
#if defined(IRON_ANDROID) || defined(IRON_IOS)
	draggable = false;
#else
	draggable = true;
#endif

	ui_box_show_custom(&box_projects_show_box, 600, 400, NULL, draggable, "");
}

void box_projects_recent_load(char *path) {
	if (!iron_file_exists(path)) {
		return;
	}
	gpu_texture_t *current = _draw_current;
	bool           in_use  = gpu_in_use;
	if (in_use)
		draw_end();

	import_arm_run_project(path);

	if (in_use)
		draw_begin(current, false, 0);
	ui_box_hide();
}

void box_projects_recent_tab() {
	if (ui_tab(box_projects_htab, tr("Recent"), true, -1, false)) {

		box_projects_draw_badge();

		g_ui->enabled              = g_config->recent_projects->length > 0;
		bool was_typing            = g_ui->is_typing;
		box_projects_hsearch->text = string_copy(ui_text_input(box_projects_hsearch, tr("Search"), UI_ALIGN_LEFT, true, true));
		g_ui->enabled              = true;
		char *first_path           = NULL; // Most recent project that passes the search filter
		for (i32 i = 0; i < g_config->recent_projects->length; ++i) {
			char *path = g_config->recent_projects->buffer[i];
#ifdef IRON_WINDOWS
			path = string_copy(string_replace_all(path, "/", "\\"));
#else
			path = string_copy(string_replace_all(path, "\\", "/"));
#endif
			char *file = substring(path, string_last_index_of(path, PATH_SEP) + 1, string_length(path));

			if (string_index_of(to_lower_case(file), to_lower_case(box_projects_hsearch->text)) < 0) {
				continue; // Search filter
			}
			if (first_path == NULL) {
				first_path = path;
			}
			if (ui_button(file, UI_ALIGN_LEFT, "")) {
				box_projects_recent_load(path);
			}
			if (g_ui->is_hovered) {
				ui_tooltip(path);
			}
		}

		if (first_path != NULL && !was_typing && g_ui->is_key_pressed && g_ui->key_code == KEY_CODE_RETURN) {
			box_projects_recent_load(first_path);
		}

		g_ui->enabled = g_config->recent_projects->length > 0;
		if (ui_icon_button(tr("Clear"), ICON_ERASE, UI_ALIGN_LEFT)) {
			g_config->recent_projects = any_array_create_from_raw((void *[]){}, 0);
			config_save();
		}
		g_ui->enabled = true;

		ui_end_element();
		if (ui_icon_button(tr("New Project..."), ICON_FILE_NEW, UI_ALIGN_LEFT)) {
			project_new_box();
		}
		if (ui_icon_button(tr("Open..."), ICON_FOLDER_OPEN, UI_ALIGN_LEFT)) {
			project_open();
			ui_box_hide();
		}
	}
}
