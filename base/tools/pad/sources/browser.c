// File browser panel

#ifndef IRON_WINDOWS
#include <unistd.h>
#endif

typedef struct browser_node {
	char        *name;
	char        *path;
	bool         is_dir;
	bool         expanded;
	any_array_t *children;
} browser_node_t;

ui_window_t    *browser_hwnd = NULL;
browser_node_t *browser_root = NULL;

// Right click menu of a file or a folder, or of a file in the git panel
// Empty space of the project panel opens the menu of the project folder
typedef struct git_file git_file_t;

enum {
	BROWSER_MENU_ITEMS,
	BROWSER_MENU_RENAME,
	BROWSER_MENU_NEW_FILE,
	BROWSER_MENU_NEW_FOLDER
}; // Other than items show a name field

ui_window_t    *browser_menu_hwnd    = NULL;
bool            browser_menu_show    = false;
browser_node_t *browser_menu_node    = NULL;
git_file_t     *browser_menu_git     = NULL;
bool            browser_menu_editor  = false; // Opened in the code editor
i32             browser_menu_mode    = BROWSER_MENU_ITEMS;
i32             browser_menu_x       = 0;
i32             browser_menu_y       = 0;
bool            browser_menu_confirm = false; // Delete or revert was pressed once
char           *browser_menu_name    = "";
bool            browser_menu_focus   = false;

// Opened file, used by the panels included before the editor
#define EDITOR_UNDO_MAX 16

typedef struct editor_undo {
	char *text;
	i32   line;
	i32   col;
} editor_undo_t;

typedef struct editor_buffer {
	char          *path;
	char          *text;
	char          *saved; // Contents on disk
	i32            line;
	f32            scroll;
	bool           tabs;      // File is indented with tabs
	any_map_t     *tab_lines; // Lines as loaded, by their text with tabs expanded
	bool           crlf;
	i64            time;  // Modification time of the file when loaded or saved
	bool           diff;  // Read-only git diff of the file at path
	i32            caret; // Line of the caret in the diff, -1 when not placed
	gpu_texture_t *image; // Picture shown instead of the text
	editor_undo_t  undo[EDITOR_UNDO_MAX];
	editor_undo_t  redo[EDITOR_UNDO_MAX];
	i32            undo_count;
	i32            redo_count;
	editor_undo_t  undo_last; // Text and cursor in the previous frame
	f64            undo_time; // Time of the last edit
} editor_buffer_t;

void             build_exec(char *cmd, bool show);
void             git_refresh();
void             git_revert(git_file_t *f);
bool             git_show;
ui_window_t     *git_hwnd;
any_map_t       *editor_buffers;
any_array_t     *editor_buffer_list;
editor_buffer_t *editor_current;
void             editor_open(char *path);
void             editor_save_all();
bool             editor_changed_on_disk();
static void      editor_set_current(editor_buffer_t *b);
void             editor_free_buffer(editor_buffer_t *b);

static browser_node_t *browser_find(any_array_t *nodes, char *name) {
	if (nodes == NULL) {
		return NULL;
	}
	for (i32 i = 0; i < (i32)nodes->length; ++i) {
		browser_node_t *n = nodes->buffer[i];
		if (string_equals(n->name, name)) {
			return n;
		}
	}
	return NULL;
}

// Reads the folder contents, expanded sub-folders stay expanded
static void browser_load(browser_node_t *node) {
	any_array_t *old   = node->children;
	any_array_t *files = file_read_directory(node->path);
	node->children     = any_array_create(0);
	for (i32 i = 0; i < (i32)files->length; ++i) {
		char *name = files->buffer[i];
		if (name[0] == '\0') {
			continue;
		}
		browser_node_t *n = ALLOC_INIT(browser_node_t, {0});
		n->name           = string_copy(name);
		n->path           = path_join(node->path, name);
		n->is_dir         = iron_is_directory(n->path);
		browser_node_t *o = browser_find(old, name);
		if (n->is_dir && o != NULL && o->is_dir && o->expanded) {
			n->expanded = true;
			n->children = o->children;
			browser_load(n);
		}
		any_array_push(node->children, n);
	}
}

