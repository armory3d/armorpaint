#pragma once

#pragma clang diagnostic ignored "-Wincompatible-pointer-types"

#include "const_data.h"
#include "iron_armpack.h"
#include "iron_array.h"
#include "iron_draw.h"
#include "iron_file.h"
#include "iron_gc.h"
#include "iron_gpu.h"
#include "iron_json.h"
#include "iron_lz4.h"
#include "iron_map.h"
#include "iron_math.h"
#include "iron_obj.h"
#include "iron_path.h"
#include "iron_string.h"
#include "iron_system.h"
#include "iron_thread.h"
#include "iron_ui.h"
#include "libs/minic.h"
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#ifdef IRON_WINDOWS
#include <Windows.h>
#elif defined(IRON_WASM)
#else
#include <sys/stat.h>
#endif
#ifdef IRON_AUDIO
#include "iron_audio.h"
#endif
#ifdef WITH_BC7
#include "libs/bc7enc.h"
#endif
#ifdef WITH_EMBED
#include EMBED_H_PATH
#endif

#define ID__(x, y) x ":" #y
#define ID_(x, y)  ID__(x, y)
#define __ID__     ID_(__FILE__, __LINE__)

int    _argc;
char **_argv;

#ifdef WITH_EMBED
buffer_t *embed_get(char *key) {
#ifdef IRON_WINDOWS
	key = string_replace_all(key, "\\", "/");
#endif
	for (int i = 0; i < embed_count; ++i) {
		if (strcmp(embed_keys[i], key) == 0) {
			buffer_t *buffer = buffer_create(0);
			buffer->buffer   = embed_values[i];
			buffer->length   = embed_sizes[i];
			return buffer;
		}
	}
	return NULL;
}
#endif

void _kickstart();
bool enable_window = true;
bool in_background = false;
int  paused_frames = 0;
#ifdef IDLE_SLEEP
bool input_down         = false;
int  last_window_width  = 0;
int  last_window_height = 0;
#endif
char temp_string[1024 * 128];
char temp_string_vs[1024 * 128];
char temp_string_fs[1024 * 128];
#ifdef IRON_WINDOWS
wchar_t        temp_wstring[1024 * 32];
struct HWND__ *iron_windows_window_handle();
#endif

void (*iron_update)(void);
void (*iron_drop_files)(char *);
void (*iron_foreground)(void);
void (*iron_background)(void);
void (*iron_shutdown)(void);
void (*iron_key_down)(int);
void (*iron_key_up)(int);
void (*iron_mouse_down)(int, int, int);
void (*iron_mouse_up)(int, int, int);
void (*iron_mouse_move)(int, int, int, int);
void (*iron_mouse_wheel)(float);
void (*iron_touch_down)(int, int, int);
void (*iron_touch_up)(int, int, int);
void (*iron_touch_move)(int, int, int);
void (*iron_pen_down)(int, int, float);
void (*iron_pen_up)(int, int, float);
void (*iron_pen_move)(int, int, float);
void (*iron_save_and_quit)(bool);

char *_substring(char *s, int32_t start, int32_t end) {
	char *buffer = calloc(1, end - start + 1);
	for (int i = 0; i < end - start; ++i) {
		buffer[i] = s[start + i];
	}
	return buffer;
}

