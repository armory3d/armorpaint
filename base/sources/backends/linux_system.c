#include "linux_system.h"
#include <X11/XKBlib.h>
#include <X11/Xcursor/Xcursor.h>
#include <X11/Xlib.h>
#include <ctype.h>
#include <errno.h>
#include <iron_gpu.h>
#include <iron_system.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_xlib.h>

#define Button6 6
#define Button7 7

static struct x11_context x11_ctx             = {0};
static size_t             clipboardStringSize = 1024;
static char              *clipboardString     = NULL;
static char               buffer[1024];
static char               save[2000];
static bool               saveInitialized = false;
static const char        *videoFormats[]  = {"ogv", NULL};
static struct timeval     start;
static bool               mouse_hidden = false;
static int                xi_extension = -1;
static Atom               XdndAware;
static Atom               XdndDrop;
static Atom               XdndEnter;
static Atom               XdndTextUriList;
static Atom               XdndStatus;
static Atom               XdndActionCopy;
static Atom               XdndSelection;
static Atom               CLIPBOARD;
static Atom               UTF8_STRING;
static Atom               XSEL_DATA;
static Atom               TARGETS;
static Atom               MULTIPLE;
static Atom               TEXT_PLAIN;
static Atom               WM_DELETE_WINDOW;
static Atom               NET_WM_NAME;
static Atom               NET_WM_ICON_NAME;
static Atom               NET_WM_STATE;
static Atom               NET_WM_STATE_FULLSCREEN;

static struct {
	void (*resize_callback)(int width, int height, void *data);
	void *resize_data;
	void *ppi_data;
	bool (*close_callback)(void *data);
	void *close_data;
} iron_internal_window_callbacks[1];

int iron_x11_error_handler(Display *display, XErrorEvent *error_event) {
	XGetErrorText(display, error_event->error_code, buffer, 1024);
	iron_error("X Error: %s", buffer);
	return 0;
}

void iron_x11_init() {
	XInitThreads(); // Fixes random ubuntu22 crash
	x11_ctx.display = XOpenDisplay(NULL);
	if (!x11_ctx.display) {
		return;
	}
	XSetErrorHandler(iron_x11_error_handler);
	clipboardString = (char *)malloc(clipboardStringSize);

	XdndAware               = XInternAtom(x11_ctx.display, "XdndAware", False);
	XdndDrop                = XInternAtom(x11_ctx.display, "XdndDrop", False);
	XdndEnter               = XInternAtom(x11_ctx.display, "XdndEnter", False);
	XdndTextUriList         = XInternAtom(x11_ctx.display, "text/uri-list", False);
	XdndStatus              = XInternAtom(x11_ctx.display, "XdndStatus", False);
	XdndActionCopy          = XInternAtom(x11_ctx.display, "XdndActionCopy", False);
	XdndSelection           = XInternAtom(x11_ctx.display, "XdndSelection", False);
	CLIPBOARD               = XInternAtom(x11_ctx.display, "CLIPBOARD", False);
	UTF8_STRING             = XInternAtom(x11_ctx.display, "UTF8_STRING", False);
	XSEL_DATA               = XInternAtom(x11_ctx.display, "XSEL_DATA", False);
	TARGETS                 = XInternAtom(x11_ctx.display, "TARGETS", False);
	MULTIPLE                = XInternAtom(x11_ctx.display, "MULTIPLE", False);
	TEXT_PLAIN              = XInternAtom(x11_ctx.display, "text/plain;charset=utf-8", False);
	WM_DELETE_WINDOW        = XInternAtom(x11_ctx.display, "WM_DELETE_WINDOW", False);
	NET_WM_NAME             = XInternAtom(x11_ctx.display, "_NET_WM_NAME", False);
	NET_WM_ICON_NAME        = XInternAtom(x11_ctx.display, "_NET_WM_ICON_NAME", False);
	NET_WM_STATE            = XInternAtom(x11_ctx.display, "_NET_WM_STATE", False);
	NET_WM_STATE_FULLSCREEN = XInternAtom(x11_ctx.display, "_NET_WM_STATE_FULLSCREEN", False);

	int event, error;
	XQueryExtension(x11_ctx.display, "XInputExtension", &xi_extension, &event, &error);
	int           num_devices;
	XIDeviceInfo *devices = XIQueryDevice(x11_ctx.display, XIAllDevices, &num_devices);
	x11_ctx.pen.id        = -1;
	x11_ctx.eraser.id     = -1;
	for (int i = 0; i < num_devices; i++) {
		XIDeviceInfo *info = &devices[i];
		strncpy(buffer, info->name, 1023);
		buffer[1023] = 0;
		for (int j = 0; buffer[j]; j++) {
			buffer[j] = tolower(buffer[j]);
		}

		bool is_pen    = strstr(buffer, "stylus") || strstr(buffer, "pen") || strstr(buffer, "wacom");
		bool is_eraser = strstr(buffer, "eraser");
		if (!is_pen && !is_eraser) {
			continue;
		}

		int    pressure_index = -1;
		double max            = 0;
		for (int j = 0; j < info->num_classes; j++) {
			if (info->classes[j]->type == XIValuatorClass) {
				XIValuatorClassInfo *val = (XIValuatorClassInfo *)info->classes[j];
				if (val->label == XInternAtom(x11_ctx.display, "Abs Pressure", False)) {
					pressure_index = val->number;
					max            = val->max;
					break;
				}
			}
		}
		if (pressure_index == -1)
			continue;

		struct x11_pen_device *d = is_pen ? &x11_ctx.pen : &x11_ctx.eraser;
		d->id                    = info->deviceid;
		d->pressure_index        = pressure_index;
		d->pressure_max          = max;
		d->press                 = is_pen ? iron_internal_pen_trigger_press : iron_internal_eraser_trigger_press;
		d->move                  = is_pen ? iron_internal_pen_trigger_move : iron_internal_eraser_trigger_move;
		d->release               = is_pen ? iron_internal_pen_trigger_release : iron_internal_eraser_trigger_release;
	}
	XIFreeDeviceInfo(devices);
}

void iron_display_init() {
	static bool display_initialized = false;
	if (display_initialized) {
		return;
	}

	iron_x11_init();

	Window              root_window      = RootWindow(x11_ctx.display, DefaultScreen(x11_ctx.display));
	XRRScreenResources *screen_resources = XRRGetScreenResourcesCurrent(x11_ctx.display, root_window);
	RROutput            primary_output   = XRRGetOutputPrimary(x11_ctx.display, root_window);

	for (int i = 0; i < screen_resources->noutput; i++) {
		if (i >= MAXIMUM_DISPLAYS) {
			iron_error("Too many screens (maximum %i)", MAXIMUM_DISPLAYS);
			break;
		}

		XRROutputInfo *output_info = XRRGetOutputInfo(x11_ctx.display, screen_resources, screen_resources->outputs[i]);
		if (output_info->connection != RR_Connected || output_info->crtc == None) {
			XRRFreeOutputInfo(output_info);
			continue;
		}

		XRRCrtcInfo *crtc_info = XRRGetCrtcInfo(x11_ctx.display, screen_resources, output_info->crtc);

		struct iron_x11_display *display = &x11_ctx.displays[x11_ctx.num_displays++];
		display->index                   = i;
		display->x                       = crtc_info->x;
		display->y                       = crtc_info->y;
		display->width                   = crtc_info->width;
		display->height                  = crtc_info->height;
		display->primary                 = screen_resources->outputs[i] == primary_output;
		display->crtc                    = output_info->crtc;
		display->output                  = screen_resources->outputs[i];

		XRRFreeOutputInfo(output_info);
		XRRFreeCrtcInfo(crtc_info);
	}

	XRRFreeScreenResources(screen_resources);
	display_initialized = true;
}

