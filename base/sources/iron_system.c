
#include "iron_system.h"
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
#include "iron_file.h"
#include <memory.h>
#include <stddef.h>

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
