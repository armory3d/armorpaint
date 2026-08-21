#include "iron_input.h"

#include "iron_alloc.h"
#include "iron_string.h"
#include "iron_system.h"
#include <math.h>
#include <stdbool.h>

f32 sys_time(void);
i32 sys_x(void);
i32 sys_y(void);

// Input

bool _input_occupied   = false;
bool _input_registered = false;

// Mouse

string_array_t *_mouse_buttons          = NULL;
u8_array_t     *_mouse_buttons_down     = NULL;
u8_array_t     *_mouse_buttons_started  = NULL;
u8_array_t     *_mouse_buttons_released = NULL;
f32             mouse_x                 = -1.0f;
f32             mouse_y                 = -1.0f;
bool            mouse_moved             = false;
f32             mouse_movement_x        = 0.0f;
f32             mouse_movement_y        = 0.0f;
f32             mouse_wheel_delta       = 0.0f;
f32             mouse_last_x            = -1.0f;
f32             mouse_last_y            = -1.0f;

#if defined(IRON_ANDROID) || defined(IRON_IOS)
f32 mouse_pinch_dist   = 0.0f;
f32 mouse_pinch_total  = 0.0f;
f32 mouse_pinch_smooth = 0.0f;
#endif

// Pen

string_array_t *pen_buttons          = NULL;
u8_array_t     *pen_buttons_down     = NULL;
u8_array_t     *pen_buttons_started  = NULL;
u8_array_t     *pen_buttons_released = NULL;
f32             pen_x                = 0.0f;
f32             pen_y                = 0.0f;
bool            pen_moved            = false;
f32             pen_movement_x       = 0.0f;
f32             pen_movement_y       = 0.0f;
f32             pen_pressure         = 0.0f;
bool            pen_connected        = false;
bool            pen_in_use           = false;
f32             pen_last_x           = -1.0f;
f32             pen_last_y           = -1.0f;

// Keyboard

string_array_t *keyboard_keys          = NULL;
i32_map_t      *keyboard_keys_down     = NULL;
i32_map_t      *keyboard_keys_started  = NULL;
i32_map_t      *keyboard_keys_released = NULL;
string_array_t *keyboard_keys_frame    = NULL;
bool            keyboard_repeat_key    = false;
f32             keyboard_repeat_time   = 0.0f;

#ifdef WITH_GAMEPAD

// Gamepad

string_array_t *gamepad_buttons_ps   = NULL;
string_array_t *gamepad_buttons_xbox = NULL;
string_array_t *gamepad_buttons      = NULL;
any_array_t    *gamepad_raws         = NULL;

#endif

void input_reset(void) {
	mouse_reset();
	pen_reset();
	keyboard_reset();
#ifdef WITH_GAMEPAD
	gamepad_reset();
#endif
}

void input_end_frame(void) {
	mouse_end_frame();
	pen_end_frame();
	keyboard_end_frame();
#ifdef WITH_GAMEPAD
	gamepad_end_frame();
#endif
}

void input_on_foreground(void) {
	mouse_reset(); // Reset mouse delta on foreground
}

