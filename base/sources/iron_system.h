#pragma once

#include "iron_array.h"
#include "iron_global.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
	IRON_LOG_LEVEL_INFO,
	IRON_LOG_LEVEL_ERROR
} iron_log_level_t;

void iron_log_args(iron_log_level_t level, const char *format, va_list args);
void iron_log(const char *format, ...);
void iron_error(const char *format, ...);

typedef struct iron_display_mode {
	int x;
	int y;
	int width;
	int height;
	int pixels_per_inch;
	int frequency;
	int bits_per_pixel;
} iron_display_mode_t;

void                iron_display_init(void);
int                 iron_primary_display(void);
int                 iron_count_displays(void);
iron_display_mode_t iron_display_current_mode(int display_index);

typedef enum {
	IRON_CURSOR_ARROW,
	IRON_CURSOR_HAND,
	IRON_CURSOR_IBEAM,
	IRON_CURSOR_SIZEWE,
	IRON_CURSOR_SIZENS
} iron_cursor_t;

typedef enum {
	IRON_WINDOW_MODE_WINDOW,
	IRON_WINDOW_MODE_FULLSCREEN
} iron_window_mode_t;

typedef enum {
	IRON_WINDOW_FEATURES_NONE        = 0,
	IRON_WINDOW_FEATURES_RESIZABLE   = 1,
	IRON_WINDOW_FEATURES_MINIMIZABLE = 2,
	IRON_WINDOW_FEATURES_MAXIMIZABLE = 4,
} iron_window_features_t;

typedef struct iron_window_options {
	const char        *title;
	int                x;
	int                y;
	int                width;
	int                height;
	int                features;
	iron_window_mode_t mode;
	int                frequency;
	bool               vsync;
	int                display_index;
	bool               visible;
	int                color_bits;
	int                depth_bits;
} iron_window_options_t;

void               iron_window_create(iron_window_options_t *win);
void               iron_window_destroy();
void               iron_window_resize(int width, int height);
void               iron_window_move(int x, int y);
void               iron_window_change_mode(iron_window_mode_t mode);
int                iron_window_x();
int                iron_window_y();
int                iron_window_width();
int                iron_window_height();
int                iron_window_display();
iron_window_mode_t iron_window_get_mode();
void               iron_window_show();
void               iron_window_hide();
void               iron_window_set_title(const char *title);
void               iron_window_set_resize_callback(void (*callback)(int x, int y, void *data), void *data);
void               iron_window_set_close_callback(bool (*callback)(void *data), void *data);
void               iron_internal_call_resize_callback(int width, int height);
bool               iron_internal_call_close_callback();

void        iron_init(iron_window_options_t *win);
const char *iron_application_name(void);
void        iron_set_app_name(const char *name);
void        iron_load_url(const char *url);
const char *iron_system_id(void);
const char *iron_language(void);

double              iron_frequency(void);
uint64_t            iron_timestamp(void);
int                 iron_hardware_threads(void);
double              iron_time(void);
void                iron_start(void);
void                iron_stop(void);
void                iron_set_keep_screen_on(bool on);
void                iron_exec_async(const char *path, char *argv[]);
extern volatile int iron_exec_async_done;
extern char        *iron_exec_async_output_file;

void iron_copy_to_clipboard(const char *text);
void iron_set_update_callback(void (*callback)(void));
void iron_set_foreground_callback(void (*callback)(void *), void *data);
void iron_set_resume_callback(void (*callback)(void *), void *data);
void iron_set_pause_callback(void (*callback)(void *), void *data);
void iron_set_background_callback(void (*callback)(void *), void *data);
void iron_set_shutdown_callback(void (*callback)(void *), void *data);
void iron_set_drop_files_callback(void (*callback)(char *, void *), void *data);
void iron_set_cut_callback(void (*callback)(void *), void *data);
void iron_set_copy_callback(void (*callback)(void *), void *data);
void iron_set_paste_callback(void (*callback)(char *, void *), void *data);