int kickstart(int argc, char **argv) {
	_argc = argc;
	_argv = argv;
#ifdef IRON_ANDROID
	char *bindir = "/";
#elif defined(IRON_IOS)
	char *bindir = "";
#elif defined(IRON_WASM)
	char *bindir = "/";
#else
	char *bindir = argv[0];
#endif

#ifdef IRON_WINDOWS // Handle non-ascii path
	HMODULE hmodule = GetModuleHandleW(NULL);
	GetModuleFileNameW(hmodule, temp_wstring, 1024);
	WideCharToMultiByte(CP_UTF8, 0, temp_wstring, -1, temp_string, 4096, NULL, NULL);
	bindir = _substring(temp_string, 0, string_last_index_of(temp_string, "\\"));
	// Running from Visual Studio
	if (argc > 1 && strlen(argv[1]) > 4 && argv[1][strlen(argv[1]) - 4] == '\\' && argv[1][strlen(argv[1]) - 3] == 'o' && argv[1][strlen(argv[1]) - 2] == 'u' &&
	    argv[1][strlen(argv[1]) - 1] == 't') {
		bindir = argv[1];
	}
#else
	bindir = _substring(bindir, 0, string_last_index_of(bindir, "/"));
#endif

	for (int i = 2; i < argc; ++i) {
		if (strcmp(argv[i], "--nowindow") == 0) {
			enable_window = false;
		}
	}

#if !defined(IRON_MACOS) && !defined(IRON_IOS)
	iron_internal_set_files_location(bindir);
#endif

	iron_threads_init();
	iron_display_init();
	gc_start(&argc);
	_kickstart();

#ifdef IRON_AUDIO
	iron_a2_shutdown();
#endif
	gc_stop();
	return 0;
}

i32 iron_get_arg_count() {
	return _argc;
}

char *iron_get_arg(i32 index) {
	return _argv[index];
}

// ██╗██████╗  ██████╗ ███╗   ██╗     █████╗ ██████╗ ██╗
// ██║██╔══██╗██╔═══██╗████╗  ██║    ██╔══██╗██╔══██╗██║
// ██║██████╔╝██║   ██║██╔██╗ ██║    ███████║██████╔╝██║
// ██║██╔══██╗██║   ██║██║╚██╗██║    ██╔══██║██╔═══╝ ██║
// ██║██║  ██║╚██████╔╝██║ ╚████║    ██║  ██║██║     ██║
// ╚═╝╚═╝  ╚═╝ ╚═════╝ ╚═╝  ╚═══╝    ╚═╝  ╚═╝╚═╝     ╚═╝

#include "engine.h"
#include "iron_input.h"
#include "iron_math.h"
#include "iron_net.h"
#include "iron_raycast.h"
#include "iron_shape.h"
#include "iron_sys.h"
#include "iron_tween.h"
#include "kong/dir.h"
#include <lz4x.h>
#include <stdio.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#ifdef IRON_DIRECT3D12
#include <d3d12.h>
#endif
#ifdef WITH_D3DCOMPILER
#include <D3Dcompiler.h>
#include <d3d11.h>
#endif
#ifdef WITH_COMPRESS
#include "iron_compress.h"
void untar_here(const char *path);
#endif
#ifdef WITH_IMAGE_WRITE
#include "iron_image.h"
#endif
#ifdef WITH_VIDEO_WRITE
#include "iron_image.h"
#endif
#if defined(IDLE_SLEEP) && !defined(IRON_WINDOWS)
#include <time.h>
#endif

void _update() {
#ifdef IRON_WINDOWS
	if (in_background && ++paused_frames > 3) {
		Sleep(1);
		return;
	}
#endif

#ifdef IDLE_SLEEP
	if (last_window_width != iron_window_width() || last_window_height != iron_window_height()) {
		last_window_width  = iron_window_width();
		last_window_height = iron_window_height();
		paused_frames      = 0;
	}
#if defined(IRON_IOS) || defined(IRON_ANDROID)
	const int start_sleep = 1200;
#else
	const int start_sleep = 120;
#endif
	if (++paused_frames > start_sleep && !input_down) {
#ifdef IRON_WINDOWS
		Sleep(1);
#elif !defined(IRON_WASM)
		struct timespec t;
		t.tv_sec  = 0;
		t.tv_nsec = 1000000;
		nanosleep(&t, NULL);
#endif
		return;
	}
	if (paused_frames == 30) {
		gc_run();
	}
#endif

	iron_net_update();
	iron_update();
	if (ui_get_current())
		ui_end_frame();
	gpu_present();
}

