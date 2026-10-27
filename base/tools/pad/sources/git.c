// Git panel

#define GIT_FILES_MAX 2000

typedef struct git_file {
	char  status; // M, A, D, R, U - untracked
	char *rel;    // Path relative to the repository
	char *path;
	char *orig;   // Original path of a renamed file
	bool  staged; // Added to the commit
} git_file_t;

ui_window_t *git_hwnd     = NULL;
bool         git_show     = false;
char        *git_root     = NULL; // NULL when the project is not in a repository
any_array_t *git_files    = NULL;
i32          git_ahead    = 0;  // Commits to be pushed
char        *git_message  = ""; // Commit message
ui_id_t      git_input_id = 0;

void build_exec(char *cmd, bool show);
bool browser_menu_clicked();
void browser_menu_open_git(git_file_t *f);

// Runs the command and returns its output
static char *git_exec(char *cmd, i32 *length) {
#ifdef IRON_WINDOWS
	HANDLE process = NULL;
	FILE  *p       = pad_popen(string("%s 2>nul", cmd), &process);
#else
	FILE *p = popen(string("%s 2>/dev/null", cmd), "r");
#endif
	i32   cap = 4096;
	i32   len = 0;
	char *out = string_alloc(cap);
	if (p != NULL) {
		char chunk[4096];
		i32  n;
		while ((n = (i32)fread(chunk, 1, sizeof(chunk), p)) > 0) {
			if (len + n + 1 > cap) {
				cap         = (len + n + 1) * 2;
				char *grown = string_alloc(cap);
				memcpy(grown, out, len);
				out = grown;
			}
			memcpy(out + len, chunk, n);
			len += n;
		}
#ifdef IRON_WINDOWS
		pad_pclose(p, process);
#else
		pclose(p);
#endif
	}
	out[len] = '\0';
	*length  = len;
	return out;
}

// Staged files first
static int git_compare(const void *a, const void *b) {
	git_file_t *fa = *(git_file_t **)a;
	git_file_t *fb = *(git_file_t **)b;
	if (fa->staged != fb->staged) {
		return fa->staged ? -1 : 1;
	}
	return strcmp(fa->rel, fb->rel);
}

static void git_add_file(char status, char *rel, char *orig, bool staged) {
	git_file_t *f = ALLOC_INIT(git_file_t, {0});
	f->status     = status;
	f->rel        = string_copy(rel);
	f->orig       = orig != NULL ? string_copy(orig) : NULL;
	f->staged     = staged;
	f->path       = path_join(git_root, f->rel);
#ifdef IRON_WINDOWS
	f->path = string_replace_all(f->path, "/", "\\");
#endif
	any_array_push(git_files, f);
}

void git_refresh() {
	git_root  = NULL;
	git_files = any_array_create(0);
	git_ahead = 0;
	if (git_hwnd != NULL) {
		git_hwnd->redraws = 2;
	}
	if (pad_project_dir == NULL) {
		return;
	}

	i32   len;
	char *root = git_exec(string("git -C \"%s\" rev-parse --show-toplevel", pad_project_dir), &len);
	while (len > 0 && (root[len - 1] == '\n' || root[len - 1] == '\r')) {
		root[--len] = '\0';
	}
	if (len == 0) {
		return;
	}
	git_root = root;

	// Nothing is printed when the branch has no upstream
	git_ahead = atoi(git_exec(string("git -C \"%s\" rev-list --count @{u}..HEAD", git_root), &len));

	// Entries are separated by zero, a renamed file is followed by its original path
	char *out = git_exec(string("git -C \"%s\" status --porcelain -z -uall", git_root), &len);
	for (i32 i = 0; i < len && git_files->length < GIT_FILES_MAX;) {
		char *entry = out + i;
		i32   elen  = string_length(entry);
		i += elen + 1;
		if (elen < 4) {
			continue;
		}
		char *orig = NULL;
		if (entry[0] == 'R' || entry[0] == 'C' || entry[1] == 'R' || entry[1] == 'C') {
			orig = out + i;
			i += string_length(out + i) + 1;
		}
		// Index and working tree status, a file changed after it was staged is listed twice
		char x        = entry[0];
		char y        = entry[1];
		bool conflict = x == 'U' || y == 'U' || (x == 'A' && y == 'A') || (x == 'D' && y == 'D');
		if (x == '?' || conflict) {
			git_add_file(conflict ? '!' : 'U', entry + 3, NULL, false);
			continue;
		}
		if (x != ' ') {
			git_add_file(x, entry + 3, x == 'R' ? orig : NULL, true);
		}
		if (y != ' ') {
			git_add_file(y, entry + 3, NULL, false);
		}
	}
	// By path, git lists the untracked files last
	qsort(git_files->buffer, git_files->length, sizeof(void *), git_compare);
}

void git_toggle() {
	git_show = !git_show;
	if (git_show) {
		git_hwnd->scroll_offset = 0;
		git_refresh();
	}
}

