
#include <iron.h>

ui_t       *ui_create(ui_options_t *ops);
ui_theme_t *ui_theme_create();
void        ui_set_font(ui_t *ui, draw_font_t *font);

ui_t        *g_ui;
ui_theme_t  *g_theme;
draw_font_t *g_font;
draw_font_t *g_font_mono;

char *pad_project_dir = NULL;
char *pad_dropped     = NULL;
#define PAD_UI_SCALE 1.3

f32   pad_browser_w     = 220; // Panel sizes before scaling
f32   pad_output_w      = 420;
i32   pad_resizing      = 0; // 1 - browser, 2 - output
bool  pad_resize_hover  = false;
bool  pad_browser_show  = true;
bool  search_show       = false;
i32   pad_window_w      = 0; // Last window rect
i32   pad_window_h      = 0;
i32   pad_window_x      = -1;
i32   pad_window_y      = -1;
i32   pad_window_frames = 0; // Frames since the window rect changed
char *pad_config_dir    = NULL;
char *pad_config_file   = NULL;
char *pad_config_notes  = ""; // Kept as is when the config is saved

char *pad_file_name(char *path) {
	return path + string_last_index_of(path, PATH_SEP) + 1;
}

char *pad_parent_dir(char *dir) {
	i32 last = string_last_index_of(dir, PATH_SEP);
	return last > 0 ? substring(dir, 0, last) : NULL;
}

void pad_save_config();
void pad_saved(char *path);

#ifdef IRON_WINDOWS
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>

FILE *pad_popen(char *cmd, HANDLE *process) {
	SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
	HANDLE              read_pipe;
	HANDLE              write_pipe;
	if (!CreatePipe(&read_pipe, &write_pipe, &sa, 0)) {
		return NULL;
	}
	SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);
	HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);

	wchar_t comspec[MAX_PATH];
	GetEnvironmentVariableW(L"ComSpec", comspec, MAX_PATH);
	i32      wlen = MultiByteToWideChar(CP_UTF8, 0, cmd, -1, NULL, 0);
	wchar_t *wcmd = malloc(sizeof(wchar_t) * wlen);
	MultiByteToWideChar(CP_UTF8, 0, cmd, -1, wcmd, wlen);
	i32      cmdline_len = wlen + MAX_PATH + 16;
	wchar_t *cmdline     = malloc(sizeof(wchar_t) * cmdline_len);
	swprintf(cmdline, cmdline_len, L"\"%s\" /s /c \"%s\"", comspec, wcmd);

	STARTUPINFOW        si = {0};
	PROCESS_INFORMATION pi = {0};
	si.cb                  = sizeof(si);
	si.dwFlags             = STARTF_USESTDHANDLES;
	si.hStdInput           = nul;
	si.hStdOutput          = write_pipe;
	si.hStdError           = write_pipe;
	BOOL success           = CreateProcessW(NULL, cmdline, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
	free(wcmd);
	free(cmdline);
	CloseHandle(write_pipe);
	CloseHandle(nul);
	if (!success) {
		CloseHandle(read_pipe);
		return NULL;
	}
	CloseHandle(pi.hThread);
	*process = pi.hProcess;
	return _fdopen(_open_osfhandle((intptr_t)read_pipe, _O_RDONLY | _O_TEXT), "r");
}

i32 pad_pclose(FILE *p, HANDLE process) {
	fclose(p);
	WaitForSingleObject(process, INFINITE);
	DWORD exit_code = 0;
	GetExitCodeProcess(process, &exit_code);
	CloseHandle(process);
	return (i32)exit_code;
}
#endif

// Side panels use dimmer text than the editor
static i32 pad_text_col;
static i32 pad_label_col;

void pad_fade_begin() {
	pad_text_col       = g_theme->TEXT_COL;
	pad_label_col      = g_theme->LABEL_COL;
	g_theme->TEXT_COL  = 0xffb4b4b4;
	g_theme->LABEL_COL = 0xffa0a0a0;
}