void browser_refresh() {
	if (browser_root != NULL) {
		browser_load(browser_root);
		browser_hwnd->redraws = 2;
	}
}

void browser_open(char *dir) {
	browser_root                = ALLOC_INIT(browser_node_t, {0});
	browser_root->name          = string_copy(pad_file_name(dir));
	browser_root->path          = string_copy(dir);
	browser_root->is_dir        = true;
	browser_root->expanded      = true;
	browser_hwnd->scroll_offset = 0;
	browser_refresh();
}

void browser_init() {
	browser_hwnd      = ui_window_create();
	browser_menu_hwnd = ui_window_create();
}

// Number of rows the node takes in the panel
static i32 browser_rows(browser_node_t *node) {
	i32 rows = 1;
	for (i32 i = 0; node->is_dir && node->expanded && i < (i32)node->children->length; ++i) {
		rows += browser_rows(node->children->buffer[i]);
	}
	return rows;
}

static bool browser_is_inside(char *path, char *dir) {
	i32 len = string_length(dir);
	return string_equals(path, dir) || (starts_with(path, dir) && starts_with(path + len, PATH_SEP));
}

// Right button released over the row which is about to be drawn
bool browser_menu_clicked() {
	return g_ui->input_released_r && ui_input_in_rect(g_ui->_window_x, g_ui->_window_y, g_ui->_window_w, g_ui->_window_h) &&
	       g_ui->input_y >= g_ui->_window_y + g_ui->window_header_h && g_ui->input_y >= g_ui->_window_y + g_ui->_y &&
	       g_ui->input_y < g_ui->_window_y + g_ui->_y + UI_ELEMENT_H();
}

static void browser_menu_open(browser_node_t *node) {
	browser_menu_node     = node;
	browser_menu_git      = NULL;
	browser_menu_editor   = false;
	browser_menu_mode     = BROWSER_MENU_ITEMS;
	browser_menu_x        = g_ui->input_x;
	browser_menu_y        = g_ui->input_y;
	browser_menu_confirm  = false;
	browser_menu_show     = true;
	browser_hwnd->redraws = 2;
}

void browser_menu_open_git(git_file_t *f) {
	browser_menu_open(NULL);
	browser_menu_git  = f;
	git_hwnd->redraws = 2;
}

// Menu of the code editor, the file has to be in the project to be revealed
void browser_menu_open_editor() {
	if (browser_root == NULL || !browser_is_inside(editor_current->path, browser_root->path)) {
		return;
	}
	browser_menu_open(NULL);
	browser_menu_editor = true;
}

void browser_menu_close() {
	if (!browser_menu_show) {
		return;
	}
	if (g_ui->text_selected_id != 0) {
		ui_deselect_text(g_ui);
		g_ui->submit_text_id = 0;
	}
	browser_menu_show     = false;
	browser_menu_node     = NULL;
	browser_menu_git      = NULL;
	browser_menu_editor   = false;
	browser_hwnd->redraws = 2;
	git_hwnd->redraws     = 2;
}

// Opened files follow the renamed file or folder
static void browser_rename(browser_node_t *node, char *name) {
	char *dir  = pad_parent_dir(node->path);
	char *path = dir != NULL ? path_join(dir, name) : NULL;
	if (path == NULL || name[0] == '\0' || string_index_of(name, "/") >= 0 || string_index_of(name, "\\") >= 0 || string_equals(name, node->name) ||
	    iron_file_exists(path) || iron_is_directory(path)) {
		return;
	}
	if (rename(node->path, path) != 0) {
		return;
	}
	i32 len = string_length(node->path);
	for (i32 i = 0; i < (i32)editor_buffer_list->length; ++i) {
		editor_buffer_t *b = editor_buffer_list->buffer[i];
		if (!browser_is_inside(b->path, node->path)) {
			continue;
		}
		map_delete(editor_buffers, b->path);
		b->path = string("%s%s", path, b->path + len);
		any_map_set(editor_buffers, b->path, b);
	}
	node->name = string_copy(name); // Renamed folder stays expanded
	node->path = path;
	browser_refresh();
	if (git_show) {
		git_refresh();
	}
	pad_save_config();
}

