// Text editor panel

ui_window_t        *editor_hwnd        = NULL;
any_map_t          *editor_buffers     = NULL; // Opened files by path
any_array_t        *editor_buffer_list = NULL;
editor_buffer_t    *editor_current     = NULL;
editor_buffer_t    *editor_last        = NULL; // Previously opened file, ctrl+tab switches to it
char               *editor_closed      = NULL; // Path of the file closed last, ctrl+tab reopens it
ui_text_coloring_t *editor_coloring    = NULL;

gpu_texture_t *editor_minimap_tex       = NULL;
u32            editor_minimap_hash      = 0;
bool           editor_minimap_scrolling = false;
bool           editor_minimap_show      = true;
ui_id_t        editor_text_id           = 0; // Widget ids depend on the window, stored while drawing it

bool  editor_search_show  = false;
bool  editor_search_focus = false;
char *editor_search       = "";
i32   editor_search_pos   = -1; // Text offset of the current match

char *editor_mark     = NULL; // Selected text, its occurrences are highlighted
i32   editor_mark_pos = -1;   // Text offset of the selection

extern char   *search_query;
extern ui_id_t search_input_id;

static bool editor_is_word_char(char c);

ui_text_coloring_t *editor_get_coloring() {
	if (editor_coloring == NULL) {
		buffer_t *blob = data_get_blob("text_coloring.json");
		if (blob == NULL) {
			return NULL;
		}
		editor_coloring = json_parse(sys_buffer_to_string(blob));
	}
	return editor_coloring;
}

// Words ending with _t are colored as types, words touching ( as function calls
// Strings, comments and keywords are blanked in rest already
static void editor_draw_coloring(char *text, char *rest, f32 x, f32 y) {
	static char types[1024];
	static char calls[1024];
	i32         len       = string_length(rest);
	bool        has_types = false;
	bool        has_calls = false;
	memset(types, ' ', len);
	memset(calls, ' ', len);
	types[len] = '\0';
	calls[len] = '\0';
	for (i32 i = 0; i < len; ++i) {
		if (!editor_is_word_char(text[i])) {
			continue;
		}
		i32 start = i;
		while (i < len && editor_is_word_char(text[i])) {
			i++;
		}
		bool is_type = i - start > 2 && text[i - 2] == '_' && text[i - 1] == 't';
		bool is_call = text[i] == '(';
		if ((!is_type && !is_call) || (text[start] >= '0' && text[start] <= '9')) {
			continue;
		}
		char *dst = is_type ? types : calls;
		for (i32 j = start; j < i; ++j) {
			if (rest[j] != ' ') {
				dst[j]  = rest[j];
				rest[j] = ' ';
				if (is_type) {
					has_types = true;
				}
				else {
					has_calls = true;
				}
			}
		}
	}
	if (has_types) {
		draw_set_color(0xffc695c6);
		draw_string(types, x, y);
	}
	if (has_calls) {
		draw_set_color(0xff6699cc);
		draw_string(calls, x, y);
	}
}

bool editor_is_modified(editor_buffer_t *b) {
	return b != NULL && !b->diff && !string_equals(b->text, b->saved);
}

// The text area holds a line in a fixed buffer
static bool editor_is_text(buffer_t *blob) {
	i32 line_len = 0;
	for (i32 i = 0; i < (i32)blob->length; ++i) {
		u8 c = blob->buffer[i];
		if (c == 0) {
			return false;
		}
		line_len = c == '\n' ? 0 : line_len + (c == '\t' ? 4 : 1);
		if (line_len > UI_TEXT_MAX - 64) {
			return false;
		}
	}
	return true;
}

// Text area edits with spaces, turn the indentation back into tabs
// Untouched lines are written the way they were loaded
static char *editor_retab(editor_buffer_t *b) {
	char *text = b->text;
	char *out  = string_alloc(string_length(text) + 1);
	i32   o    = 0;
	i32   i    = 0;
	while (text[i] != '\0') {
		i32 end = i;
		while (text[end] != '\0' && text[end] != '\n') {
			end++;
		}
		char *line = substring(text, i, end);
		char *raw  = any_map_get(b->tab_lines, line);
		if (raw != NULL) {
			strcpy(out + o, raw);
			o += string_length(raw);
		}
		else {
			i32 spaces = 0;
			while (b->tabs && line[spaces] == ' ') {
				spaces++;
			}
			for (i32 t = 0; t < spaces / 4; ++t) {
				out[o++] = '\t';
			}
			strcpy(out + o, line + spaces - spaces % 4);
			o += string_length(line) - spaces + spaces % 4;
		}
		free(line);
		i = end;
		if (text[i] == '\n') {
			out[o++] = text[i++];
		}
	}
	out[o] = '\0';
	return out;
}

static void editor_store_scroll() {
	if (editor_current != NULL) {
		editor_current->scroll = editor_hwnd->scroll_offset;
	}
}

void editor_save_buffer(editor_buffer_t *b);

// Unsaved changes are written when the file is closed or left for another one
void editor_autosave() {
	if (editor_is_modified(editor_current)) {
		editor_save_buffer(editor_current);
		pad_saved(editor_current->path);
	}
}

// Lines without a tab are stored once, as the key and the value
static void editor_free_tab_lines(any_map_t *m) {
	if (m == NULL) {
		return;
	}
	for (i32 i = 0; i < (i32)m->keys->capacity; ++i) {
		char *line = m->keys->buffer[i];
		if (line == NULL) {
			continue;
		}
		if (m->values->buffer[i] != line) {
			free(m->values->buffer[i]);
		}
		free(line);
	}
	map_free(m);
}

// The buffer has to be out of the opened files and not the current one
void editor_free_buffer(editor_buffer_t *b) {
	if (b == editor_last) {
		editor_last = NULL;
	}
	if (b->image != NULL) {
		gpu_delete_texture(b->image);
	}
	if (b->saved != b->text) { // Diff has a single text
		free(b->saved);
	}
	free(b->text);
	editor_free_tab_lines(b->tab_lines);
	for (i32 i = 0; i < b->undo_count; ++i) {
		free(b->undo[i].text);
	}
	for (i32 i = 0; i < b->redo_count; ++i) {
		free(b->redo[i].text);
	}
	free(b->undo_last.text);
	free(b); // The path stays, the config keeps the pointer of the last opened file
}