// List row, dark gray on hover, the selected row keeps its highlight
bool pad_row(char *text, bool selected) {
	i32 _HIGHLIGHT_COL = g_theme->HIGHLIGHT_COL;
	if (selected) {
		draw_set_color(g_theme->HIGHLIGHT_COL);
		ui_draw_rect(true, g_theme->SHADOWS, g_ui->_x + g_ui->button_offset_y, g_ui->_y + g_ui->button_offset_y, g_ui->_w - g_ui->button_offset_y * 2,
		             UI_BUTTON_H()); // Same as the hover rect
	}
	else {
		g_theme->HIGHLIGHT_COL = g_theme->HOVER_COL;
	}
	bool clicked           = ui_button(text, UI_ALIGN_LEFT, "");
	g_theme->HIGHLIGHT_COL = _HIGHLIGHT_COL;
	return clicked;
}

void pad_fade_end() {
	g_theme->TEXT_COL  = pad_text_col;
	g_theme->LABEL_COL = pad_label_col;
}

#include "browser.c"
#include "build.c"
#include "editor.c"
#include "git.c"
#include "picker.c"
#include "search.c"
#include "terminal.c"

// Shown panels as bits - browser, git, output, search
static i32 pad_panels() {
	return (pad_browser_show ? 1 : 0) | (git_show ? 2 : 0) | (build_show ? 4 : 0) | (search_show ? 8 : 0);
}

#define PAD_CONFIG_LINES 10

// Remembers the last opened project and file, panel sizes, the window rect and the shown panels
// Anything below these lines is free text
static char *pad_config_path() {
	return string("%sconfig.txt", iron_internal_save_path());
}

void pad_save_config() {
	// No project opened, keep the stored one unless it was closed
	char *dir  = pad_project_dir != NULL ? pad_project_dir : (pad_config_dir != NULL ? pad_config_dir : "");
	char *file = editor_current != NULL ? editor_current->path : "";
	if (string_equals(file, pad_config_path())) { // Config opened in the editor, keep the stored file
		file = pad_config_file != NULL ? pad_config_file : "";
	}
	pad_config_file = file;
	char *s = string("%s\n%s\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%s", dir, file, (i32)pad_browser_w, (i32)pad_output_w, pad_window_w, pad_window_h, pad_window_x,
	                 pad_window_y, editor_minimap_show ? 1 : 0, pad_panels(), pad_config_notes);
	buffer_t bytes = {.buffer = (u8 *)s, .length = string_length(s)};
	iron_file_save_bytes(pad_config_path(), &bytes, 0);
}

// Runs before the window is created, and when the config is edited
static void pad_load_config() {
	buffer_t *blob = iron_load_blob(pad_config_path());
	if (blob == NULL) {
		return;
	}
	editor_minimap_show = true;
	char *s             = string_alloc(blob->length + 1);
	memcpy(s, blob->buffer, blob->length);
	char *notes = s; // Free text after the settings lines
	for (i32 i = 0; i < PAD_CONFIG_LINES && notes != NULL; i++) {
		notes = strchr(notes, '\n');
		if (notes != NULL) {
			notes++;
		}
	}
	pad_config_notes      = notes != NULL ? string_copy(notes) : "";
	string_array_t *lines = string_split(s, "\n");
	pad_config_dir        = lines->buffer[0];
	pad_config_file       = lines->length > 1 ? lines->buffer[1] : "";
	if (lines->length > 3 && parse_int(lines->buffer[2]) > 0 && parse_int(lines->buffer[3]) > 0) {
		pad_browser_w = parse_int(lines->buffer[2]);
		pad_output_w  = parse_int(lines->buffer[3]);
	}
	if (lines->length > 7 && parse_int(lines->buffer[4]) >= 200 && parse_int(lines->buffer[5]) >= 200) {
		pad_window_w = parse_int(lines->buffer[4]);
		pad_window_h = parse_int(lines->buffer[5]);
		pad_window_x = parse_int(lines->buffer[6]);
		pad_window_y = parse_int(lines->buffer[7]);
	}
	if (lines->length > 8 && string_equals(lines->buffer[8], "0")) {
		editor_minimap_show = false;
	}
	if (lines->length > 9 && lines->buffer[9][0] != '\0') {
		i32 panels       = parse_int(lines->buffer[9]);
		pad_browser_show = (panels & 1) != 0;
		git_show         = (panels & 2) != 0 && !pad_browser_show; // One left panel at a time
		build_show       = (panels & 4) != 0;
		search_show      = (panels & 8) != 0 && !build_show; // One right panel at a time
	}
	if (pad_config_dir[0] == '\0') {
		pad_config_dir = NULL;
	}
}

