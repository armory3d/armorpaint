// Replaces linux_system.c when WITH_WAYLAND is defined
#include <errno.h>
#include <fcntl.h>
#include <iron_gpu.h>
#include <iron_system.h>
#include <linux/input-event-codes.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_wayland.h>
#include <wayland-client.h>
#include <wayland-cursor.h>
#include <xkbcommon/xkbcommon.h>

#include "fractional-scale.h"
#include "tablet.h"
#include "viewporter.h"
#include "xdg-decoration.h"
#include "xdg-output.h"
#include "xdg-shell.h"

#define MAXIMUM_DISPLAYS 8

typedef struct {
	struct wl_output *output;
	int               width;
	int               height;
	int               refresh; // mHz
	int               logical_width;
	int               logical_height;
} wl_display_info_t;

static struct {
	struct wl_display                     *display;
	struct wl_registry                    *registry;
	struct wl_compositor                  *compositor;
	struct wl_shm                         *shm;
	struct wl_seat                        *seat;
	struct wl_pointer                     *pointer;
	struct wl_keyboard                    *keyboard;
	struct xdg_wm_base                    *wm_base;
	struct wl_data_device_manager         *data_device_manager;
	struct wl_data_device                 *data_device;
	struct wp_viewporter                  *viewporter;
	struct wp_fractional_scale_manager_v1 *fractional_scale_manager;
	struct zxdg_decoration_manager_v1     *decoration_manager;
	struct zxdg_output_manager_v1         *output_manager;
	struct zwp_tablet_manager_v2          *tablet_manager;
	struct zwp_tablet_tool_v2             *tool; // Tool in proximity
	uint32_t                               tool_serial;
	struct wl_surface                     *tool_cursor_surface;
	struct wl_surface                     *surface;
	struct xdg_surface                    *xdg_surface;
	struct xdg_toplevel                   *toplevel;
	struct wp_viewport                    *viewport;
	struct wp_fractional_scale_v1         *fractional_scale;
	struct wl_cursor_theme                *cursor_theme;
	struct wl_surface                     *cursor_surface;
	struct xkb_context                    *xkb_context;
	struct xkb_keymap                     *xkb_keymap;
	struct xkb_state                      *xkb_state;
	struct wl_data_offer                  *selection_offer;
	struct wl_data_offer                  *drag_offer;
	struct wl_data_source                 *clipboard_source;
	wl_display_info_t                      displays[MAXIMUM_DISPLAYS];
	int                                    num_displays;
	bool                                   configured;
	bool                                   gpu_ready;
	int                                    logical_width; // Surface size in compositor coordinates
	int                                    logical_height;
	int                                    pending_width;
	int                                    pending_height;
	int                                    width; // Framebuffer size in pixels
	int                                    height;
	int                                    scale120; // Fractional scale * 120
	iron_window_mode_t                     mode;
	char                                   title[256];
	uint32_t                               pointer_serial;
	uint32_t                               input_serial;
	int                                    mouse_x;
	int                                    mouse_y;
	int                                    drag_x;
	int                                    drag_y;
	float                                  scroll_accum;
	bool                                   scroll_discrete;
	bool                                   mouse_hidden;
	iron_cursor_t                          cursor;
	int                                    repeat_rate;
	int                                    repeat_delay;
	uint32_t                               repeat_key;
	uint32_t                               repeat_char;
	uint64_t                               repeat_next;
} wl = {.scale120 = 120};

static size_t         clipboard_size   = 0;
static char          *clipboard_string = NULL;
static char           save[2000];
static bool           save_initialized = false;
static const char    *video_formats[]  = {"ogv", NULL};
static struct timeval start;

static struct {
	void (*resize_callback)(int width, int height, void *data);
	void *resize_data;
	bool (*close_callback)(void *data);
	void *close_data;
} callbacks;

static int to_pixels(int v) {
	return v * wl.scale120 / 120;
}

static int fixed_to_pixels(wl_fixed_t v) {
	return (int)(wl_fixed_to_double(v) * wl.scale120 / 120.0);
}

// Display

static void output_geometry(void *data, struct wl_output *output, int32_t x, int32_t y, int32_t pw, int32_t ph, int32_t subpixel, const char *make,
                            const char *model, int32_t transform) {}

static void output_mode(void *data, struct wl_output *output, uint32_t flags, int32_t width, int32_t height, int32_t refresh) {
	wl_display_info_t *d = data;
	if (flags & WL_OUTPUT_MODE_CURRENT) {
		d->width   = width;
		d->height  = height;
		d->refresh = refresh;
	}
}

static void output_done(void *data, struct wl_output *output) {}
static void output_scale(void *data, struct wl_output *output, int32_t factor) {}
static void output_name(void *data, struct wl_output *output, const char *name) {}
static void output_description(void *data, struct wl_output *output, const char *description) {}

static void xdg_output_logical_position(void *data, struct zxdg_output_v1 *output, int32_t x, int32_t y) {}

static void xdg_output_logical_size(void *data, struct zxdg_output_v1 *output, int32_t width, int32_t height) {
	wl_display_info_t *d = data;
	d->logical_width     = width;
	d->logical_height    = height;
}

static void xdg_output_done(void *data, struct zxdg_output_v1 *output) {}
static void xdg_output_name(void *data, struct zxdg_output_v1 *output, const char *name) {}
static void xdg_output_description(void *data, struct zxdg_output_v1 *output, const char *description) {}

static const struct zxdg_output_v1_listener xdg_output_listener = {xdg_output_logical_position, xdg_output_logical_size, xdg_output_done, xdg_output_name,
                                                                   xdg_output_description};

static const struct wl_output_listener output_listener = {output_geometry, output_mode, output_done, output_scale, output_name, output_description};

void iron_display_init() {}

int iron_primary_display(void) {
	return 0;
}

int iron_count_displays(void) {
	return wl.num_displays;
}

iron_display_mode_t iron_display_current_mode(int display_index) {
	iron_display_mode_t mode = {0};
	mode.bits_per_pixel      = 32;
	mode.pixels_per_inch     = 96;
	mode.frequency           = 60;
	if (display_index >= 0 && display_index < wl.num_displays) {
		wl_display_info_t *d = &wl.displays[display_index];
		mode.width           = d->width;
		mode.height          = d->height;
		if (d->refresh > 0) {
			mode.frequency = (d->refresh + 500) / 1000;
		}
	}
	return mode;
}

// Window

static void apply_size(void) {
	int width  = to_pixels(wl.logical_width);
	int height = to_pixels(wl.logical_height);
	if (wl.viewport != NULL) {
		wp_viewport_set_destination(wl.viewport, wl.logical_width, wl.logical_height);
	}
	if (width != wl.width || height != wl.height) {
		wl.width  = width;
		wl.height = height;
		if (wl.gpu_ready) {
			gpu_resize(width, height);
			if (callbacks.resize_callback != NULL) {
				callbacks.resize_callback(width, height, callbacks.resize_data);
			}
		}
	}
}