// New file is opened, the folder it is created in gets expanded
static void browser_create(browser_node_t *node, char *name, bool is_dir) {
	char *path = path_join(node->path, name);
	if (name[0] == '\0' || string_index_of(name, "/") >= 0 || string_index_of(name, "\\") >= 0 || iron_file_exists(path) || iron_is_directory(path)) {
		return;
	}
	if (is_dir) {
		iron_create_directory(path);
	}
	else {
		FILE *f = fopen(path, "wb");
		if (f == NULL) {
			return;
		}
		fclose(f);
	}
	node->expanded = true;
	browser_refresh();
	if (git_show) {
		git_refresh();
	}
	if (!is_dir) {
		editor_open(path);
	}
}

// Symbolic links are removed, not the files they point to
static void browser_delete_path(char *path) {
#ifdef IRON_WINDOWS
	iron_sys_command(string_tmp(iron_is_directory(path) ? "rmdir /s /q \"%s\"" : "del /f /q \"%s\"", path));
#else
	struct stat st;
	if (lstat(path, &st) != 0) {
		return;
	}
	if (!S_ISDIR(st.st_mode)) {
		unlink(path);
		return;
	}
	any_array_t *files = file_read_directory(path);
	for (i32 i = 0; i < (i32)files->length; ++i) {
		char *name = files->buffer[i];
		if (name[0] != '\0') {
			browser_delete_path(path_join(path, name));
		}
	}
	rmdir(path);
#endif
}

// Opened files of the deleted file or folder are closed
static void browser_delete(browser_node_t *node) {
	browser_delete_path(node->path);
	for (i32 i = (i32)editor_buffer_list->length - 1; i >= 0; --i) {
		editor_buffer_t *b = editor_buffer_list->buffer[i];
		if (!browser_is_inside(b->path, node->path)) {
			continue;
		}
		if (b == editor_current) {
			editor_set_current(NULL);
		}
		array_splice(editor_buffer_list, i, 1);
		map_delete(editor_buffers, b->path);
		editor_free_buffer(b);
	}
	if (editor_current != NULL && browser_is_inside(editor_current->path, node->path)) { // Diff or picture
		editor_set_current(NULL);
	}
	browser_refresh();
	if (git_show) {
		git_refresh();
	}
	pad_save_config();
}

// Folders and the sources clang-format handles
static bool browser_can_format(browser_node_t *node) {
	return node->is_dir || ends_with(node->path, ".c") || ends_with(node->path, ".h") || ends_with(node->path, ".m");
}

// Runs clang-format on the file or on the sources in the folder and its sub-folders, the progress is shown in the output panel
static void browser_format(browser_node_t *node) {
	editor_save_all(); // Formatted files are reloaded from disk
	if (!node->is_dir) {
		build_exec(string("clang-format -i --verbose \"%s\" 2>&1", node->path), true);
		return;
	}
#ifdef IRON_WINDOWS
	build_exec(string("for /r \"%s\" %%f in (*.c *.h *.m) do @clang-format -i --verbose \"%%f\" 2>&1", node->path), true);
#else
	build_exec(string("find \"%s\" -name .git -prune -o -type f \\( -name '*.c' -o -name '*.h' -o -name '*.m' \\) -exec clang-format -i --verbose {} + 2>&1",
	                  node->path),
	           true);
#endif
}

// Shows the file or the folder in explorer / finder
static void browser_show_in_explorer(char *path) {
#ifdef IRON_WINDOWS
	iron_sys_command(string_tmp(iron_is_directory(path) ? "explorer \"%s\"" : "explorer /select,\"%s\"", path));
#elif defined(IRON_MACOS)
	iron_sys_command(string_tmp(iron_is_directory(path) ? "open \"%s\"" : "open -R \"%s\"", path));
#else
	char *dir = iron_is_directory(path) ? path : pad_parent_dir(path);
	if (dir != NULL) {
		file_start(dir);
	}
#endif
}

