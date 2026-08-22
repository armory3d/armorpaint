
#include "iron_system.h"
#include "iron_alloc.h"
#include "iron_array.h"
#include "iron_draw.h"
#include "iron_file.h"
#include "iron_input.h"
#include "iron_map.h"
#include "iron_string.h"
#include <memory.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef IRON_WINDOWS
#include <backends/windows_mini.h>
#include <backends/windows_system.h>
#endif
#ifdef IRON_ANDROID
#include <android/log.h>
#endif

void iron_log_args(iron_log_level_t level, const char *format, va_list args) {
#ifdef IRON_ANDROID
	va_list args_android_copy;
	va_copy(args_android_copy, args);
	switch (level) {
	case IRON_LOG_LEVEL_INFO:
		__android_log_vprint(ANDROID_LOG_INFO, "Iron", format, args_android_copy);
		break;
	case IRON_LOG_LEVEL_ERROR:
		__android_log_vprint(ANDROID_LOG_ERROR, "Iron", format, args_android_copy);
		break;
	}
	va_end(args_android_copy);
#endif

#ifdef IRON_WINDOWS
	wchar_t buffer[4096];
	iron_microsoft_format(format, args, buffer);
	wcscat(buffer, L"\r\n");
	OutputDebugStringW(buffer);
	DWORD written;
	WriteConsoleW(GetStdHandle(level == IRON_LOG_LEVEL_INFO ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE), buffer, (DWORD)wcslen(buffer), &written, NULL);
#else
	char buffer[4096];
	vsnprintf(buffer, 4090, format, args);
	strcat(buffer, "\n");
#ifdef IRON_WASM
	printf("%s", buffer); ////
#else
	fprintf(level == IRON_LOG_LEVEL_INFO ? stdout : stderr, "%s", buffer);
#endif
#endif
}

void iron_log(const char *format, ...) {
	va_list args;
	va_start(args, format);
	iron_log_args(IRON_LOG_LEVEL_INFO, format == NULL ? "null" : format, args);
	va_end(args);
}

void iron_error(const char *format, ...) {
	{
		va_list args;
		va_start(args, format);
		iron_log_args(IRON_LOG_LEVEL_ERROR, format, args);
		va_end(args);
	}

#ifdef IRON_WINDOWS
	{
		va_list args;
		va_start(args, format);
		wchar_t buffer[4096];
		iron_microsoft_format(format, args, buffer);
		MessageBoxW(NULL, buffer, L"Error", 0);
		va_end(args);
	}
#endif
}

#if !defined(IRON_WASM) && !defined(IRON_ANDROID) && !defined(IRON_WINDOWS)
double iron_time(void) {
	return iron_timestamp() / iron_frequency();
}
#endif

static void (*update_callback)(void)               = NULL;
static void (*foreground_callback)(void *)         = NULL;
static void *foreground_callback_data              = NULL;
static void (*background_callback)(void *)         = NULL;
static void *background_callback_data              = NULL;
static void (*shutdown_callback)(void *)           = NULL;
static void *shutdown_callback_data                = NULL;
static void (*drop_files_callback)(char *, void *) = NULL;
static void *drop_files_callback_data              = NULL;
static void (*cut_callback)(void *)                = NULL;
static void *cut_callback_data                     = NULL;
static void (*copy_callback)(void *)               = NULL;
static void *copy_callback_data                    = NULL;
static void (*paste_callback)(char *, void *)      = NULL;
static void *paste_callback_data                   = NULL;

#if defined(IRON_IOS) || defined(IRON_MACOS)
bool with_autoreleasepool(bool (*f)(void));
#endif

void iron_set_update_callback(void (*callback)(void)) {
	update_callback = callback;
}

void iron_set_foreground_callback(void (*callback)(void *), void *data) {
	foreground_callback      = callback;
	foreground_callback_data = data;
}

void iron_set_background_callback(void (*callback)(void *), void *data) {
	background_callback      = callback;
	background_callback_data = data;
}

void iron_set_shutdown_callback(void (*callback)(void *), void *data) {
	shutdown_callback      = callback;
	shutdown_callback_data = data;
}

void iron_set_drop_files_callback(void (*callback)(char *, void *), void *data) {
	drop_files_callback      = callback;
	drop_files_callback_data = data;
}

void iron_set_cut_callback(void (*callback)(void *), void *data) {
	cut_callback      = callback;
	cut_callback_data = data;
}

void iron_set_copy_callback(void (*callback)(void *), void *data) {
	copy_callback      = callback;
	copy_callback_data = data;
}

void iron_set_paste_callback(void (*callback)(char *, void *), void *data) {
	paste_callback      = callback;
	paste_callback_data = data;
}

void iron_internal_update_callback(void) {
	if (update_callback != NULL) {
		update_callback();
	}
}

void iron_internal_foreground_callback(void) {
	if (foreground_callback != NULL) {
		foreground_callback(foreground_callback_data);
	}
}

void iron_internal_background_callback(void) {
	if (background_callback != NULL) {
		background_callback(background_callback_data);
	}
}

void iron_internal_shutdown_callback(void) {
	if (shutdown_callback != NULL) {
		shutdown_callback(shutdown_callback_data);
	}
}