iron_display_mode_t iron_display_current_mode(int display_index) {
	struct iron_x11_display *display = &x11_ctx.displays[display_index];
	iron_display_mode_t      mode;
	mode.x               = 0;
	mode.y               = 0;
	mode.width           = display->width;
	mode.height          = display->height;
	mode.frequency       = 60;
	mode.bits_per_pixel  = 32;
	mode.pixels_per_inch = 96;

	Window              root_window      = DefaultRootWindow(x11_ctx.display);
	XRRScreenResources *screen_resources = XRRGetScreenResourcesCurrent(x11_ctx.display, root_window);

	XRROutputInfo *output_info = XRRGetOutputInfo(x11_ctx.display, screen_resources, screen_resources->outputs[display->index]);
	if (output_info->connection != RR_Connected || output_info->crtc == None) {
		XRRFreeOutputInfo(output_info);
		XRRFreeScreenResources(screen_resources);
		return mode;
	}

	XRRCrtcInfo *crtc_info = XRRGetCrtcInfo(x11_ctx.display, screen_resources, output_info->crtc);
	for (int j = 0; j < output_info->nmode; j++) {
		RRMode       rr_mode   = crtc_info->mode;
		XRRModeInfo *mode_info = NULL;
		for (int k = 0; k < screen_resources->nmode; k++) {
			if (screen_resources->modes[k].id == rr_mode) {
				mode_info = &screen_resources->modes[k];
				break;
			}
		}

		if (mode_info == NULL) {
			continue;
		}
		mode.x               = display->x;
		mode.y               = display->y;
		mode.width           = mode_info->width;
		mode.height          = mode_info->height;
		mode.pixels_per_inch = 96;
		mode.bits_per_pixel  = 32;
		if (mode_info->hTotal && mode_info->vTotal) {
			mode.frequency = (mode_info->dotClock / (mode_info->hTotal * mode_info->vTotal));
		}
	}
	XRRFreeOutputInfo(output_info);
	XRRFreeCrtcInfo(crtc_info);
	XRRFreeScreenResources(screen_resources);
	return mode;
}

int iron_primary_display(void) {
	for (int i = 0; i < x11_ctx.num_displays; i++) {
		if (x11_ctx.displays[i].primary) {
			return i;
		}
	}
	return 0;
}

int iron_count_displays(void) {
	return x11_ctx.num_displays;
}

int iron_window_x() {
	return 0;
}

int iron_window_y() {
	return 0;
}

int iron_window_width() {
	return x11_ctx.windows[0].width;
}

int iron_window_height() {
	return x11_ctx.windows[0].height;
}

void iron_window_resize(int width, int height) {
	struct iron_x11_window *window = &x11_ctx.windows[0];
	XResizeWindow(x11_ctx.display, window->window, width, height);
}

void iron_window_move(int x, int y) {
	struct iron_x11_window *window = &x11_ctx.windows[0];
	XMoveWindow(x11_ctx.display, window->window, x, y);
}

void iron_window_change_mode(iron_window_mode_t mode) {
	struct iron_x11_window *window = &x11_ctx.windows[0];
	if (mode == window->mode) {
		return;
	}
	window->mode = mode;
	XEvent xev;
	memset(&xev, 0, sizeof(xev));
	xev.type                 = ClientMessage;
	xev.xclient.window       = window->window;
	xev.xclient.message_type = NET_WM_STATE;
	xev.xclient.format       = 32;
	xev.xclient.data.l[0]    = mode == IRON_WINDOW_MODE_FULLSCREEN ? 1 : 0;
	xev.xclient.data.l[1]    = NET_WM_STATE_FULLSCREEN;
	xev.xclient.data.l[2]    = 0;
	XMapWindow(x11_ctx.display, window->window);
	XSendEvent(x11_ctx.display, DefaultRootWindow(x11_ctx.display), False, SubstructureRedirectMask | SubstructureNotifyMask, &xev);
	XFlush(x11_ctx.display);
}

int iron_window_display() {
	struct iron_x11_window *window = &x11_ctx.windows[0];
	return window->display_index;
}

void iron_window_destroy() {
	XFlush(x11_ctx.display);
	struct iron_x11_window *window = &x11_ctx.windows[0];
	XDestroyIC(window->xInputContext);
	XCloseIM(window->xInputMethod);
	XDestroyWindow(x11_ctx.display, window->window);
	XFlush(x11_ctx.display);
	*window = (struct iron_x11_window){0};
}

void iron_window_show() {
	struct iron_x11_window *window = &x11_ctx.windows[0];
	XMapWindow(x11_ctx.display, window->window);
}

void iron_window_hide() {
	struct iron_x11_window *window = &x11_ctx.windows[0];
	XUnmapWindow(x11_ctx.display, window->window);
}

void iron_window_set_title(const char *title) {
	struct iron_x11_window *window = &x11_ctx.windows[0];
	XChangeProperty(x11_ctx.display, window->window, NET_WM_NAME, UTF8_STRING, 8, PropModeReplace, (unsigned char *)title, strlen(title));
	XChangeProperty(x11_ctx.display, window->window, NET_WM_ICON_NAME, UTF8_STRING, 8, PropModeReplace, (unsigned char *)title, strlen(title));
	XFlush(x11_ctx.display);
}

