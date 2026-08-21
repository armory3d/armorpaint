
#include "global.h"

void operator_register(char *name, void (*call)(void)) {
	any_map_set(g_operators, name, call);
}

void operator_run(char *name) {
	if (any_map_get(g_operators, name) != NULL) {
		void (*cb)(void) = any_map_get(g_operators, name);
		cb();
	}
}

void operator_update() {
	if (mouse_started_any() || keyboard_started_any()) {
		string_array_t *keys = map_keys(g_keymap);
		for (i32 i = 0; i < keys->length; ++i) {
			char *op = keys->buffer[i];
			if (operator_shortcut(any_map_get(g_keymap, op), SHORTCUT_TYPE_STARTED)) {
				operator_run(op);
			}
		}
		array_free(keys);
		free(keys);
	}
}

bool operator_shortcut(char *s, shortcut_type_t type) {
	if (string_equals(s, "") || g_config->workspace == WORKSPACE_PLAYER) {
		return false;
	}
	bool shift = string_index_of(s, "shift") >= 0;
	bool ctrl  = string_index_of(s, "ctrl") >= 0;
	bool alt   = string_index_of(s, "alt") >= 0;
	bool flag  = shift == keyboard_down("shift") && ctrl == keyboard_down("control") && alt == keyboard_down("alt");

	if (string_index_of(s, "+") > 0) {
		s = s + string_last_index_of(s, "+") + 1;
		if (string_equals(s, "number")) {
			return flag;
		}
	}
	else if (shift || ctrl || alt) {
		return flag;
	}

	bool key = false;
	if (string_equals(s, "left") || string_equals(s, "right") || string_equals(s, "middle")) {
		if (type == SHORTCUT_TYPE_DOWN) {
			key = mouse_down(s);
		}
		else {
			key = mouse_started(s);
		}
	}
	else if (type == SHORTCUT_TYPE_REPEAT) {
		key = keyboard_repeat(s);
	}
	else if (type == SHORTCUT_TYPE_DOWN) {
		key = keyboard_down(s);
	}
	else if (type == SHORTCUT_TYPE_RELEASED) {
		key = keyboard_released(s);
	}
	else {
		key = keyboard_started(s);
	}

	return flag && key;
}
