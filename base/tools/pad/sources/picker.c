// Go to file popup

#define PICKER_ROWS      12
#define PICKER_FILES_MAX 20000

ui_window_t    *picker_hwnd        = NULL;
bool            picker_show        = false;
char           *picker_query       = "";
char           *picker_query_last  = "";
bool            picker_focus       = false;
i32             picker_selected    = 0;
string_array_t *picker_files       = NULL; // Paths relative to the project
string_array_t *picker_files_lower = NULL;
i32             picker_matches[PICKER_ROWS];
i32             picker_matches_count = 0;

static char *picker_to_lower(char *s) {
	char *r = string_copy(s);
	for (i32 i = 0; r[i] != '\0'; ++i) {
		r[i] = tolower(r[i]);
	}
	return r;
}

static void picker_collect(char *dir, i32 depth) {
	if (depth > 16) {
		return;
	}
	any_array_t *files = file_read_directory(dir);
	for (i32 i = 0; i < (i32)files->length && picker_files->length < PICKER_FILES_MAX; ++i) {
		char *name = files->buffer[i];
		if (name[0] == '\0') {
			continue;
		}
		char *path = path_join(dir, name);
		if (iron_is_directory(path)) {
			if (!string_equals(name, ".git") && !string_equals(name, "build")) {
				picker_collect(path, depth + 1);
			}
			continue;
		}
		char *rel = path + string_length(pad_project_dir) + 1;
		any_array_push(picker_files, rel);
		any_array_push(picker_files_lower, picker_to_lower(rel));
	}
}

static bool picker_is_subsequence(char *s, char *query) {
	i32 q = 0;
	for (i32 i = 0; s[i] != '\0' && query[q] != '\0'; ++i) {
		if (s[i] == query[q]) {
			q++;
		}
	}
	return query[q] == '\0';
}

// Lower is better, -1 when the path does not match
static i32 picker_score(char *path, char *query) {
	i32 len = string_length(path);
	if (query[0] == '\0') {
		return 0;
	}
	char *name = path + string_last_index_of(path, PATH_SEP) + 1;
	if (starts_with(name, query)) {
		return len;
	}
	if (strstr(name, query) != NULL) {
		return 1000 + len;
	}
	if (strstr(path, query) != NULL) {
		return 2000 + len;
	}
	// Characters of the query in order, in the file name or in the whole path
	if (picker_is_subsequence(name, query)) {
		return 3000 + len;
	}
	return picker_is_subsequence(path, query) ? 4000 + len : -1;
}

static void picker_update_matches() {
	if (!string_equals(picker_query, picker_query_last)) {
		picker_query_last = picker_query;
		picker_selected   = 0;
	}
	char *query = picker_to_lower(picker_query);
	i32   scores[PICKER_ROWS];
	picker_matches_count = 0;
	for (i32 i = 0; i < (i32)picker_files->length; ++i) {
		i32 score = picker_score(picker_files_lower->buffer[i], query);
		if (score < 0) {
			continue;
		}
		// Keep the best matches sorted
		i32 pos = picker_matches_count;
		while (pos > 0 && scores[pos - 1] > score) {
			pos--;
		}
		if (pos >= PICKER_ROWS) {
			continue;
		}
		if (picker_matches_count < PICKER_ROWS) {
			picker_matches_count++;
		}
		for (i32 j = picker_matches_count - 1; j > pos; --j) {
			scores[j]         = scores[j - 1];
			picker_matches[j] = picker_matches[j - 1];
		}
		scores[pos]         = score;
		picker_matches[pos] = i;
	}
	free(query);
	if (picker_selected > picker_matches_count - 1) {
		picker_selected = picker_matches_count > 0 ? picker_matches_count - 1 : 0;
	}
}

void picker_open() {
	if (pad_project_dir == NULL) {
		return;
	}
	picker_files       = any_array_create(0);
	picker_files_lower = any_array_create(0);
	picker_collect(pad_project_dir, 0);
	picker_query    = "";
	picker_selected = 0;
	picker_show     = true;
	picker_focus    = true;
	picker_update_matches();
}

void picker_close() {
	if (g_ui->text_selected_id != 0) {
		ui_deselect_text(g_ui);
		g_ui->submit_text_id = 0;
	}
	picker_show = false;
}

static void picker_accept(i32 i) {
	if (i < picker_matches_count) {
		editor_open(path_join(pad_project_dir, picker_files->buffer[picker_matches[i]]));
	}
	picker_close();
}

static void picker_rect(i32 *x, i32 *y, i32 *w, i32 *h) {
	*w = math_floor(600 * UI_SCALE());
	if (*w > sys_w() - 40) {
		*w = sys_w() - 40;
	}
	*x = (sys_w() - *w) / 2;
	*y = math_floor(60 * UI_SCALE());
	*h = math_floor((PICKER_ROWS + 1) * (UI_ELEMENT_H() + UI_ELEMENT_OFFSET()) + UI_ELEMENT_OFFSET());
}

// Runs before the panels are drawn so the keys do not reach them
void picker_update_input() {
	picker_update_matches();

	i32 x, y, w, h;
	picker_rect(&x, &y, &w, &h);
	if (g_ui->input_started && !ui_input_in_rect(x, y, w, h)) {
		picker_close();
		return;
	}
	if (!g_ui->is_key_pressed) {
		return;
	}
	if (g_ui->key_code == KEY_CODE_RETURN) {
		picker_accept(picker_selected);
	}
	else if (g_ui->key_code == KEY_CODE_ESCAPE) {
		picker_close();
	}
	else if (g_ui->key_code == KEY_CODE_DOWN) {
		picker_selected = picker_selected < picker_matches_count - 1 ? picker_selected + 1 : 0;
	}
	else if (g_ui->key_code == KEY_CODE_UP) {
		picker_selected = picker_selected > 0 ? picker_selected - 1 : (picker_matches_count > 0 ? picker_matches_count - 1 : 0);
	}
	else {
		return;
	}
	g_ui->is_key_pressed = false; // Consume so the search field ignores the key
	g_ui->key_code       = 0;
}

void picker_init() {
	picker_hwnd = ui_window_create();
}

void picker_render() {
	i32 x, y, w, h;
	picker_rect(&x, &y, &w, &h);
	i32 _WINDOW_BG_COL     = g_theme->WINDOW_BG_COL;
	g_theme->WINDOW_BG_COL = g_theme->BUTTON_COL; // Stand out from the panels
	bool redraw            = ui_window(picker_hwnd, x, y, w, h, false);
	g_theme->WINDOW_BG_COL = _WINDOW_BG_COL;
	if (!redraw) {
		return;
	}

	ui_text_input(&picker_query, "Go to File", UI_ALIGN_LEFT, true, true);
	if (picker_focus) {
		picker_focus = false;
		ui_start_text_edit(&picker_query, UI_ALIGN_LEFT);
	}

	i32 _FILL_BUTTON_BG     = g_theme->FILL_BUTTON_BG;
	g_theme->FILL_BUTTON_BG = false;
	for (i32 i = 0; i < picker_matches_count; ++i) {
		if (i == picker_selected) {
			draw_set_color(g_theme->HIGHLIGHT_COL);
			draw_filled_rect(0, g_ui->_y, w, UI_ELEMENT_H());
		}
		if (ui_button(picker_files->buffer[picker_matches[i]], UI_ALIGN_LEFT, "")) {
			picker_accept(i);
			break;
		}
	}
	g_theme->FILL_BUTTON_BG = _FILL_BUTTON_BG;
	if (picker_matches_count == 0) {
		ui_text("No matching files", UI_ALIGN_LEFT, 0x00000000);
	}
}
