
#include "../global.h"

ui_text_coloring_t *tab_scripts_text_coloring    = NULL;
i32                 tab_scripts_selected         = 0;
gpu_texture_t      *tab_scripts_minimap_tex      = NULL;
i32                 tab_scripts_minimap_selected = -1;
extern bool         tab_scripts_minimap_dirty;
bool                tab_scripts_minimap_scrolling = false;

void tab_scripts_prepare() {
	if (g_project->script_datas == NULL) {
		g_project->script_datas = string_array_create(0);
		g_project->script_names = string_array_create(0);
	}
	if (g_project->script_datas->length == 0) {
		string_array_push(g_project->script_datas, "void main() {\n    \n}\n");
		string_array_push(g_project->script_names, "main.c");
	}
	// A script may have been removed
	if (tab_scripts_selected >= g_project->script_datas->length) {
		tab_scripts_selected = g_project->script_datas->length - 1;
	}
}

char *tab_scripts_get() {
	tab_scripts_prepare();
	return g_project->script_datas->buffer[tab_scripts_selected];
}

void tab_scripts_set(char *s) {
	tab_scripts_prepare();
	g_project->script_datas->buffer[tab_scripts_selected] = string_copy(s);
	tab_scripts_minimap_dirty                             = true;
}

static void tab_scripts_strip_line_whitespace(char *s) {
	int len   = string_length(s);
	int write = 0;
	int read  = 0;
	while (read < len) {
		// Find end of the current line
		int line_end = read;
		while (line_end < len && s[line_end] != '\n') {
			++line_end;
		}
		// Trim trailing spaces/tabs
		int trimmed_end = line_end;
		while (trimmed_end > read && (s[trimmed_end - 1] == ' ' || s[trimmed_end - 1] == '\t')) {
			--trimmed_end;
		}
		for (int i = read; i < trimmed_end; ++i) {
			s[write++] = s[i];
		}
		if (line_end < len) { // Keep the newline
			s[write++] = '\n';
		}
		read = line_end + 1;
	}
	s[write] = '\0';
}

void tab_scripts_strip_trailing_whitespace() {
	if (g_project->script_datas == NULL) {
		return;
	}
	for (i32 i = 0; i < g_project->script_datas->length; ++i) {
		g_project->script_datas->buffer[i] = string_copy(g_project->script_datas->buffer[i]);
		tab_scripts_strip_line_whitespace(g_project->script_datas->buffer[i]);
	}
}

void tab_scripts_create(char *name) {
	tab_scripts_prepare();
	i32 i = string_array_index_of(g_project->script_names, name);
	if (i < 0) {
		string_array_push(g_project->script_names, string_copy(name));
		string_array_push(g_project->script_datas, "");
		tab_scripts_selected = g_project->script_datas->length - 1;
	}
}

void tab_scripts_draw_export(char *path) {
	char *str = tab_scripts_get();
	char *f   = ui_files_filename;
	if (string_equals(f, "")) {
		f = string_copy(tr("untitled"));
	}
	path = string("%s%s%s", path, PATH_SEP, f);
	if (!ends_with(path, ".c")) {
		path = string("%s.c", path);
	}
	iron_file_save_bytes(path, sys_string_to_buffer(str), 0);
}

void tab_scripts_draw_import(char *path) {
	buffer_t *b = data_get_blob(path);
	tab_scripts_set(sys_buffer_to_string(b));
	data_delete_blob(path);
}