void iron_window_create(iron_window_options_t *win) {
	struct iron_x11_window *window = &x11_ctx.windows[0];
	window->width                  = win->width;
	window->height                 = win->height;

	Visual              *visual             = NULL;
	XSetWindowAttributes set_window_attribs = {0};

	XColor   color;
	Colormap colormap = DefaultColormap(x11_ctx.display, DefaultScreen(x11_ctx.display));
	color.red         = (32 * 65535) / 255;
	color.green       = (32 * 65535) / 255;
	color.blue        = (32 * 65535) / 255;
	XAllocColor(x11_ctx.display, colormap, &color);
	set_window_attribs.background_pixel = color.pixel;

	set_window_attribs.border_pixel = 0;
	set_window_attribs.event_mask =
	    KeyPressMask | KeyReleaseMask | ExposureMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | StructureNotifyMask | FocusChangeMask;
	int screen                  = DefaultScreen(x11_ctx.display);
	visual                      = DefaultVisual(x11_ctx.display, screen);
	int depth                   = DefaultDepth(x11_ctx.display, screen);
	set_window_attribs.colormap = XCreateColormap(x11_ctx.display, RootWindow(x11_ctx.display, screen), visual, AllocNone);
	window->window = XCreateWindow(x11_ctx.display, RootWindow(x11_ctx.display, DefaultScreen(x11_ctx.display)), 0, 0, win->width, win->height, 0, depth,
	                               InputOutput, visual, CWBackPixel | CWBorderPixel | CWColormap | CWEventMask, &set_window_attribs);

	static char        nameClass[256];
	static const char *nameClassAddendum = "_IronApplication";
	strncpy(nameClass, iron_application_name(), sizeof(nameClass) - strlen(nameClassAddendum) - 1);
	strcat(nameClass, nameClassAddendum);
	char resNameBuffer[256];
	strncpy(resNameBuffer, iron_application_name(), 256);
	XClassHint classHint = {.res_name = resNameBuffer, .res_class = nameClass};
	XSetClassHint(x11_ctx.display, window->window, &classHint);

	XSetLocaleModifiers("@im=none");
	window->xInputMethod  = XOpenIM(x11_ctx.display, NULL, NULL, NULL);
	window->xInputContext = XCreateIC(window->xInputMethod, XNInputStyle, XIMPreeditNothing | XIMStatusNothing, XNClientWindow, window->window, NULL);
	XSetICFocus(window->xInputContext);

	window->mode = IRON_WINDOW_MODE_WINDOW;
	iron_window_change_mode(win->mode);

	if (win->visible) {
		XMapWindow(x11_ctx.display, window->window);
	}

	Atom XdndVersion = 5;
	XChangeProperty(x11_ctx.display, window->window, XdndAware, XA_ATOM, 32, PropModeReplace, (unsigned char *)&XdndVersion, 1);
	XSetWMProtocols(x11_ctx.display, window->window, &WM_DELETE_WINDOW, 1);

	iron_window_set_title(win->title);

	if (x11_ctx.pen.id != -1) {
		XIEventMask   mask;
		unsigned char mask_data[XIMaskLen(XI_LASTEVENT)];
		memset(mask_data, 0, sizeof(mask_data));
		mask.deviceid = x11_ctx.pen.id;
		mask.mask_len = sizeof(mask_data);
		mask.mask     = mask_data;
		XISetMask(mask.mask, XI_Motion);
		XISetMask(mask.mask, XI_ButtonPress);
		XISetMask(mask.mask, XI_ButtonRelease);
		XISelectEvents(x11_ctx.display, window->window, &mask, 1);
	}

	gpu_init(win->depth_bits, win->vsync);
}

void iron_window_set_resize_callback(void (*callback)(int width, int height, void *data), void *data) {
	iron_internal_window_callbacks[0].resize_callback = callback;
	iron_internal_window_callbacks[0].resize_data     = data;
}

void iron_internal_call_resize_callback(int width, int height) {
	if (iron_internal_window_callbacks[0].resize_callback != NULL) {
		iron_internal_window_callbacks[0].resize_callback(width, height, iron_internal_window_callbacks[0].resize_data);
	}
}

void iron_window_set_close_callback(bool (*callback)(void *data), void *data) {
	iron_internal_window_callbacks[0].close_callback = callback;
	iron_internal_window_callbacks[0].close_data     = data;
}

bool iron_internal_call_close_callback() {
	if (iron_internal_window_callbacks[0].close_callback != NULL) {
		return iron_internal_window_callbacks[0].close_callback(iron_internal_window_callbacks[0].close_data);
	}
	return true;
}

iron_window_mode_t iron_window_get_mode() {
	return x11_ctx.windows[0].mode;
}

static void check_pen_device(struct iron_x11_window *window, XEvent *event, struct x11_pen_device *pen) {
	if (!XGetEventData(x11_ctx.display, &event->xcookie)) {
		return;
	}

	XIDeviceEvent *dev_event = (XIDeviceEvent *)event->xcookie.data;
	if (dev_event->event != window->window || dev_event->deviceid != pen->id) {
		XFreeEventData(x11_ctx.display, &event->xcookie);
		return;
	}

	float p = pen->pressure_current;
	if (XIMaskIsSet(dev_event->valuators.mask, pen->pressure_index)) {
		double *values = dev_event->valuators.values;
		int     idx    = 0;
		for (int i = 0; i < pen->pressure_index; i++) {
			if (XIMaskIsSet(dev_event->valuators.mask, i))
				idx++;
		}
		p = values[idx] / pen->pressure_max;
	}

	if (event->xcookie.evtype == XI_ButtonPress && dev_event->detail == 1) {
		pen->press(dev_event->event_x, dev_event->event_y, p);
	}
	else if (event->xcookie.evtype == XI_ButtonRelease && dev_event->detail == 1) {
		pen->release(dev_event->event_x, dev_event->event_y, p);
	}
	else if (event->xcookie.evtype == XI_Motion && p > 0) {
		pen->move(dev_event->event_x, dev_event->event_y, p);
	}
	pen->pressure_current = p;

	XFreeEventData(x11_ctx.display, &event->xcookie);
}

struct iron_x11_window *window_from_window(Window window) {
	for (int i = 0; i < 1; i++) {
		if (x11_ctx.windows[i].window == window) {
			return &x11_ctx.windows[i];
		}
	}
	return NULL;
}

void iron_vulkan_get_instance_extensions(const char **names, int *index) {
	names[(*index)++] = VK_KHR_XLIB_SURFACE_EXTENSION_NAME;
}

VkBool32 iron_vulkan_get_physical_device_presentation_support(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex) {
	return vkGetPhysicalDeviceXlibPresentationSupportKHR(physicalDevice, queueFamilyIndex, x11_ctx.display,
	                                                     DefaultVisual(x11_ctx.display, DefaultScreen(x11_ctx.display))->visualid);
}
VkResult iron_vulkan_create_surface(VkInstance instance, VkSurfaceKHR *surface) {
	VkXlibSurfaceCreateInfoKHR info = {0};
	info.sType                      = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
	info.pNext                      = NULL;
	info.flags                      = 0;
	info.dpy                        = x11_ctx.display;
	info.window                     = x11_ctx.windows[0].window;
	return vkCreateXlibSurfaceKHR(instance, &info, NULL, surface);
}