static void wm_base_ping(void *data, struct xdg_wm_base *wm_base, uint32_t serial) {
	xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {wm_base_ping};

static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
	xdg_surface_ack_configure(xdg_surface, serial);
	if (wl.pending_width > 0 && wl.pending_height > 0) {
		wl.logical_width  = wl.pending_width;
		wl.logical_height = wl.pending_height;
	}
	apply_size();
	wl.configured = true;
}

static const struct xdg_surface_listener xdg_surface_listener = {xdg_surface_configure};

static void toplevel_configure(void *data, struct xdg_toplevel *toplevel, int32_t width, int32_t height, struct wl_array *states) {
	wl.pending_width  = width;
	wl.pending_height = height;
}

static void toplevel_close(void *data, struct xdg_toplevel *toplevel) {
	if (callbacks.close_callback == NULL || callbacks.close_callback(callbacks.close_data)) {
		iron_stop();
	}
}

static void toplevel_configure_bounds(void *data, struct xdg_toplevel *toplevel, int32_t width, int32_t height) {}
static void toplevel_wm_capabilities(void *data, struct xdg_toplevel *toplevel, struct wl_array *caps) {}

static const struct xdg_toplevel_listener toplevel_listener = {toplevel_configure, toplevel_close, toplevel_configure_bounds, toplevel_wm_capabilities};

static void preferred_scale(void *data, struct wp_fractional_scale_v1 *fs, uint32_t scale) {
	if ((int)scale != wl.scale120) {
		wl.scale120 = scale;
		apply_size();
	}
}

static const struct wp_fractional_scale_v1_listener fractional_scale_listener = {preferred_scale};

int iron_window_x() {
	return 0;
}

int iron_window_y() {
	return 0;
}

int iron_window_width() {
	return wl.width;
}

int iron_window_height() {
	return wl.height;
}

int iron_window_display() {
	return 0;
}

void iron_window_resize(int width, int height) {}
void iron_window_move(int x, int y) {}
void iron_window_show() {}
void iron_window_hide() {}

void iron_window_change_mode(iron_window_mode_t mode) {
	if (mode == wl.mode) {
		return;
	}
	wl.mode = mode;
	if (mode == IRON_WINDOW_MODE_FULLSCREEN) {
		xdg_toplevel_set_fullscreen(wl.toplevel, NULL);
	}
	else {
		xdg_toplevel_unset_fullscreen(wl.toplevel);
	}
}

iron_window_mode_t iron_window_get_mode() {
	return wl.mode;
}

void iron_window_set_title(const char *title) {
	strncpy(wl.title, title, sizeof(wl.title) - 1);
	xdg_toplevel_set_title(wl.toplevel, title);
}

void iron_window_destroy() {
	if (wl.fractional_scale != NULL) {
		wp_fractional_scale_v1_destroy(wl.fractional_scale);
	}
	if (wl.viewport != NULL) {
		wp_viewport_destroy(wl.viewport);
	}
	xdg_toplevel_destroy(wl.toplevel);
	xdg_surface_destroy(wl.xdg_surface);
	wl_surface_destroy(wl.surface);
	wl_display_flush(wl.display);
}

void iron_window_set_resize_callback(void (*callback)(int width, int height, void *data), void *data) {
	callbacks.resize_callback = callback;
	callbacks.resize_data     = data;
}

void iron_window_set_close_callback(bool (*callback)(void *data), void *data) {
	callbacks.close_callback = callback;
	callbacks.close_data     = data;
}