static void editor_set_current(editor_buffer_t *b) {
	editor_store_scroll();
	editor_buffer_t *prev = editor_current;
	if (g_ui->text_selected_id != 0) {
		ui_deselect_text(g_ui);
		g_ui->submit_text_id = 0; // Line edits are already applied to the buffer
	}
	ui_text_area_clear_selection();
	if (b != NULL && b != editor_current) { // Not for a deleted or reverted file
		editor_autosave();
	}
	if (b != NULL && prev != NULL && prev != b && array_index_of(editor_buffer_list, prev) >= 0) {
		editor_last = prev;
	}
	editor_current             = b;
	editor_hwnd->scroll_offset = b != NULL ? b->scroll : 0;
	editor_hwnd->redraws       = 2;
	if (prev != NULL && prev != b && array_index_of(editor_buffer_list, prev) < 0) { // Diffs and pictures are not kept
		editor_free_buffer(prev);
	}
}

// Modification time of the file on disk, 0 when missing
static i64 editor_file_time(char *path) {
#ifdef IRON_WINDOWS
	struct _stat st;
	return _stat(path, &st) == 0 ? (i64)st.st_mtime : 0;
#elif defined(IRON_MACOS)
	struct stat st;
	return stat(path, &st) == 0 ? (i64)st.st_mtimespec.tv_sec * 1000000000 + st.st_mtimespec.tv_nsec : 0;
#else
	struct stat st;
	return stat(path, &st) == 0 ? (i64)st.st_mtim.tv_sec * 1000000000 + st.st_mtim.tv_nsec : 0;
#endif
}

// Reads the file from disk into the buffer, the previous text is left to the caller to free
static bool editor_load(editor_buffer_t *b) {
	buffer_t *blob = iron_load_blob(b->path);
	if (blob == NULL) {
		return false;
	}
	if (!editor_is_text(blob)) {
		iron_delete_blob(blob);
		return false;
	}

	char *file = string_alloc(blob->length + 1);
	memcpy(file, blob->buffer, blob->length);
	iron_delete_blob(blob);

	b->crlf    = string_index_of(file, "\r\n") > -1;
	b->tabs    = file[0] == '\t' || string_index_of(file, "\n\t") > -1;
	char *text = string_replace_all(file, "\r", "");
	free(file);
	b->text = string_replace_all(text, "\t", "    ");
	editor_free_tab_lines(b->tab_lines);
	b->tab_lines         = any_map_create();
	string_array_t *raws = string_split(text, "\n");
	free(text);
	for (i32 i = (i32)raws->length - 1; i >= 0; --i) { // The last one of the lines with the same text is used
		char *raw  = raws->buffer[i];
		char *line = strchr(raw, '\t') != NULL ? string_replace_all(raw, "\t", "    ") : raw;
		if (any_map_get(b->tab_lines, line) != NULL) {
			if (line != raw) {
				free(line);
			}
			free(raw);
			continue;
		}
		any_map_set(b->tab_lines, line, raw);
	}
	array_delete(raws);
	free(b->saved);
	b->saved = string_copy(b->text);
	b->time  = editor_file_time(b->path);
	return true;
}

bool editor_is_image(char *path) {
	char ext[8];
	i32  dot = string_last_index_of(path, ".");
	i32  len = string_length(path);
	if (dot < 0 || len - dot > 6) {
		return false;
	}
	for (i32 i = dot; i <= len; ++i) {
		ext[i - dot] = tolower(path[i]);
	}
	return string_equals(ext, ".png") || string_equals(ext, ".jpg") || string_equals(ext, ".jpeg");
}

// Shows the picture, the buffer is not kept with the opened files
static void editor_open_image(char *path) {
	buffer_t           *blob = iron_load_blob(path);
	iron_image_pixels_t pixels;
	bool                decoded = blob != NULL && iron_decode_image(blob, path, &pixels);
	if (blob != NULL) {
		iron_delete_blob(blob);
	}
	if (!decoded) {
		return;
	}
	if (pixels.width > 16384 || pixels.height > 16384) { // Texture size limit
		free(pixels.data);
		return;
	}
	editor_buffer_t *b = ALLOC_INIT(editor_buffer_t, {0});
	b->path            = string_copy(path);
	b->text            = string_copy("");
	b->saved           = string_copy("");
	b->image           = gpu_create_texture_from_pixels(&pixels);
	editor_set_current(b);
	pad_save_config();
}

void editor_open(char *path) {
	if (editor_is_image(path)) {
		editor_open_image(path);
		return;
	}
	editor_buffer_t *b = any_map_get(editor_buffers, path);
	if (b != NULL) {
		editor_set_current(b);
		return;
	}

	b       = ALLOC_INIT(editor_buffer_t, {0});
	b->path = string_copy(path);
	if (!editor_load(b)) {
		free(b->path);
		free(b);
		return;
	}
	any_map_set(editor_buffers, b->path, b);
	any_array_push(editor_buffer_list, b);
	editor_set_current(b);
	pad_save_config();
}

void editor_goto_line(i32 line);

// Shows a diff of the file, the buffer is not kept with the opened files
bool editor_open_diff(char *path, char *diff, i32 length) {
	buffer_t blob = {.buffer = (u8 *)diff, .length = length};
	if (!editor_is_text(&blob)) {
		return false;
	}
	editor_buffer_t *b = ALLOC_INIT(editor_buffer_t, {0});
	b->path            = string_copy(path);
	b->text            = string_replace_all(string_replace_all(diff, "\r", ""), "\t", "    ");
	b->saved           = b->text;
	b->diff            = true;
	b->caret           = -1;
	editor_set_current(b);
	// Scroll to the first change
	for (i32 i = 0, row = 0; b->text[i] != '\0'; ++row) {
		if (b->text[i] == '+' || b->text[i] == '-') {
			editor_goto_line(row);
			break;
		}
		char *end = strchr(b->text + i, '\n');
		if (end == NULL) {
			break;
		}
		i = (i32)(end - b->text) + 1;
	}
	return true;
}

// Line of the file at the caret of the shown diff, -1 without a caret
i32 editor_diff_line() {
	editor_buffer_t *b = editor_current;
	if (b == NULL || !b->diff || b->caret < 0) {
		return -1;
	}
	char *text = b->text;
	i32   line = 1; // Next line of the new file, 1-based
	for (i32 i = 0, row = 0; text[i] != '\0'; ++row) {
		char c = text[i];
		if (c == '@') { // @@ -a,b +c,d @@
			char *plus = strchr(text + i, '+');
			if (plus != NULL) {
				line = atoi(plus + 1);
			}
		}
		if (row == b->caret) {
			break;
		}
		if (c == ' ' || c == '+') {
			line++;
		}
		while (text[i] != '\0' && text[i] != '\n') {
			i++;
		}
		if (text[i] == '\n') {
			i++;
		}
	}
	return line > 0 ? line - 1 : 0;
}