void _copy(void *data) {
	ui_copy();
}

void _cut(void *data) {
	ui_cut();
}

void _paste(char *text, void *data) {
	ui_paste(text);
}

void _foreground(void *data) {
	iron_foreground();
	in_background = false;
	paused_frames = 0;
}

void _background(void *data) {
	iron_background();
	in_background = true;
	paused_frames = 0;
}

void _shutdown(void *data) {
	iron_shutdown();
}

void _key_down(int code, void *data) {
	iron_key_down(code);
	if (ui_get_current())
		ui_key_down(ui_get_current(), code);

#ifdef IDLE_SLEEP
	input_down    = true;
	paused_frames = 0;
#endif
}

void _key_up(int code, void *data) {
	iron_key_up(code);
	if (ui_get_current())
		ui_key_up(ui_get_current(), code);

#ifdef IDLE_SLEEP
	input_down    = false;
	paused_frames = 0;
#endif
}

void _key_press(unsigned int character, void *data) {
	if (ui_get_current())
		ui_key_press(ui_get_current(), character);

#ifdef IDLE_SLEEP
	paused_frames = 0;
#endif
}

void _mouse_down(int button, int x, int y, void *data) {
	iron_mouse_down(button, x, y);
	if (ui_get_current())
		ui_mouse_down(ui_get_current(), button, x, y);

#ifdef IDLE_SLEEP
	input_down    = true;
	paused_frames = 0;
#endif
}

void _mouse_up(int button, int x, int y, void *data) {
	iron_mouse_up(button, x, y);
	if (ui_get_current())
		ui_mouse_up(ui_get_current(), button, x, y);

#ifdef IDLE_SLEEP
	input_down    = false;
	paused_frames = 0;
#endif
}

void _mouse_move(int x, int y, int mx, int my, void *data) {
	iron_mouse_move(x, y, mx, my);
	if (ui_get_current())
		ui_mouse_move(ui_get_current(), x, y, mx, my);

#ifdef IDLE_SLEEP
	paused_frames = 0;
#endif
}

void _mouse_wheel(float delta, void *data) {
	iron_mouse_wheel(delta);
	if (ui_get_current())
		ui_mouse_wheel(ui_get_current(), delta);

#ifdef IDLE_SLEEP
	paused_frames = 0;
#endif
}

void _touch_move(int index, int x, int y) {
	iron_touch_move(index, x, y);

#if defined(IRON_ANDROID) || defined(IRON_IOS)
	if (ui_get_current())
		ui_touch_move(ui_get_current(), index, x, y);
#endif

#ifdef IDLE_SLEEP
	paused_frames = 0;
#endif
}

void _touch_down(int index, int x, int y) {
	iron_touch_down(index, x, y);

#if defined(IRON_ANDROID) || defined(IRON_IOS)
	if (ui_get_current())
		ui_touch_down(ui_get_current(), index, x, y);
#endif

#ifdef IDLE_SLEEP
	input_down    = true;
	paused_frames = 0;
#endif
}

void _touch_up(int index, int x, int y) {
	iron_touch_up(index, x, y);

#if defined(IRON_ANDROID) || defined(IRON_IOS)
	if (ui_get_current())
		ui_touch_up(ui_get_current(), index, x, y);
#endif

#ifdef IDLE_SLEEP
	input_down    = false;
	paused_frames = 0;
#endif
}

void _pen_down(int x, int y, float pressure) {
	iron_pen_down(x, y, pressure);
	if (ui_get_current())
		ui_pen_down(ui_get_current(), x, y, pressure);

#ifdef IDLE_SLEEP
	input_down    = true;
	paused_frames = 0;
#endif
}

void _pen_up(int x, int y, float pressure) {
	iron_pen_up(x, y, pressure);
	if (ui_get_current())
		ui_pen_up(ui_get_current(), x, y, pressure);

#ifdef IDLE_SLEEP
	input_down    = false;
	paused_frames = 0;
#endif
}