// Shows the whole file with its changes against the last commit marked, files without a diff are opened as they are
static void git_open(git_file_t *f) {
	editor_autosave(); // Diff is taken from the disk
	bool shown = editor_current != NULL && editor_current->diff && string_equals(editor_current->path, f->path);
	if (f->status != 'U' && !shown && !editor_is_image(f->path)) { // Clicking the shown diff again opens the file
		i32   len;
		char *diff = git_exec(string("git -C \"%s\" diff --no-color --no-ext-diff --unified=1000000 HEAD -- \"%s\"", git_root, f->rel), &len);
		// Skip the header with the command, index and file names, and the line of the only hunk
		char *hunk = starts_with(diff, "@@") ? diff : strstr(diff, "\n@@");
		if (hunk != NULL) {
			hunk += hunk[0] == '\n' ? 1 : 0;
			char *end = strchr(hunk, '\n');
			hunk      = end != NULL ? end + 1 : hunk;
			len -= (i32)(hunk - diff);
			diff = hunk;
		}
		if (len > 0 && editor_open_diff(f->path, diff, len)) {
			return;
		}
	}
	if (iron_file_exists(f->path)) {
		i32 line = shown ? editor_diff_line() : -1; // At the line of the caret in the diff
		editor_open(f->path);
		if (line >= 0 && editor_current != NULL && !editor_current->diff && string_equals(editor_current->path, f->path)) {
			editor_goto_line(line);
		}
	}
}

static void git_stage(git_file_t *f) {
	i32 len;
	if (!f->staged) {
		git_exec(string("git -C \"%s\" add -A -- \"%s\"", git_root, f->rel), &len);
	}
	else if (f->orig != NULL) {
		git_exec(string("git -C \"%s\" reset -q -- \"%s\" \"%s\"", git_root, f->rel, f->orig), &len);
	}
	else {
		git_exec(string("git -C \"%s\" reset -q -- \"%s\"", git_root, f->rel), &len);
	}
	git_refresh();
}

// Discards the changes of the file, staged or not, a new file is removed
void git_revert(git_file_t *f) {
	i32 len;
	if (f->orig != NULL) { // Renamed, the original file is restored
		git_exec(string("git -C \"%s\" reset -q -- \"%s\" \"%s\"", git_root, f->rel, f->orig), &len);
		git_exec(string("git -C \"%s\" checkout -q HEAD -- \"%s\"", git_root, f->orig), &len);
	}
	else {
		git_exec(string("git -C \"%s\" reset -q -- \"%s\"", git_root, f->rel), &len);
	}
	git_exec(string("git -C \"%s\" checkout -q HEAD -- \"%s\"", git_root, f->rel), &len); // Fails for a file which is not in the last commit
	git_exec(string("git -C \"%s\" clean -f -q -- \"%s\"", git_root, f->rel), &len);      // Removes it when left untracked

	// Opened file follows the disk, unsaved changes included
	if (editor_current != NULL && editor_current->diff && string_equals(editor_current->path, f->path)) {
		editor_close();
	}
	editor_buffer_t *b = any_map_get(editor_buffers, f->path);
	if (b != NULL) {
		if (b == editor_current && g_ui->text_selected_id != 0) {
			ui_deselect_text(g_ui);
			g_ui->submit_text_id = 0;
			ui_text_area_clear_selection();
		}
		char *old = b->text;
		if (!iron_file_exists(f->path) || !editor_load(b)) {
			if (b == editor_current) {
				editor_set_current(NULL);
			}
			array_splice(editor_buffer_list, array_index_of(editor_buffer_list, b), 1);
			map_delete(editor_buffers, b->path);
			editor_free_buffer(b);
		}
		else {
			free(old);
			if (b->line > ui_line_count(b->text) - 1) {
				b->line = ui_line_count(b->text) - 1;
			}
		}
		editor_hwnd->redraws = 2;
	}
	browser_refresh();
	git_refresh();
}

static bool git_has_staged() {
	return git_files->length > 0 && ((git_file_t *)git_files->buffer[0])->staged;
}

// Commits the staged files, the message is kept when the commit fails
static void git_commit() {
	if (git_message[0] == '\0' || git_files->length == 0) {
		return;
	}
	if (!git_has_staged()) { // Nothing staged, all changed files are committed
		i32 len;
		git_exec(string("git -C \"%s\" add -A", git_root), &len);
		git_refresh();
		if (git_root == NULL || !git_has_staged()) {
			return;
		}
	}
	// Message is passed in a file, no escaping needed
	char    *path  = string("%scommit_msg.txt", iron_internal_save_path());
	buffer_t bytes = {.buffer = (u8 *)git_message, .length = string_length(git_message)};
	iron_file_save_bytes(path, &bytes, 0);
	i32 len;
	git_exec(string("git -C \"%s\" commit -F \"%s\"", git_root, path), &len);
	remove(path);
	git_refresh();
	if (git_root != NULL && !git_has_staged()) {
		if (git_input_id != 0 && g_ui->text_selected_id == git_input_id) {
			ui_deselect_text(g_ui);
			g_ui->submit_text_id = 0;
		}
		git_message = "";
	}
}