void tab_scripts_draw_edit() {
	tab_scripts_prepare();

	if (ui_menu_button(tr("Clear"), "", ICON_ERASE)) {
		tab_scripts_set("");
	}
	g_ui->enabled = !string_equals(g_project->script_names->buffer[tab_scripts_selected], "main.c");
	if (ui_menu_button(tr("Delete"), "", ICON_DELETE)) {
		array_splice((any_array_t *)g_project->script_datas, tab_scripts_selected, 1);
		array_splice((any_array_t *)g_project->script_names, tab_scripts_selected, 1);
		tab_scripts_selected = 0;
	}
	g_ui->enabled = true;
	if (ui_menu_button(tr("Import"), "", ICON_IMPORT)) {
		ui_files_show("c", false, false, &tab_scripts_draw_import);
	}
	if (ui_menu_button(tr("Export"), "", ICON_EXPORT)) {
		ui_files_show("c", true, false, &tab_scripts_draw_export);
	}
	if (ui_menu_sub_button(ui_handle(__ID__), tr("Templates"))) {
		ui_menu_sub_begin(3);
		if (ui_menu_button("hello.c", "", ICON_DRAFT)) {
			tab_scripts_set("\
void main() {\n\
	printf(\"Hello, world!\\n\");\n\
}\n\
");
		}
		if (ui_menu_button("rotate.c", "", ICON_DRAFT)) {
			tab_scripts_set("\
void on_update() {\n\
	mesh_object_t *o = context_main_object();\n\
	transform_rotate(o->base->transform, vec4_z_axis(), 0.005);\n\
	context_t *c = script_get_context();\n\
	c->ddirty = 2;\n\
}\n\
void main() {\n\
	script_notify_on_update(on_update);\n\
}\n\
");
		}
		if (ui_menu_button("turntable.c", "", ICON_DRAFT)) {
			tab_scripts_set("\
void on_update() {\n\
	mesh_object_t *o = context_main_object();\n\
	if (!mouse_down(\"right\")) {\n\
		transform_rotate(o->base->transform, vec4_z_axis(), 0.003);\n\
	}\n\
	context_t *c = script_get_context();\n\
	c->ddirty = 2;\n\
}\n\
void main() {\n\
	context_set_viewport_mode(1); // base\n\
	script_show_envmap(true);\n\
	script_notify_on_update(on_update);\n\
}\n\
");
		}
		ui_menu_sub_end();
	}
}

// Autocomplete popup (ctrl+space) listing the script api from minic_api.c
#define TAB_SCRIPTS_AC_MAX 8
bool tab_scripts_ac_show   = false;
i32  tab_scripts_ac_offset = 0;