static int xk_to_iron(KeySym symbol) {
	if (symbol == XK_Right)
		return KEY_CODE_RIGHT;
	if (symbol == XK_Left)
		return KEY_CODE_LEFT;
	if (symbol == XK_Up)
		return KEY_CODE_UP;
	if (symbol == XK_Down)
		return KEY_CODE_DOWN;
	if (symbol == XK_space)
		return KEY_CODE_SPACE;
	if (symbol == XK_BackSpace)
		return KEY_CODE_BACKSPACE;
	if (symbol == XK_Tab)
		return KEY_CODE_TAB;
	if (symbol == XK_Return)
		return KEY_CODE_RETURN;
	if (symbol == XK_Shift_L)
		return KEY_CODE_SHIFT;
	if (symbol == XK_Shift_R)
		return KEY_CODE_SHIFT;
	if (symbol == XK_Control_L)
		return KEY_CODE_CONTROL;
	if (symbol == XK_Control_R)
		return KEY_CODE_CONTROL;
	if (symbol == XK_Alt_L)
		return KEY_CODE_ALT;
	if (symbol == XK_Alt_R)
		return KEY_CODE_ALT;
	if (symbol == XK_ISO_Prev_Group)
		return KEY_CODE_ALT; // XK_ISO_Prev_Group received instead of XK_Alt_L
	if (symbol == XK_ISO_Next_Group)
		return KEY_CODE_ALT; // XK_ISO_Next_Group received instead of XK_Alt_R
	if (symbol == XK_Delete)
		return KEY_CODE_DELETE;
	if (symbol == XK_comma)
		return KEY_CODE_COMMA;
	if (symbol == XK_period)
		return KEY_CODE_PERIOD;
	if (symbol == XK_bracketleft)
		return KEY_CODE_OPEN_BRACKET;
	if (symbol == XK_bracketright)
		return KEY_CODE_CLOSE_BRACKET;
	if (symbol == XK_braceleft)
		return KEY_CODE_OPEN_CURLY_BRACKET;
	if (symbol == XK_braceright)
		return KEY_CODE_CLOSE_CURLY_BRACKET;
	if (symbol == XK_parenleft)
		return KEY_CODE_OPEN_PAREN;
	if (symbol == XK_parenright)
		return KEY_CODE_CLOSE_PAREN;
	if (symbol == XK_backslash)
		return KEY_CODE_BACK_SLASH;
	if (symbol == XK_apostrophe)
		return KEY_CODE_QUOTE;
	if (symbol == XK_colon)
		return KEY_CODE_COLON;
	if (symbol == XK_semicolon)
		return KEY_CODE_SEMICOLON;
	if (symbol == XK_minus)
		return KEY_CODE_HYPHEN_MINUS;
	if (symbol == XK_underscore)
		return KEY_CODE_UNDERSCORE;
	if (symbol == XK_slash)
		return KEY_CODE_SLASH;
	if (symbol == XK_bar)
		return KEY_CODE_PIPE;
	if (symbol == XK_question)
		return KEY_CODE_QUESTIONMARK;
	if (symbol == XK_less)
		return KEY_CODE_LESS_THAN;
	if (symbol == XK_greater)
		return KEY_CODE_GREATER_THAN;
	if (symbol == XK_asterisk)
		return KEY_CODE_ASTERISK;
	if (symbol == XK_ampersand)
		return KEY_CODE_AMPERSAND;
	if (symbol == XK_percent)
		return KEY_CODE_PERCENT;
	if (symbol == XK_dollar)
		return KEY_CODE_DOLLAR;
	if (symbol == XK_numbersign)
		return KEY_CODE_HASH;
	if (symbol == XK_at)
		return KEY_CODE_AT;
	if (symbol == XK_exclam)
		return KEY_CODE_EXCLAMATION;
	if (symbol == XK_equal)
		return KEY_CODE_EQUALS;
	if (symbol == XK_plus)
		return KEY_CODE_ADD;
	if (symbol == XK_quoteleft)
		return KEY_CODE_BACK_QUOTE;
	if (symbol == XK_quotedbl)
		return KEY_CODE_DOUBLE_QUOTE;
	if (symbol == XK_asciitilde)
		return KEY_CODE_TILDE;
	if (symbol == XK_Pause)
		return KEY_CODE_PAUSE;
	if (symbol == XK_Scroll_Lock)
		return KEY_CODE_SCROLL_LOCK;
	if (symbol == XK_Home)
		return KEY_CODE_HOME;
	if (symbol == XK_Page_Up)
		return KEY_CODE_PAGE_UP;
	if (symbol == XK_Page_Down)
		return KEY_CODE_PAGE_DOWN;
	if (symbol == XK_End)
		return KEY_CODE_END;
	if (symbol == XK_Insert)
		return KEY_CODE_INSERT;
	if (symbol == XK_KP_Enter)
		return KEY_CODE_RETURN;
	if (symbol == XK_KP_Multiply)
		return KEY_CODE_MULTIPLY;
	if (symbol == XK_KP_Add)
		return KEY_CODE_ADD;
	if (symbol == XK_KP_Subtract)
		return KEY_CODE_SUBTRACT;
	if (symbol == XK_KP_Decimal)
		return KEY_CODE_DECIMAL;
	if (symbol == XK_KP_Divide)
		return KEY_CODE_DIVIDE;
	if (symbol == XK_KP_0)
		return KEY_CODE_NUMPAD_0;
	if (symbol == XK_KP_1)
		return KEY_CODE_NUMPAD_1;
	if (symbol == XK_KP_2)
		return KEY_CODE_NUMPAD_2;
	if (symbol == XK_KP_3)
		return KEY_CODE_NUMPAD_3;
	if (symbol == XK_KP_4)
		return KEY_CODE_NUMPAD_4;
	if (symbol == XK_KP_5)
		return KEY_CODE_NUMPAD_5;
	if (symbol == XK_KP_6)
		return KEY_CODE_NUMPAD_6;
	if (symbol == XK_KP_7)
		return KEY_CODE_NUMPAD_7;
	if (symbol == XK_KP_8)
		return KEY_CODE_NUMPAD_8;
	if (symbol == XK_KP_9)
		return KEY_CODE_NUMPAD_9;
	if (symbol == XK_KP_Insert)
		return KEY_CODE_INSERT;
	if (symbol == XK_KP_Delete)
		return KEY_CODE_DELETE;
	if (symbol == XK_KP_End)
		return KEY_CODE_END;
	if (symbol == XK_KP_Home)
		return KEY_CODE_HOME;
	if (symbol == XK_KP_Left)
		return KEY_CODE_LEFT;
	if (symbol == XK_KP_Up)
		return KEY_CODE_UP;
	if (symbol == XK_KP_Right)
		return KEY_CODE_RIGHT;
	if (symbol == XK_KP_Down)
		return KEY_CODE_DOWN;
	if (symbol == XK_KP_Page_Up)
		return KEY_CODE_PAGE_UP;
	if (symbol == XK_KP_Page_Down)
		return KEY_CODE_PAGE_DOWN;
	if (symbol == XK_Menu)
		return KEY_CODE_CONTEXT_MENU;
	if (symbol == XK_a)
		return KEY_CODE_A;
	if (symbol == XK_b)
		return KEY_CODE_B;
	if (symbol == XK_c)
		return KEY_CODE_C;
	if (symbol == XK_d)
		return KEY_CODE_D;
	if (symbol == XK_e)
		return KEY_CODE_E;
	if (symbol == XK_f)
		return KEY_CODE_F;
	if (symbol == XK_g)
		return KEY_CODE_G;
	if (symbol == XK_h)
		return KEY_CODE_H;
	if (symbol == XK_i)
		return KEY_CODE_I;
	if (symbol == XK_j)
		return KEY_CODE_J;
	if (symbol == XK_k)
		return KEY_CODE_K;
	if (symbol == XK_l)
		return KEY_CODE_L;
	if (symbol == XK_m)
		return KEY_CODE_M;
	if (symbol == XK_n)
		return KEY_CODE_N;
	if (symbol == XK_o)
		return KEY_CODE_O;
	if (symbol == XK_p)
		return KEY_CODE_P;
	if (symbol == XK_q)
		return KEY_CODE_Q;
	if (symbol == XK_r)
		return KEY_CODE_R;
	if (symbol == XK_s)
		return KEY_CODE_S;
	if (symbol == XK_t)
		return KEY_CODE_T;
	if (symbol == XK_u)
		return KEY_CODE_U;
	if (symbol == XK_v)
		return KEY_CODE_V;
	if (symbol == XK_w)
		return KEY_CODE_W;
	if (symbol == XK_x)
		return KEY_CODE_X;
	if (symbol == XK_y)
		return KEY_CODE_Y;
	if (symbol == XK_z)
		return KEY_CODE_Z;
	if (symbol == XK_1)
		return KEY_CODE_1;
	if (symbol == XK_2)
		return KEY_CODE_2;
	if (symbol == XK_3)
		return KEY_CODE_3;
	if (symbol == XK_4)
		return KEY_CODE_4;
	if (symbol == XK_5)
		return KEY_CODE_5;
	if (symbol == XK_6)
		return KEY_CODE_6;
	if (symbol == XK_7)
		return KEY_CODE_7;
	if (symbol == XK_8)
		return KEY_CODE_8;
	if (symbol == XK_9)
		return KEY_CODE_9;
	if (symbol == XK_0)
		return KEY_CODE_0;
	if (symbol == XK_Escape)
		return KEY_CODE_ESCAPE;
	if (symbol == XK_F1)
		return KEY_CODE_F1;
	if (symbol == XK_F2)
		return KEY_CODE_F2;
	if (symbol == XK_F3)
		return KEY_CODE_F3;
	if (symbol == XK_F4)
		return KEY_CODE_F4;
	if (symbol == XK_F5)
		return KEY_CODE_F5;
	if (symbol == XK_F6)
		return KEY_CODE_F6;
	if (symbol == XK_F7)
		return KEY_CODE_F7;
	if (symbol == XK_F8)
		return KEY_CODE_F8;
	if (symbol == XK_F9)
		return KEY_CODE_F9;
	if (symbol == XK_F10)
		return KEY_CODE_F10;
	if (symbol == XK_F11)
		return KEY_CODE_F11;
	if (symbol == XK_F12)
		return KEY_CODE_F12;
	return KEY_CODE_UNKNOWN;
}