void input_register(void) {
	if (_input_registered) {
		return;
	}
	_input_registered = true;

	_input_occupied = false;

	_mouse_buttons = string_array_create(0);
	string_array_push(_mouse_buttons, "left");
	string_array_push(_mouse_buttons, "right");
	string_array_push(_mouse_buttons, "middle");
	string_array_push(_mouse_buttons, "side1");
	string_array_push(_mouse_buttons, "side2");

	_mouse_buttons_down     = u8_array_create(0);
	_mouse_buttons_started  = u8_array_create(0);
	_mouse_buttons_released = u8_array_create(0);
	for (i32 i = 0; i < 5; ++i) {
		u8_array_push(_mouse_buttons_down, 0);
		u8_array_push(_mouse_buttons_started, 0);
		u8_array_push(_mouse_buttons_released, 0);
	}

	mouse_x           = -1.0f;
	mouse_y           = -1.0f;
	mouse_moved       = false;
	mouse_movement_x  = 0.0f;
	mouse_movement_y  = 0.0f;
	mouse_wheel_delta = 0.0f;
	mouse_last_x      = -1.0f;
	mouse_last_y      = -1.0f;

#if defined(IRON_ANDROID) || defined(IRON_IOS)
	mouse_pinch_dist   = 0.0f;
	mouse_pinch_total  = 0.0f;
	mouse_pinch_smooth = 0.0f;
#endif

	pen_buttons = string_array_create(0);
	string_array_push(pen_buttons, "tip");

	pen_buttons_down     = u8_array_create(0);
	pen_buttons_started  = u8_array_create(0);
	pen_buttons_released = u8_array_create(0);
	u8_array_push(pen_buttons_down, 0);
	u8_array_push(pen_buttons_started, 0);
	u8_array_push(pen_buttons_released, 0);

	pen_x          = 0.0f;
	pen_y          = 0.0f;
	pen_moved      = false;
	pen_movement_x = 0.0f;
	pen_movement_y = 0.0f;
	pen_pressure   = 0.0f;
	pen_connected  = false;
	pen_in_use     = false;
	pen_last_x     = -1.0f;
	pen_last_y     = -1.0f;

	keyboard_keys = string_array_create(0);
	string_array_push(keyboard_keys, "a");
	string_array_push(keyboard_keys, "b");
	string_array_push(keyboard_keys, "c");
	string_array_push(keyboard_keys, "d");
	string_array_push(keyboard_keys, "e");
	string_array_push(keyboard_keys, "f");
	string_array_push(keyboard_keys, "g");
	string_array_push(keyboard_keys, "h");
	string_array_push(keyboard_keys, "i");
	string_array_push(keyboard_keys, "j");
	string_array_push(keyboard_keys, "k");
	string_array_push(keyboard_keys, "l");
	string_array_push(keyboard_keys, "m");
	string_array_push(keyboard_keys, "n");
	string_array_push(keyboard_keys, "o");
	string_array_push(keyboard_keys, "p");
	string_array_push(keyboard_keys, "q");
	string_array_push(keyboard_keys, "r");
	string_array_push(keyboard_keys, "s");
	string_array_push(keyboard_keys, "t");
	string_array_push(keyboard_keys, "u");
	string_array_push(keyboard_keys, "v");
	string_array_push(keyboard_keys, "w");
	string_array_push(keyboard_keys, "x");
	string_array_push(keyboard_keys, "y");
	string_array_push(keyboard_keys, "z");
	string_array_push(keyboard_keys, "0");
	string_array_push(keyboard_keys, "1");
	string_array_push(keyboard_keys, "2");
	string_array_push(keyboard_keys, "3");
	string_array_push(keyboard_keys, "4");
	string_array_push(keyboard_keys, "5");
	string_array_push(keyboard_keys, "6");
	string_array_push(keyboard_keys, "7");
	string_array_push(keyboard_keys, "8");
	string_array_push(keyboard_keys, "9");
	string_array_push(keyboard_keys, "space");
	string_array_push(keyboard_keys, "backspace");
	string_array_push(keyboard_keys, "tab");
	string_array_push(keyboard_keys, "enter");
	string_array_push(keyboard_keys, "shift");
	string_array_push(keyboard_keys, "control");
	string_array_push(keyboard_keys, "alt");
	string_array_push(keyboard_keys, "win");
	string_array_push(keyboard_keys, "escape");
	string_array_push(keyboard_keys, "delete");
	string_array_push(keyboard_keys, "up");
	string_array_push(keyboard_keys, "down");
	string_array_push(keyboard_keys, "left");
	string_array_push(keyboard_keys, "right");
	string_array_push(keyboard_keys, "back");
	string_array_push(keyboard_keys, ",");
	string_array_push(keyboard_keys, ".");
	string_array_push(keyboard_keys, ":");
	string_array_push(keyboard_keys, ";");
	string_array_push(keyboard_keys, "<");
	string_array_push(keyboard_keys, "=");
	string_array_push(keyboard_keys, ">");
	string_array_push(keyboard_keys, "?");
	string_array_push(keyboard_keys, "!");
	string_array_push(keyboard_keys, "\"");
	string_array_push(keyboard_keys, "#");
	string_array_push(keyboard_keys, "$");
	string_array_push(keyboard_keys, "%");
	string_array_push(keyboard_keys, "&");
	string_array_push(keyboard_keys, "_");
	string_array_push(keyboard_keys, "(");
	string_array_push(keyboard_keys, ")");
	string_array_push(keyboard_keys, "*");
	string_array_push(keyboard_keys, "|");
	string_array_push(keyboard_keys, "{");
	string_array_push(keyboard_keys, "}");
	string_array_push(keyboard_keys, "[");
	string_array_push(keyboard_keys, "]");
	string_array_push(keyboard_keys, "~");
	string_array_push(keyboard_keys, "`");
	string_array_push(keyboard_keys, "/");
	string_array_push(keyboard_keys, "\\");
	string_array_push(keyboard_keys, "@");
	string_array_push(keyboard_keys, "+");
	string_array_push(keyboard_keys, "-");
	string_array_push(keyboard_keys, "f1");
	string_array_push(keyboard_keys, "f2");
	string_array_push(keyboard_keys, "f3");
	string_array_push(keyboard_keys, "f4");
	string_array_push(keyboard_keys, "f5");
	string_array_push(keyboard_keys, "f6");
	string_array_push(keyboard_keys, "f7");
	string_array_push(keyboard_keys, "f8");
	string_array_push(keyboard_keys, "f9");
	string_array_push(keyboard_keys, "f10");
	string_array_push(keyboard_keys, "f11");
	string_array_push(keyboard_keys, "f12");

	keyboard_keys_down     = i32_map_create();
	keyboard_keys_started  = i32_map_create();
	keyboard_keys_released = i32_map_create();

	keyboard_keys_frame = string_array_create(0);

	keyboard_repeat_key  = false;
	keyboard_repeat_time = 0.0f;

#ifdef WITH_GAMEPAD
	gamepad_buttons_ps = string_array_create(0);
	string_array_push(gamepad_buttons_ps, "cross");
	string_array_push(gamepad_buttons_ps, "circle");
	string_array_push(gamepad_buttons_ps, "square");
	string_array_push(gamepad_buttons_ps, "triangle");
	string_array_push(gamepad_buttons_ps, "l1");
	string_array_push(gamepad_buttons_ps, "r1");
	string_array_push(gamepad_buttons_ps, "l2");
	string_array_push(gamepad_buttons_ps, "r2");
	string_array_push(gamepad_buttons_ps, "share");
	string_array_push(gamepad_buttons_ps, "options");
	string_array_push(gamepad_buttons_ps, "l3");
	string_array_push(gamepad_buttons_ps, "r3");
	string_array_push(gamepad_buttons_ps, "up");
	string_array_push(gamepad_buttons_ps, "down");
	string_array_push(gamepad_buttons_ps, "left");
	string_array_push(gamepad_buttons_ps, "right");
	string_array_push(gamepad_buttons_ps, "home");
	string_array_push(gamepad_buttons_ps, "touchpad");

	gamepad_buttons_xbox = string_array_create(0);
	string_array_push(gamepad_buttons_xbox, "a");
	string_array_push(gamepad_buttons_xbox, "b");
	string_array_push(gamepad_buttons_xbox, "x");
	string_array_push(gamepad_buttons_xbox, "y");
	string_array_push(gamepad_buttons_xbox, "l1");
	string_array_push(gamepad_buttons_xbox, "r1");
	string_array_push(gamepad_buttons_xbox, "l2");
	string_array_push(gamepad_buttons_xbox, "r2");
	string_array_push(gamepad_buttons_xbox, "share");
	string_array_push(gamepad_buttons_xbox, "options");
	string_array_push(gamepad_buttons_xbox, "l3");
	string_array_push(gamepad_buttons_xbox, "r3");
	string_array_push(gamepad_buttons_xbox, "up");
	string_array_push(gamepad_buttons_xbox, "down");
	string_array_push(gamepad_buttons_xbox, "left");
	string_array_push(gamepad_buttons_xbox, "right");
	string_array_push(gamepad_buttons_xbox, "home");
	string_array_push(gamepad_buttons_xbox, "touchpad");

	gamepad_buttons = gamepad_buttons_ps;

	gamepad_raws = any_array_create(0);
#endif

	keyboard_reset();
	mouse_reset();
#ifdef WITH_GAMEPAD
	gamepad_reset();
#endif
}