void iron_window_create(iron_window_options_t *win) {
	wl.surface     = wl_compositor_create_surface(wl.compositor);
	wl.xdg_surface = xdg_wm_base_get_xdg_surface(wl.wm_base, wl.surface);
	xdg_surface_add_listener(wl.xdg_surface, &xdg_surface_listener, NULL);
	wl.toplevel = xdg_surface_get_toplevel(wl.xdg_surface);
	xdg_toplevel_add_listener(wl.toplevel, &toplevel_listener, NULL);
	char app_id[256];
	snprintf(app_id, sizeof(app_id), "%s_IronApplication", iron_application_name());
	xdg_toplevel_set_app_id(wl.toplevel, app_id);
	iron_window_set_title(win->title);

	if (wl.decoration_manager != NULL) {
		struct zxdg_toplevel_decoration_v1 *decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(wl.decoration_manager, wl.toplevel);
		zxdg_toplevel_decoration_v1_set_mode(decoration, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
	}
	if (wl.viewporter != NULL && wl.fractional_scale_manager != NULL) {
		wl.viewport         = wp_viewporter_get_viewport(wl.viewporter, wl.surface);
		wl.fractional_scale = wp_fractional_scale_manager_v1_get_fractional_scale(wl.fractional_scale_manager, wl.surface);
		wp_fractional_scale_v1_add_listener(wl.fractional_scale, &fractional_scale_listener, NULL);
	}

	// The preferred scale only arrives after the window is shown, guess it from the first output so the window opens at the requested size
	// in pixels, compositors keep the initial size of floating windows
	wl_display_info_t *d = &wl.displays[0];
	if (wl.viewport != NULL && wl.num_displays > 0 && d->logical_width > 0) {
		int pixels  = d->width > d->height ? d->width : d->height;
		int logical = d->logical_width > d->logical_height ? d->logical_width : d->logical_height;
		wl.scale120 = (pixels * 120 + logical / 2) / logical;
	}
	wl.logical_width  = win->width * 120 / wl.scale120;
	wl.logical_height = win->height * 120 / wl.scale120;
	wl.mode           = IRON_WINDOW_MODE_WINDOW;
	iron_window_change_mode(win->mode);

	// Wait for the first configure before rendering
	wl_surface_commit(wl.surface);
	while (!wl.configured && wl_display_dispatch(wl.display) != -1) {
	}

	gpu_init(win->depth_bits, win->vsync);
	wl.gpu_ready = true;
}

// Vulkan

void iron_vulkan_get_instance_extensions(const char **names, int *index) {
	names[(*index)++] = VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME;
}

VkBool32 iron_vulkan_get_physical_device_presentation_support(VkPhysicalDevice physical_device, uint32_t queue_family_index) {
	return vkGetPhysicalDeviceWaylandPresentationSupportKHR(physical_device, queue_family_index, wl.display);
}

VkResult iron_vulkan_create_surface(VkInstance instance, VkSurfaceKHR *surface) {
	VkWaylandSurfaceCreateInfoKHR info = {0};
	info.sType                         = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
	info.display                       = wl.display;
	info.surface                       = wl.surface;
	return vkCreateWaylandSurfaceKHR(instance, &info, NULL, surface);
}

// Data transfer

static char *receive_offer(struct wl_data_offer *offer, const char *mime) {
	int fds[2];
	if (pipe(fds) == -1) {
		return NULL;
	}
	wl_data_offer_receive(offer, mime, fds[1]);
	close(fds[1]);
	wl_display_flush(wl.display);

	size_t size = 0;
	size_t cap  = 4096;
	char  *data = malloc(cap);
	while (true) {
		struct pollfd pfd = {fds[0], POLLIN, 0};
		if (poll(&pfd, 1, 1000) <= 0) {
			break;
		}
		if (size + 4096 + 1 > cap) {
			cap *= 2;
			data = realloc(data, cap);
		}
		ssize_t n = read(fds[0], data + size, 4096);
		if (n <= 0) {
			break;
		}
		size += n;
	}
	close(fds[0]);
	data[size] = 0;
	return data;
}

static char *uri_decode(const char *src) {
	char *res = calloc(1, strlen(src) + 1);
	char *dst = res;
	while (*src) {
		unsigned int c;
		if (src[0] == '%' && src[1] && src[2] && sscanf(src + 1, "%2x", &c) == 1) {
			*dst++ = (char)c;
			src += 3;
		}
		else {
			*dst++ = *src++;
		}
	}
	return res;
}

static void drop_uri_list(char *list) {
	for (char *line = strtok(list, "\r\n"); line != NULL; line = strtok(NULL, "\r\n")) {
		if (strncmp(line, "file://", 7) != 0) {
			continue;
		}
		char *path = strchr(line + 7, '/'); // Skip optional hostname
		if (path != NULL) {
			iron_internal_drop_files_callback(uri_decode(path));
		}
	}
}

static void data_device_data_offer(void *data, struct wl_data_device *device, struct wl_data_offer *offer) {}

static void data_device_enter(void *data, struct wl_data_device *device, uint32_t serial, struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y,
                              struct wl_data_offer *offer) {
	wl.drag_offer = offer;
	wl.drag_x     = fixed_to_pixels(x);
	wl.drag_y     = fixed_to_pixels(y);
	if (offer != NULL) {
		wl_data_offer_accept(offer, serial, "text/uri-list");
		if (wl_data_offer_get_version(offer) >= 3) {
			wl_data_offer_set_actions(offer, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY);
		}
	}
}

static void data_device_leave(void *data, struct wl_data_device *device) {
	if (wl.drag_offer != NULL) {
		wl_data_offer_destroy(wl.drag_offer);
		wl.drag_offer = NULL;
	}
}

static void data_device_motion(void *data, struct wl_data_device *device, uint32_t time, wl_fixed_t x, wl_fixed_t y) {
	wl.drag_x = fixed_to_pixels(x);
	wl.drag_y = fixed_to_pixels(y);
}

static void data_device_drop(void *data, struct wl_data_device *device) {
	if (wl.drag_offer == NULL) {
		return;
	}
	char *list = receive_offer(wl.drag_offer, "text/uri-list");
	if (wl_data_offer_get_version(wl.drag_offer) >= 3) {
		wl_data_offer_finish(wl.drag_offer);
	}
	wl_data_offer_destroy(wl.drag_offer);
	wl.drag_offer = NULL;
	if (list != NULL) {
		wl.mouse_x = wl.drag_x;
		wl.mouse_y = wl.drag_y;
		iron_internal_mouse_trigger_move(wl.drag_x, wl.drag_y); // Drop at the cursor
		drop_uri_list(list);
		free(list);
	}
}

static void data_device_selection(void *data, struct wl_data_device *device, struct wl_data_offer *offer) {
	if (wl.selection_offer != NULL) {
		wl_data_offer_destroy(wl.selection_offer);
	}
	wl.selection_offer = offer;
}

static const struct wl_data_device_listener data_device_listener = {data_device_data_offer, data_device_enter, data_device_leave,
                                                                    data_device_motion,     data_device_drop,  data_device_selection};

static void source_target(void *data, struct wl_data_source *source, const char *mime) {}

static void source_send(void *data, struct wl_data_source *source, const char *mime, int32_t fd) {
	if (clipboard_string != NULL) {
		write(fd, clipboard_string, strlen(clipboard_string));
	}
	close(fd);
}

static void source_cancelled(void *data, struct wl_data_source *source) {
	wl_data_source_destroy(source);
	if (wl.clipboard_source == source) {
		wl.clipboard_source = NULL;
	}
}

static void source_dnd_drop_performed(void *data, struct wl_data_source *source) {}
static void source_dnd_finished(void *data, struct wl_data_source *source) {}
static void source_action(void *data, struct wl_data_source *source, uint32_t action) {}

static const struct wl_data_source_listener data_source_listener = {source_target,       source_send,  source_cancelled, source_dnd_drop_performed,
                                                                    source_dnd_finished, source_action};

void iron_copy_to_clipboard(const char *text) {
	size_t len = strlen(text);
	if (len + 1 > clipboard_size) {
		free(clipboard_string);
		clipboard_size   = len + 1;
		clipboard_string = malloc(clipboard_size);
	}
	strcpy(clipboard_string, text);

	if (wl.data_device == NULL) {
		return;
	}
	struct wl_data_source *source = wl_data_device_manager_create_data_source(wl.data_device_manager);
	wl_data_source_add_listener(source, &data_source_listener, NULL);
	wl_data_source_offer(source, "text/plain;charset=utf-8");
	wl_data_source_offer(source, "text/plain");
	wl_data_source_offer(source, "UTF8_STRING");
	wl_data_device_set_selection(wl.data_device, source, wl.input_serial);
	wl.clipboard_source = source;
	wl_display_flush(wl.display);
}

static void paste(void) {
	if (wl.clipboard_source != NULL) { // We own the clipboard, reading from ourselves would block
		if (clipboard_string != NULL) {
			iron_internal_paste_callback(clipboard_string);
		}
		return;
	}
	if (wl.selection_offer == NULL) {
		return;
	}
	char *text = receive_offer(wl.selection_offer, "text/plain;charset=utf-8");
	if (text != NULL) {
		iron_internal_paste_callback(text);
		free(text);
	}
}

// Pointer

static struct wl_cursor_image *cursor_image(void) {
	if (wl.mouse_hidden || wl.cursor_theme == NULL) {
		return NULL;
	}
	const char *names[2] = {"default", "left_ptr"};
	if (wl.cursor == IRON_CURSOR_HAND) {
		names[0] = "pointer", names[1] = "hand1";
	}
	else if (wl.cursor == IRON_CURSOR_IBEAM) {
		names[0] = "text", names[1] = "xterm";
	}
	else if (wl.cursor == IRON_CURSOR_SIZEWE) {
		names[0] = "ew-resize", names[1] = "sb_h_double_arrow";
	}
	else if (wl.cursor == IRON_CURSOR_SIZENS) {
		names[0] = "ns-resize", names[1] = "sb_v_double_arrow";
	}
	struct wl_cursor *cursor = wl_cursor_theme_get_cursor(wl.cursor_theme, names[0]);
	if (cursor == NULL) {
		cursor = wl_cursor_theme_get_cursor(wl.cursor_theme, names[1]);
	}
	return cursor != NULL ? cursor->images[0] : NULL;
}

static void attach_cursor(struct wl_surface *surface, struct wl_cursor_image *image) {
	wl_surface_attach(surface, wl_cursor_image_get_buffer(image), 0, 0);
	wl_surface_damage(surface, 0, 0, image->width, image->height);
	wl_surface_commit(surface);
}

static void update_cursor(void) {
	struct wl_cursor_image *image = cursor_image();
	if (wl.pointer != NULL) {
		wl_pointer_set_cursor(wl.pointer, wl.pointer_serial, image != NULL ? wl.cursor_surface : NULL, image != NULL ? image->hotspot_x : 0,
		                      image != NULL ? image->hotspot_y : 0);
		if (image != NULL) {
			attach_cursor(wl.cursor_surface, image);
		}
	}
	if (wl.tool != NULL) { // Tablet tools need their own cursor surface
		zwp_tablet_tool_v2_set_cursor(wl.tool, wl.tool_serial, image != NULL ? wl.tool_cursor_surface : NULL, image != NULL ? image->hotspot_x : 0,
		                              image != NULL ? image->hotspot_y : 0);
		if (image != NULL) {
			attach_cursor(wl.tool_cursor_surface, image);
		}
	}
}

static void pointer_enter(void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y) {
	wl.pointer_serial = serial;
	wl.mouse_x        = fixed_to_pixels(x);
	wl.mouse_y        = fixed_to_pixels(y);
	update_cursor();
	iron_internal_mouse_trigger_move(wl.mouse_x, wl.mouse_y);
}

static void pointer_leave(void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface) {}

static void pointer_motion(void *data, struct wl_pointer *pointer, uint32_t time, wl_fixed_t x, wl_fixed_t y) {
	wl.mouse_x = fixed_to_pixels(x);
	wl.mouse_y = fixed_to_pixels(y);
	iron_internal_mouse_trigger_move(wl.mouse_x, wl.mouse_y);
}

static void pointer_button(void *data, struct wl_pointer *pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state) {
	wl.input_serial = serial;
	int index;
	switch (button) {
	case BTN_LEFT:
		index = 0;
		break;
	case BTN_RIGHT:
		index = 1;
		break;
	case BTN_MIDDLE:
		index = 2;
		break;
	default:
		index = button - BTN_SIDE + 3;
		break;
	}
	if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
		iron_internal_mouse_trigger_press(index, wl.mouse_x, wl.mouse_y);
	}
	else {
		iron_internal_mouse_trigger_release(index, wl.mouse_x, wl.mouse_y);
	}
}