void *gc_alloc(size_t size);

static char *uri_decode(const char *src) {
	char *res = gc_alloc(1024);
	char *dst = res;
	char  a, b;
	while (*src) {
		if ((*src == '%') && ((a = src[1]) && (b = src[2])) && (isxdigit(a) && isxdigit(b))) {
			if (a >= 'a') {
				a -= 'a' - 'A';
			}
			if (a >= 'A') {
				a -= ('A' - 10);
			}
			else {
				a -= '0';
			}
			if (b >= 'a') {
				b -= 'a' - 'A';
			}
			if (b >= 'A') {
				b -= ('A' - 10);
			}
			else {
				b -= '0';
			}
			*dst++ = 16 * a + b;
			src += 3;
		}
		else {
			*dst++ = *src++;
		}
	}
	*dst++ = '\0';
	return res;
}

static bool _handle_messages() {
	static bool controlDown             = false;
	static int  ignoreKeycode           = 0;
	static bool preventNextKeyDownEvent = false;

	while (XPending(x11_ctx.display)) {
		XEvent event;
		XNextEvent(x11_ctx.display, &event);

		if (event.type == GenericEvent && event.xcookie.extension == xi_extension) {
			check_pen_device(&x11_ctx.windows[0], &event, &x11_ctx.pen);
			check_pen_device(&x11_ctx.windows[0], &event, &x11_ctx.eraser);
			continue;
		}

		Window                  window   = event.xclient.window;
		struct iron_x11_window *k_window = window_from_window(window);
		if (k_window == NULL) {
			continue;
		}

		switch (event.type) {
		case MappingNotify: {
			XRefreshKeyboardMapping(&event.xmapping);
			break;
		}
		case KeyPress: {
			XKeyEvent *key = (XKeyEvent *)&event;
			KeySym     keysym;
			wchar_t    wchar;
			bool       wcConverted = XwcLookupString(k_window->xInputContext, key, &wchar, 1, &keysym, NULL);

			bool isIgnoredKeySym = keysym == XK_Escape || keysym == XK_BackSpace || keysym == XK_Delete;
			if (!controlDown && !XFilterEvent(&event, window) && !isIgnoredKeySym) {
				if (wcConverted) {
					iron_internal_keyboard_trigger_key_press(wchar);
				}
			}

			if (preventNextKeyDownEvent) {
				// this keypress is a repeated keystroke and should not lead to a keydown-event
				preventNextKeyDownEvent = false;
				continue;
			}

			KeySym ksKey = XkbKeycodeToKeysym(x11_ctx.display, event.xkey.keycode, 0, 0);
			if (ksKey == XK_Control_L || ksKey == XK_Control_R) {
				controlDown = true;
			}
			else if (controlDown && (ksKey == XK_v || ksKey == XK_V)) {
				XConvertSelection(x11_ctx.display, CLIPBOARD, UTF8_STRING, XSEL_DATA, window, CurrentTime);
			}
			else if (controlDown && (ksKey == XK_c || ksKey == XK_C)) {
				XSetSelectionOwner(x11_ctx.display, CLIPBOARD, window, CurrentTime);
				iron_internal_copy_callback();
			}
			else if (controlDown && (ksKey == XK_x || ksKey == XK_X)) {
				XSetSelectionOwner(x11_ctx.display, CLIPBOARD, window, CurrentTime);
				iron_internal_cut_callback();
			}

			if (event.xkey.keycode == ignoreKeycode) {
				break;
			}
			else {
				ignoreKeycode = event.xkey.keycode;
			}

			if (ksKey < 97 || ksKey > 122) {
				ksKey = keysym;
			}

			int key_code = xk_to_iron(ksKey);
			if (key_code != KEY_CODE_UNKNOWN) {
				iron_internal_keyboard_trigger_key_down(key_code);
			}

			break;
		}
		case KeyRelease: {
			XKeyEvent *key = (XKeyEvent *)&event;

			// peek next-event to determine if this a repeated-keystroke
			XEvent nev;
			if (XPending(x11_ctx.display)) {
				XPeekEvent(x11_ctx.display, &nev);

				if (nev.type == KeyPress && nev.xkey.time == event.xkey.time && nev.xkey.keycode == event.xkey.keycode) {
					// repeated keystroke! prevent this keyup-event and next keydown-event from being fired
					preventNextKeyDownEvent = true;
					continue;
				}
			}
			KeySym keysym;
			char   c;
			XLookupString(key, &c, 1, &keysym, NULL);

			KeySym ksKey = XkbKeycodeToKeysym(x11_ctx.display, event.xkey.keycode, 0, 0);
			if (ksKey == XK_Control_L || ksKey == XK_Control_R) {
				controlDown = false;
			}
			if (event.xkey.keycode == ignoreKeycode) {
				ignoreKeycode = 0;
			}
			if (ksKey < 97 || ksKey > 122) {
				ksKey = keysym;
			}

			int key_code = xk_to_iron(ksKey);
			if (key_code != KEY_CODE_UNKNOWN) {
				iron_internal_keyboard_trigger_key_up(key_code);
			}

			break;
		}
		case ButtonPress: {
			XButtonEvent *button = (XButtonEvent *)&event;

			switch (button->button) {
			case Button1:
				iron_internal_mouse_trigger_press(0, button->x, button->y);
				break;
			case Button2:
				iron_internal_mouse_trigger_press(2, button->x, button->y);
				break;
			case Button3:
				iron_internal_mouse_trigger_press(1, button->x, button->y);
				break;
			// buttons 4-7 are for mouse wheel events because why not
			case Button4:
			case Button5:
			case Button6:
			case Button7:
				break;
			default:
				iron_internal_mouse_trigger_press(button->button - Button1 - 4, button->x, button->y);
				break;
			}
			break;
		}
		case ButtonRelease: {
			XButtonEvent *button = (XButtonEvent *)&event;

			switch (button->button) {
			case Button1:
				iron_internal_mouse_trigger_release(0, button->x, button->y);
				break;
			case Button2:
				iron_internal_mouse_trigger_release(2, button->x, button->y);
				break;
			case Button3:
				iron_internal_mouse_trigger_release(1, button->x, button->y);
				break;
			// Button4 and Button5 provide mouse wheel events because why not
			case Button4:
				iron_internal_mouse_trigger_scroll(-1.0);
				break;
			case Button5:
				iron_internal_mouse_trigger_scroll(1.0);
				break;
			// button 6 and 7 seem to be horizontal scrolling, which is not exposed in Iron's api at the moment
			case Button6:
			case Button7:
				break;
			default:
				iron_internal_mouse_trigger_release(button->button - Button1 - 4, button->x, button->y);
				break;
			}
			break;
		}
		case MotionNotify: {
			XMotionEvent *motion = (XMotionEvent *)&event;
			iron_internal_mouse_trigger_move(motion->x, motion->y);
			break;
		}
		case ConfigureNotify: {
			if (event.xconfigure.width != k_window->width || event.xconfigure.height != k_window->height) {
				k_window->width  = event.xconfigure.width;
				k_window->height = event.xconfigure.height;
				gpu_resize(event.xconfigure.width, event.xconfigure.height);
				iron_internal_call_resize_callback(event.xconfigure.width, event.xconfigure.height);
			}
			break;
		}
		case ClientMessage: {
			if (event.xclient.message_type == XdndEnter) {
				Window source_window = event.xclient.data.l[0];
				XEvent m;
				memset(&m, 0, sizeof(m));
				m.type                 = ClientMessage;
				m.xclient.window       = event.xclient.data.l[0];
				m.xclient.message_type = XdndStatus;
				m.xclient.format       = 32;
				m.xclient.data.l[0]    = window;
				m.xclient.data.l[2]    = 0;
				m.xclient.data.l[3]    = 0;
				m.xclient.data.l[1]    = 1;
				m.xclient.data.l[4]    = XdndActionCopy;
				XSendEvent(x11_ctx.display, source_window, false, NoEventMask, (XEvent *)&m);
				XFlush(x11_ctx.display);
			}
			else if (event.xclient.message_type == XdndDrop) {
				XConvertSelection(x11_ctx.display, XdndSelection, XdndTextUriList, XdndSelection, window, event.xclient.data.l[2]);
			}
			else if (event.xclient.data.l[0] == WM_DELETE_WINDOW) {
				if (iron_internal_call_close_callback()) {
					iron_window_destroy();
					iron_stop();
				}
			}
			break;
		}
		case SelectionNotify: {
			if (event.xselection.selection == CLIPBOARD) {
				char         *result;
				unsigned long ressize, restail;
				int           resbits;
				XGetWindowProperty(x11_ctx.display, window, XSEL_DATA, 0, LONG_MAX / 4, False, AnyPropertyType, &UTF8_STRING, &resbits, &ressize, &restail,
				                   (unsigned char **)&result);
				iron_internal_paste_callback(result);
				XFree(result);
			}
			else if (event.xselection.property == XdndSelection) {
				Atom           type;
				int            format;
				unsigned long  numItems;
				unsigned long  bytesAfter = 1;
				unsigned char *data       = 0;
				XGetWindowProperty(x11_ctx.display, event.xselection.requestor, event.xselection.property, 0, LONG_MAX, False, event.xselection.target, &type,
				                   &format, &numItems, &bytesAfter, &data);
				size_t pos = 0;
				size_t len = 0;
				while (pos < numItems) {
					if (data[pos] == '\r') { // Found a file
						buffer[len] = 0;
						len         = 0;
						pos += 2;                           // Avoid \n
						char *res = uri_decode(buffer + 7); // + 7 -> strip file://
						iron_internal_drop_files_callback(res);
					}
					buffer[len++] = data[pos++];
				}
				XFree(data);
			}
			break;
		}
		case SelectionRequest: {
			if (event.xselectionrequest.target == TARGETS) {
				XEvent send;
				send.xselection.type      = SelectionNotify;
				send.xselection.requestor = event.xselectionrequest.requestor;
				send.xselection.selection = event.xselectionrequest.selection;
				send.xselection.target    = event.xselectionrequest.target;
				send.xselection.property  = event.xselectionrequest.property;
				send.xselection.time      = event.xselectionrequest.time;
				Atom available[]          = {TARGETS, MULTIPLE, TEXT_PLAIN, UTF8_STRING};
				XChangeProperty(x11_ctx.display, send.xselection.requestor, send.xselection.property, XA_ATOM, 32, PropModeReplace,
				                (unsigned char *)&available[0], 4);
				XSendEvent(x11_ctx.display, send.xselection.requestor, True, 0, &send);
			}
			if (event.xselectionrequest.target == TEXT_PLAIN || event.xselectionrequest.target == UTF8_STRING) {
				XEvent send;
				send.xselection.type      = SelectionNotify;
				send.xselection.requestor = event.xselectionrequest.requestor;
				send.xselection.selection = event.xselectionrequest.selection;
				send.xselection.target    = event.xselectionrequest.target;
				send.xselection.property  = event.xselectionrequest.property;
				send.xselection.time      = event.xselectionrequest.time;
				XChangeProperty(x11_ctx.display, send.xselection.requestor, send.xselection.property, send.xselection.target, 8, PropModeReplace,
				                (const unsigned char *)clipboardString, strlen(clipboardString));
				XSendEvent(x11_ctx.display, send.xselection.requestor, True, 0, &send);
			}
			break;
		}
		case Expose: {
			break;
		}
		case FocusIn: {
			iron_internal_foreground_callback();
			break;
		}
		case FocusOut: {
			controlDown   = false;
			ignoreKeycode = 0;
			iron_internal_background_callback();
			break;
		}
		case LeaveNotify: {
			break;
		}
		case EnterNotify: {
			break;
		}
		}
	}

	return true;
}

