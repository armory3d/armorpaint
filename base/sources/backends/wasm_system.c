
#include <iron_audio.h>
#include <iron_gpu.h>
#include <iron_net.h>
#include <iron_system.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

__attribute__((import_module("imports"), import_name("js_time"))) int                js_time();
__attribute__((import_module("imports"), import_name("js_canvas_w"))) int            js_canvas_w();
__attribute__((import_module("imports"), import_name("js_canvas_h"))) int            js_canvas_h();
__attribute__((import_module("imports"), import_name("js_mouse_set_cursor"))) void   js_mouse_set_cursor(int i);
__attribute__((import_module("imports"), import_name("js_mouse_show"))) void         js_mouse_show();
__attribute__((import_module("imports"), import_name("js_mouse_hide"))) void         js_mouse_hide();
__attribute__((import_module("imports"), import_name("js_window_set_title"))) void   js_window_set_title(const char *title);
__attribute__((import_module("imports"), import_name("js_window_change_mode"))) void js_window_change_mode(int mode);
__attribute__((import_module("imports"), import_name("js_load_url"))) void           js_load_url(const char *url);
__attribute__((import_module("imports"), import_name("js_net_request"))) void
js_net_request(const char *url_base, const char *url_path, const char *data, int port, int method, int callback_id, void *callbackdata, const char *dst_path);

static iron_window_mode_t iron_internal_window_mode = IRON_WINDOW_MODE_WINDOW;
static bool               mouse_hidden              = false;

void iron_display_init(void) {}

int iron_primary_display(void) {
	return 0;
}

int iron_count_displays(void) {
	return 1;
}

iron_display_mode_t iron_display_current_mode(int display_index) {
	iron_display_mode_t mode;
	mode.x               = 0;
	mode.y               = 0;
	mode.width           = 800;
	mode.height          = 600;
	mode.pixels_per_inch = 96;
	mode.frequency       = 60;
	mode.bits_per_pixel  = 32;
	return mode;
}

bool iron_mouse_can_lock(void) {
	return false;
}

void iron_mouse_show() {
	js_mouse_show();
}
void iron_mouse_hide() {
	js_mouse_hide();
}

void iron_mouse_set_cursor(iron_cursor_t cursor_index) {
	if (mouse_hidden) {
		return;
	}
	js_mouse_set_cursor(cursor_index);
}

void iron_mouse_set_position(int x, int y) {}
void iron_mouse_get_position(int *x, int *y) {}
void iron_keyboard_show() {}
void iron_keyboard_hide() {}

void iron_init(iron_window_options_t *win) {
	gpu_init(win->depth_bits, true);
}

bool iron_internal_handle_messages() {
	return true;
}

void iron_set_keep_screen_on(bool on) {}

double iron_frequency(void) {
	return 1000.0;
}

uint64_t iron_timestamp(void) {
	return (uint64_t)(js_time());
}

double iron_time(void) {
	return js_time() / 1000.0;
}

int iron_hardware_threads(void) {
	return 1;
}

void iron_internal_shutdown(void) {}

extern int kickstart(int argc, char **argv);

__attribute__((export_name("wasm_start"))) void wasm_start(void) {
	kickstart(0, NULL);
}

__attribute__((export_name("wasm_update"))) void wasm_update(void) {
	iron_internal_update_callback();
}

__attribute__((export_name("wasm_mousedown"))) void wasm_mousedown(int button, int x, int y) {
	iron_internal_mouse_trigger_press(button, x, y);
}

__attribute__((export_name("wasm_mouseup"))) void wasm_mouseup(int button, int x, int y) {
	iron_internal_mouse_trigger_release(button, x, y);
}

__attribute__((export_name("wasm_mousemove"))) void wasm_mousemove(int x, int y) {
	iron_internal_mouse_trigger_move(x, y);
}

__attribute__((export_name("wasm_wheel"))) void wasm_wheel(float delta) {
	iron_internal_mouse_trigger_scroll(delta / 200.0);
}

__attribute__((export_name("wasm_keydown"))) void wasm_keydown(int key) {
	iron_internal_keyboard_trigger_key_down(key);
}

__attribute__((export_name("wasm_keyup"))) void wasm_keyup(int key) {
	iron_internal_keyboard_trigger_key_up(key);
}

__attribute__((export_name("wasm_keypress"))) void wasm_keypress(int key) {
	iron_internal_keyboard_trigger_key_press(key);
}

__attribute__((export_name("wasm_drop_files"))) void wasm_drop_files(char *str) {
	iron_internal_drop_files_callback(str);
}

int iron_window_x() {
	return 0;
}

int iron_window_y() {
	return 0;
}

int iron_window_width() {
	return js_canvas_w();
}

int iron_window_height() {
	return js_canvas_h();
}

void iron_window_resize(int width, int height) {}
void iron_window_move(int x, int y) {}

void iron_window_change_mode(iron_window_mode_t mode) {
	iron_internal_window_mode = mode;
	js_window_change_mode(mode);
}

void iron_window_destroy() {}
void iron_window_show() {}
void iron_window_hide() {}

void iron_window_set_title(const char *title) {
	js_window_set_title(title);
}

void iron_window_create(iron_window_options_t *win) {}
void iron_window_set_resize_callback(void (*callback)(int x, int y, void *data), void *data) {}
void iron_window_set_close_callback(bool (*callback)(void *), void *data) {}

iron_window_mode_t iron_window_get_mode() {
	return iron_internal_window_mode;
}

volatile uint64_t iron_net_bytes_downloaded = 0;

#define MAX_CALLBACKS 512

static struct {
	iron_https_callback_t callback;
	void                 *data;
	bool                  in_use;
} callbacks[MAX_CALLBACKS];

static int register_callback(iron_https_callback_t callback, void *callbackdata) {
	for (int i = 0; i < MAX_CALLBACKS; i++) {
		if (!callbacks[i].in_use) {
			callbacks[i].callback = callback;
			callbacks[i].data     = callbackdata;
			callbacks[i].in_use   = true;
			return i;
		}
	}
	return -1;
}

__attribute__((export_name("wasm_net_callback"))) void wasm_net_callback(int callback_id, char *buffer) {
	callbacks[callback_id].callback(buffer, callbacks[callback_id].data);
	callbacks[callback_id].in_use = false;
}

void iron_net_request(const char *url_base, const char *url_path, const char *data, int port, int method, iron_https_callback_t callback, void *callbackdata,
                      const char *dst_path) {
	int callback_id = register_callback(callback, callbackdata);
	if (callback_id < 0) {
		callback(NULL, callbackdata);
		return;
	}
	js_net_request(url_base, url_path, data, port, method, callback_id, callbackdata, dst_path);
}

void iron_net_update() {}

bool _save_and_quit_callback_internal() {
	return false;
}

const char *iron_language() {
	return "en";
}

void iron_load_url(const char *url) {
	js_load_url(url);
}

const char *iron_system_id() {
	return "Wasm";
}

const char *iron_internal_save_path() {
	return ".";
}

// void        iron_internal_cut_callback(void);
// void        iron_internal_copy_callback(void);
// void        iron_internal_paste_callback(char *);
