
#include "../global.h"

bool  tab_browser_known     = false;
char *tab_browser_last_path = "";
char *_tab_browser_draw_file;
char *_tab_browser_draw_b;

void tab_browser_show_directory(char *directory) {
	tab_browser_hpath->text                        = string_copy(directory);
	tab_browser_hsearch->text                      = "";
	ui_base_htabs->buffer[TAB_AREA_STATUS]->i      = 0;
	g_config->layout_tabs->buffer[TAB_AREA_STATUS] = 0;
}

void tab_browser_draw_bookmark_menu() {
	if (ui_menu_button(tr("Delete"), "", ICON_DELETE)) {
		string_array_remove(g_config->bookmarks, _tab_browser_draw_b);
		config_save();
	}
}

void tab_browser_draw_import_asset(char *path) {
	import_asset_run(path, -1.0, -1.0, true, true, NULL);
}

void tab_browser_draw_set_as_color_id_map_on_next_frame(void *_) {
	char *file        = _tab_browser_draw_file;
	i32   asset_index = -1;
	for (i32 i = 0; i < g_project->_->assets->length; ++i) {
		if (string_equals(g_project->_->assets->buffer[i]->file, file)) {
			asset_index = i;
			break;
		}
	}

	if (asset_index != -1) {
		g_context->colorid         = asset_index;
		g_context->colorid_picked  = false;
		ui_toolbar_handle->redraws = 1;
		if (g_context->tool == TOOL_TYPE_COLORID) {
			ui_header_handle->redraws = 2;
			g_context->ddirty         = 2;
		}
	}
}

void tab_browser_draw_set_as_color_id_map() {
	sys_notify_on_next_frame(&tab_browser_draw_set_as_color_id_map_on_next_frame, NULL);
}

void tab_browser_draw_set_as_mask_on_next_frame(void *_) {
	char *file        = _tab_browser_draw_file;
	i32   asset_index = -1;
	for (i32 i = 0; i < g_project->_->assets->length; ++i) {
		if (string_equals(g_project->_->assets->buffer[i]->file, file)) {
			asset_index = i;
			break;
		}
	}
	if (asset_index != -1) {
		layers_create_image_mask(g_project->_->assets->buffer[asset_index]);
	}
}

void tab_browser_draw_set_as_mask() {
	sys_notify_on_next_frame(&tab_browser_draw_set_as_mask_on_next_frame, NULL);
}

void tab_browser_draw_set_as_envmap_on_next_frame(void *_) {
	char *file        = _tab_browser_draw_file;
	i32   asset_index = -1;
	for (i32 i = 0; i < g_project->_->assets->length; ++i) {
		if (string_equals(g_project->_->assets->buffer[i]->file, file)) {
			asset_index = i;
			break;
		}
	}

	if (asset_index != -1) {
		import_envmap_run(file, project_get_image(g_project->_->assets->buffer[asset_index]));
	}
}

void tab_browser_draw_set_as_envmap() {
	sys_notify_on_next_frame(&tab_browser_draw_set_as_envmap_on_next_frame, NULL);
}

void tab_browser_draw_context_menu_draw() {
	char *file = _tab_browser_draw_file;
	if (ui_menu_button(tr("Import"), "", ICON_IMPORT)) {
		import_asset_run(file, -1.0, -1.0, true, true, NULL);
	}
	if (path_is_texture(file)) {
		if (ui_menu_button(tr("Set as Envmap"), "", ICON_LANDSCAPE)) {
			import_asset_run(file, -1.0, -1.0, true, true, &tab_browser_draw_set_as_envmap);
		}
		if (ui_menu_button(tr("Set as Mask"), "", ICON_MASK)) {
			import_asset_run(file, -1.0, -1.0, true, true, &tab_browser_draw_set_as_mask);
		}
		if (ui_menu_button(tr("Set as Color ID Map"), "", ICON_COLOR_ID)) {
			import_asset_run(file, -1.0, -1.0, true, true, &tab_browser_draw_set_as_color_id_map);
		}
	}
	if (ui_menu_button(tr("Open Externally"), "", ICON_NONE)) {
		file_start(file);
	}
}

void tab_browser_draw_context_menu(char *file) {
	gc_unroot(_tab_browser_draw_file);
	_tab_browser_draw_file = string_copy(file);
	gc_root(_tab_browser_draw_file);

	// Context menu
	ui_menu_draw(&tab_browser_draw_context_menu_draw, -1, -1);
}

void tab_browser_go_to_cloud() {
	tab_browser_hpath->text = "cloud";
}

void tab_browser_go_to_disk() {
#ifdef IRON_ANDROID
	tab_browser_hpath->text = string_copy(iron_internal_save_path());
#else
	tab_browser_hpath->text = string_copy(ui_files_default_path);
#endif
}

void tab_browser_draw_side_menu() {
	if (ui_menu_button(tr("Cloud"), "", ICON_CLOUD)) {
		tab_browser_go_to_cloud();
	}
	if (ui_menu_button(tr("Disk"), "", ICON_STORAGE)) {
		tab_browser_go_to_disk();
	}
}