bool iron_internal_handle_messages() {
	if (!_handle_messages()) {
		return false;
	}
#ifdef WITH_GAMEPAD
	iron_linux_updateHIDGamepads();
#endif
	return true;
}

const char *iron_system_id() {
	return "Linux";
}

void iron_set_keep_screen_on(bool on) {}
void iron_keyboard_show() {}
void iron_keyboard_hide() {}

bool iron_keyboard_active() {
	return true;
}

void iron_load_url(const char *url) {
	if (strncmp(url, "http://", sizeof("http://") - 1) == 0 || strncmp(url, "https://", sizeof("https://") - 1) == 0) {
		char openUrlCommand[256];
		snprintf(openUrlCommand, 256, "xdg-open %s", url);
		system(openUrlCommand);
	}
}

const char *iron_language() {
	return "en";
}

const char *iron_internal_save_path() {
	// first check for an existing directory in $HOME
	// if one exists, use it, else create one in $XDG_DATA_HOME
	// See: https://specifications.freedesktop.org/basedir-spec/basedir-spec-latest.html
	if (!saveInitialized) {
		const char *homedir;
		if ((homedir = getenv("HOME")) == NULL) {
			homedir = getpwuid(getuid())->pw_dir;
		}

		strcpy(save, homedir);
		strcat(save, "/.");
		strcat(save, iron_application_name());
		strcat(save, "/");
		struct stat st;
		if (stat(save, &st) == 0) {
			// use existing folder in $HOME
		}
		else {
			// use XDG folder
			const char *data_home;
			if ((data_home = getenv("XDG_DATA_HOME")) == NULL) {
				// $XDG_DATA_HOME is not defined, fall back to the default, $HOME/.local/share
				strcpy(save, homedir);
				strcat(save, "/.local/share/");
			}
			else {
				// use $XDG_DATA_HOME
				strcpy(save, data_home);
				if (data_home[strlen(data_home) - 1] != '/') {
					strcat(save, "/");
				}
			}
			strcat(save, iron_application_name());
			strcat(save, "/");
			int res = mkdir(save, 0700);
			if (res != 0 && errno != EEXIST) {
				iron_error("Could not create save directory '%s'. Error %d", save, errno);
			}
		}
		saveInitialized = true;
	}
	return save;
}