static void pointer_axis(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis, wl_fixed_t value) {
	if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL) {
		return;
	}
	if (wl.scroll_discrete) { // Already handled by axis_discrete
		wl.scroll_discrete = false;
		return;
	}
	wl.scroll_accum += wl_fixed_to_double(value);
	while (wl.scroll_accum >= 10.0f) {
		iron_internal_mouse_trigger_scroll(1.0f);
		wl.scroll_accum -= 10.0f;
	}
	while (wl.scroll_accum <= -10.0f) {
		iron_internal_mouse_trigger_scroll(-1.0f);
		wl.scroll_accum += 10.0f;
	}
}

static void pointer_frame(void *data, struct wl_pointer *pointer) {}
static void pointer_axis_source(void *data, struct wl_pointer *pointer, uint32_t source) {}
static void pointer_axis_stop(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis) {}

static void pointer_axis_discrete(void *data, struct wl_pointer *pointer, uint32_t axis, int32_t discrete) {
	if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL) {
		return;
	}
	wl.scroll_discrete = true;
	for (int i = 0; i < abs(discrete); ++i) {
		iron_internal_mouse_trigger_scroll(discrete > 0 ? 1.0f : -1.0f);
	}
}

static const struct wl_pointer_listener pointer_listener = {pointer_enter, pointer_leave,       pointer_motion,    pointer_button,       pointer_axis,
                                                            pointer_frame, pointer_axis_source, pointer_axis_stop, pointer_axis_discrete};

bool iron_mouse_can_lock(void) {
	return false;
}

void iron_mouse_show() {
	wl.mouse_hidden = false;
	update_cursor();
}

void iron_mouse_hide() {
	wl.mouse_hidden = true;
	update_cursor();
}

void iron_mouse_set_cursor(iron_cursor_t cursor) {
	if (cursor != wl.cursor) {
		wl.cursor = cursor;
		update_cursor();
	}
}

void iron_mouse_set_position(int x, int y) {} // Not supported by Wayland

void iron_mouse_get_position(int *x, int *y) {
	*x = wl.mouse_x;
	*y = wl.mouse_y;
}

// Tablet, pen events also drive the mouse as tablet input does not generate pointer events

typedef struct {
	bool  eraser;
	bool  down;
	bool  pending_down;
	bool  pending_up;
	bool  pending_motion;
	int   x;
	int   y;
	float pressure;
} tool_state_t;

static void tool_type(void *data, struct zwp_tablet_tool_v2 *tool, uint32_t type) {
	((tool_state_t *)data)->eraser = type == ZWP_TABLET_TOOL_V2_TYPE_ERASER;
}

static void tool_hardware_serial(void *data, struct zwp_tablet_tool_v2 *tool, uint32_t hi, uint32_t lo) {}
static void tool_hardware_id_wacom(void *data, struct zwp_tablet_tool_v2 *tool, uint32_t hi, uint32_t lo) {}
static void tool_capability(void *data, struct zwp_tablet_tool_v2 *tool, uint32_t capability) {}
static void tool_done(void *data, struct zwp_tablet_tool_v2 *tool) {}

static void tool_removed(void *data, struct zwp_tablet_tool_v2 *tool) {
	if (wl.tool == tool) {
		wl.tool = NULL;
	}
	free(data);
	zwp_tablet_tool_v2_destroy(tool);
}

static void tool_proximity_in(void *data, struct zwp_tablet_tool_v2 *tool, uint32_t serial, struct zwp_tablet_v2 *tablet, struct wl_surface *surface) {
	wl.tool        = tool;
	wl.tool_serial = serial;
	update_cursor();
}

static void tool_proximity_out(void *data, struct zwp_tablet_tool_v2 *tool) {
	if (wl.tool == tool) {
		wl.tool = NULL;
	}
}