// A file was written by the editor
void pad_saved(char *path) {
	if (string_equals(path, pad_config_path())) { // Apply the edited config
		pad_load_config();
	}
	if (git_show) {
		git_refresh();
	}
}

void pad_open_project(char *dir);

static void pad_restore_project() {
	if (pad_config_dir == NULL || !iron_is_directory(pad_config_dir)) {
		return;
	}
	char *file = pad_config_file;
	pad_open_project(pad_config_dir);
	if (file[0] != '\0' && iron_file_exists(file)) {
		editor_open(file);
	}
}

// Stores the window rect once the window stops moving, and the panels once they are toggled
static void pad_update_window_rect() {
	static i32 panels = -1;
	if (panels != pad_panels()) {
		if (panels != -1) {
			pad_save_config();
		}
		panels = pad_panels();
	}
	i32 w = iron_window_width();
	i32 h = iron_window_height();
	i32 x = iron_window_x();
	i32 y = iron_window_y();
	// Keep the windowed rect while in fullscreen or split with the terminal
	if (w == 0 || h == 0 || iron_window_get_mode() != IRON_WINDOW_MODE_WINDOW || terminal_show) {
		return;
	}
	if (w != pad_window_w || h != pad_window_h || x != pad_window_x || y != pad_window_y) {
		pad_window_w      = w;
		pad_window_h      = h;
		pad_window_x      = x;
		pad_window_y      = y;
		pad_window_frames = 1;
	}
	else if (pad_window_frames > 0 && ++pad_window_frames > 30) {
		pad_window_frames = 0;
		pad_save_config();
	}
}

void pad_open_project(char *dir) {
	dir     = string_copy(dir);
	i32 len = string_length(dir);
	if (len > 1 && ends_with(dir, PATH_SEP)) {
		dir[len - 1] = '\0';
	}
	pad_project_dir = dir;
	editor_close_all();
	browser_open(dir);
	if (git_show) {
		git_refresh();
	}
	pad_save_config();
}

static void pad_close_project() {
	if (pad_project_dir == NULL) {
		return;
	}
	pad_project_dir = NULL;
	pad_config_dir  = NULL; // Not restored on the next start
	pad_config_file = "";
	editor_close_all();
	browser_root   = NULL;
	search_results = any_array_create(0);
	git_refresh();
	picker_close();
	pad_save_config();
}

void pad_drop_files(char *path) {
	pad_dropped = string_copy(path); // Handled in the next frame
}

// Unsaved changes are written when the app quits
void pad_shutdown() {
	editor_save_all();
	terminal_shutdown();
}

void pad_foreground() {
	browser_refresh();
	if (git_show) {
		git_refresh();
	}
	editor_reload_changed();
}

