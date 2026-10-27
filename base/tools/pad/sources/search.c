// Search panel

#define SEARCH_RESULTS_MAX 1000
#define SEARCH_FILE_MAX    (2 * 1024 * 1024) // Larger files are skipped
#define SEARCH_TEXT_MAX    160

typedef struct search_result {
	char *path;
	char *rel; // Path relative to the project
	i32   line;
	char *label;
	i32   text; // Offset of the line text in the label
} search_result_t;

ui_window_t *search_hwnd     = NULL;
char        *search_query    = "";
char        *search_ran      = "";  // Query of the last search
f64          search_typed    = 0.0; // Time of the last change of the query
char        *search_summary  = NULL; // Result and file counts of the last search
bool         search_focus    = false;
any_array_t *search_results  = NULL;
ui_id_t      search_input_id = 0; // Widget ids depend on the window, stored while drawing it

// Collects the lines of the file containing the lower case query
static void search_file(char *rel, char *query, i32 query_len) {
	char     *path = path_join(pad_project_dir, rel);
	buffer_t *blob = iron_load_blob(path);
	if (blob == NULL) {
		return;
	}
	char *text    = (char *)blob->buffer;
	i32   len     = blob->length;
	bool  is_text = len <= SEARCH_FILE_MAX;
	for (i32 i = 0; i < len && i < 1024 && is_text; ++i) {
		is_text = text[i] != '\0';
	}

	i32 line       = 0;
	i32 line_start = 0;
	for (i32 i = 0; is_text && i + query_len <= len && search_results->length < SEARCH_RESULTS_MAX; ++i) {
		if (text[i] == '\n') {
			line++;
			line_start = i + 1;
			continue;
		}
		i32 j = 0;
		while (j < query_len && tolower(text[i + j]) == query[j]) {
			j++;
		}
		if (j < query_len) {
			continue;
		}

		// Show the line without the indentation
		while (text[line_start] == ' ' || text[line_start] == '\t') {
			line_start++;
		}
		char shown[SEARCH_TEXT_MAX];
		i32  n = 0;
		for (i32 k = line_start; k < len && text[k] != '\n' && text[k] != '\r' && n < SEARCH_TEXT_MAX - 1; ++k) {
			shown[n++] = text[k] == '\t' ? ' ' : text[k];
		}
		shown[n] = '\0';

		search_result_t *r = ALLOC_INIT(search_result_t, {0});
		r->path            = path;
		r->rel             = path + string_length(pad_project_dir) + 1;
		r->line            = line;
		r->label           = string("%d  %s", line + 1, shown);
		r->text            = string_length(r->label) - n;
		any_array_push(search_results, r);

		while (i < len && text[i] != '\n') { // One result per line
			i++;
		}
		i--;
	}
	iron_delete_blob(blob);
}

static void search_run() {
	search_ran                 = string_copy(search_query);
	search_results             = any_array_create(0);
	search_summary             = NULL;
	search_hwnd->scroll_offset = 0;
	if (pad_project_dir == NULL || search_query[0] == '\0') {
		return;
	}
	picker_files       = any_array_create(0);
	picker_files_lower = any_array_create(0);
	picker_collect(pad_project_dir, 0);

	char *query = picker_to_lower(search_query);
	i32   files = 0;
	for (i32 i = 0; i < (i32)picker_files->length && search_results->length < SEARCH_RESULTS_MAX; ++i) {
		i32 count = search_results->length;
		search_file(picker_files->buffer[i], query, string_length(query));
		if ((i32)search_results->length > count) {
			files++;
		}
	}
	search_summary = string("%d%s results in %d files", (i32)search_results->length, search_results->length >= SEARCH_RESULTS_MAX ? "+" : "", files);
	free(query);
}

void search_toggle() {
	search_show = !search_show;
	if (search_show) {
		search_focus = true;
	}
	else if (search_input_id != 0 && g_ui->text_selected_id == search_input_id) {
		ui_deselect_text(g_ui);
		g_ui->submit_text_id = 0;
	}
}