static void tool_down(void *data, struct zwp_tablet_tool_v2 *tool, uint32_t serial) {
	wl.input_serial                      = serial;
	((tool_state_t *)data)->pending_down = true;
}

static void tool_up(void *data, struct zwp_tablet_tool_v2 *tool) {
	((tool_state_t *)data)->pending_up = true;
}

static void tool_motion(void *data, struct zwp_tablet_tool_v2 *tool, wl_fixed_t x, wl_fixed_t y) {
	tool_state_t *t   = data;
	t->x              = fixed_to_pixels(x);
	t->y              = fixed_to_pixels(y);
	t->pending_motion = true;
}

static void tool_pressure(void *data, struct zwp_tablet_tool_v2 *tool, uint32_t pressure) {
	((tool_state_t *)data)->pressure = pressure / 65535.0f;
}

static void tool_distance(void *data, struct zwp_tablet_tool_v2 *tool, uint32_t distance) {}
static void tool_tilt(void *data, struct zwp_tablet_tool_v2 *tool, wl_fixed_t x, wl_fixed_t y) {}
static void tool_rotation(void *data, struct zwp_tablet_tool_v2 *tool, wl_fixed_t degrees) {}
static void tool_slider(void *data, struct zwp_tablet_tool_v2 *tool, int32_t position) {}
static void tool_wheel(void *data, struct zwp_tablet_tool_v2 *tool, wl_fixed_t degrees, int32_t clicks) {}

static void tool_button(void *data, struct zwp_tablet_tool_v2 *tool, uint32_t serial, uint32_t button, uint32_t state) {
	tool_state_t *t     = data;
	int           index = button == BTN_STYLUS ? 1 : button == BTN_STYLUS2 ? 2 : -1; // Barrel buttons act as right and middle mouse buttons
	if (index < 0) {
		return;
	}
	if (state == ZWP_TABLET_TOOL_V2_BUTTON_STATE_PRESSED) {
		iron_internal_mouse_trigger_press(index, t->x, t->y);
	}
	else {
		iron_internal_mouse_trigger_release(index, t->x, t->y);
	}
}

static void tool_frame(void *data, struct zwp_tablet_tool_v2 *tool, uint32_t time) {
	tool_state_t *t = data;
	if (t->pending_motion) {
		wl.mouse_x = t->x;
		wl.mouse_y = t->y;
		iron_internal_mouse_trigger_move(t->x, t->y);
		if (t->down && t->pressure > 0) {
			(t->eraser ? iron_internal_eraser_trigger_move : iron_internal_pen_trigger_move)(t->x, t->y, t->pressure);
		}
	}
	if (t->pending_down) {
		t->down = true;
		(t->eraser ? iron_internal_eraser_trigger_press : iron_internal_pen_trigger_press)(t->x, t->y, t->pressure);
		iron_internal_mouse_trigger_press(0, t->x, t->y);
	}
	if (t->pending_up) {
		t->down = false;
		(t->eraser ? iron_internal_eraser_trigger_release : iron_internal_pen_trigger_release)(t->x, t->y, t->pressure);
		iron_internal_mouse_trigger_release(0, t->x, t->y);
	}
	t->pending_down   = false;
	t->pending_up     = false;
	t->pending_motion = false;
}

static const struct zwp_tablet_tool_v2_listener tool_listener = {
    tool_type, tool_hardware_serial, tool_hardware_id_wacom, tool_capability, tool_done, tool_removed,  tool_proximity_in, tool_proximity_out, tool_down,
    tool_up,   tool_motion,          tool_pressure,          tool_distance,   tool_tilt, tool_rotation, tool_slider,       tool_wheel,         tool_button,
    tool_frame};

static void tablet_seat_tablet_added(void *data, struct zwp_tablet_seat_v2 *seat, struct zwp_tablet_v2 *tablet) {}

static void tablet_seat_tool_added(void *data, struct zwp_tablet_seat_v2 *seat, struct zwp_tablet_tool_v2 *tool) {
	zwp_tablet_tool_v2_add_listener(tool, &tool_listener, calloc(1, sizeof(tool_state_t)));
}

static void tablet_seat_pad_added(void *data, struct zwp_tablet_seat_v2 *seat, struct zwp_tablet_pad_v2 *pad) {
	zwp_tablet_pad_v2_destroy(pad); // Pad buttons are not used
}

static const struct zwp_tablet_seat_v2_listener tablet_seat_listener = {tablet_seat_tablet_added, tablet_seat_tool_added, tablet_seat_pad_added};

// Keyboard

