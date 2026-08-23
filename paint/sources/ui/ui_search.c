
#include "../global.h"

static i32             _ui_search_offset = 0;
static bool            _ui_search_first;
static char           *_ui_search_shortcut = NULL;
static string_array_t *_ui_search_keys     = NULL;

static char *ui_search_key_name(char *key) {
	if (string_equals(key, "ctrl")) {
		key = "control";
	}
	for (i32 i = 0; i < keyboard_keys->length; ++i) {
		if (string_equals(keyboard_keys->buffer[i], key)) {
			return keyboard_keys->buffer[i];
		}
	}
	return NULL;
}

static void ui_search_release_keys(void *_) {
	for (i32 i = 0; i < _ui_search_keys->length; ++i) {
		i32_map_set(keyboard_keys_down, _ui_search_keys->buffer[i], false);
	}
	array_free(_ui_search_keys);
	free(_ui_search_keys);
	_ui_search_keys = NULL;
}

static void ui_search_press_keys(void *_) {
	if (!base_ui_enabled) {
		sys_notify_on_next_frame(&ui_search_press_keys, NULL);
		return;
	}

	// Simulate key press
	any_array_t *parts = string_split(_ui_search_shortcut, "+");
	_ui_search_keys    = string_array_create(0);
	for (i32 i = 0; i < parts->length; ++i) {
		char *key = ui_search_key_name(parts->buffer[i]);
		if (key == NULL) {
			continue;
		}
		string_array_push(_ui_search_keys, key);
		i32_map_set(keyboard_keys_down, key, true);
		i32_map_set(keyboard_keys_started, key, true);
		string_array_push(keyboard_keys_frame, key);
	}
	string_split_free(parts);
	sys_notify_on_end_frame(&ui_search_release_keys, NULL);
}

static void ui_search_run(char *shortcut) {
	char *key           = string_index_of(shortcut, "+") > 0 ? shortcut + string_last_index_of(shortcut, "+") + 1 : shortcut;
	_ui_search_shortcut = shortcut;
	sys_notify_on_next_frame(&ui_search_press_keys, NULL);
}

void ui_base_operator_search_menu_draw() {
	ui_menu_h                  = UI_ELEMENT_H() * 8;
	ui_handle_t *search_handle = ui_handle(__ID__);
	char        *search        = to_lower_case(ui_text_input(search_handle, "", UI_ALIGN_LEFT, true, true));
	g_ui->changed              = false;
	if (_ui_search_first) {
		_ui_search_first    = false;
		search_handle->text = "";
		ui_start_text_edit(search_handle, UI_ALIGN_LEFT); // Focus search bar
	}

	if (search_handle->changed) {
		_ui_search_offset = 0;
	}

	if (g_ui->is_key_pressed) { // Move selection
		if (g_ui->key_code == KEY_CODE_DOWN && _ui_search_offset < 6) {
			_ui_search_offset++;
		}
		if (g_ui->key_code == KEY_CODE_UP && _ui_search_offset > 0) {
			_ui_search_offset--;
		}
	}
	bool enter              = keyboard_down("enter");
	i32  count              = 0;
	i32  _BUTTON_COL        = g_theme->BUTTON_COL;
	bool _FILL_BUTTON_BG    = g_theme->FILL_BUTTON_BG;
	g_theme->FILL_BUTTON_BG = true;
	bool _SHADOWS           = g_theme->SHADOWS;
	g_theme->SHADOWS        = false;

	string_array_t *keys = map_keys(g_keymap);
	for (i32 i = 0; i < keys->length; ++i) {
		char *n = keys->buffer[i];
		if (string_index_of(to_lower_case(n), search) >= 0) {
			g_theme->BUTTON_COL = count == _ui_search_offset ? g_theme->HIGHLIGHT_COL : g_theme->SEPARATOR_COL;
			if (ui_button(n, UI_ALIGN_LEFT, any_map_get(g_keymap, n)) || (enter && count == _ui_search_offset)) {
				if (enter) {
					g_ui->changed = true;
					count         = 6; // Trigger break
				}
				ui_search_run(any_map_get(g_keymap, n));
			}
			if (++count > 6) {
				break;
			}
		}
	}
	array_free(keys);
	free(keys);

	if (enter && count == 0) { // Hide popup on enter when command is not found
		g_ui->changed       = true;
		search_handle->text = "";
	}
	g_theme->BUTTON_COL     = _BUTTON_COL;
	g_theme->FILL_BUTTON_BG = _FILL_BUTTON_BG;
	g_theme->SHADOWS        = _SHADOWS;
}

void ui_base_operator_search() {
	_ui_search_first = true;
	ui_menu_draw(&ui_base_operator_search_menu_draw, -1, -1);
}