void iron_internal_drop_files_callback(char *filePath) {
	if (drop_files_callback != NULL) {
		drop_files_callback(filePath, drop_files_callback_data);
	}
}

void iron_internal_cut_callback(void) {
	if (cut_callback != NULL) {
		cut_callback(cut_callback_data);
	}
}

void iron_internal_copy_callback(void) {
	if (copy_callback != NULL) {
		copy_callback(copy_callback_data);
	}
}

void iron_internal_paste_callback(char *value) {
	if (paste_callback != NULL) {
		paste_callback(value, paste_callback_data);
	}
}

static bool running                = false;
static char application_name[1024] = {"Iron Application"};

const char *iron_application_name(void) {
	return application_name;
}

void iron_set_app_name(const char *name) {
	strcpy(application_name, name);
}

void iron_stop(void) {
	running = false;
}

bool iron_internal_frame(void) {
	iron_internal_update_callback();
	iron_internal_handle_messages();
	return running;
}

void iron_start(void) {
	running = true;

#if !defined(IRON_WASM)

#if defined(IRON_IOS) || defined(IRON_MACOS)
	while (with_autoreleasepool(iron_internal_frame)) {
	}
#else
	while (iron_internal_frame()) {
	}
#endif
	iron_internal_shutdown();
#endif
}

static uint8_t *current_file      = NULL;
static size_t   current_file_size = 0;

bool iron_save_file_loaded(void) {
	return true;
}

uint8_t *iron_get_save_file(void) {
	return current_file;
}

size_t iron_get_save_file_size(void) {
	return current_file_size;
}

void iron_load_save_file(const char *filename) {
	free(current_file);
	current_file      = NULL;
	current_file_size = 0;

	iron_file_reader_t reader;
	if (iron_file_reader_open(&reader, filename, IRON_FILE_TYPE_SAVE)) {
		current_file_size = iron_file_reader_size(&reader);
		current_file      = (uint8_t *)malloc(current_file_size);
		iron_file_reader_read(&reader, current_file, current_file_size);
		iron_file_reader_close(&reader);
	}
}

void iron_save_save_file(const char *filename, uint8_t *data, size_t size) {
	iron_file_writer_t writer;
	if (iron_file_writer_open(&writer, filename)) {
		iron_file_writer_write(&writer, data, (int)size);
		iron_file_writer_close(&writer);
	}
}

bool iron_save_is_saving(void) {
	return false;
}

#if !defined(IRON_WINDOWS) && !defined(IRON_LINUX) && !defined(IRON_MACOS)
void iron_copy_to_clipboard(const char *text) {
	iron_log("Oh no, iron_copy_to_clipboard is not implemented for this system.");
}
#endif

static void (*keyboard_key_down_callback)(int /*key_code*/, void * /*data*/)        = NULL;
static void *keyboard_key_down_callback_data                                        = NULL;
static void (*keyboard_key_up_callback)(int /*key_code*/, void * /*data*/)          = NULL;
static void *keyboard_key_up_callback_data                                          = NULL;
static void (*keyboard_key_press_callback)(unsigned /*character*/, void * /*data*/) = NULL;
static void *keyboard_key_press_callback_data                                       = NULL;

void iron_keyboard_set_key_down_callback(void (*value)(int /*key_code*/, void * /*data*/), void *data) {
	keyboard_key_down_callback      = value;
	keyboard_key_down_callback_data = data;
}

void iron_keyboard_set_key_up_callback(void (*value)(int /*key_code*/, void * /*data*/), void *data) {
	keyboard_key_up_callback      = value;
	keyboard_key_up_callback_data = data;
}

void iron_keyboard_set_key_press_callback(void (*value)(unsigned /*character*/, void * /*data*/), void *data) {
	keyboard_key_press_callback      = value;
	keyboard_key_press_callback_data = data;
}

void iron_internal_keyboard_trigger_key_down(int key_code) {
	if (keyboard_key_down_callback != NULL) {
		keyboard_key_down_callback(key_code, keyboard_key_down_callback_data);
	}
}

void iron_internal_keyboard_trigger_key_up(int key_code) {
	if (keyboard_key_up_callback != NULL) {
		keyboard_key_up_callback(key_code, keyboard_key_up_callback_data);
	}
}

void iron_internal_keyboard_trigger_key_press(unsigned character) {
	if (keyboard_key_press_callback != NULL) {
		keyboard_key_press_callback(character, keyboard_key_press_callback_data);
	}
}

static void (*mouse_press_callback)(int /*button*/, int /*x*/, int /*y*/, void * /*data*/)                      = NULL;
static void *mouse_press_callback_data                                                                          = NULL;
static void (*mouse_release_callback)(int /*button*/, int /*x*/, int /*y*/, void * /*data*/)                    = NULL;
static void *mouse_release_callback_data                                                                        = NULL;
static void (*mouse_move_callback)(int /*x*/, int /*y*/, int /*movementX*/, int /*movementY*/, void * /*data*/) = NULL;
static void *mouse_move_callback_data                                                                           = NULL;
static void (*mouse_scroll_callback)(float /*delta*/, void * /*data*/)                                          = NULL;
static void *mouse_scroll_callback_data                                                                         = NULL;