static void pad_update_input() {
	if (pad_dropped != NULL) {
		if (iron_is_directory(pad_dropped)) {
			pad_open_project(pad_dropped);
		}
		else {
			if (pad_project_dir == NULL && pad_parent_dir(pad_dropped) != NULL) {
				pad_open_project(pad_parent_dir(pad_dropped));
			}
			editor_open(pad_dropped);
		}
		pad_dropped = NULL;
	}

	if (picker_show) {
		picker_update_input();
		return;
	}
	if (browser_menu_show) {
		browser_menu_update_input();
		return;
	}
	if (!g_ui->is_key_pressed) {
		return;
	}
	if (g_ui->is_ctrl_down && g_ui->is_shift_down && g_ui->key_code == KEY_CODE_F) {
		search_toggle();
		if (search_show) {
			build_show = false; // One right panel at a time
		}
		g_ui->is_key_pressed = false; // Consume so the editor ignores ctrl+f
		g_ui->key_code       = 0;
	}
	else if (g_ui->is_ctrl_down && g_ui->key_code == KEY_CODE_P) {
		picker_open();
		g_ui->is_key_pressed = false;
		g_ui->key_code       = 0;
	}
	else if (g_ui->key_code == KEY_CODE_F5) {
		build_run(true, g_ui->is_shift_down); // Shift for a clean build
	}
	else if (g_ui->is_ctrl_down && g_ui->key_code == KEY_CODE_B) {
		build_run(false, false);
		g_ui->is_key_pressed = false;
		g_ui->key_code       = 0;
	}
	else if (g_ui->key_code == KEY_CODE_F11) {
		iron_window_change_mode(iron_window_get_mode() == IRON_WINDOW_MODE_WINDOW ? IRON_WINDOW_MODE_FULLSCREEN : IRON_WINDOW_MODE_WINDOW);
	}
	else if (g_ui->is_ctrl_down && (g_ui->key_code == KEY_CODE_J || g_ui->key_code == KEY_CODE_E)) {
		if (g_ui->key_code == KEY_CODE_J) {
			build_show = !build_show;
			if (search_show) {
				search_toggle();
			}
		}
		else {
			pad_browser_show = !pad_browser_show;
			git_show         = false; // One left panel at a time
		}
		g_ui->is_key_pressed = false;
		g_ui->key_code       = 0;
	}
	else if (g_ui->is_ctrl_down && g_ui->key_code == KEY_CODE_T) {
		terminal_toggle();
		g_ui->is_key_pressed = false;
		g_ui->key_code       = 0;
	}
	else if (g_ui->is_ctrl_down && g_ui->key_code == KEY_CODE_K) {
		editor_open(pad_config_path());
		g_ui->is_key_pressed = false;
		g_ui->key_code       = 0;
	}
	else if (g_ui->is_ctrl_down && g_ui->key_code == KEY_CODE_W) {
		if (editor_current == NULL) {
			pad_close_project();
		}
		else {
			editor_close();
		}
		g_ui->is_key_pressed = false;
		g_ui->key_code       = 0;
	}
	else if (g_ui->is_ctrl_down && g_ui->key_code == KEY_CODE_TAB) {
		editor_switch_last();
		g_ui->is_key_pressed = false; // Consume so the editor does not indent
		g_ui->key_code       = 0;
		g_ui->is_tab_down    = false;
	}
	else if (g_ui->is_ctrl_down && g_ui->key_code == KEY_CODE_M) {
		editor_minimap_show  = !editor_minimap_show;
		g_ui->is_key_pressed = false;
		g_ui->key_code       = 0;
		pad_save_config();
	}
	else if (g_ui->is_ctrl_down && g_ui->key_code == KEY_CODE_G) {
		git_toggle();
		if (git_show) {
			pad_browser_show = false;
		}
		g_ui->is_key_pressed = false;
		g_ui->key_code       = 0;
	}
	else if (g_ui->is_ctrl_down && g_ui->key_code == KEY_CODE_S) {
		editor_save();
		pad_saved(editor_current != NULL ? editor_current->path : "");
		g_ui->is_key_pressed = false; // Consume so the editor ignores ctrl+s
		g_ui->key_code       = 0;
	}
}

