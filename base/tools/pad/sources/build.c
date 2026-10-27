// Build and run the project with amake

#ifndef IRON_WINDOWS
#include <sys/wait.h>
#endif

#define BUILD_LOG_MAX  (64 * 1024)
#define BUILD_LINE_MAX 256 // Longer lines are trimmed

ui_window_t *build_hwnd = NULL;
bool         build_show = false;

static iron_thread_t build_thread;
static iron_mutex_t  build_mutex;
static char         *build_cmd       = NULL;
static volatile bool build_running   = false;
static volatile bool build_finished  = false;
static volatile bool build_log_dirty = false;
static i32           build_exit_code = 0;
static char          build_log[BUILD_LOG_MAX];
static i32           build_log_len = 0;
static char          build_log_view[BUILD_LOG_MAX];
static bool          build_follow     = true;  // Scroll with the output
static bool          build_filter     = false; // Show error lines only
static i32           build_config     = 0;     // Debug or release
static bool          build_clean_next = false;
static f32           build_bottom     = 0; // Scroll offset of the last row

// Called from the build thread
static void build_log_append(char *s) {
	i32 len = string_length(s);
	iron_mutex_lock(&build_mutex);
	if (build_log_len + len >= BUILD_LOG_MAX) { // Drop the oldest half
		i32 drop = BUILD_LOG_MAX / 2 < build_log_len ? BUILD_LOG_MAX / 2 : build_log_len;
		memmove(build_log, build_log + drop, build_log_len - drop);
		build_log_len -= drop;
	}
	memcpy(build_log + build_log_len, s, len);
	build_log_len += len;
	build_log[build_log_len] = '\0';
	build_log_dirty          = true;
	iron_mutex_unlock(&build_mutex);
}

static void build_thread_func(void *param) {
#ifdef IRON_WINDOWS
	HANDLE process = NULL;
	FILE  *p       = pad_popen(build_cmd, &process);
#else
	FILE *p = popen(build_cmd, "r");
#endif
	if (p == NULL) {
		build_log_append("Unable to start the build\n");
		build_exit_code = -1;
	}
	else {
		char line[1024];
		i32  line_len = 0; // A long line arrives in several reads
		while (fgets(line, sizeof(line), p) != NULL) {
			i32 n = 0;
			for (i32 i = 0; line[i] != '\0'; ++i) {
				if (line[i] == '\n') {
					line_len = 0;
				}
				else if (++line_len > BUILD_LINE_MAX) {
					continue;
				}
				line[n++] = line[i];
			}
			line[n] = '\0';
			build_log_append(line);
		}
#ifdef IRON_WINDOWS
		build_exit_code = pad_pclose(p, process);
#else
		i32 status      = pclose(p);
		build_exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
	}
	build_finished = true;
}

// Looks for ../base/make first, then up the tree for projects placed inside of base
static char *build_find_make(char *dir) {
#ifdef IRON_WINDOWS
	char *make = "make.bat";
#else
	char *make = "make";
#endif
	char *parent = pad_parent_dir(dir);
	while (parent != NULL) {
		char *path = path_join(path_join(parent, "base"), make);
		if (iron_file_exists(path)) {
			return path;
		}
		path = path_join(parent, make);
		if (iron_file_exists(path) && iron_is_directory(path_join(path_join(parent, "tools"), "amake"))) {
			return path;
		}
		parent = pad_parent_dir(parent);
	}
	return NULL;
}

// Folder with the project.c to build
static char *build_dir() {
	if (pad_project_dir == NULL) {
		return NULL;
	}
	char *dir = pad_project_dir;
	if (!iron_file_exists(path_join(dir, "project.c"))) {
		dir = path_join(pad_project_dir, "paint"); // Repository root is opened
		if (!iron_file_exists(path_join(dir, "project.c"))) {
			return NULL;
		}
	}
	return dir;
}

// Deletes the build folder, debug and release builds do not mix
static void build_clean() {
	char *dir = build_dir();
	if (dir == NULL) {
		return;
	}
	if (build_running) { // Folder is in use, clean with the next build
		build_clean_next = true;
		return;
	}
	char *path = path_join(dir, "build");
	if (!iron_is_directory(path)) {
		return;
	}
#ifdef IRON_WINDOWS
	iron_sys_command(string_tmp("rmdir /s /q \"%s\"", path));
#else
	iron_sys_command(string_tmp("rm -rf \"%s\"", path));
#endif
	browser_refresh();
}