// A file was changed by another program
bool editor_changed_on_disk() {
	for (i32 i = 0; i < (i32)editor_buffer_list->length; ++i) {
		editor_buffer_t *b = editor_buffer_list->buffer[i];
		if (editor_file_time(b->path) != b->time) {
			return true;
		}
	}
	return false;
}

// Reloads the files changed on disk, buffers with unsaved changes are kept
void editor_reload_changed() {
	for (i32 i = 0; i < (i32)editor_buffer_list->length; ++i) {
		editor_buffer_t *b    = editor_buffer_list->buffer[i];
		i64              time = editor_file_time(b->path);
		if (time == b->time) {
			continue;
		}
		if (time == 0 || editor_is_modified(b)) { // Deleted or edited here
			b->time = time;
			continue;
		}
		if (b == editor_current && g_ui->text_selected_id != 0) {
			ui_deselect_text(g_ui);
			g_ui->submit_text_id = 0;
			ui_text_area_clear_selection();
		}
		char *old = b->text;
		if (!editor_load(b)) {
			b->time = time;
			continue;
		}
		free(old);
		i32 lines = ui_line_count(b->text);
		if (b->line > lines - 1) {
			b->line = lines - 1;
		}
		editor_hwnd->redraws = 2;
	}
}

// Offset of the line start
static i32 editor_line_pos(char *text, i32 line) {
	i32 pos = 0;
	for (i32 i = 0; line > 0 && text[i] != '\0'; ++i) {
		if (text[i] == '\n') {
			line--;
			pos = i + 1;
		}
	}
	return pos;
}

// Runs clang-format on the saved source and reads it back, the cursor stays at the code it was at
// Sources without a .clang-format file in the folders above are left as they are
static void editor_format(editor_buffer_t *b) {
	if (!ends_with(b->path, ".c") && !ends_with(b->path, ".h") && !ends_with(b->path, ".m")) {
		return;
	}
#ifdef IRON_WINDOWS
	iron_sys_command(string_tmp("clang-format -i --fallback-style=none \"%s\" >nul 2>&1", b->path));
#else
	iron_sys_command(string_tmp("clang-format -i --fallback-style=none \"%s\" >/dev/null 2>&1", b->path));
#endif
	char *old = b->text;
	if (!editor_load(b)) {
		b->time = editor_file_time(b->path);
		return;
	}
	if (string_equals(old, b->text)) {
		free(old);
		return;
	}

	// Formatting moves the white space only, count the other characters in front of the cursor
	bool editing = b == editor_current && editor_text_id != 0 && g_ui->text_selected_id == editor_text_id;
	i32  pos     = editor_line_pos(old, b->line);
	for (i32 i = 0; editing && i < g_ui->cursor_x && old[pos] != '\0' && old[pos] != '\n'; ++i) {
		pos++;
	}
	i32 chars  = 0;
	i32 breaks = 0; // White space between the last character and the cursor
	i32 spaces = 0;
	for (i32 i = 0; i < pos; ++i) {
		if (old[i] == '\n') {
			breaks++;
			spaces = 0;
		}
		else if (old[i] == ' ') {
			spaces++;
		}
		else {
			chars++;
			breaks = 0;
			spaces = 0;
		}
	}
	char *text = b->text;
	i32   p    = 0;
	while (chars > 0 && text[p] != '\0') {
		if (text[p] != ' ' && text[p] != '\n') {
			chars--;
		}
		p++;
	}
	while (breaks > 0 && (text[p] == ' ' || text[p] == '\n')) {
		if (text[p] == '\n') {
			breaks--;
		}
		p++;
	}
	while (spaces > 0 && text[p] == ' ') {
		spaces--;
		p++;
	}
	i32 line       = 0;
	i32 line_start = 0;
	for (i32 i = 0; i < p; ++i) {
		if (text[i] == '\n') {
			line++;
			line_start = i + 1;
		}
	}

	if (b == editor_current) {
		editor_hwnd->scroll_offset -= (line - b->line) * UI_TEXT_AREA_LINE_H(); // Keep the line in place
		if (editor_hwnd->scroll_offset > 0) {
			editor_hwnd->scroll_offset = 0;
		}
		editor_hwnd->redraws = 2;
	}
	b->line = line;
	if (editing) { // Line being edited
		ui_text_area_clear_selection();
		i32 n = 0;
		while (text[line_start + n] != '\0' && text[line_start + n] != '\n' && n < UI_TEXT_MAX - 1) {
			g_ui->text_selected[n] = text[line_start + n];
			n++;
		}
		g_ui->text_selected[n] = '\0';
		strcpy(g_ui->text_original, g_ui->text_selected);
		g_ui->cursor_x         = p - line_start > n ? n : p - line_start;
		g_ui->highlight_anchor = g_ui->cursor_x;
		g_ui->cursor_sticky_x  = g_ui->cursor_x;
	}
	free(old);
}

void editor_save_buffer(editor_buffer_t *b) {
	char *out = editor_retab(b);
	if (b->crlf) {
		char *lf = out;
		out      = string_replace_all(lf, "\n", "\r\n");
		free(lf);
	}
	buffer_t bytes = {.buffer = (u8 *)out, .length = string_length(out)};
	iron_file_save_bytes(b->path, &bytes, 0);
	free(out);
	free(b->saved);
	b->saved = string_copy(b->text);
	b->time  = editor_file_time(b->path);
	editor_format(b);
}

void editor_save() {
	if (editor_current != NULL && !editor_current->diff && editor_current->image == NULL) {
		editor_save_buffer(editor_current);
	}
}

void editor_save_all() {
	for (i32 i = 0; i < (i32)editor_buffer_list->length; ++i) {
		editor_buffer_t *b = editor_buffer_list->buffer[i];
		if (editor_is_modified(b)) {
			editor_save_buffer(b);
		}
	}
}

// Tints the added and removed lines of a diff
static void editor_draw_diff(char *text, f32 x, f32 y, f32 w) {
	f32 line_h = UI_TEXT_AREA_LINE_H();
	for (i32 i = 0; text[i] != '\0'; y += line_h) {
		char c = text[i];
		if (y + line_h > 0 && y < g_ui->_window_h && (c == '+' || c == '-' || c == '@')) {
			draw_set_color(c == '+' ? 0x2200ff00 : (c == '-' ? 0x22ff0000 : 0x220080ff));
			draw_filled_rect(x, y, w, line_h);
		}
		while (text[i] != '\0' && text[i] != '\n') {
			i++;
		}
		if (text[i] == '\n') {
			i++;
		}
	}
}