void iron_mouse_set_press_callback(void (*value)(int /*button*/, int /*x*/, int /*y*/, void * /*data*/), void *data) {
	mouse_press_callback      = value;
	mouse_press_callback_data = data;
}

void iron_mouse_set_release_callback(void (*value)(int /*button*/, int /*x*/, int /*y*/, void * /*data*/), void *data) {
	mouse_release_callback      = value;
	mouse_release_callback_data = data;
}

void iron_mouse_set_move_callback(void (*value)(int /*x*/, int /*y*/, int /*movement_x*/, int /*movement_y*/, void * /*data*/), void *data) {
	mouse_move_callback      = value;
	mouse_move_callback_data = data;
}

void iron_mouse_set_scroll_callback(void (*value)(float /*delta*/, void * /*data*/), void *data) {
	mouse_scroll_callback      = value;
	mouse_scroll_callback_data = data;
}

void iron_internal_mouse_trigger_release(int button, int x, int y) {
	if (mouse_release_callback != NULL) {
		mouse_release_callback(button, x, y, mouse_release_callback_data);
	}
}

void iron_internal_mouse_trigger_scroll(float delta) {
	if (mouse_scroll_callback != NULL) {
		mouse_scroll_callback(delta, mouse_scroll_callback_data);
	}
}

void iron_internal_mouse_window_activated() {
	if (iron_mouse_is_locked()) {
		iron_mouse_hide();
	}
}
void iron_internal_mouse_window_deactivated() {
	if (iron_mouse_is_locked()) {
		iron_mouse_show();
	}
}

static bool moved    = false;
static bool locked   = false;
static int  preLockX = 0;
static int  preLockY = 0;
static int  lastX    = 0;
static int  lastY    = 0;

void iron_internal_mouse_trigger_press(int button, int x, int y) {
	lastX = x;
	lastY = y;
	if (mouse_press_callback != NULL) {
		mouse_press_callback(button, x, y, mouse_press_callback_data);
	}
}

void iron_internal_mouse_trigger_move(int x, int y) {
	int movementX = 0;
	int movementY = 0;
	if (iron_mouse_is_locked()) {
		movementX = x - preLockX;
		movementY = y - preLockY;
		if (movementX != 0 || movementY != 0) {
			iron_mouse_set_position(preLockX, preLockY);
			x = preLockX;
			y = preLockY;
		}
	}
	else if (moved) {
		movementX = x - lastX;
		movementY = y - lastY;
	}
	moved = true;

	lastX = x;
	lastY = y;
	if (mouse_move_callback != NULL && (movementX != 0 || movementY != 0)) {
		mouse_move_callback(x, y, movementX, movementY, mouse_move_callback_data);
	}
}

bool iron_mouse_is_locked(void) {
	return locked;
}

void iron_mouse_lock() {
	if (iron_mouse_is_locked() || !iron_mouse_can_lock()) {
		return;
	}
	locked = true;
	iron_mouse_get_position(&preLockX, &preLockY);
	iron_mouse_hide();
}

void iron_mouse_unlock(void) {
	if (!iron_mouse_is_locked() || !iron_mouse_can_lock()) {
		return;
	}
	moved  = false;
	locked = false;
	iron_mouse_set_position(preLockX, preLockY);
	iron_mouse_show();
}

static void (*pen_press_callback)(int /*x*/, int /*y*/, float /*pressure*/)   = NULL;
static void (*pen_move_callback)(int /*x*/, int /*y*/, float /*pressure*/)    = NULL;
static void (*pen_release_callback)(int /*x*/, int /*y*/, float /*pressure*/) = NULL;

static void (*eraser_press_callback)(int /*x*/, int /*y*/, float /*pressure*/)   = NULL;
static void (*eraser_move_callback)(int /*x*/, int /*y*/, float /*pressure*/)    = NULL;
static void (*eraser_release_callback)(int /*x*/, int /*y*/, float /*pressure*/) = NULL;

void iron_pen_set_press_callback(void (*value)(int /*x*/, int /*y*/, float /*pressure*/)) {
	pen_press_callback = value;
}

void iron_pen_set_move_callback(void (*value)(int /*x*/, int /*y*/, float /*pressure*/)) {
	pen_move_callback = value;
}

void iron_pen_set_release_callback(void (*value)(int /*x*/, int /*y*/, float /*pressure*/)) {
	pen_release_callback = value;
}

void iron_eraser_set_press_callback(void (*value)(int /*x*/, int /*y*/, float /*pressure*/)) {
	eraser_press_callback = value;
}

void iron_eraser_set_move_callback(void (*value)(int /*x*/, int /*y*/, float /*pressure*/)) {
	eraser_move_callback = value;
}

void iron_eraser_set_release_callback(void (*value)(int /*x*/, int /*y*/, float /*pressure*/)) {
	eraser_release_callback = value;
}

void iron_internal_pen_trigger_press(int x, int y, float pressure) {
	if (pen_press_callback != NULL) {
		pen_press_callback(x, y, pressure);
	}
}

void iron_internal_pen_trigger_move(int x, int y, float pressure) {
	if (pen_move_callback != NULL) {
		pen_move_callback(x, y, pressure);
	}
}