// Window title shows the current file and whether it has unsaved changes, then the project
static void pad_update_title() {
	static char last[512] = "";
	char       *title     = "ArmorPad";
	if (pad_project_dir != NULL) {
		title = string_tmp("%s - ArmorPad", pad_file_name(pad_project_dir));
	}
	if (editor_current != NULL) {
		title = string_tmp("%s%s - %s", pad_file_name(editor_current->path), editor_is_modified(editor_current) ? " *" : "", title);
	}
	if (!string_equals(title, last) && string_length(title) < (i32)sizeof(last)) {
		strcpy(last, title);
		sys_title_set(title);
	}
}

// Drag the panel dividers to resize the left panel and the output panel
static void pad_update_resize(i32 w, i32 main_h, i32 *browser_w, i32 *output_w) {
	f32  scale  = UI_SCALE();
	i32  min_w  = math_floor(100 * scale);
	i32  grab   = math_floor(4 * scale);
	i32  space  = w - math_floor(200 * scale);  // Keep some room for the editor
	bool left   = pad_browser_show || git_show; // Browser and git share the left panel
	bool right  = build_show || search_show;    // Output and search share the right panel
	bool over_b = !picker_show && !browser_menu_show && left && g_ui->input_y < main_h && math_abs(g_ui->input_x - *browser_w) <= grab;
	bool over_o = !picker_show && !browser_menu_show && right && g_ui->input_y < main_h && math_abs(g_ui->input_x - (w - *output_w)) <= grab;

	if (g_ui->input_started && pad_resizing == 0) {
		pad_resizing = over_b ? 1 : (over_o ? 2 : 0);
	}
	if (pad_resizing == 1) {
		pad_browser_w = g_ui->input_x / scale;
	}
	else if (pad_resizing == 2) {
		pad_output_w = (w - g_ui->input_x) / scale;
	}
	bool hover = pad_resizing != 0 || over_b || over_o;
	if (hover != pad_resize_hover) {
		pad_resize_hover = hover;
		iron_mouse_set_cursor(hover ? IRON_CURSOR_SIZEWE : IRON_CURSOR_ARROW);
	}

	// Fit the panels into the window
	*browser_w = math_floor(pad_browser_w * scale);
	*output_w  = right ? math_floor(pad_output_w * scale) : 0;
	if (*browser_w > space - (right ? min_w : 0)) {
		*browser_w = space - (right ? min_w : 0);
	}
	if (*browser_w < min_w) {
		*browser_w = min_w;
	}
	if (!left) {
		*browser_w = 0;
	}
	if (right) {
		if (*output_w > space - *browser_w) {
			*output_w = space - *browser_w;
		}
		if (*output_w < min_w) {
			*output_w = min_w;
		}
	}
	if (pad_resizing != 0) { // Do not store sizes which do not fit
		if (left) {
			pad_browser_w = *browser_w / scale;
		}
		if (right) {
			pad_output_w = *output_w / scale;
		}
	}
	if (pad_resizing != 0 && !g_ui->input_down) {
		pad_resizing = 0;
		pad_save_config();
	}
}

void pad_render(void *_) {
	build_update();
	terminal_update();
	pad_update_input();
	pad_update_window_rect();
	pad_update_title();
	static i32 reload_frames = 0;
	if (++reload_frames % 30 == 0) {
		editor_reload_changed();
	}

	i32 w         = sys_w();
	i32 h         = sys_h();
	i32 main_h    = h;
	i32 browser_w = math_floor(pad_browser_w * UI_SCALE());
	i32 output_w  = (build_show || search_show) ? math_floor(pad_output_w * UI_SCALE()) : 0;
	pad_update_resize(w, main_h, &browser_w, &output_w);

	draw_begin(NULL, true, g_theme->BUTTON_COL); // Panel dividers
	draw_end();

	ui_begin(g_ui);
	g_ui->input_enabled = !picker_show && !browser_menu_show && !pad_resize_hover; // Popup or panel resizing takes the input
	i32 editor_x        = 0;
	if (git_show) {
		git_render(0, 0, browser_w, main_h);
		editor_x = browser_w + 1;
	}
	else if (pad_browser_show) {
		browser_render(0, 0, browser_w, main_h);
		editor_x = browser_w + 1;
	}
	editor_render(editor_x, 0, w - editor_x - output_w, main_h);
	if (search_show) {
		search_render(w - output_w + 1, 0, output_w - 1, main_h);
	}
	else if (build_show) {
		build_render(w - output_w + 1, 0, output_w - 1, main_h);
	}
	g_ui->input_enabled = true;
	if (picker_show) {
		picker_render();
	}
	if (browser_menu_show) {
		browser_menu_render();
	}
	ui_end();
}

