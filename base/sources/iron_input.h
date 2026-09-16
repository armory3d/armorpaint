#pragma once

#include "iron_array.h"
#include "iron_global.h"
#include "iron_map.h"
#include <stdbool.h>

// Input

extern bool _input_occupied;
extern bool _input_registered;

void input_reset(void);
void input_end_frame(void);
void input_on_foreground(void);
void input_register(void);

// Mouse

extern string_array_t *_mouse_buttons;
extern u8_array_t     *_mouse_buttons_down;
extern u8_array_t     *_mouse_buttons_started;
extern u8_array_t     *_mouse_buttons_released;
extern f32             mouse_x;
extern f32             mouse_y;
extern bool            mouse_moved;
extern f32             mouse_movement_x;
extern f32             mouse_movement_y;
extern f32             mouse_wheel_delta;
extern f32             mouse_last_x;
extern f32             mouse_last_y;

void mouse_end_frame(void);
void mouse_reset(void);
i32  mouse_button_index(char *button);
bool mouse_down(char *button);
bool mouse_down_any(void);
bool mouse_started(char *button);
bool mouse_started_any(void);
bool mouse_released(char *button);
void mouse_down_listener(i32 index, i32 x, i32 y);
void mouse_up_listener(i32 index, i32 x, i32 y);
void mouse_move_listener(i32 x, i32 y, i32 movement_x, i32 movement_y);
void mouse_wheel_listener(f32 delta);

#if defined(IRON_ANDROID) || defined(IRON_IOS)
extern f32 mouse_pinch_dist;
extern f32 mouse_pinch_total;
extern f32 mouse_pinch_smooth;

void mouse_on_touch_down(i32 index, i32 x, i32 y);
void mouse_on_touch_up(i32 index, i32 x, i32 y);
void mouse_on_touch_move(i32 index, i32 x, i32 y);
#endif

f32 mouse_view_x(void);
f32 mouse_view_y(void);

// Pen

extern string_array_t *pen_buttons;
extern u8_array_t     *pen_buttons_down;
extern u8_array_t     *pen_buttons_started;
extern u8_array_t     *pen_buttons_released;
extern f32             pen_x;
extern f32             pen_y;
extern bool            pen_moved;
extern f32             pen_movement_x;
extern f32             pen_movement_y;
extern f32             pen_pressure;
extern bool            pen_connected;
extern bool            pen_in_use;
extern f32             pen_last_x;
extern f32             pen_last_y;

void pen_end_frame(void);
void pen_reset(void);
i32  pen_button_index(char *unused);
bool pen_down(char *button);
bool pen_started(char *button);
bool pen_released(char *button);
void pen_down_listener(i32 x, i32 y, f32 pressure);
void pen_up_listener(i32 x, i32 y, f32 pressure);
void pen_move_listener(i32 x, i32 y, f32 pressure);
f32  pen_view_x(void);
f32  pen_view_y(void);

// Keyboard

extern string_array_t *keyboard_keys;
extern i32_map_t      *keyboard_keys_down;
extern i32_map_t      *keyboard_keys_started;
extern i32_map_t      *keyboard_keys_released;
extern string_array_t *keyboard_keys_frame;
extern bool            keyboard_repeat_key;
extern f64             keyboard_repeat_time;

void  keyboard_end_frame(void);
void  keyboard_reset(void);
bool  keyboard_down(char *key);
bool  keyboard_started(char *key);
bool  keyboard_started_any(void);
bool  keyboard_released(char *key);
bool  keyboard_repeat(char *key);
char *keyboard_key_code(i32 key);
void  keyboard_down_listener(i32 code);
void  keyboard_up_listener(i32 code);

#ifdef WITH_GAMEPAD

// Gamepad

typedef struct gamepad_stick {
	f32  x;
	f32  y;
	f32  last_x;
	f32  last_y;
	bool moved;
	f32  movement_x;
	f32  movement_y;
} gamepad_stick_t;

typedef struct gamepad {
	f32_array_t     *buttons_down;
	u8_array_t      *buttons_started;
	u8_array_t      *buttons_released;
	i32_array_t     *buttons_frame;
	gamepad_stick_t *left_stick;
	gamepad_stick_t *right_stick;
} gamepad_t;

extern string_array_t *gamepad_buttons_ps;
extern string_array_t *gamepad_buttons_xbox;
extern string_array_t *gamepad_buttons;
extern any_array_t    *gamepad_raws;

void             gamepad_end_frame(void);
gamepad_stick_t *gamepad_stick_create(void);
gamepad_t       *gamepad_create(void);
void             gamepad_reset(void);
char            *gamepad_key_code(i32 button);
i32              gamepad_button_index(char *button);
f32              gamepad_down(i32 i, char *button);
bool             gamepad_started(i32 i, char *button);
bool             gamepad_released(i32 i, char *button);
void             gamepad_axis_listener(i32 i, i32 axis, f32 value);
void             gamepad_button_listener(i32 i, i32 button, f32 value);

#endif