void _pen_move(int x, int y, float pressure) {
	iron_pen_move(x, y, pressure);
	if (ui_get_current())
		ui_pen_move(ui_get_current(), x, y, pressure);

#ifdef IDLE_SLEEP
	paused_frames = 0;
#endif
}

void _drop_files(char *file_path, void *data) {
// Update mouse position
#ifdef IRON_WINDOWS
	POINT p;
	GetCursorPos(&p);
	ScreenToClient(iron_windows_window_handle(), &p);
	_mouse_move(p.x, p.y, 0, 0, NULL);
#endif

	iron_drop_files(file_path);

	in_background = false;
#ifdef IDLE_SLEEP
	paused_frames = 0;
#endif
}

f32 math_floor(f32 x) {
	return floorf(x);
}
f32 math_cos(f32 x) {
	return cosf(x);
}
f32 math_sin(f32 x) {
	return sinf(x);
}
f32 math_tan(f32 x) {
	return tanf(x);
}
f32 math_sqrt(f32 x) {
	return sqrtf(x);
}
f32 math_abs(f32 x) {
	return fabsf(x);
}
f32 math_random() {
	return rand() / (float)RAND_MAX;
}
f32 math_atan2(f32 y, f32 x) {
	return atan2f(y, x);
}
f32 math_asin(f32 x) {
	return asinf(x);
}
f32 math_pi() {
	return 3.14159265358979323846;
}
f32 math_pow(f32 x, f32 y) {
	return powf(x, y);
}
f32 math_round(f32 x) {
	return roundf(x);
}
f32 math_ceil(f32 x) {
	return ceilf(x);
}
f32 math_min(f32 x, f32 y) {
	return x < y ? x : y;
}
f32 math_max(f32 x, f32 y) {
	return x > y ? x : y;
}
f32 math_log(f32 x) {
	return logf(x);
}
f32 math_log2(f32 x) {
	return log2f(x);
}
f32 math_atan(f32 x) {
	return atanf(x);
}
f32 math_acos(f32 x) {
	return acosf(x);
}
f32 math_exp(f32 x) {
	return expf(x);
}
f32 math_fmod(f32 x, f32 y) {
	return fmod(x, y);
}

#ifdef _WIN32
i32 parse_int(const char *s) {
	return _strtoi64(s, NULL, 10);
}
i32 parse_int_hex(const char *s) {
	return _strtoi64(s, NULL, 16);
}
#else
i32 parse_int(const char *s) {
	return strtol(s, NULL, 10);
}
i32 parse_int_hex(const char *s) {
	return strtol(s, NULL, 16);
}
#endif

f32 parse_float(const char *s) {
	return strtof(s, NULL);
}

i32 color_from_floats(f32 r, f32 g, f32 b, f32 a) {
	return ((int)(a * 255) << 24) | ((int)(r * 255) << 16) | ((int)(g * 255) << 8) | (int)(b * 255);
}

u8 color_get_rb(i32 c) {
	return (c & 0x00ff0000) >> 16;
}

u8 color_get_gb(i32 c) {
	return (c & 0x0000ff00) >> 8;
}

u8 color_get_bb(i32 c) {
	return c & 0x000000ff;
}

u8 color_get_ab(i32 c) {
	return c & 0x000000ff;
}

i32 color_set_rb(i32 c, u8 i) {
	return (color_get_ab(c) << 24) | (i << 16) | (color_get_gb(c) << 8) | color_get_bb(c);
}

i32 color_set_gb(i32 c, u8 i) {
	return (color_get_ab(c) << 24) | (color_get_rb(c) << 16) | (i << 8) | color_get_bb(c);
}

i32 color_set_bb(i32 c, u8 i) {
	return (color_get_ab(c) << 24) | (color_get_rb(c) << 16) | (color_get_gb(c) << 8) | i;
}

i32 color_set_ab(i32 c, u8 i) {
	return (i << 24) | (color_get_rb(c) << 16) | (color_get_gb(c) << 8) | color_get_bb(c);
}