// Expands the folders the file is in and scrolls the project panel to it
static void browser_reveal(char *path) {
	browser_node_t *node = browser_root;
	i32             row  = 0;
	while (node != NULL && node->is_dir) {
		browser_node_t *next = NULL;
		for (i32 i = 0; i < (i32)node->children->length && next == NULL; ++i) {
			browser_node_t *n = node->children->buffer[i];
			if (browser_is_inside(path, n->path)) {
				next = n;
			}
			else {
				row += browser_rows(n);
			}
		}
		if (next == NULL || !next->is_dir) {
			break;
		}
		row++;
		if (!next->expanded) {
			next->expanded = true;
			browser_load(next);
		}
		node = next;
	}
	pad_browser_show = true;
	git_show         = false; // One left panel at a time
	f32 row_h        = UI_ELEMENT_H() + UI_ELEMENT_OFFSET();
	f32 y            = row * row_h + browser_hwnd->scroll_offset;
	if (y < 0 || y > sys_h() - row_h * 2) { // Out of view
		f32 scroll                  = sys_h() / 3.0 - row * row_h;
		browser_hwnd->scroll_offset = scroll < 0 ? scroll : 0;
	}
	browser_hwnd->redraws = 2;
	pad_save_config();
}

static void browser_menu_rect(i32 *x, i32 *y, i32 *w, i32 *h) {
	i32 rows = browser_menu_node != NULL && browser_can_format(browser_menu_node) ? 4 : 3; // File
	if (browser_menu_mode != BROWSER_MENU_ITEMS || browser_menu_git != NULL || browser_menu_editor) {
		rows = 1;
	}
	else if (browser_menu_node->is_dir) {
		rows = browser_menu_node == browser_root ? 4 : 6; // Project folder is not renamed or deleted
	}
	*w = math_floor((browser_menu_mode != BROWSER_MENU_ITEMS ? 320 : 160) * UI_SCALE());
	*h = math_floor(rows * (UI_ELEMENT_H() + UI_ELEMENT_OFFSET()) + UI_ELEMENT_OFFSET());
	*x = browser_menu_x;
	*y = browser_menu_y;
	if (*x + *w > sys_w()) { // Keep inside of the window
		*x = sys_w() - *w;
	}
	if (*y + *h > sys_h()) {
		*y = sys_h() - *h;
	}
	if (*x < 0) {
		*x = 0;
	}
	if (*y < 0) {
		*y = 0;
	}
}

// Runs before the panels are drawn so the keys do not reach them
void browser_menu_update_input() {
	i32 x, y, w, h;
	browser_menu_rect(&x, &y, &w, &h);
	if ((g_ui->input_started || g_ui->input_started_r) && !ui_input_in_rect(x, y, w, h)) {
		browser_menu_close();
		return;
	}
	if (!g_ui->is_key_pressed) {
		return;
	}
	if (g_ui->key_code == KEY_CODE_ESCAPE) {
		browser_menu_close();
	}
	else if (g_ui->key_code == KEY_CODE_RETURN && browser_menu_mode != BROWSER_MENU_ITEMS) {
		browser_node_t *node = browser_menu_node;
		i32             mode = browser_menu_mode;
		char           *name = string_copy(g_ui->text_selected_id != 0 ? g_ui->text_selected : browser_menu_name);
		browser_menu_close();
		if (mode == BROWSER_MENU_RENAME) {
			browser_rename(node, name);
		}
		else {
			browser_create(node, name, mode == BROWSER_MENU_NEW_FOLDER);
		}
	}
	else {
		return;
	}
	g_ui->is_key_pressed = false; // Consume so the name field ignores the key
	g_ui->key_code       = 0;
}

#define BROWSER_ICON_COL 0xff6e6e6e
#define BROWSER_ICON_W   18 // Room for the icon before the name, unscaled

