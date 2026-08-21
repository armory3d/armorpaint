#include "iron_sys.h"

#include "iron_array.h"
#include "iron_draw.h"
#include "iron_alloc.h"
#include "iron_input.h"
#include "iron_map.h"
#include "iron_string.h"
#include "iron_system.h"
#include <stdbool.h>
#include <string.h>

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

any_map_t *_sys_shaders    = NULL;
f32        _sys_start_time = 0.0f;
char       _sys_window_title[1024];

any_array_t *_sys_foreground_listeners = NULL;
any_array_t *_sys_background_listeners = NULL;
any_array_t *_sys_shutdown_listeners   = NULL;
any_array_t *_sys_drop_files_listeners = NULL;

any_array_t *_sys_on_next_frames = NULL;
any_array_t *_sys_on_end_frames  = NULL;
any_array_t *_sys_on_updates     = NULL;
i32          _sys_lastw          = -1;
i32          _sys_lasth          = -1;

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
	_sys_shutdown_listeners = any_array_create(0);
	_sys_drop_files_listeners = any_array_create(0);
	_sys_on_next_frames = any_array_create(0);
	_sys_on_end_frames = any_array_create(0);
	_sys_on_updates = any_array_create(0);
	_sys_shaders = any_map_create();

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

void video_unload(video_t *self) {}

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