// Pull and push, printed to the output panel
static void git_sync() {
	build_exec(string("git -C \"%s\" pull 2>&1 && git -C \"%s\" push 2>&1", git_root, git_root), true);
}

// File row with a button to stage or unstage it shown on hover
static bool git_row(git_file_t *f, char *label, bool selected, bool *stage) {
	static f32         ratio[2];
	static f32_array_t ratios = {.buffer = ratio, .length = 2};
	if (!ui_get_hover(UI_ELEMENT_H())) {
		return pad_row(label, selected);
	}
	ratio[1] = UI_ELEMENT_H() / g_ui->_w;
	ratio[0] = 1.0 - ratio[1];
	ui_row(&ratios);
	bool clicked = pad_row(label, selected);
	*stage       = ui_button(f->staged ? "-" : "+", UI_ALIGN_CENTER, "");
	return clicked;
}

void git_init() {
	git_hwnd  = ui_window_create();
	git_files = any_array_create(0);
	// Fail instead of waiting for credentials
#ifdef IRON_WINDOWS
	_putenv("GIT_TERMINAL_PROMPT=0");
#else
	setenv("GIT_TERMINAL_PROMPT", "0", 1);
#endif
}

void git_render(i32 x, i32 y, i32 w, i32 h) {
	if (!ui_window(git_hwnd, x, y, w, h, false)) {
		return;
	}
	if (pad_project_dir == NULL) {
		return;
	}
	pad_fade_begin();
	if (git_root == NULL) {
		ui_text("No git repository", UI_ALIGN_LEFT, 0x00000000);
		pad_fade_end();
		return;
	}

	git_input_id = ui_widget_id(&git_message, UI_ID_TEXT);
	bool commit  = false;
	ui_begin_sticky();
	ui_text_input(&git_message, "Message", UI_ALIGN_LEFT, true, true);
	bool sync        = git_files->length == 0;                      // Everything is committed
	g_ui->enabled    = sync || !editor_is_modified(editor_current); // Unsaved changes would be left out of the commit
	char *sync_label = git_ahead > 0 ? string_tmp("Sync (%d %s)", git_ahead, git_ahead == 1 ? "commit" : "commits") : "Sync";
	bool  clicked    = ui_button(sync ? sync_label : "Commit", UI_ALIGN_CENTER, "");
	g_ui->enabled    = true;
	if (clicked) {
		if (sync) {
			git_sync();
		}
		else {
			commit = true;
		}
	}
	ui_end_sticky();
	if (commit) {
		git_commit();
		pad_fade_end();
		return;
	}
	if (git_files->length == 0) {
		ui_text("No changes", UI_ALIGN_LEFT, 0x00000000);
		pad_fade_end();
		return;
	}

	i32 _FILL_BUTTON_BG     = g_theme->FILL_BUTTON_BG;
	i32 _TEXT_COL           = g_theme->TEXT_COL;
	g_theme->FILL_BUTTON_BG = false;
	git_file_t *opened      = NULL; // Applied once all rows are drawn, cutting the list short would scroll the panel
	git_file_t *staged      = NULL;
	for (i32 i = 0; i < (i32)git_files->length; ++i) {
		git_file_t *f = git_files->buffer[i];
		if (i == 0 || f->staged != ((git_file_t *)git_files->buffer[i - 1])->staged) { // Staged files are listed first
			g_theme->TEXT_COL = pad_text_col;
			ui_text(f->staged ? "Staged" : "Changes", UI_ALIGN_LEFT, 0x00000000);
			g_theme->TEXT_COL = _TEXT_COL;
		}
		// File name first, the folder is cut off in a narrow panel
		i32   slash = string_last_index_of(f->rel, "/");
		char *label =
		    slash > -1 ? string_tmp("%c  %s  %s", f->status, f->rel + slash + 1, substring(f->rel, 0, slash)) : string_tmp("%c  %s", f->status, f->rel);
		bool stage    = false;
		bool selected = editor_current != NULL && string_equals(editor_current->path, f->path);
		if (browser_menu_show) {
			selected = f == browser_menu_git; // Mark the row the menu belongs to
		}
		else if (browser_menu_clicked()) {
			browser_menu_open_git(f);
		}
		if (git_row(f, label, selected, &stage)) {
			opened = f;
		}
		if (stage) {
			staged = f;
		}
	}
	if (staged != NULL) {
		git_stage(staged);
	}
	else if (opened != NULL) {
		git_open(opened);
	}
	g_theme->FILL_BUTTON_BG = _FILL_BUTTON_BG;
	pad_fade_end();
}