void _iron_init(iron_window_options_t *ops) {
	ops->display_index = -1;
	ops->visible       = enable_window;
	ops->color_bits    = 32;
	iron_init(ops);
	iron_random_init((int)(iron_time() * 1000));
	iron_set_cut_callback(_cut, NULL);
	iron_set_copy_callback(_copy, NULL);
	iron_set_paste_callback(_paste, NULL);
	iron_keyboard_set_key_press_callback(_key_press, NULL);
}

void _iron_set_update_callback(void (*callback)(void)) {
	iron_update = callback;
	iron_set_update_callback(_update);
}

void _iron_set_drop_files_callback(void (*callback)(char *)) {
	iron_drop_files = callback;
	iron_set_drop_files_callback(_drop_files, NULL);
}

void iron_set_application_state_callback(void (*on_foreground)(void), void (*on_background)(void), void (*on_shutdown)(void)) {
	iron_set_foreground_callback(on_foreground != NULL ? _foreground : NULL, NULL);
	iron_set_background_callback(on_background != NULL ? _background : NULL, NULL);
	iron_set_shutdown_callback(on_shutdown != NULL ? _shutdown : NULL, NULL);
	iron_foreground = on_foreground;
	iron_background = on_background;
	iron_shutdown   = on_shutdown;
}

void iron_set_keyboard_down_callback(void (*callback)(int)) {
	iron_key_down = callback;
	iron_keyboard_set_key_down_callback(_key_down, NULL);
}

void iron_set_keyboard_up_callback(void (*callback)(int)) {
	iron_key_up = callback;
	iron_keyboard_set_key_up_callback(_key_up, NULL);
}

void iron_set_mouse_down_callback(void (*callback)(int, int, int)) {
	iron_mouse_down = callback;
	iron_mouse_set_press_callback(_mouse_down, NULL);
}

void iron_set_mouse_up_callback(void (*callback)(int, int, int)) {
	iron_mouse_up = callback;
	iron_mouse_set_release_callback(_mouse_up, NULL);
}

void iron_set_mouse_move_callback(void (*callback)(int, int, int, int)) {
	iron_mouse_move = callback;
	iron_mouse_set_move_callback(_mouse_move, NULL);
}

void iron_set_mouse_wheel_callback(void (*callback)(float)) {
	iron_mouse_wheel = callback;
	iron_mouse_set_scroll_callback(_mouse_wheel, NULL);
}

void iron_set_touch_down_callback(void (*callback)(int, int, int)) {
	iron_touch_down = callback;
	iron_surface_set_touch_start_callback(_touch_down);
}

void iron_set_touch_up_callback(void (*callback)(int, int, int)) {
	iron_touch_up = callback;
	iron_surface_set_touch_end_callback(_touch_up);
}

void iron_set_touch_move_callback(void (*callback)(int, int, int)) {
	iron_touch_move = callback;
	iron_surface_set_move_callback(_touch_move);
}

void iron_set_pen_down_callback(void (*callback)(int, int, float)) {
	iron_pen_down = callback;
	iron_pen_set_press_callback(_pen_down);
}

void iron_set_pen_up_callback(void (*callback)(int, int, float)) {
	iron_pen_up = callback;
	iron_pen_set_release_callback(_pen_up);
}

void iron_set_pen_move_callback(void (*callback)(int, int, float)) {
	iron_pen_move = callback;
	iron_pen_set_move_callback(_pen_move);
}

#ifdef WITH_GAMEPAD

void (*iron_gamepad_axis)(int, int, float);
void (*iron_gamepad_button)(int, int, float);

void _gamepad_axis(int gamepad, int axis, float value, void *data) {
	iron_gamepad_axis(gamepad, axis, value);

#ifdef IDLE_SLEEP
	paused_frames = 0;
#endif
}