// Offset of the first occurrence at or after from, -1 when there is none
static i32 editor_find(char *text, char *needle, i32 from) {
	if (needle[0] == '\0') {
		return -1;
	}
	for (i32 i = from < 0 ? 0 : from; text[i] != '\0'; ++i) {
		i32 j = 0;
		while (needle[j] != '\0' && tolower(text[i + j]) == tolower(needle[j])) {
			j++;
		}
		if (needle[j] == '\0') {
			return i;
		}
	}
	return -1;
}

static bool editor_is_word_char(char c) {
	return c == '_' || (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

// Comments the line being edited after its indentation, or removes the comment
// The text area picks the changed line up
static void editor_toggle_comment() {
	char *line   = g_ui->text_selected;
	i32   len    = string_length(line);
	i32   indent = 0;
	while (line[indent] == ' ' || line[indent] == '\t') {
		indent++;
	}
	if (indent == len) { // Blank line
		return;
	}
	i32 delta;
	if (line[indent] == '/' && line[indent + 1] == '/') {
		delta = line[indent + 2] == ' ' ? -3 : -2;
		memmove(line + indent, line + indent - delta, len - indent + delta + 1);
	}
	else {
		delta = 3;
		if (len + delta > UI_TEXT_MAX - 1) {
			return;
		}
		memmove(line + indent + delta, line + indent, len - indent + 1);
		memcpy(line + indent, "// ", 3);
	}
	// Cursor and selection stay on the same characters
	if (g_ui->cursor_x > indent) {
		g_ui->cursor_x = g_ui->cursor_x + delta > indent ? g_ui->cursor_x + delta : indent;
	}
	if (g_ui->highlight_anchor > indent) {
		g_ui->highlight_anchor = g_ui->highlight_anchor + delta > indent ? g_ui->highlight_anchor + delta : indent;
	}
	g_ui->cursor_sticky_x = g_ui->cursor_x;
	editor_hwnd->redraws  = 2;
}

// Selects a part of the line in the text area, runs while drawing the editor window
static void editor_select(i32 line, i32 col, i32 len) {
	ui_id_t id = ui_widget_id(&editor_current->text, UI_ID_TEXT);
	if (g_ui->text_selected_id != 0 && g_ui->text_selected_id != id) {
		ui_deselect_text(g_ui);
		g_ui->submit_text_id = 0;
	}
	ui_text_area_clear_selection();
	char *src = editor_current->text + editor_line_pos(editor_current->text, line);
	i32   n   = 0;
	while (src[n] != '\0' && src[n] != '\n' && n < UI_TEXT_MAX - 1) {
		g_ui->text_selected[n] = src[n];
		n++;
	}
	g_ui->text_selected[n] = '\0';
	strcpy(g_ui->text_original, g_ui->text_selected);
	g_ui->text_selected_id = id;
	g_ui->is_typing        = true;
	g_ui->highlight_anchor = col;
	g_ui->cursor_x         = col + len;
	g_ui->cursor_sticky_x  = g_ui->cursor_x;
	editor_current->line   = line;
}

// Selects the next occurrence of the whole word at the text offset, wraps at the end
static void editor_word_next(i32 pos, i32 len) {
	char *text = editor_current->text;
	i32   end  = string_length(text);
	if (pos + len > end) {
		return;
	}
	for (i32 n = 1; n < end; ++n) {
		i32 p = (pos + n) % end;
		if (p + len > end || strncmp(text + p, text + pos, len) != 0 || (p > 0 && editor_is_word_char(text[p - 1])) || editor_is_word_char(text[p + len])) {
			continue;
		}
		i32 line       = 0;
		i32 line_start = 0;
		for (i32 i = 0; i < p; ++i) {
			if (text[i] == '\n') {
				line++;
				line_start = i + 1;
			}
		}
		f32 y = line * UI_TEXT_AREA_LINE_H() + editor_hwnd->scroll_offset;
		if (y < 0 || y > g_ui->_window_h - UI_ELEMENT_H() * 2) { // Out of view
			editor_goto_line(line);
		}
		editor_select(line, p - line_start, len);
		return;
	}
}

// Number of matches and the 1-based index of the current one, 0 when not at a match
static void editor_search_count(char *text, i32 *current, i32 *total) {
	i32 len  = string_length(editor_search);
	*current = 0;
	*total   = 0;
	for (i32 p = editor_find(text, editor_search, 0); p != -1; p = editor_find(text, editor_search, p + len)) {
		(*total)++;
		if (p == editor_search_pos) {
			*current = *total;
		}
	}
}

// Scrolls the current file to the line
void editor_goto_line(i32 line) {
	editor_current->line       = line;
	f32 scroll                 = sys_h() / 3.0 - line * UI_TEXT_AREA_LINE_H();
	editor_hwnd->scroll_offset = scroll < 0 ? scroll : 0;
	editor_hwnd->redraws       = 2;
}

// Selects the next match, wraps at the end
static void editor_search_next() {
	char *text = editor_current->text;
	i32   len  = string_length(editor_search);
	i32   end  = string_length(text);
	if (editor_search_pos >= end) { // Text got shorter
		editor_search_pos = -1;
	}
	// Step over the current match, or start right after the stored offset
	i32 from = editor_search_pos + (editor_find(text, editor_search, editor_search_pos) == editor_search_pos ? len : 1);
	i32 p    = editor_find(text, editor_search, from > end ? end : from);
	if (p == -1) {
		p = editor_find(text, editor_search, 0);
	}
	if (p == -1) {
		return;
	}
	editor_search_pos = p;
	i32 line          = 0;
	i32 line_start    = 0;
	for (i32 i = 0; i < p; ++i) {
		if (text[i] == '\n') {
			line++;
			line_start = i + 1;
		}
	}
	editor_goto_line(line);
	editor_select(line, p - line_start, len);
}

// Scrolls to the first match from the search start as the query is typed, the search box keeps the focus
static void editor_search_typed() {
	char *text = editor_current->text;
	i32   p    = editor_find(text, editor_search, editor_search_pos < string_length(text) ? editor_search_pos : 0);
	if (p == -1) {
		p = editor_find(text, editor_search, 0);
	}
	if (p == -1) {
		return;
	}
	i32 line = 0;
	for (i32 i = 0; i < p; ++i) {
		if (text[i] == '\n') {
			line++;
		}
	}
	f32 y = line * UI_TEXT_AREA_LINE_H() + editor_hwnd->scroll_offset;
	if (y < 0 || y > g_ui->_window_h - UI_ELEMENT_H() * 3) { // Out of view or below the search box
		editor_goto_line(line);
	}
}

static void editor_undo_push(editor_undo_t *steps, i32 *count, editor_undo_t step) {
	if (*count == EDITOR_UNDO_MAX) { // Drop the oldest step
		free(steps[0].text);
		for (i32 i = 1; i < EDITOR_UNDO_MAX; ++i) {
			steps[i - 1] = steps[i];
		}
		(*count)--;
	}
	steps[(*count)++] = step;
}

// Stores the previous text once the buffer changes, edits typed in a row on a line make a single step
static void editor_undo_record() {
	editor_buffer_t *b = editor_current;
	if (b->undo_last.text != NULL && !string_equals(b->undo_last.text, b->text)) {
		f64 now = iron_time();
		if (b->undo_count == 0 || now - b->undo_time > 1.0 || b->undo_last.line != b->line) {
			editor_undo_push(b->undo, &b->undo_count, b->undo_last); // Step owns the text now
		}
		else {
			free(b->undo_last.text);
		}
		for (i32 i = 0; i < b->redo_count; ++i) {
			free(b->redo[i].text);
		}
		b->undo_time      = now;
		b->redo_count     = 0;
		b->undo_last.text = NULL;
	}
	if (b->undo_last.text == NULL) {
		b->undo_last.text = string_copy(b->text);
	}
	b->undo_last.line = b->line;
	b->undo_last.col  = g_ui->text_selected_id == editor_text_id ? g_ui->cursor_x : 0;
}

// Undo or redo a step, runs while drawing the editor window
static void editor_undo_apply(bool redo) {
	editor_buffer_t *b     = editor_current;
	editor_undo_t   *from  = redo ? b->redo : b->undo;
	i32             *count = redo ? &b->redo_count : &b->undo_count;
	if (*count == 0) {
		return;
	}
	editor_undo_t step = from[--(*count)];
	editor_undo_t now  = {.text = string_copy(b->text), .line = b->line, .col = g_ui->text_selected_id == editor_text_id ? g_ui->cursor_x : 0};
	editor_undo_push(redo ? b->undo : b->redo, redo ? &b->undo_count : &b->redo_count, now);
	free(b->text);
	free(b->undo_last.text);
	b->text           = string_copy(step.text);
	b->undo_last.text = step.text;
	b->undo_time      = 0; // The next edit is a new step

	i32 line  = step.line;
	i32 lines = ui_line_count(b->text);
	if (line > lines - 1) {
		line = lines - 1;
	}
	i32 len = 0;
	for (char *c = b->text + editor_line_pos(b->text, line); *c != '\0' && *c != '\n'; ++c) {
		len++;
	}
	f32 y = line * UI_TEXT_AREA_LINE_H() + editor_hwnd->scroll_offset;
	if (y < 0 || y > g_ui->_window_h - UI_ELEMENT_H() * 2) { // Out of view
		editor_goto_line(line);
	}
	editor_select(line, step.col > len ? len : step.col, 0);
	editor_hwnd->redraws = 2;
}

static bool editor_minimap_visible(f32 *x, f32 *y, f32 *w, f32 *h) {
	if (!editor_minimap_show || g_ui->_window_w <= 600 * UI_SCALE()) {
		return false;
	}
	f32 mm_w = 150 * UI_SCALE();
	*x       = g_ui->_window_w - mm_w;
	*y       = g_ui->window_header_h;
	*w       = mm_w;
	*h       = g_ui->_window_h - g_ui->window_header_h;
	return true;
}

static void editor_cache_minimap(char *text) {
	f32 line_h = 2 * UI_SCALE();
	f32 char_w = 1 * UI_SCALE();
	i32 tex_w  = math_floor(150 * UI_SCALE());
	i32 tex_h  = math_floor(ui_line_count(text) * line_h);
	if (tex_h < 1) {
		tex_h = 1;
	}
	if (tex_h > 16384) { // Texture size limit, the rest of a long file is not drawn
		tex_h = 16384;
	}

	if (editor_minimap_tex == NULL || editor_minimap_tex->width != tex_w || editor_minimap_tex->height != tex_h) {
		if (editor_minimap_tex != NULL) {
			gpu_delete_texture(editor_minimap_tex);
		}
		editor_minimap_tex = gpu_create_render_target(tex_w, tex_h, GPU_TEXTURE_FORMAT_RGBA32);
	}

	// Render each word as a small rect
	draw_begin(editor_minimap_tex, true, 0x00000000);
	draw_set_color(g_theme->HOVER_COL + 0x00202020);
	i32 line = 0;
	i32 col  = 0;
	for (i32 i = 0; text[i] != '\0'; ++i) {
		if (text[i] == '\n') {
			line++;
			col = 0;
			continue;
		}
		if (text[i] != ' ') {
			i32 len = 1;
			while (text[i + len] != '\0' && text[i + len] != ' ' && text[i + len] != '\n') {
				len++;
			}
			draw_filled_rect(col * char_w, line * line_h, len * char_w, line_h);
			i += len - 1;
			col += len - 1;
		}
		col++;
	}
	draw_end();
}

static void editor_draw_minimap(char *text, bool diff, f32 mm_x, f32 mm_y, f32 mm_w, f32 mm_h) {
	if (editor_minimap_tex == NULL) {
		return;
	}
	f32 line_h          = 2 * UI_SCALE();
	i32 line_count      = ui_line_count(text);
	f32 content_h       = line_count * UI_TEXT_AREA_LINE_H();
	f32 full_h          = line_count * line_h;
	f32 scroll_progress = content_h > 0 ? -g_ui->current_window->scroll_offset / content_h : 0;
	f32 out_of_screen   = full_h - mm_h;
	if (out_of_screen < 0) {
		out_of_screen = 0;
	}

	// Draw the visible region of the cached minimap
	f32 tex_h = editor_minimap_tex->height;
	f32 sh    = tex_h < mm_h ? tex_h : mm_h;
	f32 sy    = out_of_screen * scroll_progress;
	if (sy > tex_h - sh) {
		sy = tex_h - sh;
	}
	if (sy < 0) {
		sy = 0;
	}
	draw_set_color(0xffffffff);
	draw_sub_image(editor_minimap_tex, 0, sy, mm_w, sh, mm_x, mm_y);

	// Added and removed lines of a diff
	if (diff) {
		f32 y = -sy;
		for (i32 i = 0; text[i] != '\0' && y < mm_h; y += line_h) {
			char c = text[i];
			if (y >= 0 && y + line_h <= mm_h && (c == '+' || c == '-')) {
				draw_set_color(c == '+' ? 0x6600ff00 : 0x66ff0000);
				draw_filled_rect(mm_x, mm_y + y, mm_w, line_h);
			}
			while (text[i] != '\0' && text[i] != '\n') {
				i++;
			}
			if (text[i] == '\n') {
				i++;
			}
		}
	}

	// Occurrences of the selected text
	if (editor_mark != NULL) {
		f32 char_w     = 1 * UI_SCALE();
		i32 len        = string_length(editor_mark);
		i32 line       = 0;
		i32 line_start = 0;
		i32 scan       = 0;
		for (i32 p = editor_find(text, editor_mark, 0); p != -1; p = editor_find(text, editor_mark, p + len)) {
			for (; scan < p; ++scan) {
				if (text[scan] == '\n') {
					line++;
					line_start = scan + 1;
				}
			}
			f32 y = line * line_h - sy;
			if (y < 0 || y + line_h > mm_h) {
				continue;
			}
			f32 x = (p - line_start) * char_w;
			f32 w = len * char_w < 4 * UI_SCALE() ? 4 * UI_SCALE() : len * char_w;
			if (x + w > mm_w) { // Long lines are marked at the edge
				x = mm_w - w > 0 ? mm_w - w : 0;
			}
			draw_set_color(p == editor_mark_pos ? 0xffffee00 : 0xbbffee00);
			draw_filled_rect(mm_x + x, mm_y + y, w, line_h);
		}
	}

	i32 offset = math_floor(sy / line_h);

	// View box
	f32 visible_area = out_of_screen > 0 ? mm_h : full_h;
	f32 box_h        = (mm_h / UI_TEXT_AREA_LINE_H()) * line_h;
	f32 box_y        = mm_y + scroll_progress * visible_area;
	if (box_y < mm_y) {
		box_y = mm_y;
	}
	else if (box_y > mm_y + mm_h - box_h) {
		box_y = mm_y + mm_h - box_h;
	}
	draw_set_color(0x1cffffff);
	draw_filled_rect(mm_x, box_y, mm_w, box_h);

	// Drag the minimap to scroll the text area
	if (editor_minimap_scrolling && content_h > mm_h) {
		f32 my_local      = g_ui->input_y - (g_ui->_window_y + mm_y);
		i32 picked        = offset + math_floor(my_local / line_h);
		f32 visible_lines = mm_h / UI_TEXT_AREA_LINE_H();
		f32 target        = (picked - visible_lines / 2) * UI_TEXT_AREA_LINE_H();
		f32 max_scroll    = (line_count - 1) * UI_TEXT_AREA_LINE_H(); // Last line at the top
		if (target < 0) {
			target = 0;
		}
		else if (target > max_scroll) {
			target = max_scroll;
		}
		g_ui->current_window->scroll_offset = -target;
	}
}

// Switches between the current and the previously opened file, a file which was just closed is opened again
void editor_switch_last() {
	if (editor_current == NULL && editor_closed != NULL) {
		char *path    = editor_closed;
		editor_closed = NULL;
		editor_open(path);
	}
	else if (editor_last != NULL && editor_last != editor_current) {
		editor_set_current(editor_last);
	}
}

// Closes the current file
void editor_close() {
	editor_buffer_t *b = editor_current;
	if (b == NULL) {
		return;
	}
	editor_autosave();
	editor_set_current(NULL);
	i32 i = array_index_of(editor_buffer_list, b); // Diffs and pictures are not kept, those are freed already
	if (i >= 0) {
		array_splice(editor_buffer_list, i, 1);
		map_delete(editor_buffers, b->path);
		editor_closed = b->path; // Not freed with the buffer
		editor_free_buffer(b);
	}
	pad_save_config();
}

void editor_close_all() {
	editor_closed = NULL;
	editor_autosave();
	editor_set_current(NULL);
	for (i32 i = 0; i < (i32)editor_buffer_list->length; ++i) {
		editor_free_buffer(editor_buffer_list->buffer[i]);
	}
	map_free(editor_buffers);
	array_delete(editor_buffer_list);
	editor_buffers     = any_map_create();
	editor_buffer_list = any_array_create(0);
}

void editor_init() {
	editor_hwnd        = ui_window_create();
	editor_buffers     = any_map_create();
	editor_buffer_list = any_array_create(0);
}

void browser_menu_open_editor();

void editor_render(i32 x, i32 y, i32 w, i32 h) {
	if (!ui_window(editor_hwnd, x, y, w, h, false)) {
		return;
	}

	if (editor_current == NULL) {
		// Faded shortcuts in the middle of the panel, the names end and the keys start at its center
		static char       *names[]   = {"Go to File",
		                                "Previous File",
		                                "Save",
		                                "Search",
		                                "Select Word",
		                                "Toggle Comment",
		                                "Undo",
		                                "Redo",
		                                "Search in Files",
		                                "Toggle Project Tree",
		                                "Toggle Git Changes",
		                                "Toggle Output",
		                                "Toggle Minimap",
		                                "Open Config",
		                                "Close File or Project",
		                                "Build",
		                                "Build and Run",
		                                "Clean Build and Run",
		                                "Toggle Fullscreen"};
		static char       *keys[]    = {"Ctrl+P", "Ctrl+Tab", "Ctrl+S", "Ctrl+F", "Ctrl+D", "Ctrl+/", "Ctrl+Z", "Ctrl+Shift+Z", "Ctrl+Shift+F", "Ctrl+E", "Ctrl+G",
		                                "Ctrl+J", "Ctrl+M", "Ctrl+K", "Ctrl+W", "Ctrl+B", "F5",           "Shift+F5",     "F11"};
		static f32         ratio[2]  = {0.5, 0.5};
		static f32_array_t ratios    = {.buffer = ratio, .length = 2};
		i32                count     = sizeof(names) / sizeof(names[0]);
		i32                _TEXT_COL = g_theme->TEXT_COL;
		g_theme->TEXT_COL            = 0xff606060;
		g_ui->_y += math_floor((h - count * (UI_ELEMENT_H() + UI_ELEMENT_OFFSET())) / 2.0);
		i32 _TEXT_OFFSET     = g_theme->TEXT_OFFSET;
		g_theme->TEXT_OFFSET = 10; // Half of the gap between the columns
		for (i32 i = 0; i < count; ++i) {
			ui_row(&ratios);
			ui_text(names[i], UI_ALIGN_RIGHT, 0x00000000);
			ui_text(keys[i], UI_ALIGN_LEFT, 0x00000000);
		}
		g_theme->TEXT_OFFSET = _TEXT_OFFSET;
		g_theme->TEXT_COL    = _TEXT_COL;
		return;
	}

	// Picture, scaled down to fit the panel
	if (editor_current->image != NULL) {
		gpu_texture_t *image = editor_current->image;
		f32            pad   = 16 * UI_SCALE();
		f32            scale = 1.0;
		if (image->width * scale > w - pad * 2) {
			scale = (w - pad * 2) / image->width;
		}
		f32 label_h = UI_ELEMENT_H() + UI_ELEMENT_OFFSET(); // Size is shown below the picture
		if (image->height * scale > h - pad * 2 - label_h) {
			scale = (h - pad * 2 - label_h) / image->height;
		}
		f32 iw                     = math_floor(image->width * scale);
		f32 ih                     = math_floor(image->height * scale);
		f32 iy                     = math_floor((h - label_h - ih) / 2.0);
		editor_hwnd->scroll_offset = 0;
		draw_set_color(0xffffffff);
		draw_scaled_image(image, math_floor((w - iw) / 2.0), iy, iw, ih);
		g_ui->_y = iy + ih + UI_ELEMENT_OFFSET();
		pad_fade_begin();
		ui_text(string_tmp("%d x %d", image->width, image->height), UI_ALIGN_CENTER, 0x00000000);
		pad_fade_end();
		return;
	}

	// Cache minimap
	char *text = editor_current->text;
	u32   hash = 2166136261u;
	for (i32 i = 0; text[i] != '\0'; ++i) {
		hash = (hash ^ (u8)text[i]) * 16777619u;
	}
	f32  mm_x, mm_y, mm_w, mm_h;
	bool minimap_on = editor_minimap_visible(&mm_x, &mm_y, &mm_w, &mm_h);
	if (minimap_on && (editor_minimap_tex == NULL || editor_minimap_hash != hash)) {
		editor_minimap_hash = hash;
		draw_end();
		editor_cache_minimap(text);
		draw_begin(&g_ui->current_window->texture, false, 0);
	}
	if (minimap_on && g_ui->input_started && ui_input_in_rect(g_ui->_window_x + mm_x, g_ui->_window_y + mm_y, mm_w, mm_h)) {
		editor_minimap_scrolling = true;
	}
	if (!g_ui->input_down) {
		editor_minimap_scrolling = false;
	}

	draw_font_t *_font      = g_ui->ops->font;
	i32          _font_size = g_ui->font_size;
	ui_set_font(g_ui, g_font_mono);
	g_ui->font_size              = math_floor(15 * UI_SCALE());
	ui_text_area_line_numbers    = true;
	ui_text_area_scroll_past_end = true;
	ui_text_area_coloring        = editor_get_coloring();
	ui_text_coloring_draw        = editor_draw_coloring;

	// Open the search box on ctrl+f
	editor_text_id        = ui_widget_id(&editor_current->text, UI_ID_TEXT);
	bool is_text_selected = g_ui->text_selected_id == editor_text_id;
	bool search_selected  = g_ui->text_selected_id == ui_widget_id(&editor_search, UI_ID_TEXT);
	if (g_ui->input_enabled && g_ui->is_ctrl_down && g_ui->is_key_pressed && g_ui->key_code == KEY_CODE_F) { // Not below the file picker
		editor_search_show  = true;
		editor_search_focus = true;
		// Start at the current line
		editor_search_pos = editor_line_pos(editor_current->text, editor_current->line) - 1;
		// Pre-fill the search with selected text
		if (is_text_selected && g_ui->cursor_x != g_ui->highlight_anchor) {
			i32 a   = g_ui->cursor_x < g_ui->highlight_anchor ? g_ui->cursor_x : g_ui->highlight_anchor;
			i32 b   = g_ui->cursor_x < g_ui->highlight_anchor ? g_ui->highlight_anchor : g_ui->cursor_x;
			i32 len = string_length(g_ui->text_selected);
			if (a < 0) {
				a = 0;
			}
			if (b > len) {
				b = len;
			}
			if (a < b) {
				editor_search = substring(g_ui->text_selected, a, b);
			}
		}
		g_ui->is_key_pressed = false;
		g_ui->key_code       = 0;
	}
	// ctrl+d to select the word at the cursor, once more for its next occurrence
	if (is_text_selected && g_ui->is_ctrl_down && g_ui->is_key_pressed && g_ui->key_code == KEY_CODE_D) {
		char *line  = g_ui->text_selected;
		i32   len   = string_length(line);
		i32   start = g_ui->cursor_x > len ? len : g_ui->cursor_x;
		i32   end   = start;
		while (start > 0 && editor_is_word_char(line[start - 1])) {
			start--;
		}
		while (end < len && editor_is_word_char(line[end])) {
			end++;
		}
		i32 a = g_ui->cursor_x < g_ui->highlight_anchor ? g_ui->cursor_x : g_ui->highlight_anchor;
		i32 b = g_ui->cursor_x < g_ui->highlight_anchor ? g_ui->highlight_anchor : g_ui->cursor_x;
		if (end > start && a == start && b == end) { // Word is selected already
			editor_word_next(editor_line_pos(editor_current->text, editor_current->line) + start, end - start);
		}
		else if (end > start) {
			ui_text_area_clear_selection();
			g_ui->highlight_anchor = start;
			g_ui->cursor_x         = end;
			g_ui->cursor_sticky_x  = end;
		}
		g_ui->is_key_pressed = false; // Consume so the editor ignores ctrl+d
		g_ui->key_code       = 0;
	}

	// ctrl+/ to comment the line at the cursor, or to uncomment it
	if (is_text_selected && !editor_current->diff && g_ui->is_ctrl_down && g_ui->is_key_pressed && g_ui->key_code == KEY_CODE_SLASH) {
		editor_toggle_comment();
		g_ui->is_key_pressed = false; // Consume so the editor ignores ctrl+/
		g_ui->key_code       = 0;
	}

	// ctrl+z to undo, ctrl+shift+z to redo
	if (g_ui->input_enabled && !editor_current->diff && (is_text_selected || g_ui->text_selected_id == 0) && g_ui->is_ctrl_down && g_ui->is_key_pressed &&
	    g_ui->key_code == KEY_CODE_Z) {
		editor_undo_apply(g_ui->is_shift_down);
		is_text_selected     = g_ui->text_selected_id == editor_text_id;
		g_ui->is_key_pressed = false;
		g_ui->key_code       = 0;
	}

	// Enter in the search box selects the next match, the editor takes the focus
	// Enter goes on to the next one while a match is selected
	bool match_selected = false;
	if (editor_search_show && is_text_selected && editor_search[0] != '\0') {
		i32 a          = g_ui->cursor_x < g_ui->highlight_anchor ? g_ui->cursor_x : g_ui->highlight_anchor;
		i32 b          = g_ui->cursor_x < g_ui->highlight_anchor ? g_ui->highlight_anchor : g_ui->cursor_x;
		i32 len        = string_length(editor_search);
		match_selected = a >= 0 && b - a == len && b <= string_length(g_ui->text_selected) && editor_find(g_ui->text_selected + a, editor_search, 0) == 0;
	}
	if (editor_search_show && (search_selected || match_selected) && g_ui->is_key_pressed && g_ui->key_code == KEY_CODE_RETURN) {
		if (search_selected) {
			editor_search = string_copy(g_ui->text_selected);
		}
		editor_search_next();
		g_ui->is_key_pressed = false; // Not a new line
		g_ui->key_code       = 0;
	}

	// Occurrences of the selected text are highlighted in the text and on the minimap, not for a multi-line selection
	static char mark[UI_TEXT_MAX];
	editor_mark     = NULL;
	i32  sel_line   = ui_text_area_selection_line();
	bool multi_line = sel_line != -1 && sel_line != editor_current->line;
	if (g_ui->text_selected_id == ui_widget_id(&editor_current->text, UI_ID_TEXT) && g_ui->cursor_x != g_ui->highlight_anchor && !multi_line) {
		i32 a      = g_ui->cursor_x < g_ui->highlight_anchor ? g_ui->cursor_x : g_ui->highlight_anchor;
		i32 b      = g_ui->cursor_x < g_ui->highlight_anchor ? g_ui->highlight_anchor : g_ui->cursor_x;
		i32 len    = string_length(g_ui->text_selected);
		a          = a < 0 ? 0 : a;
		b          = b > len ? len : b;
		bool blank = true;
		for (i32 i = a; i < b; ++i) {
			mark[i - a] = g_ui->text_selected[i];
			blank       = blank && g_ui->text_selected[i] == ' ';
		}
		if (b - a >= 2 && !blank) { // A single character would mark the whole file
			mark[b - a]     = '\0';
			editor_mark     = mark;
			editor_mark_pos = editor_line_pos(editor_current->text, editor_current->line) + a;
		}
	}
	// Search panel query otherwise, as typed
	if (editor_mark == NULL && search_show) {
		char *query = g_ui->text_selected_id == search_input_id ? g_ui->text_selected : search_query;
		i32   len   = string_length(query);
		if (len >= 2 && len < UI_TEXT_MAX) {
			strcpy(mark, query);
			editor_mark     = mark;
			editor_mark_pos = -1;
		}
	}

	// Search box query, as typed
	if (editor_mark == NULL && editor_search_show && search_selected) {
		i32 len = string_length(editor_search);
		if (len >= 2 && len < UI_TEXT_MAX) {
			strcpy(mark, editor_search);
			editor_mark     = mark;
			editor_mark_pos = -1;
		}
	}

	// Search box
	f32  sb_h           = UI_ELEMENT_H() + UI_ELEMENT_OFFSET() * 2;
	f32  sb_y           = g_ui->_window_h - sb_h;
	f32  sb_w           = minimap_on ? mm_x : g_ui->_window_w;
	bool sb_hover       = editor_search_show && ui_input_in_rect(g_ui->_window_x, g_ui->_window_y + sb_y, sb_w, sb_h);
	ui_text_area_search = editor_mark;

	// Prevent text area clicks while scrolling the minimap or using the search box
	bool _input_enabled = g_ui->input_enabled;
	if (editor_minimap_scrolling || sb_hover) {
		g_ui->input_enabled = false;
	}
	f32 _w = g_ui->_w;
	if (minimap_on) {
		g_ui->_w = mm_x - g_ui->_x; // Text ends where the minimap starts
	}
	f32 text_x = g_ui->_x;
	f32 text_y = g_ui->_y;
	f32 text_w = g_ui->_w;
	char *prev_text = editor_current->text;
	ui_text_area(&editor_current->text, &editor_current->line, UI_ALIGN_LEFT, !editor_current->diff, "", false);
	if (editor_current->text != prev_text && editor_current->saved != prev_text) { // Edited, the text area made a new string
		free(prev_text);
	}
	if (g_ui->input_enabled && g_ui->input_released_r && ui_input_in_rect(g_ui->_window_x, g_ui->_window_y, g_ui->_window_w, g_ui->_window_h)) {
		browser_menu_open_editor();
	}
	g_ui->_w            = _w;
	g_ui->input_enabled = _input_enabled;
	ui_text_area_search = NULL;
	if (!editor_current->diff) {
		editor_undo_record();
	}
	if (editor_current->diff) {
		if (g_ui->text_selected_id == editor_text_id) {
			editor_current->caret = editor_current->line;
		}
		editor_draw_diff(editor_current->text, text_x, text_y, text_w);
	}

	if (minimap_on) {
		editor_draw_minimap(editor_current->text, editor_current->diff, mm_x, mm_y, mm_w, mm_h);
	}

	ui_text_area_line_numbers    = false;
	ui_text_area_scroll_past_end = false;
	ui_text_area_coloring        = NULL;
	ui_text_coloring_draw        = NULL;
	ui_set_font(g_ui, _font);
	g_ui->font_size = _font_size;

	if (editor_search_show) {
		f32 _x = g_ui->_x;
		f32 _y = g_ui->_y;
		f32 _w = g_ui->_w;
		draw_set_color(g_theme->WINDOW_BG_COL);
		draw_filled_rect(0, sb_y, sb_w, sb_h);
		draw_set_color(g_theme->BUTTON_COL);
		draw_filled_rect(0, sb_y, sb_w, 1);
		g_ui->_x = 0;
		g_ui->_y = sb_y + UI_ELEMENT_OFFSET();
		g_ui->_w = sb_w;
		// Current match and the number of matches, 0 before the first enter
		char *label = "Search";
		if (editor_search[0] != '\0') {
			i32 current, total;
			editor_search_count(editor_current->text, &current, &total);
			label = string_tmp("%d/%d", current, total);
		}
		char *prev_search = editor_search;
		ui_text_input(&editor_search, label, UI_ALIGN_LEFT, true, true);
		if (editor_search != prev_search) {
			editor_search_typed();
		}
		if (editor_search_focus) { // Ctrl+f to open
			editor_search_focus = false;
			ui_start_text_edit(&editor_search, UI_ALIGN_LEFT);
			g_ui->cursor_x         = string_length(editor_search);
			g_ui->highlight_anchor = 0;
		}
		// Esc to close
		if (g_ui->is_escape_down) {
			editor_search_show = false;
		}
		g_ui->_x = _x;
		g_ui->_y = _y;
		g_ui->_w = _w;
	}
}