// Mouse

void mouse_end_frame(void) {
	_mouse_buttons_started->buffer[0] = _mouse_buttons_started->buffer[1] = _mouse_buttons_started->buffer[2] = _mouse_buttons_started->buffer[3] =
	    _mouse_buttons_started->buffer[4]                                                                     = false;
	_mouse_buttons_released->buffer[0] = _mouse_buttons_released->buffer[1] = _mouse_buttons_released->buffer[2] = _mouse_buttons_released->buffer[3] =
	    _mouse_buttons_released->buffer[4]                                                                       = false;
	mouse_moved                                                                                                  = false;
	mouse_movement_x                                                                                             = 0;
	mouse_movement_y                                                                                             = 0;
	mouse_wheel_delta                                                                                            = 0;
}

void mouse_reset(void) {
	_mouse_buttons_down->buffer[0] = _mouse_buttons_down->buffer[1] = _mouse_buttons_down->buffer[2] = _mouse_buttons_down->buffer[3] =
	    _mouse_buttons_down->buffer[4]                                                               = false;
	mouse_end_frame();
}

i32 mouse_button_index(char *button) {
	for (i32 i = 0; i < (i32)_mouse_buttons->length; ++i) {
		if (string_equals(_mouse_buttons->buffer[i], button)) {
			return i;
		}
	}
	return 0;
}