enum {
	BROWSER_ICON_FILE,
	BROWSER_ICON_FOLDER,
	BROWSER_ICON_FOLDER_OPEN,
	BROWSER_ICON_RENAME,
	BROWSER_ICON_DELETE,
	BROWSER_ICON_FORMAT,
	BROWSER_ICON_REVERT
};

static void browser_draw_icon(i32 icon, f32 x, f32 y) {
	f32 s = UI_SCALE();
	draw_set_color(BROWSER_ICON_COL);
	if (icon == BROWSER_ICON_FOLDER) {
		y += math_floor((UI_ELEMENT_H() - 10 * s) / 2.0);
		draw_filled_rect(x, y, 5 * s, 2 * s); // Tab
		draw_filled_rect(x, y + 2 * s, 12 * s, 8 * s);
	}
	else if (icon == BROWSER_ICON_FOLDER_OPEN) {
		y += math_floor((UI_ELEMENT_H() - 10 * s) / 2.0);
		draw_filled_rect(x, y, 5 * s, 2 * s); // Tab
		draw_rect(x + 0.5 * s, y + 2.5 * s, 11 * s, 7 * s, s);
		// Front cover leaning to the right
		draw_filled_triangle(x + 3 * s, y + 5 * s, x + 15 * s, y + 5 * s, x + 12 * s, y + 10 * s);
		draw_filled_triangle(x + 3 * s, y + 5 * s, x + 12 * s, y + 10 * s, x, y + 10 * s);
	}
	else if (icon == BROWSER_ICON_RENAME) { // Pencil
		y += math_floor((UI_ELEMENT_H() - 12 * s) / 2.0);
		draw_filled_triangle(x + 8 * s, y + 1 * s, x + 11 * s, y + 4 * s, x + 5 * s, y + 10 * s);
		draw_filled_triangle(x + 8 * s, y + 1 * s, x + 5 * s, y + 10 * s, x + 2 * s, y + 7 * s);
		draw_filled_triangle(x + 2 * s, y + 8 * s, x + 4 * s, y + 10 * s, x + 1 * s, y + 11 * s); // Tip
	}
	else if (icon == BROWSER_ICON_DELETE) { // Trash can
		y += math_floor((UI_ELEMENT_H() - 12 * s) / 2.0);
		draw_filled_rect(x + 4 * s, y, 4 * s, s);              // Handle
		draw_filled_rect(x + 1 * s, y + 1 * s, 10 * s, 2 * s); // Lid
		draw_rect(x + 2.5 * s, y + 4.5 * s, 7 * s, 7 * s, s);
		draw_filled_rect(x + 5.5 * s, y + 6 * s, s, 4 * s);
	}
	else if (icon == BROWSER_ICON_FORMAT) { // Indented lines
		y += math_floor((UI_ELEMENT_H() - 12 * s) / 2.0);
		draw_filled_rect(x, y + 1 * s, 12 * s, s);
		draw_filled_rect(x + 4 * s, y + 4 * s, 8 * s, s);
		draw_filled_rect(x + 4 * s, y + 7 * s, 8 * s, s);
		draw_filled_rect(x, y + 10 * s, 12 * s, s);
	}
	else if (icon == BROWSER_ICON_REVERT) { // Arrow turning back to the left
		y += math_floor((UI_ELEMENT_H() - 12 * s) / 2.0);
		draw_filled_triangle(x, y + 4 * s, x + 4 * s, y, x + 4 * s, y + 8 * s);
		draw_filled_rect(x + 4 * s, y + 3 * s, 7 * s, 2 * s);
		draw_filled_rect(x + 9 * s, y + 5 * s, 2 * s, 6 * s);
		draw_filled_rect(x + 3 * s, y + 9 * s, 6 * s, 2 * s);
	}
	else {
		y += math_floor((UI_ELEMENT_H() - 12 * s) / 2.0);
		draw_rect(x + 1.5 * s, y, 9 * s, 12 * s, s);
		draw_filled_rect(x + 3.5 * s, y + 3 * s, 5 * s, s); // Text lines
		draw_filled_rect(x + 3.5 * s, y + 6 * s, 5 * s, s);
	}
}