// Debug builds compile faster and are the default, release builds embed the data, clean deletes the build folder first
void build_run(bool run, bool clean) {
	if (build_running) {
		return;
	}
	char *dir = build_dir();
	if (dir == NULL) {
		return;
	}
	char *make = build_find_make(dir);
	if (make == NULL) {
		return;
	}

	editor_save_all();
	clean            = clean || build_clean_next;
	build_clean_next = false;
	char *debug      = build_config == 0 ? " --debug" : " --embed";
#ifdef IRON_WINDOWS
	build_exec(string("cd /d \"%s\" && %s\"%s\" %s%s 2>&1", dir, clean ? "(if exist build rmdir /s /q build) && " : "", make, run ? "--run" : "--compile",
	                  debug),
	           false);
#else
	build_exec(string("cd \"%s\" && %s\"%s\" %s%s 2>&1", dir, clean ? "rm -rf build && " : "", make, run ? "--run" : "--compile", debug), false);
#endif
}

// Runs the command in a thread, its output goes to the output panel
void build_exec(char *cmd, bool show) {
	if (build_running) {
		return;
	}
	build_cmd       = cmd;
	build_log_len   = 0;
	build_log[0]    = '\0';
	build_log_dirty = true;
	if (show) {
		build_show  = true;
		search_show = false;
	}
	build_follow   = true;
	build_finished = false;
	build_running  = true;
	iron_thread_init(&build_thread, build_thread_func, NULL);
}

void build_update() {
	if (build_finished) {
		iron_thread_wait_and_destroy(&build_thread);
		build_finished = false;
		build_running  = false;
		if (build_exit_code != 0 && !build_show) { // Failed
			build_show  = true;
			search_show = false;
		}
		browser_refresh(); // Build may have created files
		if (git_show) {
			git_refresh();
		}
	}
	if (build_log_dirty && build_hwnd != NULL) {
		build_hwnd->redraws = 2;
	}
}

bool terminal_idle_wake();

// Keeps the frames coming while the build prints, an opened file changes on disk or the terminal opens or closes
bool build_idle_wake() {
	return build_finished || build_log_dirty || editor_changed_on_disk() || terminal_idle_wake();
}

void build_init() {
	build_hwnd = ui_window_create();
	iron_mutex_init(&build_mutex);
}

// Reads the next row of the log, lines are wrapped to cols
static i32 build_next_row(i32 pos, i32 end, i32 cols, char *row) {
	i32 n = 0;
	while (pos < end && build_log_view[pos] != '\n' && n < cols) {
		char c = build_log_view[pos++];
		if (c != '\r') {
			row[n++] = c == '\t' ? ' ' : c;
		}
	}
	if (pos < end && build_log_view[pos] == '\n') {
		pos++;
	}
	row[n] = '\0';
	return pos;
}

static bool build_line_contains(i32 pos, i32 end, char *word) {
	i32 len = string_length(word);
	for (; pos + len <= end && build_log_view[pos] != '\n'; ++pos) {
		i32 i = 0;
		while (i < len && tolower(build_log_view[pos + i]) == word[i]) {
			i++;
		}
		if (i == len) {
			return true;
		}
	}
	return false;
}

// With the filter on, moves pos at a line start to the next line containing "error"
static i32 build_filter_skip(i32 pos, i32 end) {
	while (build_filter && pos < end && !build_line_contains(pos, end, "error")) {
		while (pos < end && build_log_view[pos] != '\n') {
			pos++;
		}
		pos++;
	}
	return pos;
}