bool mouse_down(char *button) {
	return _mouse_buttons_down->buffer[mouse_button_index(button)];
}

bool mouse_down_any(void) {
	return _mouse_buttons_down->buffer[0] || _mouse_buttons_down->buffer[1] || _mouse_buttons_down->buffer[2] || _mouse_buttons_down->buffer[3] ||
	       _mouse_buttons_down->buffer[4];
}

bool mouse_started(char *button) {
	return _mouse_buttons_started->buffer[mouse_button_index(button)];
}

bool mouse_started_any(void) {
	return _mouse_buttons_started->buffer[0] || _mouse_buttons_started->buffer[1] || _mouse_buttons_started->buffer[2] || _mouse_buttons_started->buffer[3] ||
	       _mouse_buttons_started->buffer[4];
}

bool mouse_released(char *button) {
	return _mouse_buttons_released->buffer[mouse_button_index(button)];
}

void mouse_down_listener(i32 index, i32 x, i32 y) {
	if (pen_in_use) {
		return;
	}

	_mouse_buttons_down->buffer[index]    = true;
	_mouse_buttons_started->buffer[index] = true;
	mouse_x                               = (f32)x;
	mouse_y                               = (f32)y;
#if defined(IRON_ANDROID) || defined(IRON_IOS)
	if (index == 0) {
		mouse_last_x = (f32)x;
		mouse_last_y = (f32)y;
	}
#endif
}

void mouse_up_listener(i32 index, i32 x, i32 y) {
	if (pen_in_use) {
		return;
	}

	_mouse_buttons_down->buffer[index]     = false;
	_mouse_buttons_released->buffer[index] = true;
	mouse_x                                = (f32)x;
	mouse_y                                = (f32)y;
}

void mouse_move_listener(i32 x, i32 y, i32 movement_x, i32 movement_y) {
	if (mouse_last_x == -1.0f && mouse_last_y == -1.0f) { // First frame init
		mouse_last_x = (f32)x;
		mouse_last_y = (f32)y;
	}
	if (iron_mouse_is_locked()) {
		// Can be called multiple times per frame
		mouse_movement_x += (f32)movement_x;
		mouse_movement_y += (f32)movement_y;
	}
	else {
		mouse_movement_x += (f32)x - mouse_last_x;
		mouse_movement_y += (f32)y - mouse_last_y;
	}
	mouse_last_x = (f32)x;
	mouse_last_y = (f32)y;
	mouse_x      = (f32)x;
	mouse_y      = (f32)y;
	mouse_moved  = true;
}

void mouse_wheel_listener(f32 delta) {
	mouse_wheel_delta = delta;
}

#if defined(IRON_ANDROID) || defined(IRON_IOS)
void mouse_on_touch_down(i32 index, i32 x, i32 y) {
	if (index == 1) { // Two fingers down - right mouse button
		_mouse_buttons_down->buffer[0] = false;
		mouse_down_listener(1, (i32)mouse_x, (i32)mouse_y);
		mouse_pinch_total = 0.0f;
		mouse_pinch_dist  = 0.0f;
	}
	else if (index == 2) { // Three fingers down - middle mouse button
		_mouse_buttons_down->buffer[1] = false;
		mouse_down_listener(2, (i32)mouse_x, (i32)mouse_y);
	}
}