// Marks the matches of the last search in the row
static void search_draw_matches(search_result_t *r, f32 x, f32 y, f32 w) {
	i32 len = string_length(search_ran);
	f32 h   = UI_ELEMENT_H() - g_ui->button_offset_y * 3.0;
	draw_set_color(0x77ffee00);
	for (i32 i = r->text; len > 0 && r->label[i] != '\0'; ++i) {
		i32 j = 0;
		while (j < len && tolower(r->label[i + j]) == tolower(search_ran[j])) {
			j++;
		}
		if (j < len) {
			continue;
		}
		f32 mx = UI_TEXT_OFFSET() + draw_sub_string_width(g_ui->ops->font, g_ui->font_size, r->label, 0, i);
		f32 mw = draw_sub_string_width(g_ui->ops->font, g_ui->font_size, r->label, i, i + len);
		if (mx + mw > w - 10.0 * UI_SCALE()) { // Label is truncated here
			break;
		}
		draw_filled_rect(x + mx, y + g_ui->button_offset_y * 1.5, mw, h);
		i += len - 1;
	}
}

void search_init() {
	search_hwnd    = ui_window_create();
	search_results = any_array_create(0);
}

void search_render(i32 x, i32 y, i32 w, i32 h) {
	if (!ui_window(search_hwnd, x, y, w, h, false)) {
		return;
	}
	// Enter runs the search, consumed so the field stays focused
	search_input_id = ui_widget_id(&search_query, UI_ID_TEXT);
	if (g_ui->text_selected_id == search_input_id && g_ui->is_key_pressed && g_ui->key_code == KEY_CODE_RETURN) {
		search_query = string_copy(g_ui->text_selected);
		search_run();
		g_ui->is_key_pressed = false;
		g_ui->key_code       = 0;
	}
	pad_fade_begin();
	ui_begin_sticky();
	static char typed[UI_TEXT_MAX] = "";
	ui_text_input(&search_query, search_summary != NULL ? search_summary : "Search", UI_ALIGN_LEFT, true, true);
	// Search as typed, once the typing pauses
	if (!string_equals(search_query, typed) && string_length(search_query) < UI_TEXT_MAX) {
		strcpy(typed, search_query);
		search_typed = iron_time();
	}
	if (!string_equals(search_query, search_ran) && iron_time() - search_typed > 0.2) {
		search_run();
	}
	if (search_focus) {
		search_focus = false;
		ui_start_text_edit(&search_query, UI_ALIGN_LEFT);
		g_ui->cursor_x         = string_length(search_query);
		g_ui->highlight_anchor = 0;
	}
	ui_end_sticky();

	i32 _FILL_BUTTON_BG     = g_theme->FILL_BUTTON_BG;
	i32 _TEXT_COL           = g_theme->TEXT_COL;
	i32 _TEXT_OFFSET        = g_theme->TEXT_OFFSET;
	g_theme->FILL_BUTTON_BG = false;
	char *rel               = NULL;
	for (i32 i = 0; i < (i32)search_results->length; ++i) {
		search_result_t *r = search_results->buffer[i];
		if (rel != r->rel) { // Results are grouped by file
			rel = r->rel;
			if (ui_is_visible(UI_ELEMENT_H())) {
				browser_draw_icon(BROWSER_ICON_FILE, g_ui->_x + UI_TEXT_OFFSET(), g_ui->_y);
			}
			g_theme->TEXT_COL    = g_theme->LABEL_COL;
			g_theme->TEXT_OFFSET = _TEXT_OFFSET + BROWSER_ICON_W; // Name is placed after the icon
			ui_text(rel, UI_ALIGN_LEFT, 0x00000000);
			g_theme->TEXT_COL    = _TEXT_COL;
			g_theme->TEXT_OFFSET = _TEXT_OFFSET;
		}
		f32  row_x   = g_ui->_x;
		f32  row_y   = g_ui->_y;
		bool visible = ui_is_visible(UI_ELEMENT_H());
		bool clicked = pad_row(r->label, false);
		if (visible) {
			search_draw_matches(r, row_x, row_y, g_ui->_w);
		}
		if (clicked) {
			editor_open(r->path);
			if (editor_current != NULL && string_equals(editor_current->path, r->path)) {
				editor_goto_line(r->line);
			}
			break;
		}
	}
	g_theme->FILL_BUTTON_BG = _FILL_BUTTON_BG;
	pad_fade_end();
}