bool        iron_internal_frame(void);
const char *iron_internal_save_path(void);
bool        iron_internal_handle_messages(void);
void        iron_internal_shutdown(void);
void        iron_internal_update_callback(void);
void        iron_internal_foreground_callback(void);
void        iron_internal_background_callback(void);
void        iron_internal_shutdown_callback(void);
void        iron_internal_drop_files_callback(char *);
void        iron_internal_cut_callback(void);
void        iron_internal_copy_callback(void);
void        iron_internal_paste_callback(char *);

typedef enum {
	KEY_CODE_UNKNOWN             = 0,
	KEY_CODE_BACK                = 1,
	KEY_CODE_CANCEL              = 3,
	KEY_CODE_HELP                = 6,
	KEY_CODE_BACKSPACE           = 8,
	KEY_CODE_TAB                 = 9,
	KEY_CODE_RETURN              = 13,
	KEY_CODE_SHIFT               = 16,
	KEY_CODE_CONTROL             = 17,
	KEY_CODE_ALT                 = 18,
	KEY_CODE_PAUSE               = 19,
	KEY_CODE_CAPS_LOCK           = 20,
	KEY_CODE_ESCAPE              = 27,
	KEY_CODE_SPACE               = 32,
	KEY_CODE_PAGE_UP             = 33,
	KEY_CODE_PAGE_DOWN           = 34,
	KEY_CODE_END                 = 35,
	KEY_CODE_HOME                = 36,
	KEY_CODE_LEFT                = 37,
	KEY_CODE_UP                  = 38,
	KEY_CODE_RIGHT               = 39,
	KEY_CODE_DOWN                = 40,
	KEY_CODE_PRINT_SCREEN        = 44,
	KEY_CODE_INSERT              = 45,
	KEY_CODE_DELETE              = 46,
	KEY_CODE_0                   = 48,
	KEY_CODE_1                   = 49,
	KEY_CODE_2                   = 50,
	KEY_CODE_3                   = 51,
	KEY_CODE_4                   = 52,
	KEY_CODE_5                   = 53,
	KEY_CODE_6                   = 54,
	KEY_CODE_7                   = 55,
	KEY_CODE_8                   = 56,
	KEY_CODE_9                   = 57,
	KEY_CODE_COLON               = 58,
	KEY_CODE_SEMICOLON           = 59,
	KEY_CODE_LESS_THAN           = 60,
	KEY_CODE_EQUALS              = 61,
	KEY_CODE_GREATER_THAN        = 62,
	KEY_CODE_QUESTIONMARK        = 63,
	KEY_CODE_AT                  = 64,
	KEY_CODE_A                   = 65,
	KEY_CODE_B                   = 66,
	KEY_CODE_C                   = 67,
	KEY_CODE_D                   = 68,
	KEY_CODE_E                   = 69,
	KEY_CODE_F                   = 70,
	KEY_CODE_G                   = 71,
	KEY_CODE_H                   = 72,
	KEY_CODE_I                   = 73,
	KEY_CODE_J                   = 74,
	KEY_CODE_K                   = 75,
	KEY_CODE_L                   = 76,
	KEY_CODE_M                   = 77,
	KEY_CODE_N                   = 78,
	KEY_CODE_O                   = 79,
	KEY_CODE_P                   = 80,
	KEY_CODE_Q                   = 81,
	KEY_CODE_R                   = 82,
	KEY_CODE_S                   = 83,
	KEY_CODE_T                   = 84,
	KEY_CODE_U                   = 85,
	KEY_CODE_V                   = 86,
	KEY_CODE_W                   = 87,
	KEY_CODE_X                   = 88,
	KEY_CODE_Y                   = 89,
	KEY_CODE_Z                   = 90,
	KEY_CODE_WIN                 = 91,
	KEY_CODE_CONTEXT_MENU        = 93,
	KEY_CODE_SLEEP               = 95,
	KEY_CODE_NUMPAD_0            = 96,
	KEY_CODE_NUMPAD_1            = 97,
	KEY_CODE_NUMPAD_2            = 98,
	KEY_CODE_NUMPAD_3            = 99,
	KEY_CODE_NUMPAD_4            = 100,
	KEY_CODE_NUMPAD_5            = 101,
	KEY_CODE_NUMPAD_6            = 102,
	KEY_CODE_NUMPAD_7            = 103,
	KEY_CODE_NUMPAD_8            = 104,
	KEY_CODE_NUMPAD_9            = 105,
	KEY_CODE_MULTIPLY            = 106,
	KEY_CODE_ADD                 = 107,
	KEY_CODE_SEPARATOR           = 108,
	KEY_CODE_SUBTRACT            = 109,
	KEY_CODE_DECIMAL             = 110,
	KEY_CODE_DIVIDE              = 111,
	KEY_CODE_F1                  = 112,
	KEY_CODE_F2                  = 113,
	KEY_CODE_F3                  = 114,
	KEY_CODE_F4                  = 115,
	KEY_CODE_F5                  = 116,
	KEY_CODE_F6                  = 117,
	KEY_CODE_F7                  = 118,
	KEY_CODE_F8                  = 119,
	KEY_CODE_F9                  = 120,
	KEY_CODE_F10                 = 121,
	KEY_CODE_F11                 = 122,
	KEY_CODE_F12                 = 123,
	KEY_CODE_F13                 = 124,
	KEY_CODE_F14                 = 125,
	KEY_CODE_F15                 = 126,
	KEY_CODE_F16                 = 127,
	KEY_CODE_F17                 = 128,
	KEY_CODE_F18                 = 129,
	KEY_CODE_F19                 = 130,
	KEY_CODE_F20                 = 131,
	KEY_CODE_F21                 = 132,
	KEY_CODE_F22                 = 133,
	KEY_CODE_F23                 = 134,
	KEY_CODE_F24                 = 135,
	KEY_CODE_NUM_LOCK            = 144,
	KEY_CODE_SCROLL_LOCK         = 145,
	KEY_CODE_EXCLAMATION         = 161,
	KEY_CODE_DOUBLE_QUOTE        = 162,
	KEY_CODE_HASH                = 163,
	KEY_CODE_DOLLAR              = 164,
	KEY_CODE_PERCENT             = 165,
	KEY_CODE_AMPERSAND           = 166,
	KEY_CODE_UNDERSCORE          = 167,
	KEY_CODE_OPEN_PAREN          = 168,
	KEY_CODE_CLOSE_PAREN         = 169,
	KEY_CODE_ASTERISK            = 170,
	KEY_CODE_PLUS                = 171,
	KEY_CODE_PIPE                = 172,
	KEY_CODE_HYPHEN_MINUS        = 173,
	KEY_CODE_OPEN_CURLY_BRACKET  = 174,
	KEY_CODE_CLOSE_CURLY_BRACKET = 175,
	KEY_CODE_TILDE               = 176,
	KEY_CODE_VOLUME_MUTE         = 181,
	KEY_CODE_VOLUME_DOWN         = 182,
	KEY_CODE_VOLUME_UP           = 183,
	KEY_CODE_COMMA               = 188,
	KEY_CODE_PERIOD              = 190,
	KEY_CODE_SLASH               = 191,
	KEY_CODE_BACK_QUOTE          = 192,
	KEY_CODE_OPEN_BRACKET        = 219,
	KEY_CODE_BACK_SLASH          = 220,
	KEY_CODE_CLOSE_BRACKET       = 221,
	KEY_CODE_QUOTE               = 222,
	KEY_CODE_META                = 224,
	KEY_CODE_ALT_GR              = 225,
} key_code_t;