void mouse_on_touch_up(i32 index, i32 x, i32 y) {
	if (index == 1) {
		mouse_up_listener(1, (i32)mouse_x, (i32)mouse_y);
		mouse_pinch_dist   = 0.0f;
		mouse_pinch_total  = 0.0f;
		mouse_pinch_smooth = 0.0f;
	}
	else if (index == 2) {
		mouse_up_listener(2, (i32)mouse_x, (i32)mouse_y);
	}
}

void mouse_on_touch_move(i32 index, i32 x, i32 y) {
	// Pinch to zoom - mouse wheel
	if (index == 1) {
		f32 dx           = mouse_x - (f32)x;
		f32 dy           = mouse_y - (f32)y;
		f32 new_distance = sqrtf(dx * dx + dy * dy);
		if (mouse_pinch_dist == 0.0f) {
			mouse_pinch_dist = new_distance;
			return;
		}
		f32 delta            = mouse_pinch_dist - new_distance;
		mouse_pinch_dist     = new_distance;
		f32 smoothing_factor = 0.3f;
		mouse_pinch_smooth   = mouse_pinch_smooth * smoothing_factor + delta * (1.0f - smoothing_factor);
		mouse_pinch_total += mouse_pinch_smooth;
		f32 deadzone = 2.0f;
		if (fabsf(mouse_pinch_total) > deadzone) {
			mouse_wheel_delta = mouse_pinch_total / 40.0f;
			mouse_pinch_total = 0.0f;
		}
	}
}
#endif

f32 mouse_view_x(void) {
	return mouse_x - (f32)sys_x();
}

f32 mouse_view_y(void) {
	return mouse_y - (f32)sys_y();
}

// Pen

void pen_end_frame(void) {
	pen_buttons_started->buffer[0]  = false;
	pen_buttons_released->buffer[0] = false;
	pen_moved                       = false;
	pen_movement_x                  = 0;
	pen_movement_y                  = 0;
	pen_in_use                      = false;
}

void pen_reset(void) {
	pen_buttons_down->buffer[0] = false;
	pen_end_frame();
}

i32 pen_button_index(char *unused) {
	return 0;
}

bool pen_down(char *button) {
	return pen_buttons_down->buffer[pen_button_index(button)];
}

bool pen_started(char *button) {
	return pen_buttons_started->buffer[pen_button_index(button)];
}

bool pen_released(char *button) {
	return pen_buttons_released->buffer[pen_button_index(button)];
}

void pen_down_listener(i32 x, i32 y, f32 pressure) {
	pen_buttons_down->buffer[0]    = true;
	pen_buttons_started->buffer[0] = true;
	pen_x                          = (f32)x;
	pen_y                          = (f32)y;
	pen_pressure                   = pressure;

#if !defined(IRON_ANDROID) && !defined(IRON_IOS)
	mouse_down_listener(0, x, y);
#endif
}

void pen_up_listener(i32 x, i32 y, f32 pressure) {
#if !defined(IRON_ANDROID) && !defined(IRON_IOS)
	if (pen_buttons_started->buffer[0]) {
		pen_buttons_started->buffer[0] = false;
		pen_in_use                     = true;
		return;
	}
#endif

	pen_buttons_down->buffer[0]     = false;
	pen_buttons_released->buffer[0] = true;
	pen_x                           = (f32)x;
	pen_y                           = (f32)y;
	pen_pressure                    = pressure;

#if !defined(IRON_ANDROID) && !defined(IRON_IOS)
	mouse_up_listener(0, x, y);
	pen_in_use = true; // On pen release, additional mouse down & up events are fired at once - filter those out
#endif
}

