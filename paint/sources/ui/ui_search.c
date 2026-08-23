
#include "../global.h"

i32  ui_base_operator_search_offset = 0;
bool _ui_base_operator_search_first;

void ui_base_operator_search_menu_draw() {
	ui_menu_h                  = UI_ELEMENT_H() * 8;
	ui_handle_t *search_handle = ui_handle(__ID__);
	char        *search        = ui_text_input(search_handle, "", UI_ALIGN_LEFT, true, true);
	g_ui->changed              = false;
	if (_ui_base_operator_search_first) {
		_ui_base_operator_search_first = false;
		search_handle->text            = "";
		ui_start_text_edit(search_handle, UI_ALIGN_LEFT); // Focus search bar
	}

	if (search_handle->changed) {
		ui_base_operator_search_offset = 0;
	}

	if (g_ui->is_key_pressed) { // Move selection
		if (g_ui->key_code == KEY_CODE_DOWN && ui_base_operator_search_offset < 6) {
			ui_base_operator_search_offset++;
		}
		if (g_ui->key_code == KEY_CODE_UP && ui_base_operator_search_offset > 0) {
			ui_base_operator_search_offset--;
		}
	}
	bool enter      = keyboard_down("enter");
	i32  count      = 0;
	i32  BUTTON_COL = g_theme->BUTTON_COL;

	string_array_t *keys = map_keys(g_keymap);
	for (i32 i = 0; i < keys->length; ++i) {
		char *n = keys->buffer[i];
		if (string_index_of(n, search) >= 0) {
			g_theme->BUTTON_COL = count == ui_base_operator_search_offset ? g_theme->HIGHLIGHT_COL : g_theme->SEPARATOR_COL;
			if (ui_button(n, UI_ALIGN_LEFT, any_map_get(g_keymap, n)) || (enter && count == ui_base_operator_search_offset)) {
				if (enter) {
					g_ui->changed = true;
					count         = 6; // Trigger break
				}
				// operator_run(n);
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
	g_theme->BUTTON_COL = BUTTON_COL;
}

void ui_base_operator_search() {
	_ui_base_operator_search_first = true;
	ui_menu_draw(&ui_base_operator_search_menu_draw, -1, -1);
}