void iron_keyboard_show(void);
void iron_keyboard_hide(void);
bool iron_keyboard_active(void);
void iron_keyboard_set_key_down_callback(void (*value)(int /*key_code*/, void * /*data*/), void *data);
void iron_keyboard_set_key_up_callback(void (*value)(int /*key_code*/, void * /*data*/), void *data);
void iron_keyboard_set_key_press_callback(void (*value)(unsigned /*character*/, void * /*data*/), void *data);
void iron_internal_keyboard_trigger_key_down(int key_code);
void iron_internal_keyboard_trigger_key_up(int key_code);
void iron_internal_keyboard_trigger_key_press(unsigned character);
void iron_mouse_set_press_callback(void (*value)(int /*button*/, int /*x*/, int /*y*/, void * /*data*/), void *data);
void iron_mouse_set_release_callback(void (*value)(int /*button*/, int /*x*/, int /*y*/, void * /*data*/), void *data);
void iron_mouse_set_move_callback(void (*value)(int /*x*/, int /*y*/, int /*movement_x*/, int /*movement_y*/, void * /*data*/), void *data);
void iron_mouse_set_scroll_callback(void (*value)(float /*delta*/, void * /*data*/), void *data);
bool iron_mouse_can_lock(void);
bool iron_mouse_is_locked(void);
void iron_mouse_lock();
void iron_mouse_unlock(void);
void iron_mouse_set_cursor(iron_cursor_t cursor);
void iron_mouse_show(void);
void iron_mouse_hide(void);
void iron_mouse_set_position(int x, int y);
void iron_mouse_get_position(int *x, int *y);
void iron_internal_mouse_trigger_press(int button, int x, int y);
void iron_internal_mouse_trigger_release(int button, int x, int y);
void iron_internal_mouse_trigger_move(int x, int y);
void iron_internal_mouse_trigger_scroll(float delta);
void iron_internal_mouse_window_activated();
void iron_internal_mouse_window_deactivated();
void iron_pen_set_press_callback(void (*value)(int /*x*/, int /*y*/, float /*pressure*/));
void iron_pen_set_move_callback(void (*value)(int /*x*/, int /*y*/, float /*pressure*/));
void iron_pen_set_release_callback(void (*value)(int /*x*/, int /*y*/, float /*pressure*/));
void iron_eraser_set_press_callback(void (*value)(int /*x*/, int /*y*/, float /*pressure*/));
void iron_eraser_set_move_callback(void (*value)(int /*x*/, int /*y*/, float /*pressure*/));
void iron_eraser_set_release_callback(void (*value)(int /*x*/, int /*y*/, float /*pressure*/));
void iron_internal_pen_trigger_move(int x, int y, float pressure);
void iron_internal_pen_trigger_press(int x, int y, float pressure);
void iron_internal_pen_trigger_release(int x, int y, float pressure);
void iron_internal_eraser_trigger_move(int x, int y, float pressure);
void iron_internal_eraser_trigger_press(int x, int y, float pressure);
void iron_internal_eraser_trigger_release(int x, int y, float pressure);
void iron_surface_set_touch_start_callback(void (*value)(int /*index*/, int /*x*/, int /*y*/));
void iron_surface_set_move_callback(void (*value)(int /*index*/, int /*x*/, int /*y*/));
void iron_surface_set_touch_end_callback(void (*value)(int /*index*/, int /*x*/, int /*y*/));
void iron_internal_surface_trigger_touch_start(int index, int x, int y);
void iron_internal_surface_trigger_move(int index, int x, int y);
void iron_internal_surface_trigger_touch_end(int index, int x, int y);