static int keysym_to_iron(xkb_keysym_t sym) {
	if (sym >= XKB_KEY_a && sym <= XKB_KEY_z) {
		return KEY_CODE_A + (sym - XKB_KEY_a);
	}
	if (sym >= XKB_KEY_0 && sym <= XKB_KEY_9) {
		return KEY_CODE_0 + (sym - XKB_KEY_0);
	}
	if (sym >= XKB_KEY_KP_0 && sym <= XKB_KEY_KP_9) {
		return KEY_CODE_NUMPAD_0 + (sym - XKB_KEY_KP_0);
	}
	if (sym >= XKB_KEY_F1 && sym <= XKB_KEY_F12) {
		return KEY_CODE_F1 + (sym - XKB_KEY_F1);
	}
	switch (sym) {
	case XKB_KEY_Right:
	case XKB_KEY_KP_Right:
		return KEY_CODE_RIGHT;
	case XKB_KEY_Left:
	case XKB_KEY_KP_Left:
		return KEY_CODE_LEFT;
	case XKB_KEY_Up:
	case XKB_KEY_KP_Up:
		return KEY_CODE_UP;
	case XKB_KEY_Down:
	case XKB_KEY_KP_Down:
		return KEY_CODE_DOWN;
	case XKB_KEY_space:
		return KEY_CODE_SPACE;
	case XKB_KEY_BackSpace:
		return KEY_CODE_BACKSPACE;
	case XKB_KEY_Tab:
	case XKB_KEY_ISO_Left_Tab:
		return KEY_CODE_TAB;
	case XKB_KEY_Return:
	case XKB_KEY_KP_Enter:
		return KEY_CODE_RETURN;
	case XKB_KEY_Shift_L:
	case XKB_KEY_Shift_R:
		return KEY_CODE_SHIFT;
	case XKB_KEY_Control_L:
	case XKB_KEY_Control_R:
		return KEY_CODE_CONTROL;
	case XKB_KEY_Alt_L:
	case XKB_KEY_Alt_R:
	case XKB_KEY_ISO_Prev_Group:
	case XKB_KEY_ISO_Next_Group:
		return KEY_CODE_ALT;
	case XKB_KEY_Delete:
	case XKB_KEY_KP_Delete:
		return KEY_CODE_DELETE;
	case XKB_KEY_comma:
		return KEY_CODE_COMMA;
	case XKB_KEY_period:
		return KEY_CODE_PERIOD;
	case XKB_KEY_bracketleft:
		return KEY_CODE_OPEN_BRACKET;
	case XKB_KEY_bracketright:
		return KEY_CODE_CLOSE_BRACKET;
	case XKB_KEY_braceleft:
		return KEY_CODE_OPEN_CURLY_BRACKET;
	case XKB_KEY_braceright:
		return KEY_CODE_CLOSE_CURLY_BRACKET;
	case XKB_KEY_parenleft:
		return KEY_CODE_OPEN_PAREN;
	case XKB_KEY_parenright:
		return KEY_CODE_CLOSE_PAREN;
	case XKB_KEY_backslash:
		return KEY_CODE_BACK_SLASH;
	case XKB_KEY_apostrophe:
		return KEY_CODE_QUOTE;
	case XKB_KEY_colon:
		return KEY_CODE_COLON;
	case XKB_KEY_semicolon:
		return KEY_CODE_SEMICOLON;
	case XKB_KEY_minus:
		return KEY_CODE_HYPHEN_MINUS;
	case XKB_KEY_underscore:
		return KEY_CODE_UNDERSCORE;
	case XKB_KEY_slash:
		return KEY_CODE_SLASH;
	case XKB_KEY_bar:
		return KEY_CODE_PIPE;
	case XKB_KEY_question:
		return KEY_CODE_QUESTIONMARK;
	case XKB_KEY_less:
		return KEY_CODE_LESS_THAN;
	case XKB_KEY_greater:
		return KEY_CODE_GREATER_THAN;
	case XKB_KEY_asterisk:
		return KEY_CODE_ASTERISK;
	case XKB_KEY_ampersand:
		return KEY_CODE_AMPERSAND;
	case XKB_KEY_percent:
		return KEY_CODE_PERCENT;
	case XKB_KEY_dollar:
		return KEY_CODE_DOLLAR;
	case XKB_KEY_numbersign:
		return KEY_CODE_HASH;
	case XKB_KEY_at:
		return KEY_CODE_AT;
	case XKB_KEY_exclam:
		return KEY_CODE_EXCLAMATION;
	case XKB_KEY_equal:
		return KEY_CODE_EQUALS;
	case XKB_KEY_plus:
	case XKB_KEY_KP_Add:
		return KEY_CODE_ADD;
	case XKB_KEY_grave:
		return KEY_CODE_BACK_QUOTE;
	case XKB_KEY_quotedbl:
		return KEY_CODE_DOUBLE_QUOTE;
	case XKB_KEY_asciitilde:
		return KEY_CODE_TILDE;
	case XKB_KEY_Pause:
		return KEY_CODE_PAUSE;
	case XKB_KEY_Scroll_Lock:
		return KEY_CODE_SCROLL_LOCK;
	case XKB_KEY_Home:
	case XKB_KEY_KP_Home:
		return KEY_CODE_HOME;
	case XKB_KEY_Page_Up:
	case XKB_KEY_KP_Page_Up:
		return KEY_CODE_PAGE_UP;
	case XKB_KEY_Page_Down:
	case XKB_KEY_KP_Page_Down:
		return KEY_CODE_PAGE_DOWN;
	case XKB_KEY_End:
	case XKB_KEY_KP_End:
		return KEY_CODE_END;
	case XKB_KEY_Insert:
	case XKB_KEY_KP_Insert:
		return KEY_CODE_INSERT;
	case XKB_KEY_KP_Multiply:
		return KEY_CODE_MULTIPLY;
	case XKB_KEY_KP_Subtract:
		return KEY_CODE_SUBTRACT;
	case XKB_KEY_KP_Decimal:
		return KEY_CODE_DECIMAL;
	case XKB_KEY_KP_Divide:
		return KEY_CODE_DIVIDE;
	case XKB_KEY_Menu:
		return KEY_CODE_CONTEXT_MENU;
	case XKB_KEY_Escape:
		return KEY_CODE_ESCAPE;
	}
	return KEY_CODE_UNKNOWN;
}

// Letters use the unshifted keysym so shortcuts work with modifiers held, other keys the shifted one
static int key_to_iron(uint32_t key) {
	xkb_layout_index_t  layout = xkb_state_key_get_layout(wl.xkb_state, key);
	const xkb_keysym_t *syms;
	int                 count = xkb_keymap_key_get_syms_by_level(wl.xkb_keymap, key, layout, 0, &syms);
	xkb_keysym_t        sym   = count > 0 ? syms[0] : XKB_KEY_NoSymbol;
	if (sym < XKB_KEY_a || sym > XKB_KEY_z) {
		sym = xkb_state_key_get_one_sym(wl.xkb_state, key);
	}
	return keysym_to_iron(sym);
}

static uint64_t now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void keyboard_keymap(void *data, struct wl_keyboard *keyboard, uint32_t format, int32_t fd, uint32_t size) {
	if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
		close(fd);
		return;
	}
	char *map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (map == MAP_FAILED) {
		return;
	}
	struct xkb_keymap *keymap = xkb_keymap_new_from_string(wl.xkb_context, map, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
	munmap(map, size);
	if (keymap == NULL) {
		return;
	}
	xkb_state_unref(wl.xkb_state);
	xkb_keymap_unref(wl.xkb_keymap);
	wl.xkb_keymap = keymap;
	wl.xkb_state  = xkb_state_new(keymap);
}

static void keyboard_enter(void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface, struct wl_array *keys) {
	wl.input_serial = serial;
	iron_internal_foreground_callback();
}

static void keyboard_leave(void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface) {
	wl.repeat_key = 0;
	iron_internal_background_callback();
}