// Menu item, the text is placed after the icon
static bool browser_menu_button(char *text, i32 icon) {
	f32 icon_x           = g_ui->_x + UI_TEXT_OFFSET();
	f32 icon_y           = g_ui->_y;
	i32 _TEXT_OFFSET     = g_theme->TEXT_OFFSET;
	g_theme->TEXT_OFFSET = _TEXT_OFFSET + BROWSER_ICON_W;
	bool clicked         = ui_button(text, UI_ALIGN_LEFT, "");
	g_theme->TEXT_OFFSET = _TEXT_OFFSET;
	browser_draw_icon(icon, icon_x, icon_y);
	return clicked;
}

void browser_menu_render() {
	i32 x, y, w, h;
	browser_menu_rect(&x, &y, &w, &h);
	i32 _WINDOW_BG_COL     = g_theme->WINDOW_BG_COL;
	g_theme->WINDOW_BG_COL = g_theme->BUTTON_COL; // Stand out from the panels
	bool redraw            = ui_window(browser_menu_hwnd, x, y, w, h, false);
	g_theme->WINDOW_BG_COL = _WINDOW_BG_COL;
	if (!redraw) {
		return;
	}

	if (browser_menu_mode != BROWSER_MENU_ITEMS) {
		char *label = browser_menu_mode == BROWSER_MENU_NEW_FILE ? "New File" : (browser_menu_mode == BROWSER_MENU_NEW_FOLDER ? "New Folder" : "");
		ui_text_input(&browser_menu_name, label, UI_ALIGN_LEFT, true, true);
		if (browser_menu_focus) {
			browser_menu_focus = false;
			ui_start_text_edit(&browser_menu_name, UI_ALIGN_LEFT);
		}
		return;
	}

	browser_node_t *node            = browser_menu_node;
	i32             _FILL_BUTTON_BG = g_theme->FILL_BUTTON_BG;
	g_theme->FILL_BUTTON_BG         = false;
	if (browser_menu_git != NULL) {
		// The first press asks to confirm, the changes are lost
		if (browser_menu_button(browser_menu_confirm ? "Confirm Revert" : "Revert", BROWSER_ICON_REVERT)) {
			if (browser_menu_confirm) {
				git_file_t *f = browser_menu_git;
				browser_menu_close();
				git_revert(f);
			}
			else {
				browser_menu_confirm = true;
			}
		}
	}
	else if (browser_menu_editor) {
		if (browser_menu_button("Reveal in Project", BROWSER_ICON_FOLDER_OPEN)) {
			browser_menu_close();
			browser_reveal(editor_current->path);
		}
	}
	else if (node->is_dir && browser_menu_button("New File", BROWSER_ICON_FILE)) {
		browser_menu_mode          = BROWSER_MENU_NEW_FILE;
		browser_menu_name          = "";
		browser_menu_focus         = true;
		browser_menu_hwnd->redraws = 2;
	}
	else if (node->is_dir && browser_menu_button("New Folder", BROWSER_ICON_FOLDER)) {
		browser_menu_mode          = BROWSER_MENU_NEW_FOLDER;
		browser_menu_name          = "";
		browser_menu_focus         = true;
		browser_menu_hwnd->redraws = 2;
	}
	else if (node != browser_root && browser_menu_button("Rename", BROWSER_ICON_RENAME)) {
		browser_menu_mode          = BROWSER_MENU_RENAME;
		browser_menu_name          = string_copy(node->name);
		browser_menu_focus         = true;
		browser_menu_hwnd->redraws = 2; // Menu turns into the name field
	}
	// The first press asks to confirm, deleted files are not moved to the trash
	else if (node != browser_root && browser_menu_button(browser_menu_confirm ? "Confirm Delete" : "Delete", BROWSER_ICON_DELETE)) {
		if (browser_menu_confirm) {
			browser_menu_close();
			browser_delete(node);
		}
		else {
			browser_menu_confirm = true;
		}
	}
	else if (browser_can_format(node) && browser_menu_button("Format", BROWSER_ICON_FORMAT)) {
		browser_menu_close();
		browser_format(node);
	}
	else if (browser_menu_button("Show in Explorer", BROWSER_ICON_FOLDER_OPEN)) {
		browser_menu_close();
		browser_show_in_explorer(node->path);
	}
	g_theme->FILL_BUTTON_BG = _FILL_BUTTON_BG;
}