void build_render(i32 x, i32 y, i32 w, i32 h) {
	if (!ui_window(build_hwnd, x, y, w, h, false)) {
		return;
	}

	pad_fade_begin();
	static f32_array_t *row_ratios = NULL;
	if (row_ratios == NULL) {
		row_ratios = f32_array_create_from_raw((f32[]){0.5, 0.25, 0.25}, 3);
	}
	static string_array_t *configs = NULL;
	if (configs == NULL) {
		configs            = string_array_create(2);
		configs->buffer[0] = "Debug";
		configs->buffer[1] = "Release";
	}
	ui_begin_sticky();
	ui_row(row_ratios);
	ui_text("", UI_ALIGN_LEFT, 0x00000000);
	static i32 last_config = 0;
	ui_combo(&build_config, configs, "", false, UI_ALIGN_LEFT, false);
	if (build_config != last_config) {
		last_config = build_config;
		build_clean();
	}
	i32 _BUTTON_COL = g_theme->BUTTON_COL;
	if (build_filter) {
		g_theme->BUTTON_COL = g_theme->HIGHLIGHT_COL;
	}
	bool filter_clicked = ui_button("Filter", UI_ALIGN_CENTER, "");
	g_theme->BUTTON_COL = _BUTTON_COL;
	if (filter_clicked) {
		build_filter = !build_filter;
		build_follow = true;
	}
	ui_end_sticky();

	iron_mutex_lock(&build_mutex);
	memcpy(build_log_view, build_log, build_log_len + 1);
	i32 end         = build_log_len;
	build_log_dirty = false;
	iron_mutex_unlock(&build_mutex);

	draw_font_t *_font      = g_ui->ops->font;
	i32          _font_size = g_ui->font_size;
	i32          _TEXT_COL  = g_theme->TEXT_COL;
	ui_set_font(g_ui, g_font_mono);
	g_ui->font_size = math_floor(13 * UI_SCALE());

	char row[512];
	i32  cols = (i32)((g_ui->_w - UI_TEXT_OFFSET() * 2) / draw_string_width(g_font_mono, g_ui->font_size, "a"));
	cols      = cols < 8 ? 8 : (cols > (i32)sizeof(row) - 1 ? (i32)sizeof(row) - 1 : cols);
	i32 rows  = 0;
	for (i32 pos = 0; pos < end;) {
		if (pos == 0 || build_log_view[pos - 1] == '\n') {
			pos = build_filter_skip(pos, end);
		}
		if (pos < end) {
			pos = build_next_row(pos, end, cols, row);
			rows++;
		}
	}

	// Follow the output unless scrolled up
	f32 header_h = UI_ELEMENT_H() + UI_ELEMENT_OFFSET();
	f32 bottom   = h - header_h - (rows + 1) * UI_ELEMENT_H();
	if (bottom > 0) {
		bottom = 0;
	}
	bool hover     = ui_input_in_rect(x, y, w, h);
	bool dragging  = g_ui->scroll_handle == build_hwnd;
	bool at_bottom = build_hwnd->scroll_offset <= build_bottom + UI_ELEMENT_H();
	if ((hover && g_ui->input_wheel_delta < 0) || (dragging && !at_bottom)) {
		build_follow = false;
	}
	else if (at_bottom && ((hover && g_ui->input_wheel_delta > 0) || dragging)) {
		build_follow = true;
	}
	if (build_follow && build_hwnd->scroll_offset != bottom) {
		g_ui->_y += bottom - build_hwnd->scroll_offset;
		build_hwnd->scroll_offset = bottom;
	}
	build_bottom = bottom;

	bool line_start = true;
	for (i32 pos = 0; pos < end;) {
		if (line_start) {
			pos = build_filter_skip(pos, end);
			if (pos >= end) {
				break;
			}
		}
		if (line_start) { // Highlight errors and warnings
			bool error        = build_line_contains(pos, end, "error") || build_line_contains(pos, end, "failed");
			g_theme->TEXT_COL = error ? 0xffff5555 : (build_line_contains(pos, end, "warning") ? 0xffe5c07b : _TEXT_COL);
		}
		pos        = build_next_row(pos, end, cols, row);
		line_start = pos > 0 && build_log_view[pos - 1] == '\n';
		ui_text(row[0] != '\0' ? row : " ", UI_ALIGN_LEFT, 0x00000000);
		g_ui->_y -= UI_ELEMENT_OFFSET();
	}
	g_ui->_y += UI_ELEMENT_H(); // Bottom padding

	g_theme->TEXT_COL = _TEXT_COL;
	ui_set_font(g_ui, _font);
	g_ui->font_size = _font_size;
	pad_fade_end();
}