void tab_browser_draw(ui_handle_t *htab) {
	char *title = tr("Browser");

#ifdef IRON_IOS
	if (config_is_iphone()) {
		title = string("  %s", title);
	}
#endif

	if (ui_tab(htab, title, false, -1, false) && g_ui->_window_h > ui_statusbar_default_h * UI_SCALE()) {
		if (g_config->bookmarks == NULL) {
			g_config->bookmarks = any_array_create_from_raw((void *[]){}, 0);
		}

		i32  bookmarks_w = math_floor(100 * UI_SCALE());
		bool show_full   = g_ui->_w > (500 * UI_SCALE());
		bool in_focus    = g_ui->input_x > g_ui->_window_x && g_ui->input_x < g_ui->_window_x + g_ui->_window_w && g_ui->input_y > g_ui->_window_y &&
		                g_ui->input_y < g_ui->_window_y + g_ui->_window_h;

		if (string_equals(tab_browser_hpath->text, "") && g_config->bookmarks->length > 0) { // Init to first bookmark
			tab_browser_hpath->text = string_copy(g_config->bookmarks->buffer[0]);
#ifdef IRON_WINDOWS
			tab_browser_hpath->text = string_copy(string_replace_all(tab_browser_hpath->text, "/", "\\"));
#endif
		}

		ui_begin_sticky();

		if (show_full) {
			f32 step = (1.0 - bookmarks_w / (float)g_ui->_w);
			if (!string_equals(tab_browser_hsearch->text, "")) {
				f32_array_t *row = f32_array_create_from_raw(
				    (f32[]){
				        bookmarks_w / (float)g_ui->_w,
				        step * 0.07,
				        step * 0.07,
				        step * 0.66,
				        step * 0.17,
				        step * 0.03,
				    },
				    6);
				ui_row(row);
			}
			else {
				f32_array_t *row = f32_array_create_from_raw(
				    (f32[]){
				        bookmarks_w / (float)g_ui->_w,
				        step * 0.07,
				        step * 0.07,
				        step * 0.66,
				        step * 0.2,
				    },
				    5);
				ui_row(row);
			}

			// Bookmark
			if (ui_icon_button(tr("Bookmark"), ICON_PLUS, UI_ALIGN_LEFT)) {
				char *bookmark = tab_browser_hpath->text;
#ifdef IRON_WINDOWS
				bookmark = string_copy(string_replace_all(bookmark, "\\", "/"));
#endif
				any_array_push(g_config->bookmarks, bookmark);
				config_save();
			}

			// Refresh
			in_focus = g_ui->input_x > g_ui->_window_x && g_ui->input_x < g_ui->_window_x + g_ui->_window_w && g_ui->input_y > g_ui->_window_y &&
			           g_ui->input_y < g_ui->_window_y + g_ui->_window_h;
			if (ui_icon_button(tr("Refresh"), ICON_REFRESH, UI_ALIGN_CENTER) || (in_focus && g_ui->is_key_pressed && g_ui->key_code == KEY_CODE_F5)) {
				tab_browser_refresh = true;
			}
		}
		else {
			// Menu, Up, Refresh
			f32_array_t *row = f32_array_create_from_raw(
			    (f32[]){
			        0.5 / 4.0,
			        0.5 / 4.0,
			        3.0 / 4.0,
			    },
			    3);
			ui_row(row);
			if (ui_icon_button("", ICON_MENU, UI_ALIGN_CENTER)) {
				ui_menu_draw(&tab_browser_draw_side_menu, -1, -1);
			}
		}

		// Previous folder
		char *text   = tab_browser_hpath->text;
		i32   i1     = string_index_of(text, PATH_SEP);
		bool  nested = i1 > -1 && string_length(text) - 1 > i1;
#ifdef IRON_WINDOWS
		// Server addresses like \\server are not nested
		nested = nested && !(string_length(text) >= 2 && string_equals(char_at(text, 0), PATH_SEP) && string_equals(char_at(text, 1), PATH_SEP) &&
		                     string_last_index_of(text, PATH_SEP) == 1);
#endif
		g_ui->enabled = nested;
		if (ui_icon_button("", ICON_CHEVRON_LEFT, UI_ALIGN_CENTER)) {
			ui_files_go_up(tab_browser_hpath);
		}
		g_ui->enabled = true;
		if (g_ui->is_hovered) {
			ui_tooltip(tr("Previous folder"));
		}

#ifdef IRON_ANDROID
		bool  stripped = false;
		char *strip    = "/storage/emulated/0/";
		if (starts_with(tab_browser_hpath->text, strip)) {
			tab_browser_hpath->text = string_copy(substring(tab_browser_hpath->text, string_length(strip) - 1, string_length(tab_browser_hpath->text)));
			stripped                = true;
		}
#endif

		tab_browser_hpath->text = string_copy(ui_text_input(tab_browser_hpath, tr("Path"), UI_ALIGN_LEFT, true, false));

#ifdef IRON_ANDROID
		if (stripped) {
			tab_browser_hpath->text = string("/storage/emulated/0%s", tab_browser_hpath->text);
		}
#endif

		if (show_full) {
			tab_browser_hsearch->text = string_copy(ui_text_input(tab_browser_hsearch, tr("Search"), UI_ALIGN_LEFT, true, true));
			if (g_ui->is_hovered) {
				ui_tooltip(string("%s\n%s", tr("ctrl+f to search"), tr("esc to cancel")));
			}
			if (g_ui->is_ctrl_down && g_ui->is_key_pressed && g_ui->key_code == KEY_CODE_F) { // Start searching via ctrl+f
				ui_start_text_edit(tab_browser_hsearch, UI_ALIGN_LEFT);
			}
			if (!string_equals(tab_browser_hsearch->text, "") && (ui_button(tr("X"), UI_ALIGN_CENTER, "") || g_ui->is_escape_down)) {
				tab_browser_hsearch->text = "";
			}
		}

		ui_end_sticky();

		if (!string_equals(tab_browser_last_path, tab_browser_hpath->text)) {
			tab_browser_hsearch->text = "";
		}
		gc_unroot(tab_browser_last_path);
		tab_browser_last_path = string_copy(tab_browser_hpath->text);
		gc_root(tab_browser_last_path);

		f32 _y = g_ui->_y;
		if (show_full) {
			g_ui->_x = bookmarks_w;
			g_ui->_w -= bookmarks_w;
		}

		ui_files_file_browser(tab_browser_hpath, true, tab_browser_hsearch->text, tab_browser_refresh, &tab_browser_draw_context_menu);

		tab_browser_refresh = false;

		if (tab_browser_known) {
			char *path = tab_browser_hpath->text;
			sys_notify_on_next_frame(&tab_browser_draw_import_asset, path);
			tab_browser_hpath->text = string_copy(substring(tab_browser_hpath->text, 0, string_last_index_of(tab_browser_hpath->text, PATH_SEP)));
		}
		char *hpath_text = tab_browser_hpath->text;
		tab_browser_known =
		    string_index_of(substring(tab_browser_hpath->text, string_last_index_of(tab_browser_hpath->text, PATH_SEP), string_length(hpath_text)), ".") > 0;
#ifdef IRON_ANDROID
		if (ends_with(tab_browser_hpath->text, string(".%s", to_lower_case(manifest_title)))) {
			tab_browser_known = false;
		}
#endif
		if (tab_browser_known && iron_is_directory(tab_browser_hpath->text)) {
			tab_browser_known = false;
		}

		if (show_full) {
			i32 bottom_y = g_ui->_y;
			g_ui->_x     = 0;
			g_ui->_y     = _y;
			g_ui->_w     = bookmarks_w;

			if (ui_icon_button(tr("Cloud"), ICON_CLOUD, UI_ALIGN_LEFT)) {
				tab_browser_go_to_cloud();
			}

			if (ui_icon_button(tr("Disk"), ICON_STORAGE, UI_ALIGN_LEFT)) {
				tab_browser_go_to_disk();
			}

			for (i32 i = 0; i < g_config->bookmarks->length; ++i) {
				char *b      = g_config->bookmarks->buffer[i];
				char *folder = substring(b, string_last_index_of(b, "/") + 1, string_length(b));

				if (ui_icon_button(folder, ICON_FOLDER, UI_ALIGN_LEFT)) {
					tab_browser_hpath->text = string_copy(b);
#ifdef IRON_WINDOWS
					tab_browser_hpath->text = string_copy(string_replace_all(tab_browser_hpath->text, "/", "\\"));
#endif
				}

				if (g_ui->is_hovered && g_ui->input_released_r) {
					gc_unroot(_tab_browser_draw_b);
					_tab_browser_draw_b = string_copy(b);
					gc_root(_tab_browser_draw_b);
					ui_menu_draw(&tab_browser_draw_bookmark_menu, -1, -1);
				}
			}
			if (g_ui->_y < bottom_y) {
				g_ui->_y = bottom_y;
			}
		}

		if (in_focus && !g_ui->is_typing) {
			if (g_ui->is_key_pressed && g_ui->key_code == KEY_CODE_LEFT)
				ui_files_navigate(-1, 0);
			if (g_ui->is_key_pressed && g_ui->key_code == KEY_CODE_RIGHT)
				ui_files_navigate(1, 0);
			if (g_ui->is_key_pressed && g_ui->key_code == KEY_CODE_UP)
				ui_files_navigate(0, -1);
			if (g_ui->is_key_pressed && g_ui->key_code == KEY_CODE_DOWN)
				ui_files_navigate(0, 1);
			if (g_ui->is_key_pressed && g_ui->key_code == KEY_CODE_RETURN)
				ui_files_enter_selected(tab_browser_hpath);
			if (g_ui->is_key_pressed && g_ui->key_code == KEY_CODE_BACKSPACE && nested)
				ui_files_go_up(tab_browser_hpath);
			if (g_ui->is_escape_down)
				ui_files_selected = -1;
		}
	}
}