void _kickstart() {
	iron_window_options_t *ops =
	    ALLOC_INIT(iron_window_options_t, {.title      = "ArmorPad",
	                                       .width      = 1280,
	                                       .height     = 800,
	                                       .x          = -1,
	                                       .y          = -1,
	                                       .features   = IRON_WINDOW_FEATURES_RESIZABLE | IRON_WINDOW_FEATURES_MINIMIZABLE | IRON_WINDOW_FEATURES_MAXIMIZABLE,
	                                       .mode       = IRON_WINDOW_MODE_WINDOW,
	                                       .frequency  = sys_display_frequency(),
	                                       .vsync      = true,
	                                       .depth_bits = 0});
	f32 scale = 1.0;
#ifdef IRON_MACOS
	ops->width *= 2;
	ops->height *= 2;
	scale = 2.0;
#endif
	iron_set_app_name("ArmorPad"); // Used to locate the config folder
	pad_load_config();
	if (pad_window_w > 0) {
		ops->width  = pad_window_w;
		ops->height = pad_window_h;
		if (pad_window_x != 0 || pad_window_y != 0) { // Backends without window position report zero
			ops->x = pad_window_x;
			ops->y = pad_window_y;
		}
	}
	sys_start(ops);
	if (sys_display_width() >= 2560 && sys_display_height() >= 1600) {
		scale = 2.0;
	}

	// Full black theme
	g_theme                = ui_theme_create();
	g_theme->WINDOW_BG_COL = 0xff000000;
	g_theme->SEPARATOR_COL = 0xff000000;
	g_theme->BUTTON_COL    = 0xff141414;
	g_theme->HOVER_COL     = 0xff232323;
	g_theme->PRESSED_COL   = 0xff060606;
	g_theme->VIEWPORT_COL  = 0xff000000;
	g_font                 = data_get_font("font.ttf");
	g_font_mono            = data_get_font("font_mono.ttf");
	draw_font_init(g_font);
	draw_font_init(g_font_mono);
	g_ui                = ui_create(ALLOC_INIT(ui_options_t, {.theme = g_theme, .font = g_font, .scale_factor = scale * PAD_UI_SCALE}));
	g_ui->always_redraw = true;

	editor_init();
	browser_init();
	git_init();
	build_init();
	picker_init();
	search_init();
	iron_idle_wake = build_idle_wake;

	sys_notify_on_update(pad_render, NULL);
	sys_notify_on_drop_files(pad_drop_files);
	sys_notify_on_app_state(pad_foreground, NULL, pad_shutdown);

	// armorpad <folder>
	if (iron_get_arg_count() > 1 && iron_is_directory(iron_get_arg(1))) {
		pad_open_project(iron_get_arg(1));
	}
	else {
		pad_restore_project();
	}

	iron_start();
	pad_update_window_rect(); // Store the final window rect, kept as is when the window is already destroyed
	pad_save_config();
}

////

any_map_t *ui_children;
any_map_t *ui_nodes_custom_buttons;
i32        pipes_offset;
char      *strings_check_internet_connection() {
    return "";
}
void  console_error(char *s) {}
void  console_info(char *s) {}
char *tr(char *id) {
	return id;
}
i32 pipes_get_constant_location(char *s) {
	return 0;
}