void _gamepad_button(int gamepad, int button, float value, void *data) {
	iron_gamepad_button(gamepad, button, value);

#ifdef IDLE_SLEEP
	paused_frames = 0;
#endif
}

void iron_set_gamepad_axis_callback(void (*callback)(int, int, float)) {
	iron_gamepad_axis = callback;
	iron_gamepad_set_axis_callback(_gamepad_axis, NULL);
}

void iron_set_gamepad_button_callback(void (*callback)(int, int, float)) {
	iron_gamepad_button = callback;
	iron_gamepad_set_button_callback(_gamepad_button, NULL);
}

#endif

void gpu_delete_buffer(gpu_buffer_t *buffer) {
	gpu_buffer_destroy(buffer);
	free(buffer);
}

void *gpu_create_index_buffer(i32 count) {
	gpu_buffer_t *buffer = (gpu_buffer_t *)malloc(sizeof(gpu_buffer_t));
	gpu_index_buffer_init(buffer, count);
	return buffer;
}

void *gpu_create_vertex_buffer(i32 count, gpu_vertex_structure_t *structure) {
	gpu_buffer_t *buffer = (gpu_buffer_t *)malloc(sizeof(gpu_buffer_t));
	gpu_vertex_buffer_init(buffer, count, structure);
	return buffer;
}

gpu_shader_t *gpu_create_shader(buffer_t *data, i32 shader_type) {
	gpu_shader_t *shader = (gpu_shader_t *)malloc(sizeof(gpu_shader_t));
	gpu_shader_init(shader, data->buffer, data->length, (gpu_shader_type_t)shader_type);
	return shader;
}

#ifdef WITH_KONG
void gpu_create_shaders_from_kong(char *kong, char **vs, char **fs, int *vs_size, int *fs_size);
#endif

gpu_shader_t *gpu_create_shader_from_source(char *source, int source_size, gpu_shader_type_t shader_type) {
	gpu_shader_t *shader        = (gpu_shader_t *)malloc(sizeof(gpu_shader_t));
	char         *temp_string_s = shader_type == GPU_SHADER_TYPE_VERTEX ? temp_string_vs : temp_string_fs;

#ifdef WITH_D3DCOMPILER

	strcpy(temp_string_s, source);

	ID3DBlob *error_message;
	ID3DBlob *shader_buffer;
	UINT      flags = D3DCOMPILE_SKIP_OPTIMIZATION | D3DCOMPILE_SKIP_VALIDATION;
	HRESULT hr = D3DCompile(temp_string_s, strlen(source) + 1, NULL, NULL, NULL, "main", shader_type == GPU_SHADER_TYPE_VERTEX ? "vs_5_0" : "ps_5_0", flags, 0,
	                        &shader_buffer, &error_message);
	if (hr != S_OK) {
		iron_log("%s", (char *)error_message->lpVtbl->GetBufferPointer(error_message));
		return NULL;
	}

	int size = shader_buffer->lpVtbl->GetBufferSize(shader_buffer);
	gpu_shader_init(shader, (char *)shader_buffer->lpVtbl->GetBufferPointer(shader_buffer), size, shader_type);
	shader_buffer->lpVtbl->Release(shader_buffer);

#elif defined(IRON_METAL)

	strcpy(temp_string_s, source);
	gpu_shader_init(shader, temp_string_s, strlen(temp_string_s), shader_type);

#elif defined(IRON_VULKAN)

	gpu_shader_init(shader, source, source_size, shader_type);

#elif defined(IRON_WASM)

	strcpy(temp_string_s, source);
	gpu_shader_init(shader, temp_string_s, strlen(temp_string_s), shader_type);

#endif

	return shader;
}

gpu_pipeline_t *gpu_create_pipeline() {
	gpu_pipeline_t *pipeline = (gpu_pipeline_t *)malloc(sizeof(gpu_pipeline_t));
	gpu_pipeline_init(pipeline);
	return pipeline;
}