static void keyboard_key(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t time, uint32_t scancode, uint32_t state) {
	if (wl.xkb_state == NULL) {
		return;
	}
	wl.input_serial = serial;
	uint32_t key    = scancode + 8;
	int      code   = key_to_iron(key);

	if (state == WL_KEYBOARD_KEY_STATE_RELEASED) {
		if (key == wl.repeat_key) {
			wl.repeat_key = 0;
		}
		if (code != KEY_CODE_UNKNOWN) {
			iron_internal_keyboard_trigger_key_up(code);
		}
		return;
	}

	bool ctrl = xkb_state_mod_name_is_active(wl.xkb_state, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0;
	if (ctrl && code == KEY_CODE_V) {
		paste();
	}
	else if (ctrl && code == KEY_CODE_C) {
		iron_internal_copy_callback();
	}
	else if (ctrl && code == KEY_CODE_X) {
		iron_internal_cut_callback();
	}

	xkb_keysym_t sym = xkb_state_key_get_one_sym(wl.xkb_state, key);
	uint32_t     ch  = xkb_state_key_get_utf32(wl.xkb_state, key);
	bool         ignored =
	    sym == XKB_KEY_Escape || sym == XKB_KEY_BackSpace || sym == XKB_KEY_Delete || sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter || sym == XKB_KEY_Tab;
	if (!ctrl && !ignored && ch >= 32 && ch != 127) {
		iron_internal_keyboard_trigger_key_press(ch);
		if (wl.repeat_rate > 0 && xkb_keymap_key_repeats(wl.xkb_keymap, key)) {
			wl.repeat_key  = key;
			wl.repeat_char = ch;
			wl.repeat_next = now_ms() + wl.repeat_delay;
		}
	}

	if (code != KEY_CODE_UNKNOWN) {
		iron_internal_keyboard_trigger_key_down(code);
	}
}

static void keyboard_modifiers(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t depressed, uint32_t latched, uint32_t locked,
                               uint32_t group) {
	if (wl.xkb_state != NULL) {
		xkb_state_update_mask(wl.xkb_state, depressed, latched, locked, 0, 0, group);
	}
}

static void keyboard_repeat_info(void *data, struct wl_keyboard *keyboard, int32_t rate, int32_t delay) {
	wl.repeat_rate  = rate;
	wl.repeat_delay = delay;
}

static const struct wl_keyboard_listener keyboard_listener = {keyboard_keymap, keyboard_enter,     keyboard_leave,
                                                              keyboard_key,    keyboard_modifiers, keyboard_repeat_info};

static void key_repeat(void) {
	if (wl.repeat_key == 0) {
		return;
	}
	uint64_t now = now_ms();
	while (now >= wl.repeat_next) {
		iron_internal_keyboard_trigger_key_press(wl.repeat_char);
		wl.repeat_next += 1000 / wl.repeat_rate;
	}
}

bool iron_keyboard_active() {
	return true;
}

void iron_keyboard_show() {}
void iron_keyboard_hide() {}

// Registry

static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t caps) {
	if ((caps & WL_SEAT_CAPABILITY_POINTER) && wl.pointer == NULL) {
		wl.pointer = wl_seat_get_pointer(seat);
		wl_pointer_add_listener(wl.pointer, &pointer_listener, NULL);
	}
	if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && wl.keyboard == NULL) {
		wl.keyboard = wl_seat_get_keyboard(seat);
		wl_keyboard_add_listener(wl.keyboard, &keyboard_listener, NULL);
	}
}

static void seat_name(void *data, struct wl_seat *seat, const char *name) {}

static const struct wl_seat_listener seat_listener = {seat_capabilities, seat_name};

static void registry_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
	if (strcmp(interface, wl_compositor_interface.name) == 0) {
		wl.compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
	}
	else if (strcmp(interface, wl_shm_interface.name) == 0) {
		wl.shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	}
	else if (strcmp(interface, wl_seat_interface.name) == 0 && wl.seat == NULL) {
		wl.seat = wl_registry_bind(registry, name, &wl_seat_interface, version < 7 ? version : 7); // 8 replaces axis_discrete
		wl_seat_add_listener(wl.seat, &seat_listener, NULL);
	}
	else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
		wl.wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
		xdg_wm_base_add_listener(wl.wm_base, &wm_base_listener, NULL);
	}
	else if (strcmp(interface, wl_data_device_manager_interface.name) == 0) {
		wl.data_device_manager = wl_registry_bind(registry, name, &wl_data_device_manager_interface, version < 3 ? version : 3);
	}
	else if (strcmp(interface, wp_viewporter_interface.name) == 0) {
		wl.viewporter = wl_registry_bind(registry, name, &wp_viewporter_interface, 1);
	}
	else if (strcmp(interface, wp_fractional_scale_manager_v1_interface.name) == 0) {
		wl.fractional_scale_manager = wl_registry_bind(registry, name, &wp_fractional_scale_manager_v1_interface, 1);
	}
	else if (strcmp(interface, zwp_tablet_manager_v2_interface.name) == 0) {
		wl.tablet_manager = wl_registry_bind(registry, name, &zwp_tablet_manager_v2_interface, 1);
	}
	else if (strcmp(interface, zxdg_output_manager_v1_interface.name) == 0) {
		wl.output_manager = wl_registry_bind(registry, name, &zxdg_output_manager_v1_interface, 1);
	}
	else if (strcmp(interface, zxdg_decoration_manager_v1_interface.name) == 0) {
		wl.decoration_manager = wl_registry_bind(registry, name, &zxdg_decoration_manager_v1_interface, 1);
	}
	else if (strcmp(interface, wl_output_interface.name) == 0 && wl.num_displays < MAXIMUM_DISPLAYS) {
		wl_display_info_t *d = &wl.displays[wl.num_displays++];
		d->output            = wl_registry_bind(registry, name, &wl_output_interface, version < 4 ? version : 4);
		wl_output_add_listener(d->output, &output_listener, d);
	}
}

static void registry_global_remove(void *data, struct wl_registry *registry, uint32_t name) {}

static const struct wl_registry_listener registry_listener = {registry_global, registry_global_remove};

static void wayland_init(void) {
	wl.display = wl_display_connect(NULL);
	if (wl.display == NULL) {
		iron_error("Could not connect to the Wayland display");
		exit(1);
	}
	wl.xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	wl.registry    = wl_display_get_registry(wl.display);
	wl_registry_add_listener(wl.registry, &registry_listener, NULL);
	wl_display_roundtrip(wl.display); // Globals
	wl_display_roundtrip(wl.display); // Seat capabilities, output modes

	if (wl.output_manager != NULL) { // Logical output sizes
		for (int i = 0; i < wl.num_displays; ++i) {
			struct zxdg_output_v1 *output = zxdg_output_manager_v1_get_xdg_output(wl.output_manager, wl.displays[i].output);
			zxdg_output_v1_add_listener(output, &xdg_output_listener, &wl.displays[i]);
		}
		wl_display_roundtrip(wl.display);
	}

	if (wl.compositor == NULL || wl.wm_base == NULL) {
		iron_error("Wayland compositor does not support xdg_wm_base");
		exit(1);
	}
	if (wl.tablet_manager != NULL && wl.seat != NULL) {
		struct zwp_tablet_seat_v2 *tablet_seat = zwp_tablet_manager_v2_get_tablet_seat(wl.tablet_manager, wl.seat);
		zwp_tablet_seat_v2_add_listener(tablet_seat, &tablet_seat_listener, NULL);
	}
	if (wl.data_device_manager != NULL && wl.seat != NULL) {
		wl.data_device = wl_data_device_manager_get_data_device(wl.data_device_manager, wl.seat);
		wl_data_device_add_listener(wl.data_device, &data_device_listener, NULL);
	}
	if (wl.shm != NULL) {
		const char *theme      = getenv("XCURSOR_THEME");
		const char *size       = getenv("XCURSOR_SIZE");
		wl.cursor_theme        = wl_cursor_theme_load(theme, size != NULL ? atoi(size) : 24, wl.shm);
		wl.cursor_surface      = wl_compositor_create_surface(wl.compositor);
		wl.tool_cursor_surface = wl_compositor_create_surface(wl.compositor);
	}
}