void iron_internal_pen_trigger_release(int x, int y, float pressure) {
	if (pen_release_callback != NULL) {
		pen_release_callback(x, y, pressure);
	}
}

void iron_internal_eraser_trigger_press(int x, int y, float pressure) {
	if (eraser_press_callback != NULL) {
		eraser_press_callback(x, y, pressure);
	}
}

void iron_internal_eraser_trigger_move(int x, int y, float pressure) {
	if (eraser_move_callback != NULL) {
		eraser_move_callback(x, y, pressure);
	}
}

void iron_internal_eraser_trigger_release(int x, int y, float pressure) {
	if (eraser_release_callback != NULL) {
		eraser_release_callback(x, y, pressure);
	}
}

static void (*surface_touch_start_callback)(int /*index*/, int /*x*/, int /*y*/) = NULL;
static void (*surface_move_callback)(int /*index*/, int /*x*/, int /*y*/)        = NULL;
static void (*surface_touch_end_callback)(int /*index*/, int /*x*/, int /*y*/)   = NULL;

void iron_surface_set_touch_start_callback(void (*value)(int /*index*/, int /*x*/, int /*y*/)) {
	surface_touch_start_callback = value;
}

void iron_surface_set_move_callback(void (*value)(int /*index*/, int /*x*/, int /*y*/)) {
	surface_move_callback = value;
}

void iron_surface_set_touch_end_callback(void (*value)(int /*index*/, int /*x*/, int /*y*/)) {
	surface_touch_end_callback = value;
}

void iron_internal_surface_trigger_touch_start(int index, int x, int y) {
	if (surface_touch_start_callback != NULL) {
		surface_touch_start_callback(index, x, y);
	}
}

void iron_internal_surface_trigger_move(int index, int x, int y) {
	if (surface_move_callback != NULL) {
		surface_move_callback(index, x, y);
	}
}

void iron_internal_surface_trigger_touch_end(int index, int x, int y) {
	if (surface_touch_end_callback != NULL) {
		surface_touch_end_callback(index, x, y);
	}
}

#ifdef WITH_GAMEPAD

static void (*gamepad_connect_callback)(int /*gamepad*/, void * /*userdata*/)                                 = NULL;
static void *gamepad_connect_callback_userdata                                                                = NULL;
static void (*gamepad_disconnect_callback)(int /*gamepad*/, void * /*userdata*/)                              = NULL;
static void *gamepad_disconnect_callback_userdata                                                             = NULL;
static void (*gamepad_axis_callback)(int /*gamepad*/, int /*axis*/, float /*value*/, void * /*userdata*/)     = NULL;
static void *gamepad_axis_callback_userdata                                                                   = NULL;
static void (*gamepad_button_callback)(int /*gamepad*/, int /*button*/, float /*value*/, void * /*userdata*/) = NULL;
static void *gamepad_button_callback_userdata                                                                 = NULL;

void iron_gamepad_set_connect_callback(void (*value)(int /*gamepad*/, void * /*userdata*/), void *userdata) {
	gamepad_connect_callback          = value;
	gamepad_connect_callback_userdata = userdata;
}

void iron_gamepad_set_disconnect_callback(void (*value)(int /*gamepad*/, void * /*userdata*/), void *userdata) {
	gamepad_disconnect_callback          = value;
	gamepad_disconnect_callback_userdata = userdata;
}

void iron_gamepad_set_axis_callback(void (*value)(int /*gamepad*/, int /*axis*/, float /*value*/, void * /*userdata*/), void *userdata) {
	gamepad_axis_callback          = value;
	gamepad_axis_callback_userdata = userdata;
}

void iron_gamepad_set_button_callback(void (*value)(int /*gamepad*/, int /*button*/, float /*value*/, void * /*userdata*/), void *userdata) {
	gamepad_button_callback          = value;
	gamepad_button_callback_userdata = userdata;
}

void iron_internal_gamepad_trigger_connect(int gamepad) {
	if (gamepad_connect_callback != NULL) {
		gamepad_connect_callback(gamepad, gamepad_connect_callback_userdata);
	}
}

void iron_internal_gamepad_trigger_disconnect(int gamepad) {
	if (gamepad_disconnect_callback != NULL) {
		gamepad_disconnect_callback(gamepad, gamepad_disconnect_callback_userdata);
	}
}

void iron_internal_gamepad_trigger_axis(int gamepad, int axis, float value) {
	if (gamepad_axis_callback != NULL) {
		gamepad_axis_callback(gamepad, axis, value, gamepad_axis_callback_userdata);
	}
}

void iron_internal_gamepad_trigger_button(int gamepad, int button, float value) {
	if (gamepad_button_callback != NULL) {
		gamepad_button_callback(gamepad, button, value, gamepad_button_callback_userdata);
	}
}

#endif