#ifdef WITH_GAMEPAD
#define IRON_GAMEPAD_MAX_COUNT 8
void        iron_gamepad_set_connect_callback(void (*value)(int /*gamepad*/, void        */*userdata*/), void *userdata);
void        iron_gamepad_set_disconnect_callback(void (*value)(int /*gamepad*/, void        */*userdata*/), void *userdata);
void        iron_gamepad_set_axis_callback(void (*value)(int /*gamepad*/, int /*axis*/, float /*value*/, void        */*userdata*/), void *userdata);
void        iron_gamepad_set_button_callback(void (*value)(int /*gamepad*/, int /*button*/, float /*value*/, void        */*userdata*/), void *userdata);
const char *iron_gamepad_vendor(int gamepad);
const char *iron_gamepad_product_name(int gamepad);
bool        iron_gamepad_connected(int gamepad);
void        iron_gamepad_rumble(int gamepad, float left, float right);
void        iron_internal_gamepad_trigger_connect(int gamepad);
void        iron_internal_gamepad_trigger_disconnect(int gamepad);
void        iron_internal_gamepad_trigger_axis(int gamepad, int axis, float value);
void        iron_internal_gamepad_trigger_button(int gamepad, int button, float value);
#endif

i32             iron_sys_command(char *cmd);
string_array_t *iron_open_dialog(char *filter_list, char *default_path, bool open_multiple);
char           *iron_save_dialog(char *filter_list, char *default_path);