void gpu_delete_pipeline(gpu_pipeline_t *pipeline) {
	gpu_pipeline_destroy(pipeline);
	free(pipeline);
}

gpu_texture_t *gpu_create_render_target(i32 width, i32 height, i32 format) {
	gpu_texture_t *render_target = (gpu_texture_t *)malloc(sizeof(gpu_texture_t));
	gpu_render_target_init(render_target, width, height, (gpu_texture_format_t)format);
	render_target->buffer = NULL;
	return render_target;
}

gpu_texture_t *gpu_create_texture_from_bytes(buffer_t *data, i32 width, i32 height, i32 format) {
	gpu_texture_t *texture = (gpu_texture_t *)malloc(sizeof(gpu_texture_t));
	texture->buffer        = NULL;
	gpu_texture_init_from_bytes(texture, data->buffer, width, height, (gpu_texture_format_t)format);
	return texture;
}

gpu_texture_t *gpu_create_texture_from_encoded_bytes(buffer_t *data, char *format) {
	if (data == NULL || data->length == 0) {
		return NULL;
	}
	gpu_texture_t *texture = (gpu_texture_t *)malloc(sizeof(gpu_texture_t));
	texture->buffer        = NULL;
	unsigned char       *texture_data;
	gpu_texture_format_t texture_format;
	int                  width;
	int                  height;

	if (ends_with(format, "k")) {
		width  = iron_read_s32le(data->buffer);
		height = iron_read_s32le(data->buffer + 4);
		char fourcc[5];
		fourcc[0]           = data->buffer[8];
		fourcc[1]           = data->buffer[9];
		fourcc[2]           = data->buffer[10];
		fourcc[3]           = data->buffer[11];
		fourcc[4]           = 0;
		int compressed_size = data->length - 12;
		if (strcmp(fourcc, "LZ4 ") == 0) {
			int output_size = width * height * 4;
			texture_data    = (unsigned char *)malloc(output_size);
			LZ4_decompress_safe((char *)data->buffer + 12, (char *)texture_data, compressed_size, output_size);
			texture_format = GPU_TEXTURE_FORMAT_RGBA32;
		}
		else { // "LZ4F"
			int output_size = width * height * 8;
			texture_data    = (unsigned char *)malloc(output_size);
			LZ4_decompress_safe((char *)data->buffer + 12, (char *)texture_data, compressed_size, output_size);
			texture_format = GPU_TEXTURE_FORMAT_RGBA64;
		}
	}
	else if (ends_with(format, "hdr")) {
		int comp;
		texture_data   = (unsigned char *)stbi_loadf_from_memory(data->buffer, data->length, &width, &height, &comp, 4);
		texture_format = GPU_TEXTURE_FORMAT_RGBA64;
		// F32 to F16
		float    *f32_data = (float *)texture_data;
		uint16_t *f16_data = (uint16_t *)texture_data;
		for (int i = 0; i < width * height * 4; ++i) {
			f16_data[i] = float_to_half_fast(f32_data[i]);
		}
	}
	else { // jpg, png, ..
		int comp;
		texture_data   = stbi_load_from_memory(data->buffer, data->length, &width, &height, &comp, 4);
		texture_format = GPU_TEXTURE_FORMAT_RGBA32;
	}

	// double t = iron_time(); ////
	gpu_texture_init_from_bytes(texture, texture_data, width, height, texture_format);
	// iron_log("gpu_texture_init_from_bytes in %fs\n", iron_time() - t); ////
	free(texture_data);

	return texture;
}

void gpu_delete_texture(gpu_texture_t *texture) {
	gpu_texture_destroy(texture);
	free(texture);
}