i32 iron_sys_command(char *cmd) {
#ifdef IRON_WINDOWS

	int      wlen = MultiByteToWideChar(CP_UTF8, 0, cmd, -1, NULL, 0);
	wchar_t *wstr = malloc(sizeof(wchar_t) * wlen);
	MultiByteToWideChar(CP_UTF8, 0, cmd, -1, wstr, wlen);
	wchar_t comspec[MAX_PATH];
	GetEnvironmentVariableW(L"ComSpec", comspec, MAX_PATH);
	wchar_t cmdline[2048];
	swprintf(cmdline, 2048, L"\"%s\" /c \"%s\"", comspec, wstr);
	STARTUPINFO si;
	memset(&si, 0, sizeof(si));
	si.cb          = sizeof(si);
	si.dwFlags     = STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_HIDE;
	PROCESS_INFORMATION pi;
	memset(&pi, 0, sizeof(pi));
	CreateProcessW(NULL, cmdline, NULL, NULL, false, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
	free(wstr);
	WaitForSingleObject(pi.hProcess, INFINITE);
	DWORD exit_code = 0;
	GetExitCodeProcess(pi.hProcess, &exit_code);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	int result = (int)exit_code;

#elif defined(IRON_IOS)
	int result = 0;
#else
	int result = system(cmd);
#endif
	return result;
}

#ifdef WITH_NFD

#include <nfd.h>

string_array_t *iron_open_dialog(char *filter_list, char *default_path, bool open_multiple) {
	nfdpathset_t out_paths;
	nfdchar_t   *out_path;
	nfdresult_t  result = open_multiple ? NFD_OpenDialogMultiple(filter_list, default_path, &out_paths) : NFD_OpenDialog(filter_list, default_path, &out_path);

	if (result == NFD_OKAY) {
		int             path_count = open_multiple ? (int)NFD_PathSet_GetCount(&out_paths) : 1;
		string_array_t *result     = any_array_create(path_count);

		if (open_multiple) {
			for (int i = 0; i < path_count; ++i) {
				nfdchar_t *out_path = NFD_PathSet_GetPath(&out_paths, i);
				result->buffer[i]   = out_path;
			}
			// NFD_PathSet_Free(&out_paths);
		}
		else {
			result->buffer[0] = out_path;
			// free(out_path);
		}
		return result;
	}
	return NULL;
}

static char iron_save_dialog_path[512];

char *iron_save_dialog(char *filter_list, char *default_path) {
	nfdchar_t  *out_path = NULL;
	nfdresult_t result   = NFD_SaveDialog(filter_list, default_path, &out_path);
	if (result == NFD_OKAY) {
		strcpy(iron_save_dialog_path, out_path);
		free(out_path);
		return iron_save_dialog_path;
	}
	return NULL;
}

#elif defined(IRON_ANDROID)

#include "backends/android_file_dialog.h"

extern char temp_string[1024 * 128];

string_array_t *iron_open_dialog(char *filter_list, char *default_path, bool open_multiple) {
	AndroidFileDialogOpen();
	return NULL;
}

char *iron_save_dialog(char *filter_list, char *default_path) {
	wchar_t *out_path = AndroidFileDialogSave();
	wcstombs(temp_string, out_path, sizeof(temp_string));
	return temp_string;
}

#elif defined(IRON_IOS)

#include "backends/ios_file_dialog.h"
#include <wchar.h>

extern char temp_string[1024 * 128];

string_array_t *iron_open_dialog(char *filter_list, char *default_path, bool open_multiple) {
	// Once finished drop_files callback is called
	IOSFileDialogOpen();
	return NULL;
}

char *iron_save_dialog(char *filter_list, char *default_path) {
	// Path to app document directory
	wchar_t *out_path = IOSFileDialogSave();
	wcstombs(temp_string, out_path, sizeof(temp_string));
	return temp_string;
}

#elif defined(IRON_WASM)

__attribute__((import_module("imports"), import_name("js_open_dialog"))) void  js_open_dialog();
__attribute__((import_module("imports"), import_name("js_save_dialog"))) char *js_save_dialog();

string_array_t *iron_open_dialog(char *filter_list, char *default_path, bool open_multiple) {
	js_open_dialog();
	return NULL;
}

char *iron_save_dialog(char *filter_list, char *default_path) {
	return js_save_dialog();
}

#endif

void          _iron_init(iron_window_options_t *ops);
void          _iron_set_update_callback(void (*callback)(void));
void          _iron_set_drop_files_callback(void (*callback)(char *));
void          iron_set_application_state_callback(void (*on_foreground)(void), void (*on_background)(void), void (*on_shutdown)(void));
void          iron_set_keyboard_down_callback(void (*callback)(int));
void          iron_set_keyboard_up_callback(void (*callback)(int));
void          iron_set_mouse_down_callback(void (*callback)(int, int, int));
void          iron_set_mouse_up_callback(void (*callback)(int, int, int));
void          iron_set_mouse_move_callback(void (*callback)(int, int, int, int));
void          iron_set_mouse_wheel_callback(void (*callback)(float));
void          iron_set_touch_down_callback(void (*callback)(int, int, int));
void          iron_set_touch_up_callback(void (*callback)(int, int, int));
void          iron_set_touch_move_callback(void (*callback)(int, int, int));
void          iron_set_pen_down_callback(void (*callback)(int, int, float));
void          iron_set_pen_up_callback(void (*callback)(int, int, float));
void          iron_set_pen_move_callback(void (*callback)(int, int, float));
buffer_t     *iron_load_blob(char *file);
i32           iron_display_width(i32 index);
i32           iron_display_height(i32 index);
i32           iron_display_frequency(i32 index);
i32           iron_display_ppi(i32 index);
bool          iron_display_is_primary(i32 index);
gpu_shader_t *gpu_create_shader(buffer_t *data, i32 shader_type);
void          iron_delete_blob(buffer_t *buffer);
char         *data_path(void);

#ifdef WITH_GAMEPAD
void iron_set_gamepad_axis_callback(void (*callback)(int, int, float));
void iron_set_gamepad_button_callback(void (*callback)(int, int, float));
#endif

any_map_t   *_sys_shaders    = NULL;
f32          _sys_start_time = 0.0f;
char         _sys_window_title[1024];
any_array_t *_sys_foreground_listeners = NULL;
any_array_t *_sys_background_listeners = NULL;
any_array_t *_sys_shutdown_listeners   = NULL;
any_array_t *_sys_drop_files_listeners = NULL;
any_array_t *_sys_on_next_frames       = NULL;
any_array_t *_sys_on_end_frames        = NULL;
any_array_t *_sys_on_updates           = NULL;
i32          _sys_lastw                = -1;
i32          _sys_lasth                = -1;

void (*sys_on_resize)(void) = NULL;
i32 (*sys_on_w)(void)       = NULL;
i32 (*sys_on_h)(void)       = NULL;
i32 (*sys_on_x)(void)       = NULL;
i32 (*sys_on_y)(void)       = NULL;

static f32 _sys_time_last       = 0.0f;
static f32 _sys_time_real_delta = 0.0f;
static i32 _sys_time_frequency  = -1;

static sys_callback_t *_sys_callback_create(void (*f)(void)) {
	sys_callback_t *cb = calloc(1, sizeof(sys_callback_t));
	cb->f              = f;
	return cb;
}

static callback_t *_callback_create(void (*f)(void *data), void *data) {
	callback_t *cb = calloc(1, sizeof(callback_t));
	cb->f          = f;
	cb->data       = data;
	return cb;
}

void sys_start(iron_window_options_t *ops) {
	_sys_foreground_listeners = any_array_create(0);
	_sys_background_listeners = any_array_create(0);
	_sys_shutdown_listeners   = any_array_create(0);
	_sys_drop_files_listeners = any_array_create(0);
	_sys_on_next_frames       = any_array_create(0);
	_sys_on_end_frames        = any_array_create(0);
	_sys_on_updates           = any_array_create(0);
	_sys_shaders              = any_map_create();

	_iron_init(ops);

	_sys_start_time = (f32)iron_time();

	char *dp  = data_path();
	char *ext = sys_shader_ext();

	char path_image_vert[512];
	char path_image_frag[512];
	char path_rect_vert[512];
	char path_rect_frag[512];
	char path_tris_vert[512];
	char path_tris_frag[512];
	char path_text_vert[512];
	char path_text_frag[512];

	strcpy(path_image_vert, dp);
	strcat(path_image_vert, "draw_image.vert");
	strcat(path_image_vert, ext);
	strcpy(path_image_frag, dp);
	strcat(path_image_frag, "draw_image.frag");
	strcat(path_image_frag, ext);
	strcpy(path_rect_vert, dp);
	strcat(path_rect_vert, "draw_rect.vert");
	strcat(path_rect_vert, ext);
	strcpy(path_rect_frag, dp);
	strcat(path_rect_frag, "draw_rect.frag");
	strcat(path_rect_frag, ext);
	strcpy(path_tris_vert, dp);
	strcat(path_tris_vert, "draw_tris.vert");
	strcat(path_tris_vert, ext);
	strcpy(path_tris_frag, dp);
	strcat(path_tris_frag, "draw_tris.frag");
	strcat(path_tris_frag, ext);
	strcpy(path_text_vert, dp);
	strcat(path_text_vert, "draw_text.vert");
	strcat(path_text_vert, ext);
	strcpy(path_text_frag, dp);
	strcat(path_text_frag, "draw_text.frag");
	strcat(path_text_frag, ext);

	buffer_t *draw_blobs[8] = {iron_load_blob(path_image_vert), iron_load_blob(path_image_frag), iron_load_blob(path_rect_vert),
	                           iron_load_blob(path_rect_frag),  iron_load_blob(path_tris_vert),  iron_load_blob(path_tris_frag),
	                           iron_load_blob(path_text_vert),  iron_load_blob(path_text_frag)};
	draw_init(draw_blobs[0], draw_blobs[1], draw_blobs[2], draw_blobs[3], draw_blobs[4], draw_blobs[5], draw_blobs[6], draw_blobs[7]);
	for (int i = 0; i < 8; ++i) {
		iron_delete_blob(draw_blobs[i]);
	}

	_iron_set_update_callback(sys_render);
	_iron_set_drop_files_callback(sys_drop_files_callback);
	iron_set_application_state_callback(sys_foreground_callback, sys_background_callback, sys_shutdown_callback);
	iron_set_keyboard_down_callback(sys_keyboard_down_callback);
	iron_set_keyboard_up_callback(sys_keyboard_up_callback);
	iron_set_mouse_down_callback(sys_mouse_down_callback);
	iron_set_mouse_up_callback(sys_mouse_up_callback);
	iron_set_mouse_move_callback(sys_mouse_move_callback);
	iron_set_mouse_wheel_callback(sys_mouse_wheel_callback);
	iron_set_touch_down_callback(sys_touch_down_callback);
	iron_set_touch_up_callback(sys_touch_up_callback);
	iron_set_touch_move_callback(sys_touch_move_callback);
	iron_set_pen_down_callback(sys_pen_down_callback);
	iron_set_pen_up_callback(sys_pen_up_callback);
	iron_set_pen_move_callback(sys_pen_move_callback);
#ifdef WITH_GAMEPAD
	iron_set_gamepad_axis_callback(sys_gamepad_axis_callback);
	iron_set_gamepad_button_callback(sys_gamepad_button_callback);
#endif
	input_register();
}

void sys_notify_on_app_state(void (*on_foreground)(void), void (*on_background)(void), void (*on_shutdown)(void)) {
	if (on_foreground != NULL) {
		any_array_push(_sys_foreground_listeners, _sys_callback_create(on_foreground));
	}
	if (on_background != NULL) {
		any_array_push(_sys_background_listeners, _sys_callback_create(on_background));
	}
	if (on_shutdown != NULL) {
		any_array_push(_sys_shutdown_listeners, _sys_callback_create(on_shutdown));
	}
}

void sys_notify_on_drop_files(void (*drop_files_listener)(char *s)) {
	sys_string_callback_t *cb = calloc(1, sizeof(sys_string_callback_t));
	cb->f                     = drop_files_listener;
	any_array_push(_sys_drop_files_listeners, cb);
}

void sys_foreground(void) {
	for (i32 i = 0; i < (i32)_sys_foreground_listeners->length; ++i) {
		sys_callback_t *cb = _sys_foreground_listeners->buffer[i];
		cb->f();
	}
	input_on_foreground();
}

void sys_background(void) {
	for (i32 i = 0; i < (i32)_sys_background_listeners->length; ++i) {
		sys_callback_t *cb = _sys_background_listeners->buffer[i];
		cb->f();
	}
}

void sys_shutdown(void) {
	for (i32 i = 0; i < (i32)_sys_shutdown_listeners->length; ++i) {
		sys_callback_t *cb = _sys_shutdown_listeners->buffer[i];
		cb->f();
	}
}

void sys_drop_files(char *file_path) {
	for (i32 i = 0; i < (i32)_sys_drop_files_listeners->length; ++i) {
		sys_string_callback_t *cb = _sys_drop_files_listeners->buffer[i];
		cb->f(file_path);
	}
}

f32 sys_time(void) {
	return (f32)iron_time() - _sys_start_time;
}

void sys_drop_files_callback(char *file_path) {
	sys_drop_files(file_path);
}

void sys_foreground_callback(void) {
	sys_foreground();
}

void sys_background_callback(void) {
	sys_background();
}

void sys_shutdown_callback(void) {
	sys_shutdown();
}

void sys_keyboard_down_callback(i32 code) {
	keyboard_down_listener(code);
}

void sys_keyboard_up_callback(i32 code) {
	keyboard_up_listener(code);
}

void sys_mouse_down_callback(i32 button, i32 x, i32 y) {
	mouse_down_listener(button, x, y);
}

void sys_mouse_up_callback(i32 button, i32 x, i32 y) {
	mouse_up_listener(button, x, y);
}

void sys_mouse_move_callback(i32 x, i32 y, i32 mx, i32 my) {
	mouse_move_listener(x, y, mx, my);
}

void sys_mouse_wheel_callback(f32 delta) {
	mouse_wheel_listener(delta);
}

void sys_touch_down_callback(i32 index, i32 x, i32 y) {
#if defined(IRON_ANDROID) || defined(IRON_IOS)
	mouse_on_touch_down(index, x, y);
#endif
}

void sys_touch_up_callback(i32 index, i32 x, i32 y) {
#if defined(IRON_ANDROID) || defined(IRON_IOS)
	mouse_on_touch_up(index, x, y);
#endif
}

void sys_touch_move_callback(i32 index, i32 x, i32 y) {
#if defined(IRON_ANDROID) || defined(IRON_IOS)
	mouse_on_touch_move(index, x, y);
#endif
}

void sys_pen_down_callback(i32 x, i32 y, f32 pressure) {
	pen_down_listener(x, y, pressure);
}

void sys_pen_up_callback(i32 x, i32 y, f32 pressure) {
	pen_up_listener(x, y, pressure);
}

void sys_pen_move_callback(i32 x, i32 y, f32 pressure) {
	pen_move_listener(x, y, pressure);
}

#ifdef WITH_GAMEPAD
void sys_gamepad_axis_callback(i32 gamepad, i32 axis, f32 value) {
	gamepad_axis_listener(gamepad, axis, value);
}

void sys_gamepad_button_callback(i32 gamepad, i32 button, f32 value) {
	gamepad_button_listener(gamepad, button, value);
}
#endif

char *sys_title(void) {
	return _sys_window_title;
}

void sys_title_set(char *value) {
	iron_window_set_title(value);
	strcpy(_sys_window_title, value);
}

i32 sys_display_primary_id(void) {
	for (i32 i = 0; i < iron_count_displays(); ++i) {
		if (iron_display_is_primary(i)) {
			return i;
		}
	}
	return 0;
}

i32 sys_display_width(void) {
	return iron_display_width(sys_display_primary_id());
}

i32 sys_display_height(void) {
	return iron_display_height(sys_display_primary_id());
}

i32 sys_display_frequency(void) {
	return iron_display_frequency(sys_display_primary_id());
}

i32 sys_display_ppi(void) {
	return iron_display_ppi(sys_display_primary_id());
}

char *sys_shader_ext(void) {
#if defined(IRON_VULKAN)
	return ".spirv";
#elif defined(IRON_METAL)
	return ".metal";
#elif defined(IRON_WASM)
	return ".wgsl";
#else
	return ".d3d11";
#endif
}

gpu_shader_t *sys_get_shader(char *name) {
	gpu_shader_t *shader = any_map_get(_sys_shaders, name);
	if (shader == NULL) {
		char  path[512];
		char *dp  = data_path();
		char *ext = sys_shader_ext();
		strcpy(path, dp);
		strcat(path, name);
		strcat(path, ext);
		gpu_shader_type_t shader_type = ends_with(name, ".frag") ? GPU_SHADER_TYPE_FRAGMENT : GPU_SHADER_TYPE_VERTEX;
		buffer_t         *blob        = iron_load_blob(path);
		shader                        = gpu_create_shader(blob, (i32)shader_type);
		iron_delete_blob(blob);
		any_map_set(_sys_shaders, name, shader);
	}
	return shader;
}

i32 sys_w(void) {
	if (sys_on_w != NULL) {
		return sys_on_w();
	}
	return iron_window_width();
}

i32 sys_h(void) {
	if (sys_on_h != NULL) {
		return sys_on_h();
	}
	return iron_window_height();
}

i32 sys_x(void) {
	if (sys_on_x != NULL) {
		return sys_on_x();
	}
	return 0;
}

i32 sys_y(void) {
	if (sys_on_y != NULL) {
		return sys_on_y();
	}
	return 0;
}

f32 sys_delta(void) {
	if (_sys_time_frequency < 0) {
		_sys_time_frequency = sys_display_frequency();
	}
	return 1.0f / (f32)_sys_time_frequency;
}

f32 sys_real_delta(void) {
	return _sys_time_real_delta;
}

static void _sys_run_callbacks(any_array_t *cbs, i32 len) {
	for (i32 i = 0; i < len; ++i) {
		callback_t *cb = cbs->buffer[i];
		if (cb != NULL) { // Callback may have been removed
			cb->f(cb->data);
		}
	}
}

static void _sys_run_callbacks_once(any_array_t *cbs, i32 len) {
	for (i32 i = 0; i < len; ++i) {
		callback_t *cb = cbs->buffer[i];
		if (cb != NULL) {
			cb->f(cb->data);
			free(cb);
		}
	}
	array_splice(cbs, 0, len);
}

void sys_render(void) {
	i32 len = _sys_on_next_frames->length;
	_sys_run_callbacks_once(_sys_on_next_frames, len);

	scene_render_frame();
	_sys_run_callbacks(_sys_on_updates, _sys_on_updates->length);

	len = _sys_on_end_frames->length;
	_sys_run_callbacks_once(_sys_on_end_frames, len);

	input_end_frame();

	// Rebuild projection on window resize
	if (_sys_lastw == -1) {
		_sys_lastw = sys_w();
		_sys_lasth = sys_h();
	}
	if (_sys_lastw != sys_w() || _sys_lasth != sys_h()) {
		if (sys_on_resize != NULL) {
			sys_on_resize();
		}
	}
	_sys_lastw           = sys_w();
	_sys_lasth           = sys_h();
	_sys_time_real_delta = sys_time() - _sys_time_last;
	_sys_time_last       = sys_time();
}

// Hooks

void sys_notify_on_update(void (*f)(void *data), void *data) {
	any_array_push(_sys_on_updates, _callback_create(f, data));
}

void sys_notify_on_next_frame(void (*f)(void *data), void *data) {
	any_array_push(_sys_on_next_frames, _callback_create(f, data));
}

void sys_notify_on_end_frame(void (*f)(void *data), void *data) {
	any_array_push(_sys_on_end_frames, _callback_create(f, data));
}

static void _sys_remove_callback(any_array_t *ar, void (*f)(void *data)) {
	for (i32 i = 0; i < (i32)ar->length; ++i) {
		callback_t *cb = ar->buffer[i];
		if (cb->f == f) {
			array_splice(ar, i, 1);
			break;
		}
	}
}

void sys_remove_update(void (*f)(void *data)) {
	_sys_remove_callback(_sys_on_updates, f);
}

char *sys_buffer_to_string(buffer_t *b) {
	char *str = string_alloc(b->length + 1);
	memcpy(str, b->buffer, b->length);
	return str;
}

buffer_t *sys_string_to_buffer(char *str) {
	u8_array_t *b = u8_array_create(string_length(str));
	memcpy(b->buffer, str, string_length(str));
	return b;
}