static bool tab_scripts_is_ident(char c) {
	return c == '_' || (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static i32 tab_scripts_line_start(char *text, i32 line) {
	i32 pos = 0;
	i32 l   = 0;
	while (l < line && text[pos] != '\0') {
		if (text[pos] == '\n') {
			l++;
		}
		pos++;
	}
	return pos;
}

static i32 tab_scripts_prefix_len(char *text, i32 cursor) {
	i32 n = 0;
	while (cursor - n - 1 >= 0 && tab_scripts_is_ident(text[cursor - n - 1])) {
		n++;
	}
	return n;
}

static i32 tab_scripts_suffix_len(char *text, i32 cursor) {
	i32 n = 0;
	while (tab_scripts_is_ident(text[cursor + n])) {
		n++;
	}
	return n;
}

// Collect up to TAB_SCRIPTS_AC_MAX api symbols containing prefix
static i32 tab_scripts_autocomplete_matches(char *prefix, char **names) {
	char *lower_prefix = to_lower_case(prefix);
	i32   func_count   = minic_ext_func_count_get();
	i32   global_count = minic_global_count_get();
	i32   total        = func_count + global_count;
	i32   n            = 0;
	for (i32 i = 0; i < total && n < TAB_SCRIPTS_AC_MAX; ++i) {
		char *name = (char *)(i < func_count ? minic_ext_func_name_at(i) : minic_global_name_at(i - func_count));
		if (starts_with(to_lower_case(name), lower_prefix)) {
			names[n++] = name;
		}
	}
	for (i32 i = 0; i < total && n < TAB_SCRIPTS_AC_MAX; ++i) {
		char *name  = (char *)(i < func_count ? minic_ext_func_name_at(i) : minic_global_name_at(i - func_count));
		char *lower = to_lower_case(name);
		if (!starts_with(lower, lower_prefix) && string_index_of(lower, lower_prefix) >= 0) {
			names[n++] = name;
		}
	}
	return n;
}

// Replace the whole identifier around the cursor with the chosen symbol
static void tab_scripts_autocomplete_complete(char *name, i32 prefix_len, i32 suffix_len) {
	tab_scripts_prepare();
	char *text   = g_project->script_datas->buffer[tab_scripts_selected];
	i32   line   = tab_scripts_hscript->i;
	i32   col    = g_ui->cursor_x;
	i32   cur    = tab_scripts_line_start(text, line) + col;
	char *before = substring(text, 0, cur - prefix_len);
	char *after  = substring(text, cur + suffix_len, string_length(text));
	tab_scripts_set(string("%s%s%s", before, name, after));
	tab_scripts_hscript->text = g_project->script_datas->buffer[tab_scripts_selected];
	i32 new_col               = col - prefix_len + string_length(name);
	g_ui->cursor_x            = new_col;
	g_ui->highlight_anchor    = new_col;
	g_ui->cursor_sticky_x     = new_col;
	strcpy(g_ui->text_selected, ui_extract_line(tab_scripts_hscript->text, line)); // Keep the active line in sync
	tab_scripts_ac_show = false;
}

static void tab_scripts_toggle_comment() {
	tab_scripts_prepare();
	char *text  = g_project->script_datas->buffer[tab_scripts_selected];
	i32   line  = tab_scripts_hscript->i;
	i32   col   = g_ui->cursor_x;
	i32   start = tab_scripts_line_start(text, line);
	i32   end   = start;
	while (text[end] != '\0' && text[end] != '\n') {
		end++;
	}
	// Indentation level: first non-space char on the line
	i32 ind = start;
	while (ind < end && text[ind] == ' ') {
		ind++;
	}
	i32 ind_col = ind - start;

	i32   delta;
	char *before = substring(text, 0, ind);
	if (text[ind] == '/' && ind + 1 < end && text[ind + 1] == '/') {
		// Remove "// " or "//"
		i32   remove = (ind + 2 < end && text[ind + 2] == ' ') ? 3 : 2;
		char *after  = substring(text, ind + remove, string_length(text));
		tab_scripts_set(string("%s%s", before, after));
		delta = -remove;
	}
	else {
		// Prepend "// "
		char *after = substring(text, ind, string_length(text));
		tab_scripts_set(string("%s// %s", before, after));
		delta = 3;
	}

	tab_scripts_hscript->text = g_project->script_datas->buffer[tab_scripts_selected];

	// Keep the caret on the same characters
	i32 new_col;
	if (col <= ind_col) {
		new_col = col; // Caret within indentation, unaffected
	}
	else if (delta < 0 && col < ind_col - delta) {
		new_col = ind_col; // Caret was inside the removed "// "
	}
	else {
		new_col = col + delta;
	}
	g_ui->cursor_x         = new_col;
	g_ui->highlight_anchor = new_col;
	g_ui->cursor_sticky_x  = new_col;
	strcpy(g_ui->text_selected, ui_extract_line(tab_scripts_hscript->text, line)); // Keep the active line in sync
}

static bool tab_scripts_minimap_visible(f32 *x, f32 *y, f32 *w, f32 *h) {
	if (g_ui->_window_w <= 800 * UI_SCALE()) {
		return false;
	}
	f32 mm_w = 150 * UI_SCALE();
	*x       = g_ui->_window_w - mm_w;
	*y       = g_ui->window_header_h;
	*w       = mm_w;
	*h       = g_ui->_window_h - g_ui->window_header_h;
	return true;
}

static void tab_scripts_cache_minimap() {
	tab_scripts_minimap_dirty = false;

	char *text = tab_scripts_get();
	if (text == NULL) {
		return;
	}
	f32             line_h = 2 * UI_SCALE();
	f32             char_w = 1 * UI_SCALE();
	i32             tex_w  = math_floor(150 * UI_SCALE());
	string_array_t *lines  = string_split(text, "\n");
	i32             tex_h  = math_floor(lines->length * line_h);
	if (tex_h < 1) {
		tex_h = 1;
	}

	// Create the render target
	if (tab_scripts_minimap_tex == NULL || tab_scripts_minimap_tex->width != tex_w || tab_scripts_minimap_tex->height != tex_h) {
		if (tab_scripts_minimap_tex != NULL) {
			gpu_delete_texture(tab_scripts_minimap_tex);
		}
		gc_unroot(tab_scripts_minimap_tex);
		tab_scripts_minimap_tex = gpu_create_render_target(tex_w, tex_h, GPU_TEXTURE_FORMAT_RGBA32);
		gc_root(tab_scripts_minimap_tex);
	}

	// Render each word as a small rect
	draw_begin(tab_scripts_minimap_tex, true, 0x00000000);
	draw_set_color(g_theme->HOVER_COL);
	for (i32 i = 0; i < lines->length; ++i) {
		string_array_t *words = string_split(lines->buffer[i], " ");
		f32             x     = 0;
		for (i32 j = 0; j < words->length; ++j) {
			i32 len = string_length(words->buffer[j]);
			if (len > 0) {
				draw_filled_rect(x, i * line_h, len * char_w, line_h);
			}
			x += (len + 1) * char_w;
		}
	}
	draw_end();
}

static void tab_scripts_draw_minimap(f32 mm_x, f32 mm_y, f32 mm_w, f32 mm_h) {
	if (tab_scripts_minimap_tex == NULL) {
		return;
	}
	f32             line_h          = 2 * UI_SCALE();
	string_array_t *lines           = string_split(tab_scripts_hscript->text, "\n");
	f32             content_h       = lines->length * UI_ELEMENT_H();
	f32             full_h          = lines->length * line_h;
	f32             scroll_progress = content_h > 0 ? -g_ui->current_window->scroll_offset / content_h : 0;
	f32             out_of_screen   = full_h - mm_h;
	if (out_of_screen < 0) {
		out_of_screen = 0;
	}

	// Draw the visible region of the cached minimap
	f32 tex_h = tab_scripts_minimap_tex->height;
	f32 sh    = tex_h < mm_h ? tex_h : mm_h;
	f32 sy    = out_of_screen * scroll_progress;
	if (sy > tex_h - sh) {
		sy = tex_h - sh;
	}
	if (sy < 0) {
		sy = 0;
	}
	draw_set_color(0xffffffff);
	draw_sub_image(tab_scripts_minimap_tex, 0, sy, mm_w, sh, mm_x, mm_y);

	i32 offset = math_floor(sy / line_h);

	// View box
	f32 visible_area = out_of_screen > 0 ? mm_h : full_h;
	f32 box_h        = (mm_h / UI_ELEMENT_H()) * line_h;
	f32 box_y        = mm_y + scroll_progress * visible_area;
	if (box_y < mm_y) {
		box_y = mm_y;
	}
	else if (box_y > mm_y + mm_h - box_h) {
		box_y = mm_y + mm_h - box_h;
	}
	draw_set_color(0x11ffffff);
	draw_filled_rect(mm_x, box_y, mm_w, box_h);

	// Drag the minimap to scroll the text area
	if (tab_scripts_minimap_scrolling && content_h > mm_h) {
		f32 my_local      = g_ui->input_y - (g_ui->_window_y + mm_y);
		i32 picked        = offset + math_floor(my_local / line_h);
		f32 visible_lines = mm_h / UI_ELEMENT_H();
		f32 target        = (picked - visible_lines / 2) * UI_ELEMENT_H();
		f32 max_scroll    = (lines->length - 1) * UI_ELEMENT_H(); // Last line at the top
		if (target < 0) {
			target = 0;
		}
		else if (target > max_scroll) {
			target = max_scroll;
		}
		g_ui->current_window->scroll_offset = -target;
	}
}

void tab_scripts_draw(ui_handle_t *htab) {
	if (ui_tab(htab, tr("Scripts"), false, -1, false)) {

		// Cache minimap
		f32  mm_x, mm_y, mm_w, mm_h;
		bool minimap_on = tab_scripts_minimap_visible(&mm_x, &mm_y, &mm_w, &mm_h);
		if (minimap_on && tab_scripts_minimap_dirty) {
			draw_end();
			tab_scripts_cache_minimap();
			draw_begin(&g_ui->current_window->texture, false, 0);
		}

		ui_begin_sticky();
		f32_array_t *row = f32_array_create_from_raw(
		    (f32[]){
		        -70,
		        -70,
		        -140,
		    },
		    3);

		// #ifndef NDEBUG
		// 		if (g_config->experimental) {
		// 			f32_array_push(row, -90);
		// 		}
		// #endif

		ui_row(row);

		if (ui_icon_button(tr("Run"), ICON_PLAY, UI_ALIGN_CENTER)) {
			minic_ctx_t *ctx = minic_eval(tab_scripts_hscript->text);
			// minic_ctx_free(ctx);
		}

		if (ui_icon_button(tr("Edit"), ICON_EDIT, UI_ALIGN_CENTER)) {
			ui_menu_draw(&tab_scripts_draw_edit, -1, -1);
		}

		tab_scripts_prepare();
		ui_handle_t *file_handle = ui_handle(__ID__);
		file_handle->i           = tab_scripts_selected;
		tab_scripts_selected     = ui_combo(file_handle, g_project->script_names, tr("File"), false, UI_ALIGN_LEFT, true);

		// #ifndef NDEBUG
		// 		if (g_config->experimental && ui_icon_button("Run Tests", ICON_PLAY, UI_ALIGN_CENTER)) {
		// 			minic_tests();
		// 		}
		// #endif

		ui_end_sticky();

		draw_font_t *_font      = g_font;
		i32          _font_size = g_ui->font_size;
		draw_font_t *f          = data_get_font("font_mono.ttf");
		ui_set_font(g_ui, f);
		g_ui->font_size              = math_floor(15 * UI_SCALE());
		ui_text_area_line_numbers    = true;
		ui_text_area_scroll_past_end = true;
		gc_unroot(ui_text_area_coloring);
		ui_text_area_coloring = tab_scripts_get_text_coloring();
		gc_root(ui_text_area_coloring);

		tab_scripts_prepare();

		tab_scripts_hscript->text = g_project->script_datas->buffer[tab_scripts_selected];

		bool ac_selected = g_ui->text_selected_handle == tab_scripts_hscript;
		if (!ac_selected) {
			tab_scripts_ac_show = false;
		}

		// Open the autocomplete popup on ctrl+space
		if (ac_selected && g_ui->is_ctrl_down && g_ui->is_key_pressed && g_ui->key_code == KEY_CODE_SPACE) {
			tab_scripts_ac_show   = true;
			tab_scripts_ac_offset = 0;
			g_ui->is_key_pressed  = false; // Consume so the editor ignores ctrl+space
			minic_register_builtins();     // Ensure the script api is registered
		}

		// Toggle line comment on ctrl+/
		if (ac_selected && g_ui->is_ctrl_down && g_ui->is_key_pressed && g_ui->key_code == KEY_CODE_SLASH) {
			tab_scripts_toggle_comment();
			g_ui->is_key_pressed = false; // Consume so the editor ignores ctrl+/
			g_ui->key_code       = 0;
		}

		bool ac_accept = false;
		if (tab_scripts_ac_show && ac_selected && g_ui->is_key_pressed) {
			if (g_ui->key_code == KEY_CODE_DOWN) {
				tab_scripts_ac_offset++;
				g_ui->is_key_pressed = false;
				g_ui->key_code       = 0;
			}
			else if (g_ui->key_code == KEY_CODE_UP) {
				tab_scripts_ac_offset--;
				g_ui->is_key_pressed = false;
				g_ui->key_code       = 0;
			}
			else if (g_ui->key_code == KEY_CODE_RETURN || g_ui->key_code == KEY_CODE_TAB) {
				ac_accept            = true;
				g_ui->is_key_pressed = false;
				g_ui->key_code       = 0;
			}
			else if (g_ui->key_code == KEY_CODE_ESCAPE) {
				tab_scripts_ac_show  = false;
				g_ui->is_key_pressed = false;
				g_ui->key_code       = 0;
			}
		}

		if (ac_selected && g_ui->is_key_pressed) {
			tab_scripts_minimap_dirty = true;
		}

		if (tab_scripts_minimap_selected != tab_scripts_selected) {
			tab_scripts_minimap_selected = tab_scripts_selected;
			tab_scripts_minimap_dirty    = true;
		}

		minimap_on = tab_scripts_minimap_visible(&mm_x, &mm_y, &mm_w, &mm_h);
		if (minimap_on && g_ui->input_started && ui_input_in_rect(g_ui->_window_x + mm_x, g_ui->_window_y + mm_y, mm_w, mm_h)) {
			tab_scripts_minimap_scrolling = true;
		}
		if (!g_ui->input_down) {
			tab_scripts_minimap_scrolling = false;
		}

		// Prevent text area clicks while scrolling the minimap
		bool _input_enabled = g_ui->input_enabled;
		if (tab_scripts_minimap_scrolling) {
			g_ui->input_enabled = false;
		}
		ui_text_area(tab_scripts_hscript, UI_ALIGN_LEFT, true, "", false);
		g_ui->input_enabled                                   = _input_enabled;
		g_project->script_datas->buffer[tab_scripts_selected] = tab_scripts_hscript->text;

		if (minimap_on) {
			tab_scripts_draw_minimap(mm_x, mm_y, mm_w, mm_h);
		}

		// Autocomplete popup
		if (tab_scripts_ac_show && ac_selected) {
			char *text   = tab_scripts_hscript->text;
			i32   line   = tab_scripts_hscript->i;
			i32   col    = g_ui->cursor_x;
			i32   cur    = tab_scripts_line_start(text, line) + col;
			i32   plen   = tab_scripts_prefix_len(text, cur);
			i32   slen   = tab_scripts_suffix_len(text, cur);
			char *prefix = substring(text, cur - plen, cur);

			char *names[TAB_SCRIPTS_AC_MAX];
			i32   n = tab_scripts_autocomplete_matches(prefix, names);
			if (n == 0) {
				tab_scripts_ac_show = false;
			}
			else {
				tab_scripts_ac_offset = tab_scripts_ac_offset < 0 ? 0 : (tab_scripts_ac_offset > n - 1 ? n - 1 : tab_scripts_ac_offset);
				if (ac_accept) {
					tab_scripts_autocomplete_complete(names[tab_scripts_ac_offset], plen, slen);
				}
				else {
					// Draw the list below the caret
					i32 row_h = math_floor(g_ui->font_size + 8 * UI_SCALE());
					f32 pad   = 8 * UI_SCALE();
					f32 max_w = 0;
					for (i32 i = 0; i < n; ++i) {
						f32 w = draw_string_width(g_font, g_ui->font_size, names[i]);
						max_w = w > max_w ? w : max_w;
					}
					f32 px = g_ui->cursor_screen_x;
					f32 py = g_ui->cursor_screen_y + UI_ELEMENT_H();
					f32 pw = max_w + pad * 2;
					f32 ph = row_h * n;

					ui_draw_shadow(px, py, pw, ph);
					draw_set_color(g_theme->SEPARATOR_COL);
					draw_filled_rect(px, py, pw, ph);
					draw_set_font(g_font, g_ui->font_size);
					for (i32 i = 0; i < n; ++i) {
						f32 iy = py + row_h * i;
						if (i == tab_scripts_ac_offset) {
							draw_set_color(g_theme->HIGHLIGHT_COL);
							draw_filled_rect(px, iy, pw, row_h);
						}
						draw_set_color(g_theme->TEXT_COL);
						draw_string(names[i], px + pad, iy + math_floor((row_h - g_ui->font_size) / 2.0));
					}
				}
			}
		}

		ui_text_area_line_numbers    = false;
		ui_text_area_scroll_past_end = false;
		gc_unroot(ui_text_area_coloring);
		ui_text_area_coloring = NULL;
		ui_set_font(g_ui, _font);
		g_ui->font_size = _font_size;
	}
}

ui_text_coloring_t *tab_scripts_get_text_coloring() {
	if (tab_scripts_text_coloring == NULL) {
		buffer_t *blob = data_get_blob("text_coloring.json");
		gc_unroot(tab_scripts_text_coloring);
		tab_scripts_text_coloring = json_parse(sys_buffer_to_string(blob));
		gc_root(tab_scripts_text_coloring);
	}
	return tab_scripts_text_coloring;
}