// System

bool iron_internal_handle_messages() {
	while (wl_display_prepare_read(wl.display) != 0) {
		wl_display_dispatch_pending(wl.display);
	}
	wl_display_flush(wl.display);
	struct pollfd pfd = {wl_display_get_fd(wl.display), POLLIN, 0};
	if (poll(&pfd, 1, 0) > 0) {
		wl_display_read_events(wl.display);
	}
	else {
		wl_display_cancel_read(wl.display);
	}
	wl_display_dispatch_pending(wl.display);
	key_repeat();
	return true;
}

const char *iron_system_id() {
	return "Linux";
}

void iron_set_keep_screen_on(bool on) {}

void iron_load_url(const char *url) {
	if (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0) {
		// No shell, '&' and other metacharacters in the url are passed as is
		pid_t pid = fork();
		if (pid == 0) {
			execlp("xdg-open", "xdg-open", url, (char *)NULL);
			_exit(1);
		}
		else if (pid > 0) {
			int status;
			waitpid(pid, &status, 0);
		}
	}
}

const char *iron_language() {
	return "en";
}

const char *iron_internal_save_path() {
	if (!save_initialized) {
		const char *homedir = getenv("HOME");
		if (homedir == NULL) {
			homedir = getpwuid(getuid())->pw_dir;
		}
		snprintf(save, sizeof(save), "%s/.%s/", homedir, iron_application_name());
		struct stat st;
		if (stat(save, &st) != 0) {
			const char *data_home = getenv("XDG_DATA_HOME");
			if (data_home == NULL) {
				snprintf(save, sizeof(save), "%s/.local/share/%s/", homedir, iron_application_name());
			}
			else {
				snprintf(save, sizeof(save), "%s/%s/", data_home, iron_application_name());
			}
			if (mkdir(save, 0700) != 0 && errno != EEXIST) {
				iron_error("Could not create save directory '%s'. Error %d", save, errno);
			}
		}
		save_initialized = true;
	}
	return save;
}

const char **iron_video_formats() {
	return video_formats;
}

double iron_frequency(void) {
	return 1000000.0;
}

uint64_t iron_timestamp(void) {
	struct timeval now;
	gettimeofday(&now, NULL);
	now.tv_sec -= start.tv_sec;
	now.tv_usec -= start.tv_usec;
	return (uint64_t)now.tv_sec * 1000000 + (uint64_t)now.tv_usec;
}

int iron_hardware_threads(void) {
	return sysconf(_SC_NPROCESSORS_ONLN);
}

void iron_init(iron_window_options_t *ops) {
	gettimeofday(&start, NULL);
	wayland_init();
	iron_set_app_name(ops->title);
	iron_window_create(ops);
}

void gpu_destroy(void);

void iron_internal_shutdown() {
	free(clipboard_string);
	gpu_destroy();
	wl_display_disconnect(wl.display);
	iron_internal_shutdown_callback();
}

int main(int argc, char **argv) {
	return kickstart(argc, argv);
}

#ifdef WITH_GAMEPAD
const char *iron_gamepad_vendor(int gamepad) {
	return "";
}

const char *iron_gamepad_product_name(int gamepad) {
	return "";
}

bool iron_gamepad_connected(int gamepad) {
	return false;
}

void iron_gamepad_rumble(int gamepad, float left, float right) {}
#endif

extern void (*iron_save_and_quit)(bool);

#ifdef WITH_NFD // Has gtk
#include <gtk/gtk.h>
static bool iron_gtk_setlocale_disabled = false;
void        iron_gtk_disable_setlocale() {
	if (iron_gtk_setlocale_disabled) {
		return;
	}
	gtk_disable_setlocale();
	iron_gtk_setlocale_disabled = true;
}
#endif

bool _save_and_quit_callback_internal() {
	bool save = false;
#ifdef WITH_NFD // Has gtk
	if (strstr(wl.title, "* - ArmorPaint") != NULL) {
		iron_gtk_disable_setlocale();
		gtk_init(NULL, NULL);
		GtkWidget *dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_YES_NO, "Project has been modified, save changes?");
		gtk_window_set_title(GTK_WINDOW(dialog), "Save Changes?");
		gtk_widget_realize(dialog);
		gint res = gtk_dialog_run(GTK_DIALOG(dialog));
		gtk_widget_destroy(dialog);
		while (g_main_context_pending(NULL)) {
			g_main_context_iteration(NULL, FALSE);
		}
		if (res == GTK_RESPONSE_YES) {
			save = true;
		}
		else if (res != GTK_RESPONSE_NO) {
			return false;
		}
	}
#endif
	iron_save_and_quit(save);
	return false;
}

static pid_t child_pid                   = -1;
volatile int iron_exec_async_done        = 1;
char        *iron_exec_async_output_file = NULL;

static void iron_exec_handler(int sig) {
	int status;
	if (waitpid(child_pid, &status, WNOHANG) > 0) {
		iron_exec_async_done = 1;
	}
}

void iron_exec_async(const char *path, char *argv[]) {
	iron_exec_async_done = 0;
	pid_t parent_pid     = getpid();
	child_pid            = fork();
	if (child_pid == 0) {
		prctl(PR_SET_PDEATHSIG, SIGTERM);
		if (getppid() != parent_pid) {
			_exit(1);
		}
		int fd = iron_exec_async_output_file != NULL ? open(iron_exec_async_output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644) : open("/dev/null", O_WRONLY);
		dup2(fd, STDOUT_FILENO);
		dup2(fd, STDERR_FILENO);
		if (fd > STDERR_FILENO) {
			close(fd);
		}
		int in_fd = open("/dev/null", O_RDONLY);
		dup2(in_fd, STDIN_FILENO);
		if (in_fd > STDERR_FILENO) {
			close(in_fd);
		}
		char *home = getenv("HOME");
		char *env  = getenv("PATH");
		if (home != NULL) {
			char new_path[4096];
			snprintf(new_path, sizeof(new_path), "%s/.local/bin:%s/bin:%s", home, home, env);
			setenv("PATH", new_path, 1);
		}
		execvp(path, argv);
		_exit(1);
	}
	else {
		struct sigaction sa = {0};
		sa.sa_handler       = iron_exec_handler;
		sa.sa_flags         = SA_RESTART;
		sigemptyset(&sa.sa_mask);
		sigaction(SIGCHLD, &sa, NULL);
	}
}