const char **iron_video_formats() {
	return videoFormats;
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

void iron_init(iron_window_options_t *ops) {
	gettimeofday(&start, NULL);
#ifdef WITH_GAMEPAD
	iron_linux_initHIDGamepads();
#endif
	iron_x11_init();
	iron_display_init();
	iron_set_app_name(ops->title);
	iron_window_create(ops);
}

void iron_internal_shutdown() {
#ifdef WITH_GAMEPAD
	iron_linux_closeHIDGamepads();
#endif
	free(clipboardString);
	XCloseDisplay(x11_ctx.display);
	iron_internal_shutdown_callback();
}

int main(int argc, char **argv) {
	return kickstart(argc, argv);
}

void iron_copy_to_clipboard(const char *text) {
	size_t textLength = strlen(text);
	if (textLength >= clipboardStringSize) {
		free(clipboardString);
		clipboardStringSize = textLength + 1;
		clipboardString     = (char *)malloc(clipboardStringSize);
	}
	strcpy(clipboardString, text);
}

int iron_hardware_threads(void) {
	return sysconf(_SC_NPROCESSORS_ONLN);
}

bool iron_mouse_can_lock(void) {
	return true;
}

void iron_mouse_show() {
	struct iron_x11_window *window = &x11_ctx.windows[0];
	if (mouse_hidden) {
		XUndefineCursor(x11_ctx.display, window->window);
		mouse_hidden = false;
	}
}

void iron_mouse_hide() {
	struct iron_x11_window *window = &x11_ctx.windows[0];
	if (!mouse_hidden) {
		XColor col;
		col.pixel      = 0;
		col.red        = 0;
		col.green      = 0;
		col.blue       = 0;
		col.flags      = DoRed | DoGreen | DoBlue;
		col.pad        = 0;
		char   data[1] = {'\0'};
		Pixmap blank   = XCreateBitmapFromData(x11_ctx.display, window->window, data, 1, 1);
		Cursor cursor  = XCreatePixmapCursor(x11_ctx.display, blank, blank, &col, &col, 0, 0);
		XDefineCursor(x11_ctx.display, window->window, cursor);
		XFreePixmap(x11_ctx.display, blank);
		mouse_hidden = true;
	}
}

void iron_mouse_set_cursor(iron_cursor_t cursor_index) {
	if (mouse_hidden) {
		return;
	}

	Cursor cursor;
	if (cursor_index == IRON_CURSOR_HAND) {
		cursor = XcursorLibraryLoadCursor(x11_ctx.display, "hand1");
	}
	else if (cursor_index == IRON_CURSOR_IBEAM) {
		cursor = XcursorLibraryLoadCursor(x11_ctx.display, "xterm");
	}
	else if (cursor_index == IRON_CURSOR_SIZEWE) {
		cursor = XcursorLibraryLoadCursor(x11_ctx.display, "sb_h_double_arrow");
	}
	else if (cursor_index == IRON_CURSOR_SIZENS) {
		cursor = XcursorLibraryLoadCursor(x11_ctx.display, "sb_v_double_arrow");
	}
	else {
		cursor = XcursorLibraryLoadCursor(x11_ctx.display, "left_ptr"); // "arrow"
	}
	struct iron_x11_window *window = &x11_ctx.windows[0];
	XDefineCursor(x11_ctx.display, window->window, cursor);
}

void iron_mouse_set_position(int x, int y) {
	struct iron_x11_window *window = &x11_ctx.windows[0];
	XWarpPointer(x11_ctx.display, None, window->window, 0, 0, 0, 0, x, y);
	XFlush(x11_ctx.display);
}

void iron_mouse_get_position(int *x, int *y) {
	struct iron_x11_window *window = &x11_ctx.windows[0];
	Window                  inwin;
	Window                  inchildwin;
	int                     rootx, rooty;
	unsigned int            mask;
	XQueryPointer(x11_ctx.display, window->window, &inwin, &inchildwin, &rootx, &rooty, x, y, &mask);
}

#ifdef WITH_GAMEPAD

#include <fcntl.h>
#include <libudev.h>
#include <linux/joystick.h>

struct HIDGamepad {
	int             idx;
	char            gamepad_dev_name[256];
	char            name[385];
	int             file_descriptor;
	bool            connected;
	struct js_event gamepadEvent;
};

static void HIDGamepad_open(struct HIDGamepad *pad) {
	pad->file_descriptor = open(pad->gamepad_dev_name, O_RDONLY | O_NONBLOCK);
	if (pad->file_descriptor < 0) {
		pad->connected = false;
	}
	else {
		pad->connected = true;

		char buf[128];
		if (ioctl(pad->file_descriptor, JSIOCGNAME(sizeof(buf)), buf) < 0) {
			strncpy(buf, "Unknown", sizeof(buf));
		}
		pad->name[0] = 0;
		// snprintf(pad->name, sizeof(pad->name), "%s(%s)", buf, pad->gamepad_dev_name); // TODO: valgrind error
		iron_internal_gamepad_trigger_connect(pad->idx);
	}
}

static void HIDGamepad_init(struct HIDGamepad *pad, int index) {
	pad->file_descriptor     = -1;
	pad->connected           = false;
	pad->gamepad_dev_name[0] = 0;
	if (index >= 0 && index < 12) {
		pad->idx = index;
		snprintf(pad->gamepad_dev_name, sizeof(pad->gamepad_dev_name), "/dev/input/js%d", pad->idx);
		HIDGamepad_open(pad);
	}
}

static void HIDGamepad_close(struct HIDGamepad *pad) {
	if (pad->connected) {
		iron_internal_gamepad_trigger_disconnect(pad->idx);
		close(pad->file_descriptor);
		pad->file_descriptor = -1;
		pad->connected       = false;
	}
}

void HIDGamepad_processEvent(struct HIDGamepad *pad, struct js_event e) {
	switch (e.type) {
	case JS_EVENT_BUTTON:
		iron_internal_gamepad_trigger_button(pad->idx, e.number, e.value);
		break;
	case JS_EVENT_AXIS: {
		float value = e.number % 2 == 0 ? e.value : -e.value;
		iron_internal_gamepad_trigger_axis(pad->idx, e.number, value / 32767.0f);
		break;
	}
	default:
		break;
	}
}

void HIDGamepad_update(struct HIDGamepad *pad) {
	if (pad->connected) {
		while (read(pad->file_descriptor, &pad->gamepadEvent, sizeof(pad->gamepadEvent)) > 0) {
			HIDGamepad_processEvent(pad, pad->gamepadEvent);
		}
	}
}

struct HIDGamepadUdevHelper {
	struct udev         *udevPtr;
	struct udev_monitor *udevMonitorPtr;
	int                  udevMonitorFD;
};

static struct HIDGamepadUdevHelper udev_helper;

static struct HIDGamepad gamepads[IRON_GAMEPAD_MAX_COUNT];

static void HIDGamepadUdevHelper_openOrCloseGamepad(struct HIDGamepadUdevHelper *helper, struct udev_device *dev) {
	const char *action = udev_device_get_action(dev);
	if (!action)
		action = "add";

	const char *joystickDevnodeName = strstr(udev_device_get_devnode(dev), "js");

	if (joystickDevnodeName) {
		int joystickDevnodeIndex;
		sscanf(joystickDevnodeName, "js%d", &joystickDevnodeIndex);

		if (!strcmp(action, "add")) {
			HIDGamepad_open(&gamepads[joystickDevnodeIndex]);
		}

		if (!strcmp(action, "remove")) {
			HIDGamepad_close(&gamepads[joystickDevnodeIndex]);
		}
	}
}

static void HIDGamepadUdevHelper_processDevice(struct HIDGamepadUdevHelper *helper, struct udev_device *dev) {
	if (dev) {
		if (udev_device_get_devnode(dev))
			HIDGamepadUdevHelper_openOrCloseGamepad(helper, dev);

		udev_device_unref(dev);
	}
}

static void HIDGamepadUdevHelper_init(struct HIDGamepadUdevHelper *helper) {
	struct udev *udevPtrNew = udev_new();

	// enumerate
	struct udev_enumerate *enumerate = udev_enumerate_new(udevPtrNew);

	udev_enumerate_add_match_subsystem(enumerate, "input");
	udev_enumerate_scan_devices(enumerate);

	struct udev_list_entry *devices = udev_enumerate_get_list_entry(enumerate);
	struct udev_list_entry *entry;

	udev_list_entry_foreach(entry, devices) {
		const char         *path = udev_list_entry_get_name(entry);
		struct udev_device *dev  = udev_device_new_from_syspath(udevPtrNew, path);
		HIDGamepadUdevHelper_processDevice(helper, dev);
	}

	udev_enumerate_unref(enumerate);

	// setup mon
	helper->udevMonitorPtr = udev_monitor_new_from_netlink(udevPtrNew, "udev");

	udev_monitor_filter_add_match_subsystem_devtype(helper->udevMonitorPtr, "input", NULL);
	udev_monitor_enable_receiving(helper->udevMonitorPtr);

	helper->udevMonitorFD = udev_monitor_get_fd(helper->udevMonitorPtr);

	helper->udevPtr = udevPtrNew;
}

static void HIDGamepadUdevHelper_update(struct HIDGamepadUdevHelper *helper) {
	fd_set fds;
	FD_ZERO(&fds);
	FD_SET(helper->udevMonitorFD, &fds);

	if (FD_ISSET(helper->udevMonitorFD, &fds)) {
		struct udev_device *dev = udev_monitor_receive_device(helper->udevMonitorPtr);
		HIDGamepadUdevHelper_processDevice(helper, dev);
	}
}

static void HIDGamepadUdevHelper_close(struct HIDGamepadUdevHelper *helper) {
	udev_unref(helper->udevPtr);
}

void iron_linux_initHIDGamepads() {
	for (int i = 0; i < IRON_GAMEPAD_MAX_COUNT; ++i) {
		HIDGamepad_init(&gamepads[i], i);
	}
	HIDGamepadUdevHelper_init(&udev_helper);
}

void iron_linux_updateHIDGamepads() {
	HIDGamepadUdevHelper_update(&udev_helper);
	for (int i = 0; i < IRON_GAMEPAD_MAX_COUNT; ++i) {
		HIDGamepad_update(&gamepads[i]);
	}
}

void iron_linux_closeHIDGamepads() {
	HIDGamepadUdevHelper_close(&udev_helper);
}

const char *iron_gamepad_vendor(int gamepad) {
	return "Linux gamepad";
}

const char *iron_gamepad_product_name(int gamepad) {
	return gamepad >= 0 && gamepad < IRON_GAMEPAD_MAX_COUNT ? gamepads[gamepad].name : "";
}

bool iron_gamepad_connected(int gamepad) {
	return gamepad >= 0 && gamepad < IRON_GAMEPAD_MAX_COUNT && gamepads[gamepad].connected;
}

void iron_gamepad_rumble(int gamepad, float left, float right) {}

#endif

#ifdef WITH_NFD // Has gtk
#include <gtk/gtk.h>
extern void (*iron_save_and_quit)(bool);
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
#ifdef WITH_NFD // Has gtk
	bool          save = false;
	XTextProperty text_prop;
	XGetTextProperty(x11_ctx.display, x11_ctx.windows[0].window, &text_prop, NET_WM_NAME);
	char *title = (char *)text_prop.value;
	bool  dirty = strstr(title, "* - ArmorPaint") != NULL;
	// XFree(title);
	if (dirty) {
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
		else if (res == GTK_RESPONSE_NO) {
			save = false;
		}
		else {
			return false;
		}
	}
	iron_save_and_quit(save);
#endif
	return false;
}

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
static pid_t child_pid                   = -1;
volatile int iron_exec_async_done        = 1;
char        *iron_exec_async_output_file = NULL;

void iron_exec_handler(int sig) {
	int status;
	if (waitpid(child_pid, &status, WNOHANG) > 0) {
		iron_exec_async_done = 1;
	}
}

void iron_exec_async(const char *path, char *argv[]) {
	iron_exec_async_done = 0;
	child_pid            = fork();
	if (child_pid == 0) {
		int fd = iron_exec_async_output_file != NULL ? open(iron_exec_async_output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644) : open("/dev/null", O_WRONLY);
		dup2(fd, STDOUT_FILENO);
		dup2(fd, STDERR_FILENO);
		if (fd > STDERR_FILENO) {
			close(fd);
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