void pen_move_listener(i32 x, i32 y, f32 pressure) {
#ifdef IRON_IOS
	// Listen to pen hover if no other input is active
	if (!pen_buttons_down->buffer[0] && pressure == 0.0f) {
		if (!mouse_down_any()) {
			mouse_move_listener(x, y, 0, 0);
		}
		return;
	}
#endif

	if (pen_last_x == -1.0f && pen_last_y == -1.0f) { // First frame init
		pen_last_x = (f32)x;
		pen_last_y = (f32)y;
	}
	pen_movement_x = (f32)x - pen_last_x;
	pen_movement_y = (f32)y - pen_last_y;
	pen_last_x     = (f32)x;
	pen_last_y     = (f32)y;
	pen_x          = (f32)x;
	pen_y          = (f32)y;
	pen_moved      = true;
	pen_pressure   = pressure;
	pen_connected  = true;
}

f32 pen_view_x(void) {
	return pen_x - (f32)sys_x();
}

f32 pen_view_y(void) {
	return pen_y - (f32)sys_y();
}

// Keyboard

void keyboard_end_frame(void) {
	if (keyboard_keys_frame->length > 0) {
		for (i32 i = 0; i < (i32)keyboard_keys_frame->length; ++i) {
			char *s = keyboard_keys_frame->buffer[i];
			i32_map_set(keyboard_keys_started, s, false);
			i32_map_set(keyboard_keys_released, s, false);
		}
		array_splice((any_array_t *)keyboard_keys_frame, 0, keyboard_keys_frame->length);
	}

	if (sys_time() - keyboard_repeat_time > 0.05f) {
		keyboard_repeat_time = sys_time();
		keyboard_repeat_key  = true;
	}
	else {
		keyboard_repeat_key = false;
	}
}

void keyboard_reset(void) {
	for (i32 i = 0; i < (i32)keyboard_keys->length; ++i) {
		char *s = keyboard_keys->buffer[i];
		i32_map_set(keyboard_keys_down, s, false);
		i32_map_set(keyboard_keys_started, s, false);
		i32_map_set(keyboard_keys_released, s, false);
	}
	keyboard_end_frame();
}

bool keyboard_down(char *key) {
	return i32_map_get(keyboard_keys_down, key);
}

bool keyboard_started(char *key) {
	return i32_map_get(keyboard_keys_started, key);
}

bool keyboard_started_any(void) {
	return keyboard_keys_frame->length > 0;
}

bool keyboard_released(char *key) {
	return i32_map_get(keyboard_keys_released, key);
}

bool keyboard_repeat(char *key) {
	return i32_map_get(keyboard_keys_started, key) || (keyboard_repeat_key && i32_map_get(keyboard_keys_down, key));
}