gpu_texture_t *iron_load_texture(char *file) {
#ifdef WITH_EMBED
	buffer_t *b = embed_get(file);
	if (b != NULL) {
		return gpu_create_texture_from_encoded_bytes(b, ".k");
	}
#endif

	iron_file_reader_t reader;
	if (!iron_file_reader_open(&reader, file, IRON_FILE_TYPE_ASSET)) {
		return NULL;
	}
	int            size = (int)iron_file_reader_size(&reader);
	unsigned char *data = (unsigned char *)malloc(size);
	iron_file_reader_read(&reader, data, size);
	iron_file_reader_close(&reader);
	buffer_t buf;
	buf.buffer = data;
	buf.length = size;
	return gpu_create_texture_from_encoded_bytes(&buf, file);
}

void *iron_load_sound(char *file) {
#ifdef IRON_AUDIO
	iron_a1_init();
	iron_a1_sound_t *sound = iron_a1_sound_create(file);
	return sound;
#endif
	return NULL;
}

buffer_t *iron_load_blob(char *file) {
#ifdef WITH_EMBED
	buffer_t *b = embed_get(file);
	if (b != NULL) {
		return b;
	}
#endif

	iron_file_reader_t reader;
	if (!iron_file_reader_open(&reader, file, IRON_FILE_TYPE_ASSET)) {
		return NULL;
	}
	uint32_t  reader_size = (uint32_t)iron_file_reader_size(&reader);
	buffer_t *buffer      = buffer_create(reader_size);
	iron_file_reader_read(&reader, buffer->buffer, reader_size);
	iron_file_reader_close(&reader);
	return buffer;
}

i32 iron_display_ppi(i32 index) {
	return iron_display_current_mode(index).pixels_per_inch;
}

i32 iron_display_width(i32 index) {
	return iron_display_current_mode(index).width;
}

i32 iron_display_height(i32 index) {
	return iron_display_current_mode(index).height;
}

i32 iron_display_x(i32 index) {
	return iron_display_current_mode(index).x;
}

i32 iron_display_y(i32 index) {
	return iron_display_current_mode(index).y;
}

i32 iron_display_frequency(i32 index) {
	return iron_display_current_mode(index).frequency;
}

bool iron_display_is_primary(i32 index) {
	return index == iron_primary_display();
}

buffer_t *gpu_get_texture_pixels(gpu_texture_t *image) {
	if (image->buffer == NULL) {
		image->buffer         = malloc(sizeof(buffer_t));
		image->buffer->buffer = NULL;
	}

	image->buffer->length = gpu_texture_format_size(image->format) * image->width * image->height;

	if (image->buffer->buffer == NULL) {
		image->buffer->buffer = malloc(image->buffer->length);
	}

	gpu_get_render_target_pixels(image, image->buffer->buffer);

#ifdef WITH_BC7
	if (image->format == GPU_TEXTURE_FORMAT_RGBA32_BC7) {
		uint8_t *compressed   = image->buffer->buffer;
		image->buffer->buffer = malloc((size_t)image->width * image->height * 4);
		bc7enc_decompress(image->buffer->buffer, compressed, image->width, image->height);
		free(compressed);
	}
#endif

	return image->buffer;
}

void _gpu_begin(gpu_texture_t *render_target, any_array_t *additional, gpu_texture_t *depth_buffer, gpu_clear_t flags, unsigned color, float depth) {
	if (render_target == NULL) {
		gpu_begin(NULL, 0, NULL, flags, color, depth);
	}
	else {
		int32_t        length            = 1;
		gpu_texture_t *render_targets[8] = {render_target, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
		if (additional != NULL) {
			length = additional->length + 1;
			for (int32_t i = 1; i < length; ++i) {
				render_targets[i] = additional->buffer[i - 1];
			}
		}
		gpu_begin(render_targets, length, depth_buffer, flags, color, depth);
	}
}

bool _save_and_quit_callback_internal();

bool _save_and_quit_callback(void *data) {
	return _save_and_quit_callback_internal();
}

void iron_set_save_and_quit_callback(void (*callback)(bool)) {
	iron_save_and_quit = callback;
	iron_window_set_close_callback(_save_and_quit_callback, NULL);
}

void iron_delay_idle_sleep() {
	paused_frames = 0;
}