static void browser_draw(browser_node_t *node, i32 depth) {
	f32 _x           = g_ui->_x;
	i32 _w           = g_ui->_w;
	i32 _TEXT_OFFSET = g_theme->TEXT_OFFSET;
	i32 _TEXT_COL    = g_theme->TEXT_COL;
	f32 indent       = depth * 14 * UI_SCALE();
	for (i32 i = 0; i < (i32)node->children->length; ++i) {
		browser_node_t *n = node->children->buffer[i];
		g_ui->_x          = _x + indent;
		g_ui->_w          = _w - indent;
		f32  icon_x       = g_ui->_x + UI_TEXT_OFFSET();
		f32  icon_y       = g_ui->_y;
		bool selected     = !n->is_dir && editor_current != NULL && string_equals(editor_current->path, n->path);
		if (browser_menu_show) {
			if (!browser_menu_editor) {
				selected = n == browser_menu_node; // Mark the row the menu belongs to
			}
		}
		else if (browser_menu_clicked()) {
			browser_menu_open(n);
		}
		// Name is placed after the icon
		g_theme->TEXT_OFFSET = _TEXT_OFFSET + BROWSER_ICON_W;
		g_theme->TEXT_COL    = n->is_dir ? g_theme->LABEL_COL : _TEXT_COL;
		bool clicked         = pad_row(n->name, selected);
		g_theme->TEXT_OFFSET = _TEXT_OFFSET;
		g_theme->TEXT_COL    = _TEXT_COL;
		g_ui->_x             = _x;
		g_ui->_w             = _w;
		if (n->is_dir) {
			if (clicked) {
				n->expanded = !n->expanded;
				if (n->expanded) {
					browser_load(n);
				}
			}
			browser_draw_icon(n->expanded ? BROWSER_ICON_FOLDER_OPEN : BROWSER_ICON_FOLDER, icon_x, icon_y);
			if (n->expanded) {
				browser_draw(n, depth + 1);
			}
		}
		else {
			browser_draw_icon(BROWSER_ICON_FILE, icon_x, icon_y);
			if (clicked) {
				editor_open(n->path);
			}
		}
	}
}

void browser_render(i32 x, i32 y, i32 w, i32 h) {
	if (!ui_window(browser_hwnd, x, y, w, h, false)) {
		return;
	}
	if (browser_root == NULL) {
		// Faded hint in the middle of the panel
		i32 _TEXT_COL     = g_theme->TEXT_COL;
		g_theme->TEXT_COL = 0xff606060;
		g_ui->_y += math_floor((h - UI_ELEMENT_H()) / 2.0);
		ui_text("Drop a folder to open a project", UI_ALIGN_CENTER, 0x00000000);
		g_theme->TEXT_COL = _TEXT_COL;
		return;
	}
	pad_fade_begin();
	i32 _FILL_BUTTON_BG     = g_theme->FILL_BUTTON_BG;
	g_theme->FILL_BUTTON_BG = false;
	browser_draw(browser_root, 0);
	// Right click below the rows
	if (!browser_menu_show && g_ui->input_released_r && ui_input_in_rect(g_ui->_window_x, g_ui->_window_y, g_ui->_window_w, g_ui->_window_h) &&
	    g_ui->input_y >= g_ui->_window_y + g_ui->_y) {
		browser_menu_open(browser_root);
	}
	g_theme->FILL_BUTTON_BG = _FILL_BUTTON_BG;
	pad_fade_end();
}