char *keyboard_key_code(i32 key) {
	if (key == KEY_CODE_SPACE) {
		return "space";
	}
	if (key == KEY_CODE_BACKSPACE) {
		return "backspace";
	}
	if (key == KEY_CODE_TAB) {
		return "tab";
	}
	if (key == KEY_CODE_RETURN) {
		return "enter";
	}
	if (key == KEY_CODE_SHIFT) {
		return "shift";
	}
	if (key == KEY_CODE_CONTROL) {
		return "control";
	}
#ifdef IRON_MACOS
	if (key == KEY_CODE_META) {
		return "control";
	}
#endif
	if (key == KEY_CODE_ALT) {
		return "alt";
	}
	if (key == KEY_CODE_WIN) {
		return "win";
	}
	if (key == KEY_CODE_ESCAPE) {
		return "escape";
	}
	if (key == KEY_CODE_DELETE) {
		return "delete";
	}
	if (key == KEY_CODE_UP) {
		return "up";
	}
	if (key == KEY_CODE_DOWN) {
		return "down";
	}
	if (key == KEY_CODE_LEFT) {
		return "left";
	}
	if (key == KEY_CODE_RIGHT) {
		return "right";
	}
	if (key == KEY_CODE_BACK) {
		return "back";
	}
	if (key == KEY_CODE_COMMA) {
		return ",";
	}
	if (key == KEY_CODE_PERIOD) {
		return ".";
	}
	if (key == KEY_CODE_COLON) {
		return ":";
	}
	if (key == KEY_CODE_SEMICOLON) {
		return ";";
	}
	if (key == KEY_CODE_LESS_THAN) {
		return "<";
	}
	if (key == KEY_CODE_EQUALS) {
		return "=";
	}
	if (key == KEY_CODE_GREATER_THAN) {
		return ">";
	}
	if (key == KEY_CODE_QUESTIONMARK) {
		return "?";
	}
	if (key == KEY_CODE_EXCLAMATION) {
		return "!";
	}
	if (key == KEY_CODE_DOUBLE_QUOTE) {
		return "\"";
	}
	if (key == KEY_CODE_HASH) {
		return "#";
	}
	if (key == KEY_CODE_DOLLAR) {
		return "$";
	}
	if (key == KEY_CODE_PERCENT) {
		return "%";
	}
	if (key == KEY_CODE_AMPERSAND) {
		return "&";
	}
	if (key == KEY_CODE_UNDERSCORE) {
		return "_";
	}
	if (key == KEY_CODE_OPEN_PAREN) {
		return "(";
	}
	if (key == KEY_CODE_CLOSE_PAREN) {
		return ")";
	}
	if (key == KEY_CODE_ASTERISK) {
		return "*";
	}
	if (key == KEY_CODE_PIPE) {
		return "|";
	}
	if (key == KEY_CODE_OPEN_CURLY_BRACKET) {
		return "{";
	}
	if (key == KEY_CODE_CLOSE_CURLY_BRACKET) {
		return "}";
	}
	if (key == KEY_CODE_OPEN_BRACKET) {
		return "[";
	}
	if (key == KEY_CODE_CLOSE_BRACKET) {
		return "]";
	}
	if (key == KEY_CODE_TILDE) {
		return "~";
	}
	if (key == KEY_CODE_BACK_QUOTE) {
		return "`";
	}
	if (key == KEY_CODE_SLASH) {
		return "/";
	}
	if (key == KEY_CODE_BACK_SLASH) {
		return "\\";
	}
	if (key == KEY_CODE_AT) {
		return "@";
	}
	if (key == KEY_CODE_ADD || key == KEY_CODE_PLUS) {
		return "+";
	}
	if (key == KEY_CODE_SUBTRACT || key == KEY_CODE_HYPHEN_MINUS) {
		return "-";
	}
	if (key == KEY_CODE_MULTIPLY) {
		return "*";
	}
	if (key == KEY_CODE_DIVIDE || key == KEY_CODE_DECIMAL) {
		return "/";
	}
	if (key == KEY_CODE_0 || key == KEY_CODE_NUMPAD_0) {
		return "0";
	}
	if (key == KEY_CODE_1 || key == KEY_CODE_NUMPAD_1) {
		return "1";
	}
	if (key == KEY_CODE_2 || key == KEY_CODE_NUMPAD_2) {
		return "2";
	}
	if (key == KEY_CODE_3 || key == KEY_CODE_NUMPAD_3) {
		return "3";
	}
	if (key == KEY_CODE_4 || key == KEY_CODE_NUMPAD_4) {
		return "4";
	}
	if (key == KEY_CODE_5 || key == KEY_CODE_NUMPAD_5) {
		return "5";
	}
	if (key == KEY_CODE_6 || key == KEY_CODE_NUMPAD_6) {
		return "6";
	}
	if (key == KEY_CODE_7 || key == KEY_CODE_NUMPAD_7) {
		return "7";
	}
	if (key == KEY_CODE_8 || key == KEY_CODE_NUMPAD_8) {
		return "8";
	}
	if (key == KEY_CODE_9 || key == KEY_CODE_NUMPAD_9) {
		return "9";
	}
	if (key == KEY_CODE_F1) {
		return "f1";
	}
	if (key == KEY_CODE_F2) {
		return "f2";
	}
	if (key == KEY_CODE_F3) {
		return "f3";
	}
	if (key == KEY_CODE_F4) {
		return "f4";
	}
	if (key == KEY_CODE_F5) {
		return "f5";
	}
	if (key == KEY_CODE_F6) {
		return "f6";
	}
	if (key == KEY_CODE_F7) {
		return "f7";
	}
	if (key == KEY_CODE_F8) {
		return "f8";
	}
	if (key == KEY_CODE_F9) {
		return "f9";
	}
	if (key == KEY_CODE_F10) {
		return "f10";
	}
	if (key == KEY_CODE_F11) {
		return "f11";
	}
	if (key == KEY_CODE_F12) {
		return "f12";
	}
	return to_lower_case(string_from_char_code(key));
}

void keyboard_down_listener(i32 code) {
	char *s = keyboard_key_code(code);
	string_array_push(keyboard_keys_frame, s);
	i32_map_set(keyboard_keys_started, s, true);
	i32_map_set(keyboard_keys_down, s, true);
	keyboard_repeat_time = sys_time() + 0.4f;

#ifdef IRON_ANDROID_RMB // Detect right mouse button on Android
	if (code == KEY_CODE_BACK) {
		if (!_mouse_buttons_down->buffer[1]) {
			mouse_down_listener(1, (i32)mouse_x, (i32)mouse_y);
		}
	}
#endif
}

void keyboard_up_listener(i32 code) {
	char *s = keyboard_key_code(code);
	string_array_push(keyboard_keys_frame, s);
	i32_map_set(keyboard_keys_released, s, true);
	i32_map_set(keyboard_keys_down, s, false);

#ifdef IRON_ANDROID_RMB
	if (code == KEY_CODE_BACK) {
		mouse_up_listener(1, (i32)mouse_x, (i32)mouse_y);
	}
#endif
}

// Gamepad

#ifdef WITH_GAMEPAD

void gamepad_end_frame(void) {}

gamepad_stick_t *gamepad_stick_create(void) {
	gamepad_stick_t *raw = calloc(1, sizeof(gamepad_stick_t));
	raw->x               = 0.0f;
	raw->y               = 0.0f;
	raw->last_x          = 0.0f;
	raw->last_y          = 0.0f;
	raw->moved           = false;
	raw->movement_x      = 0.0f;
	raw->movement_y      = 0.0f;
	return raw;
}

gamepad_t *gamepad_create(void) {
	gamepad_t *raw        = calloc(1, sizeof(gamepad_t));
	raw->buttons_down     = f32_array_create(0);
	raw->buttons_started  = u8_array_create(0);
	raw->buttons_released = u8_array_create(0);
	raw->buttons_frame    = i32_array_create(0);
	raw->left_stick       = gamepad_stick_create();
	raw->right_stick      = gamepad_stick_create();
	return raw;
}

void gamepad_reset(void) {
	gamepad_raws = any_array_create(0);
	for (i32 i = 0; i < 4; ++i) {
		gamepad_t *g = gamepad_create();
		any_array_push(gamepad_raws, g);
		for (i32 j = 0; j < (i32)gamepad_buttons->length; ++j) {
			f32_array_push(g->buttons_down, 0.0f);
			u8_array_push(g->buttons_started, false);
			u8_array_push(g->buttons_released, false);
		}
	}
	gamepad_end_frame();
}

char *gamepad_key_code(i32 button) {
	return gamepad_buttons->buffer[button];
}

i32 gamepad_button_index(char *button) {
	for (i32 i = 0; i < (i32)gamepad_buttons->length; ++i) {
		if (string_equals(gamepad_buttons->buffer[i], button)) {
			return i;
		}
	}
	return 0;
}

f32 gamepad_down(i32 i, char *button) {
	gamepad_t *g = gamepad_raws->buffer[i];
	return g->buttons_down->buffer[gamepad_button_index(button)];
}

bool gamepad_started(i32 i, char *button) {
	gamepad_t *g = gamepad_raws->buffer[i];
	return g->buttons_started->buffer[gamepad_button_index(button)];
}

bool gamepad_released(i32 i, char *button) {
	gamepad_t *g = gamepad_raws->buffer[i];
	return g->buttons_released->buffer[gamepad_button_index(button)];
}

void gamepad_axis_listener(i32 i, i32 axis, f32 value) {
	gamepad_t       *raw   = gamepad_raws->buffer[i];
	gamepad_stick_t *stick = axis <= 1 ? raw->left_stick : raw->right_stick;

	if (axis == 0 || axis == 2) { // X
		stick->last_x     = stick->x;
		stick->x          = value;
		stick->movement_x = stick->x - stick->last_x;
	}
	else if (axis == 1 || axis == 3) { // Y
		stick->last_y     = stick->y;
		stick->y          = value;
		stick->movement_y = stick->y - stick->last_y;
	}
	stick->moved = true;
}

void gamepad_button_listener(i32 i, i32 button, f32 value) {}

#endif
