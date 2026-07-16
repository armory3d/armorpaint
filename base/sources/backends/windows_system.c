
#define WIN32_LEAN_AND_MEAN
#include "windows_system.h"
#include <Windows.h>
#include <Windowsx.h>
#include <dwmapi.h>
#include <iron_gpu.h>
#include <iron_system.h>
#include <iron_thread.h>
#include <iron_video.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdio.h>

void iron_microsoft_format(const char *format, va_list args, wchar_t *buffer) {
	char cbuffer[4096];
	vsprintf(cbuffer, format, args);
	MultiByteToWideChar(CP_UTF8, 0, cbuffer, -1, buffer, 4096);
}

#define MAXIMUM_DISPLAYS 8

typedef struct {
	struct HMONITOR__ *monitor;
	char               name[32];
	bool               primary, available, mode_changed;
	int                index, x, y, width, height, ppi, frequency, bpp;
} DisplayData;

static DisplayData displays[MAXIMUM_DISPLAYS];
static DEVMODEA    original_modes[MAXIMUM_DISPLAYS];
static int         screen_counter      = 0;
static bool        display_initialized = false;

typedef enum {
	MDT_EFFECTIVE_DPI = 0,
	MDT_ANGULAR_DPI   = 1,
	MDT_RAW_DPI       = 2,
	MDT_DEFAULT       = MDT_EFFECTIVE_DPI
} MONITOR_DPI_TYPE;
typedef HRESULT(WINAPI *GetDpiForMonitorType)(HMONITOR hmonitor, MONITOR_DPI_TYPE dpiType, UINT *dpiX, UINT *dpiY);
static GetDpiForMonitorType MyGetDpiForMonitor = NULL;

static BOOL CALLBACK EnumerationCallback(HMONITOR monitor, HDC hdc_unused, LPRECT rect_unused, LPARAM lparam) {
	MONITORINFOEXA info;
	memset(&info, 0, sizeof(MONITORINFOEXA));
	info.cbSize = sizeof(MONITORINFOEXA);

	if (GetMonitorInfoA(monitor, (MONITORINFO *)&info) == FALSE) {
		return FALSE;
	}

	int free_slot = 0;
	for (; free_slot < MAXIMUM_DISPLAYS; ++free_slot) {
		if (displays[free_slot].monitor == monitor) {
			return FALSE;
		}

		if (displays[free_slot].monitor == NULL) {
			break;
		}
	}

	DisplayData *display = &displays[free_slot];
	strncpy(display->name, info.szDevice, 31);
	display->name[31]  = 0;
	display->index     = free_slot;
	display->monitor   = monitor;
	display->primary   = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
	display->available = true;
	display->x         = info.rcMonitor.left;
	display->y         = info.rcMonitor.top;
	display->width     = info.rcMonitor.right - info.rcMonitor.left;
	display->height    = info.rcMonitor.bottom - info.rcMonitor.top;

	HDC hdc      = CreateDCA(NULL, display->name, NULL, NULL);
	display->ppi = GetDeviceCaps(hdc, LOGPIXELSX);
	/* int scale =*/GetDeviceCaps(hdc, SCALINGFACTORX);
	DeleteDC(hdc);

	if (MyGetDpiForMonitor != NULL) {
		unsigned dpiX, dpiY;
		MyGetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
		display->ppi = (int)dpiX;
	}

	memset(&original_modes[free_slot], 0, sizeof(DEVMODEA));
	original_modes[free_slot].dmSize = sizeof(DEVMODEA);
	EnumDisplaySettingsA(display->name, ENUM_CURRENT_SETTINGS, &original_modes[free_slot]);
	display->frequency = original_modes[free_slot].dmDisplayFrequency;
	display->bpp       = original_modes[free_slot].dmBitsPerPel;

	++screen_counter;
	return TRUE;
}

void iron_display_init() {
	if (display_initialized) {
		return;
	}
	HMODULE shcore = LoadLibraryA("Shcore.dll");
	if (shcore != NULL) {
		MyGetDpiForMonitor = (GetDpiForMonitorType)GetProcAddress(shcore, "GetDpiForMonitor");
	}
	memset(displays, 0, sizeof(DisplayData) * MAXIMUM_DISPLAYS);
	EnumDisplayMonitors(NULL, NULL, EnumerationCallback, 0);
	display_initialized = true;
}

int iron_windows_get_display_for_monitor(struct HMONITOR__ *monitor) {
	for (int i = 0; i < MAXIMUM_DISPLAYS; ++i) {
		if (displays[i].monitor == monitor) {
			return i;
		}
	}
	return -1;
}

int iron_count_displays() {
	return screen_counter;
}

int iron_primary_display() {
	for (int i = 0; i < MAXIMUM_DISPLAYS; ++i) {
		DisplayData *display = &displays[i];

		if (display->available && display->primary) {
			return i;
		}
	}
	return -1;
}

bool iron_windows_set_display_mode(int display_index, int width, int height, int bpp, int frequency) {
	DisplayData *display  = &displays[display_index];
	display->mode_changed = true;
	DEVMODEA mode         = {0};
	mode.dmSize           = sizeof(mode);
	strcpy((char *)mode.dmDeviceName, display->name);
	mode.dmPelsWidth        = width;
	mode.dmPelsHeight       = height;
	mode.dmBitsPerPel       = bpp;
	mode.dmDisplayFrequency = frequency;
	mode.dmFields           = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY;
	bool success            = ChangeDisplaySettingsA(&mode, CDS_FULLSCREEN) == DISP_CHANGE_SUCCESSFUL;
	if (!success) {
		mode.dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT;
		success       = ChangeDisplaySettingsA(&mode, CDS_FULLSCREEN) == DISP_CHANGE_SUCCESSFUL;
	}
	return success;
}

void iron_windows_restore_display(int display) {
	if (displays[display].mode_changed) {
		ChangeDisplaySettingsA(&original_modes[display], 0);
	}
}

void iron_windows_restore_displays() {
	for (int i = 0; i < MAXIMUM_DISPLAYS; ++i) {
		iron_windows_restore_display(i);
	}
}

iron_display_mode_t iron_display_current_mode(int display_index) {
	DisplayData        *display = &displays[display_index];
	iron_display_mode_t mode;
	mode.x               = display->x;
	mode.y               = display->y;
	mode.width           = display->width;
	mode.height          = display->height;
	mode.pixels_per_inch = display->ppi;
	mode.frequency       = display->frequency;
	mode.bits_per_pixel  = display->bpp;
	return mode;
}

bool iron_mouse_can_lock(void) {
	return true;
}

void iron_mouse_show() {
	// Work around the internal counter of ShowCursor
	while (ShowCursor(true) < 0) {
	}
}

void iron_mouse_hide() {
	// Work around the internal counter of ShowCursor
	while (ShowCursor(false) >= 0) {
	}
}

void iron_mouse_set_position(int x, int y) {
	POINT point;
	point.x = x;
	point.y = y;
	ClientToScreen(iron_windows_window_handle(), &point);
	SetCursorPos(point.x, point.y);
}

void iron_mouse_get_position(int *x, int *y) {
	POINT point;
	GetCursorPos(&point);
	ScreenToClient(iron_windows_window_handle(), &point);
	*x = point.x;
	*y = point.y;
}

#define MAX_TOUCH_POINTS      10
#define IRON_DINPUT_MAX_COUNT 8

struct touchpoint {
	int sysID;
	int x;
	int y;
};

static struct touchpoint touchPoints[MAX_TOUCH_POINTS];
static int               mouseX, mouseY;
static bool              keyPressed[256];
static int               keyTranslated[256]; // http://msdn.microsoft.com/en-us/library/windows/desktop/dd375731(v=vs.85).aspx

static int GetTouchIndex(int dwID) {
	for (int i = 0; i < MAX_TOUCH_POINTS; i++) {
		if (touchPoints[i].sysID == dwID) {
			return i;
		}
	}
	return -1;
}

static int GetAddTouchIndex(int dwID) {
	for (int i = 0; i < MAX_TOUCH_POINTS; i++) {
		if (touchPoints[i].sysID == dwID) {
			return i;
		}
	}
	for (int i = 0; i < MAX_TOUCH_POINTS; i++) {
		if (touchPoints[i].sysID == -1) {
			touchPoints[i].sysID = dwID;
			return i;
		}
	}
	return -1;
}

static void ReleaseTouchIndex(int dwID) {
	for (int i = 0; i < MAX_TOUCH_POINTS; i++) {
		if (touchPoints[i].sysID == dwID) {
			touchPoints[i].sysID = -1;
			touchPoints[i].x     = -1;
			touchPoints[i].y     = -1;
		}
	}
}

static void initKeyTranslation() {
	for (int i = 0; i < 256; ++i) {
		keyTranslated[i] = KEY_CODE_UNKNOWN;
	}
	keyTranslated[VK_BACK]       = KEY_CODE_BACKSPACE;
	keyTranslated[VK_TAB]        = KEY_CODE_TAB;
	keyTranslated[VK_RETURN]     = KEY_CODE_RETURN;
	keyTranslated[VK_SHIFT]      = KEY_CODE_SHIFT;
	keyTranslated[VK_CONTROL]    = KEY_CODE_CONTROL;
	keyTranslated[VK_MENU]       = KEY_CODE_ALT;
	keyTranslated[VK_PAUSE]      = KEY_CODE_PAUSE;
	keyTranslated[VK_CAPITAL]    = KEY_CODE_CAPS_LOCK;
	keyTranslated[VK_ESCAPE]     = KEY_CODE_ESCAPE;
	keyTranslated[VK_SPACE]      = KEY_CODE_SPACE;
	keyTranslated[VK_PRIOR]      = KEY_CODE_PAGE_UP;
	keyTranslated[VK_NEXT]       = KEY_CODE_PAGE_DOWN;
	keyTranslated[VK_END]        = KEY_CODE_END;
	keyTranslated[VK_HOME]       = KEY_CODE_HOME;
	keyTranslated[VK_LEFT]       = KEY_CODE_LEFT;
	keyTranslated[VK_UP]         = KEY_CODE_UP;
	keyTranslated[VK_RIGHT]      = KEY_CODE_RIGHT;
	keyTranslated[VK_DOWN]       = KEY_CODE_DOWN;
	keyTranslated[VK_INSERT]     = KEY_CODE_INSERT;
	keyTranslated[VK_DELETE]     = KEY_CODE_DELETE;
	keyTranslated[VK_HELP]       = KEY_CODE_HELP;
	keyTranslated[0x30]          = KEY_CODE_0;
	keyTranslated[0x31]          = KEY_CODE_1;
	keyTranslated[0x32]          = KEY_CODE_2;
	keyTranslated[0x33]          = KEY_CODE_3;
	keyTranslated[0x34]          = KEY_CODE_4;
	keyTranslated[0x35]          = KEY_CODE_5;
	keyTranslated[0x36]          = KEY_CODE_6;
	keyTranslated[0x37]          = KEY_CODE_7;
	keyTranslated[0x38]          = KEY_CODE_8;
	keyTranslated[0x39]          = KEY_CODE_9;
	keyTranslated[0x41]          = KEY_CODE_A;
	keyTranslated[0x42]          = KEY_CODE_B;
	keyTranslated[0x43]          = KEY_CODE_C;
	keyTranslated[0x44]          = KEY_CODE_D;
	keyTranslated[0x45]          = KEY_CODE_E;
	keyTranslated[0x46]          = KEY_CODE_F;
	keyTranslated[0x47]          = KEY_CODE_G;
	keyTranslated[0x48]          = KEY_CODE_H;
	keyTranslated[0x49]          = KEY_CODE_I;
	keyTranslated[0x4A]          = KEY_CODE_J;
	keyTranslated[0x4B]          = KEY_CODE_K;
	keyTranslated[0x4C]          = KEY_CODE_L;
	keyTranslated[0x4D]          = KEY_CODE_M;
	keyTranslated[0x4E]          = KEY_CODE_N;
	keyTranslated[0x4F]          = KEY_CODE_O;
	keyTranslated[0x50]          = KEY_CODE_P;
	keyTranslated[0x51]          = KEY_CODE_Q;
	keyTranslated[0x52]          = KEY_CODE_R;
	keyTranslated[0x53]          = KEY_CODE_S;
	keyTranslated[0x54]          = KEY_CODE_T;
	keyTranslated[0x55]          = KEY_CODE_U;
	keyTranslated[0x56]          = KEY_CODE_V;
	keyTranslated[0x57]          = KEY_CODE_W;
	keyTranslated[0x58]          = KEY_CODE_X;
	keyTranslated[0x59]          = KEY_CODE_Y;
	keyTranslated[0x5A]          = KEY_CODE_Z;
	keyTranslated[VK_LWIN]       = KEY_CODE_WIN;
	keyTranslated[VK_RWIN]       = KEY_CODE_WIN;
	keyTranslated[VK_APPS]       = KEY_CODE_CONTEXT_MENU;
	keyTranslated[VK_NUMPAD0]    = KEY_CODE_NUMPAD_0;
	keyTranslated[VK_NUMPAD1]    = KEY_CODE_NUMPAD_1;
	keyTranslated[VK_NUMPAD2]    = KEY_CODE_NUMPAD_2;
	keyTranslated[VK_NUMPAD3]    = KEY_CODE_NUMPAD_3;
	keyTranslated[VK_NUMPAD4]    = KEY_CODE_NUMPAD_4;
	keyTranslated[VK_NUMPAD5]    = KEY_CODE_NUMPAD_5;
	keyTranslated[VK_NUMPAD6]    = KEY_CODE_NUMPAD_6;
	keyTranslated[VK_NUMPAD7]    = KEY_CODE_NUMPAD_7;
	keyTranslated[VK_NUMPAD8]    = KEY_CODE_NUMPAD_8;
	keyTranslated[VK_NUMPAD9]    = KEY_CODE_NUMPAD_9;
	keyTranslated[VK_MULTIPLY]   = KEY_CODE_MULTIPLY;
	keyTranslated[VK_ADD]        = KEY_CODE_ADD;
	keyTranslated[VK_SUBTRACT]   = KEY_CODE_SUBTRACT;
	keyTranslated[VK_DECIMAL]    = KEY_CODE_DECIMAL;
	keyTranslated[VK_DIVIDE]     = KEY_CODE_DIVIDE;
	keyTranslated[VK_F1]         = KEY_CODE_F1;
	keyTranslated[VK_F2]         = KEY_CODE_F2;
	keyTranslated[VK_F3]         = KEY_CODE_F3;
	keyTranslated[VK_F4]         = KEY_CODE_F4;
	keyTranslated[VK_F5]         = KEY_CODE_F5;
	keyTranslated[VK_F6]         = KEY_CODE_F6;
	keyTranslated[VK_F7]         = KEY_CODE_F7;
	keyTranslated[VK_F8]         = KEY_CODE_F8;
	keyTranslated[VK_F9]         = KEY_CODE_F9;
	keyTranslated[VK_F10]        = KEY_CODE_F10;
	keyTranslated[VK_F11]        = KEY_CODE_F11;
	keyTranslated[VK_F12]        = KEY_CODE_F12;
	keyTranslated[VK_F13]        = KEY_CODE_F13;
	keyTranslated[VK_F14]        = KEY_CODE_F14;
	keyTranslated[VK_F15]        = KEY_CODE_F15;
	keyTranslated[VK_F16]        = KEY_CODE_F16;
	keyTranslated[VK_F17]        = KEY_CODE_F17;
	keyTranslated[VK_F18]        = KEY_CODE_F18;
	keyTranslated[VK_F19]        = KEY_CODE_F19;
	keyTranslated[VK_F20]        = KEY_CODE_F20;
	keyTranslated[VK_F21]        = KEY_CODE_F21;
	keyTranslated[VK_F22]        = KEY_CODE_F22;
	keyTranslated[VK_F23]        = KEY_CODE_F23;
	keyTranslated[VK_F24]        = KEY_CODE_F24;
	keyTranslated[VK_NUMLOCK]    = KEY_CODE_NUM_LOCK;
	keyTranslated[VK_SCROLL]     = KEY_CODE_SCROLL_LOCK;
	keyTranslated[VK_LSHIFT]     = KEY_CODE_SHIFT;
	keyTranslated[VK_RSHIFT]     = KEY_CODE_SHIFT;
	keyTranslated[VK_LCONTROL]   = KEY_CODE_CONTROL;
	keyTranslated[VK_RCONTROL]   = KEY_CODE_CONTROL;
	keyTranslated[VK_OEM_1]      = KEY_CODE_SEMICOLON;
	keyTranslated[VK_OEM_PLUS]   = KEY_CODE_PLUS;
	keyTranslated[VK_OEM_COMMA]  = KEY_CODE_COMMA;
	keyTranslated[VK_OEM_MINUS]  = KEY_CODE_HYPHEN_MINUS;
	keyTranslated[VK_OEM_PERIOD] = KEY_CODE_PERIOD;
	keyTranslated[VK_OEM_2]      = KEY_CODE_SLASH;
	keyTranslated[VK_OEM_3]      = KEY_CODE_BACK_QUOTE;
	keyTranslated[VK_OEM_4]      = KEY_CODE_OPEN_BRACKET;
	keyTranslated[VK_OEM_5]      = KEY_CODE_BACK_SLASH;
	keyTranslated[VK_OEM_6]      = KEY_CODE_CLOSE_BRACKET;
	keyTranslated[VK_OEM_7]      = KEY_CODE_QUOTE;
}

#ifdef WITH_GAMEPAD
static bool detectGamepad = true;
static bool gamepadFound  = false;
#endif

#define HANDLE_ALT_ENTER

static bool    cursors_initialized = false;
static int     cursor              = 0;
static HCURSOR cursors[5];
static bool    bg_erased = false;

void iron_mouse_set_cursor(iron_cursor_t set_cursor) {
	cursor = set_cursor;
	if (cursors_initialized) {
		SetCursor(cursors[cursor]);
	}
	// Set hand icon for drag even when mouse button is pressed
	if (set_cursor == IRON_CURSOR_HAND) {
		SetCursor(LoadCursor(NULL, IDC_HAND));
	}
}

LRESULT WINAPI IronWindowsMessageProcedure(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	DWORD            pointerId;
	POINTER_INFO     pointerInfo = {0};
	POINTER_PEN_INFO penInfo     = {0};
	static bool      controlDown = false;
#ifdef HANDLE_ALT_ENTER
	static bool altDown = false;
#endif
	static int last_window_width  = -1;
	static int last_window_height = -1;
	static int last_window_x      = INT_MIN;
	static int last_window_y      = INT_MIN;

	switch (msg) {
	case WM_NCCREATE:
		EnableNonClientDpiScaling(hWnd);
		break;
	case WM_DPICHANGED: {
		break;
	}
	case WM_MOVE:
	case WM_MOVING:
	case WM_SIZING:
		// Scheduler::breakTime();
		break;
	case WM_SIZE: {
		int width  = LOWORD(lParam);
		int height = HIWORD(lParam);
		gpu_resize(width, height);
		iron_internal_call_resize_callback(width, height);
		break;
	}
	case WM_CLOSE: {
		if (iron_internal_call_close_callback()) {
			iron_window_destroy();
			iron_stop();
			return 0;
		}
		return 0;
	}
	case WM_ERASEBKGND: {
		if (bg_erased) {
			return 1;
		}
		bg_erased = true;
	}
	case WM_ACTIVATE:
		if (LOWORD(wParam) == WA_ACTIVE || LOWORD(wParam) == WA_CLICKACTIVE) {
			iron_internal_mouse_window_activated();
			iron_internal_foreground_callback();
		}
		else {
			iron_internal_mouse_window_deactivated();
			iron_internal_background_callback();
#ifdef HANDLE_ALT_ENTER
			altDown = false;
#endif
		}
		RegisterTouchWindow(hWnd, 0);
		break;
	case WM_MOUSELEAVE:
		break;
	case WM_MOUSEMOVE:
		mouseX = GET_X_LPARAM(lParam);
		mouseY = GET_Y_LPARAM(lParam);
		iron_internal_mouse_trigger_move(mouseX, mouseY);
		break;
	case WM_CREATE:
		cursors[0]          = LoadCursor(0, IDC_ARROW);
		cursors[1]          = LoadCursor(0, IDC_HAND);
		cursors[2]          = LoadCursor(0, IDC_IBEAM);
		cursors[3]          = LoadCursor(0, IDC_SIZEWE);
		cursors[4]          = LoadCursor(0, IDC_SIZENS);
		cursors_initialized = true;
		return TRUE;
	case WM_SETCURSOR:
		if (LOWORD(lParam) == HTCLIENT) {
			SetCursor(cursors[cursor]);
			return TRUE;
		}
		break;
	case WM_LBUTTONDOWN:
		if (!iron_mouse_is_locked())
			SetCapture(hWnd);
		mouseX = GET_X_LPARAM(lParam);
		mouseY = GET_Y_LPARAM(lParam);
		iron_internal_mouse_trigger_press(0, mouseX, mouseY);
		break;
	case WM_LBUTTONUP:
		if (!iron_mouse_is_locked())
			ReleaseCapture();
		mouseX = GET_X_LPARAM(lParam);
		mouseY = GET_Y_LPARAM(lParam);
		iron_internal_mouse_trigger_release(0, mouseX, mouseY);
		break;
	case WM_RBUTTONDOWN:
		mouseX = GET_X_LPARAM(lParam);
		mouseY = GET_Y_LPARAM(lParam);
		iron_internal_mouse_trigger_press(1, mouseX, mouseY);
		break;
	case WM_RBUTTONUP:
		mouseX = GET_X_LPARAM(lParam);
		mouseY = GET_Y_LPARAM(lParam);
		iron_internal_mouse_trigger_release(1, mouseX, mouseY);
		break;
	case WM_MBUTTONDOWN:
		mouseX = GET_X_LPARAM(lParam);
		mouseY = GET_Y_LPARAM(lParam);
		iron_internal_mouse_trigger_press(2, mouseX, mouseY);
		break;
	case WM_MBUTTONUP:
		mouseX = GET_X_LPARAM(lParam);
		mouseY = GET_Y_LPARAM(lParam);
		iron_internal_mouse_trigger_release(2, mouseX, mouseY);
		break;
	case WM_XBUTTONDOWN:
		mouseX = GET_X_LPARAM(lParam);
		mouseY = GET_Y_LPARAM(lParam);
		iron_internal_mouse_trigger_press(HIWORD(wParam) + 2, mouseX, mouseY);
		break;
	case WM_XBUTTONUP:
		mouseX = GET_X_LPARAM(lParam);
		mouseY = GET_Y_LPARAM(lParam);
		iron_internal_mouse_trigger_release(HIWORD(wParam) + 2, mouseX, mouseY);
		break;
	case WM_MOUSEWHEEL:
		iron_internal_mouse_trigger_scroll(GET_WHEEL_DELTA_WPARAM(wParam) / -120.0);
		break;
	case WM_POINTERDOWN:
		pointerId = GET_POINTERID_WPARAM(wParam);
		GetPointerInfo(pointerId, &pointerInfo);
		if (pointerInfo.pointerType == PT_PEN) {
			GetPointerPenInfo(pointerId, &penInfo);
			ScreenToClient(hWnd, &pointerInfo.ptPixelLocation);
			iron_internal_pen_trigger_press(pointerInfo.ptPixelLocation.x, pointerInfo.ptPixelLocation.y, penInfo.pressure / 1024.0f);
		}
		break;
	case WM_POINTERUP:
		pointerId = GET_POINTERID_WPARAM(wParam);
		GetPointerInfo(pointerId, &pointerInfo);
		if (pointerInfo.pointerType == PT_PEN) {
			GetPointerPenInfo(pointerId, &penInfo);
			ScreenToClient(hWnd, &pointerInfo.ptPixelLocation);
			iron_internal_pen_trigger_release(pointerInfo.ptPixelLocation.x, pointerInfo.ptPixelLocation.y, penInfo.pressure / 1024.0f);
		}
		break;
	case WM_POINTERUPDATE:
		pointerId = GET_POINTERID_WPARAM(wParam);
		GetPointerInfo(pointerId, &pointerInfo);
		if (pointerInfo.pointerType == PT_PEN) {
			GetPointerPenInfo(pointerId, &penInfo);
			ScreenToClient(hWnd, &pointerInfo.ptPixelLocation);
			iron_internal_pen_trigger_move(pointerInfo.ptPixelLocation.x, pointerInfo.ptPixelLocation.y, penInfo.pressure / 1024.0f);
		}
		break;
	case WM_TOUCH: {
		BOOL        bHandled = FALSE;
		UINT        cInputs  = LOWORD(wParam);
		PTOUCHINPUT pInputs  = _alloca(cInputs * sizeof(TOUCHINPUT));
		POINT       ptInput;
		int         tindex;
		if (pInputs) {
			if (GetTouchInputInfo((HTOUCHINPUT)lParam, cInputs, pInputs, sizeof(TOUCHINPUT))) {
				for (int i = 0; i < (int)cInputs; i++) {
					TOUCHINPUT ti = pInputs[i];
					if (ti.dwID != 0) {
						ptInput.x = TOUCH_COORD_TO_PIXEL(ti.x);
						ptInput.y = TOUCH_COORD_TO_PIXEL(ti.y);
						ScreenToClient(hWnd, &ptInput);

						if (ti.dwFlags & TOUCHEVENTF_UP) {
							tindex = GetTouchIndex(ti.dwID);
							ReleaseTouchIndex(ti.dwID);
							iron_internal_surface_trigger_touch_end(tindex, ptInput.x, ptInput.y);
						}
						else {
							bool touchExisits = GetTouchIndex(ti.dwID) != -1;
							tindex            = GetAddTouchIndex(ti.dwID);
							if (tindex >= 0) {
								if (touchExisits) {
									if (touchPoints[tindex].x != ptInput.x || touchPoints[tindex].y != ptInput.y) {
										touchPoints[tindex].x = ptInput.x;
										touchPoints[tindex].y = ptInput.y;
										iron_internal_surface_trigger_move(tindex, ptInput.x, ptInput.y);
									}
								}
								else {
									touchPoints[tindex].x = ptInput.x;
									touchPoints[tindex].y = ptInput.y;
									iron_internal_surface_trigger_touch_start(tindex, ptInput.x, ptInput.y);
								}
							}
						}
					}
					bHandled = TRUE;

					if (!CloseTouchInputHandle((HTOUCHINPUT)lParam)) {
					}
				}
			}
		}
		if (bHandled)
			CloseTouchInputHandle((HTOUCHINPUT)lParam);
		else
			DefWindowProcW(hWnd, WM_TOUCH, wParam, lParam);

		InvalidateRect(hWnd, NULL, FALSE);
	} break;
	case WM_KEYDOWN:
	case WM_SYSKEYDOWN:
		if (!keyPressed[wParam]) {
			keyPressed[wParam] = true;

			if (keyTranslated[wParam] == KEY_CODE_CONTROL) {
				controlDown = true;
			}
#ifdef HANDLE_ALT_ENTER
			else if (keyTranslated[wParam] == KEY_CODE_ALT) {
				altDown = true;
			}
#endif
			else {
				if (controlDown && keyTranslated[wParam] == KEY_CODE_X) {
					iron_internal_cut_callback();
				}

				if (controlDown && keyTranslated[wParam] == KEY_CODE_C) {
					iron_internal_copy_callback();
				}

				if (controlDown && keyTranslated[wParam] == KEY_CODE_V) {
					if (IsClipboardFormatAvailable(CF_UNICODETEXT)) {
						OpenClipboard(hWnd);
						HANDLE handle = GetClipboardData(CF_UNICODETEXT);
						if (handle != NULL) {
							wchar_t *wtext = (wchar_t *)GlobalLock(handle);
							if (wtext != NULL) {
								char text[4096];
								WideCharToMultiByte(CP_UTF8, 0, wtext, -1, text, 4096, NULL, NULL);
								iron_internal_paste_callback(text);
								GlobalUnlock(handle);
							}
						}
						CloseClipboard();
					}
				}

#ifdef HANDLE_ALT_ENTER
				if (altDown && keyTranslated[wParam] == KEY_CODE_RETURN) {
					if (iron_window_get_mode() == IRON_WINDOW_MODE_WINDOW) {
						last_window_width  = iron_window_width();
						last_window_height = iron_window_height();
						last_window_x      = iron_window_x();
						last_window_y      = iron_window_y();
						iron_window_change_mode(IRON_WINDOW_MODE_FULLSCREEN);
					}
					else {
						iron_window_change_mode(IRON_WINDOW_MODE_WINDOW);
						if (last_window_width > 0 && last_window_height > 0) {
							iron_window_resize(last_window_width, last_window_height);
						}
						if (last_window_x > INT_MIN && last_window_y > INT_MIN) {
							iron_window_move(last_window_x, last_window_y);
						}
					}
				}
#endif
			}
			// No auto-repeat
			iron_internal_keyboard_trigger_key_down(keyTranslated[wParam]);
		}
		// Auto-repeat
		// iron_internal_keyboard_trigger_key_down(keyTranslated[wParam]);
		break;
	case WM_KEYUP:
	case WM_SYSKEYUP:
		keyPressed[wParam] = false;

		if (keyTranslated[wParam] == KEY_CODE_CONTROL) {
			controlDown = false;
		}
#ifdef HANDLE_ALT_ENTER
		if (keyTranslated[wParam] == KEY_CODE_ALT) {
			altDown = false;
		}
#endif

		iron_internal_keyboard_trigger_key_up(keyTranslated[wParam]);
		break;
	case WM_CHAR:
		switch (wParam) {
		case 0x1B: // escape
			break;
		default:
			iron_internal_keyboard_trigger_key_press((unsigned)wParam);
			break;
		}
		break;
	case WM_SYSCOMMAND:
		switch (wParam) {
		case SC_KEYMENU:
			return 0;
		case SC_SCREENSAVE:
		case SC_MONITORPOWER:
			return 0;
		case SC_MINIMIZE:
			break;
		case SC_RESTORE:
		case SC_MAXIMIZE:
			break;
		}
		break;
	case WM_DEVICECHANGE:
#ifdef WITH_GAMEPAD
		detectGamepad = true;
#endif
		break;
	case WM_DROPFILES: {
		HDROP    hDrop = (HDROP)wParam;
		unsigned count = DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0);
		for (unsigned i = 0; i < count; ++i) {
			wchar_t filePath[260];
			if (DragQueryFileW(hDrop, i, filePath, 260)) {
				char buffer[1024];
				WideCharToMultiByte(CP_UTF8, 0, filePath, wcslen(filePath) + 1, buffer, sizeof(buffer), NULL, NULL);
				iron_internal_drop_files_callback(buffer);
			}
		}
		DragFinish(hDrop);
		break;
	}
	}
	return DefWindowProcW(hWnd, msg, wParam, lParam);
}

bool iron_internal_handle_messages() {
	MSG message;
	while (PeekMessageW(&message, 0, 0, 0, PM_REMOVE)) {
		TranslateMessage(&message);
		DispatchMessageW(&message);
	}
#ifdef WITH_GAMEPAD
	iron_gamepad_handle_messages();
#endif
	return true;
}

static bool keyboardshown = false;
static char language[3]   = {0};

void iron_keyboard_show() {
	keyboardshown = true;
}

void iron_keyboard_hide() {
	keyboardshown = false;
}

bool iron_keyboard_active() {
	return keyboardshown;
}

void iron_load_url(const char *url) {
	if (strncmp(url, "http://", sizeof("http://") - 1) == 0 || strncmp(url, "https://", sizeof("https://") - 1) == 0) {
		wchar_t wurl[1024];
		MultiByteToWideChar(CP_UTF8, 0, url, -1, wurl, 1024);
		INT_PTR ret = (INT_PTR)ShellExecuteW(NULL, L"open", wurl, NULL, NULL, SW_SHOWNORMAL);
		if (ret <= 32) {
			iron_log("Error opening url %s", url);
		}
	}
}

void iron_set_keep_screen_on(bool on) {}

const char *iron_language() {
	wchar_t wlanguage[3] = {0};

	if (GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_SISO639LANGNAME, wlanguage, 3)) {
		WideCharToMultiByte(CP_UTF8, 0, wlanguage, -1, language, 3, NULL, NULL);
		return language;
	}
	return "en";
}

const char *iron_system_id() {
	return "Windows";
}

static bool co_initialized = false;

void iron_windows_co_initialize(void) {
	if (!co_initialized) {
		CoInitializeEx(0, COINIT_MULTITHREADED);
		co_initialized = true;
	}
}

static wchar_t savePathw[2048] = {0};
static char    savePath[2048]  = {0};

static void findSavePath() {
	iron_windows_co_initialize();
	IKnownFolderManager *folders = NULL;
	CoCreateInstance(&CLSID_KnownFolderManager, NULL, CLSCTX_INPROC_SERVER, &IID_IKnownFolderManager, (LPVOID *)&folders);
	IKnownFolder *folder = NULL;
	folders->lpVtbl->GetFolder(folders, &FOLDERID_SavedGames, &folder);

	LPWSTR path;
	folder->lpVtbl->GetPath(folder, 0, &path);

	wcscpy(savePathw, path);
	wcscat(savePathw, L"\\");
	wchar_t name[1024];
	MultiByteToWideChar(CP_UTF8, 0, iron_application_name(), -1, name, 1024);
	wcscat(savePathw, name);
	wcscat(savePathw, L"\\");

	SHCreateDirectoryExW(NULL, savePathw, NULL);
	WideCharToMultiByte(CP_UTF8, 0, savePathw, -1, savePath, 1024, NULL, NULL);

	CoTaskMemFree(path);
	folder->lpVtbl->Release(folder);
	folders->lpVtbl->Release(folders);
}

const char *iron_internal_save_path() {
	if (savePath[0] == 0)
		findSavePath();
	return savePath;
}

static const char   *videoFormats[] = {"ogv", NULL};
static LARGE_INTEGER frequency;
static LARGE_INTEGER startCount;

const char **iron_video_formats() {
	return videoFormats;
}

double iron_frequency() {
	return (double)frequency.QuadPart;
}

uint64_t iron_timestamp(void) {
	LARGE_INTEGER stamp;
	QueryPerformanceCounter(&stamp);
	return stamp.QuadPart - startCount.QuadPart;
}

double iron_time(void) {
	LARGE_INTEGER stamp;
	QueryPerformanceCounter(&stamp);
	return (double)(stamp.QuadPart - startCount.QuadPart) / (double)frequency.QuadPart;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
	int ret = kickstart(__argc, __argv);
	if (ret != 0) {
#ifdef NDEBUG
		MessageBox(0, L"Unknown Error", L"Error", MB_OK);
#else
		__debugbreak();
#endif
	}
	return ret;
}

void iron_init(iron_window_options_t *ops) {
	initKeyTranslation();
	for (int i = 0; i < 256; ++i) {
		keyPressed[i] = false;
	}

	for (int i = 0; i < MAX_TOUCH_POINTS; i++) {
		touchPoints[i].sysID = -1;
		touchPoints[i].x     = -1;
		touchPoints[i].y     = -1;
	}

	iron_display_init();

	QueryPerformanceCounter(&startCount);
	QueryPerformanceFrequency(&frequency);

	for (int i = 0; i < 256; ++i) {
		keyPressed[i] = false;
	}

	iron_set_app_name(ops->title);
	iron_window_create(ops);

	// Maximized window has x < -1, prevent window centering
	if (ops->x < -1 && ops->y < -1) {
		iron_window_move(ops->x, ops->y);
	}

#ifdef WITH_GAMEPAD
	loadXInput();
	initializeDirectInput();
#endif
}

void iron_internal_shutdown() {
	iron_windows_hide_windows();
	iron_internal_shutdown_callback();
	iron_windows_destroy_windows();
	iron_windows_restore_displays();
}

void iron_copy_to_clipboard(const char *text) {
	wchar_t wtext[4096];
	MultiByteToWideChar(CP_UTF8, 0, text, -1, wtext, 4096);
	OpenClipboard(iron_windows_window_handle());
	EmptyClipboard();
	size_t size   = (wcslen(wtext) + 1) * sizeof(wchar_t);
	HANDLE handle = GlobalAlloc(GMEM_MOVEABLE, size);
	void  *data   = GlobalLock(handle);
	memcpy(data, wtext, size);
	GlobalUnlock(handle);
	SetClipboardData(CF_UNICODETEXT, handle);
	CloseClipboard();
}

int iron_hardware_threads(void) {
	SYSTEM_INFO sysinfo;
	GetSystemInfo(&sysinfo);
	return sysinfo.dwNumberOfProcessors;
}

struct HWND__;
typedef unsigned long DWORD;

// Dark mode
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
struct HWND__ *iron_windows_window_handle();
// Enable visual styles for ui controls
#pragma comment( \
    linker,      \
    "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

typedef struct {
	struct HWND__ *handle;
	int            display_index;
	bool           mouseInside;
	int            index;
	int            x, y, mode, bpp, frequency, features;
	int            manualWidth, manualHeight;
	void (*resizeCallback)(int x, int y, void *data);
	void *resizeCallbackData;
	bool (*closeCallback)(void *data);
	void *closeCallbackData;
} WindowData;

LRESULT WINAPI IronWindowsMessageProcedure(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static WindowData windows[1] = {0};

const wchar_t *windowClassName = L"IronWindow";

static void RegisterWindowClass(HINSTANCE hInstance, const wchar_t *className) {
	WNDCLASSEXW wc = {sizeof(WNDCLASSEXA),
	                  CS_OWNDC /*CS_CLASSDC*/,
	                  IronWindowsMessageProcedure,
	                  0L,
	                  0L,
	                  hInstance,
	                  LoadIconW(hInstance, MAKEINTRESOURCEW(107)),
	                  LoadCursor(NULL, IDC_ARROW),
	                  CreateSolidBrush(0x00202020),
	                  0,
	                  className,
	                  0};
	RegisterClassExW(&wc);
}

static DWORD getStyle(int features) {
	DWORD style = WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_POPUP | WS_CAPTION | WS_SYSMENU;
	if (features & IRON_WINDOW_FEATURES_RESIZABLE) {
		style |= WS_SIZEBOX;
	}
	if (features & IRON_WINDOW_FEATURES_MAXIMIZABLE) {
		style |= WS_MAXIMIZEBOX;
	}
	if (features & IRON_WINDOW_FEATURES_MINIMIZABLE) {
		style |= WS_MINIMIZEBOX;
	}
	return style;
}

static DWORD getExStyle(int features) {
	DWORD exStyle = WS_EX_APPWINDOW | WS_EX_WINDOWEDGE;
	return exStyle;
}

int iron_window_x() {
	RECT rect;
	GetWindowRect(windows[0].handle, &rect);
	windows[0].x = rect.left;
	return windows[0].x;
}

int iron_window_y() {
	RECT rect;
	GetWindowRect(windows[0].handle, &rect);
	windows[0].y = rect.top;
	return windows[0].y;
}

int iron_window_width() {
	RECT rect;
	GetClientRect(windows[0].handle, &rect);
	return rect.right;
}

int iron_window_height() {
	RECT rect;
	GetClientRect(windows[0].handle, &rect);
	return rect.bottom;
}

static DWORD getDwStyle(iron_window_mode_t mode, int features) {
	switch (mode) {
	case IRON_WINDOW_MODE_FULLSCREEN:
		return WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_POPUP;
	case IRON_WINDOW_MODE_WINDOW:
	default:
		return getStyle(features);
	}
}

static DWORD getDwExStyle(iron_window_mode_t mode, int features) {
	switch (mode) {
	case IRON_WINDOW_MODE_FULLSCREEN:
		return WS_EX_APPWINDOW;
	case IRON_WINDOW_MODE_WINDOW:
	default:
		return getExStyle(features);
	}
}

static void createWindow(const wchar_t *title, int x, int y, int width, int height, int bpp, int frequency, int features, iron_window_mode_t windowMode,
                         int target_display_index) {
	HINSTANCE inst = GetModuleHandleW(NULL);
	RegisterWindowClass(inst, windowClassName);

	int display_index = target_display_index == -1 ? iron_primary_display() : target_display_index;

	RECT WindowRect;
	WindowRect.left   = 0;
	WindowRect.right  = width;
	WindowRect.top    = 0;
	WindowRect.bottom = height;

	AdjustWindowRectEx(&WindowRect, getDwStyle(windowMode, features), FALSE, getDwExStyle(windowMode, features));

	iron_display_mode_t display_mode = iron_display_current_mode(display_index);

	int dstx = display_mode.x;
	int dsty = display_mode.y;
	int dstw = width;
	int dsth = height;

	switch (windowMode) {
	case IRON_WINDOW_MODE_WINDOW:
		dstx += x < 0 ? (display_mode.width - width) / 2 : x;
		dsty += y < 0 ? (display_mode.height - height) / 2 : y;
		dstw = WindowRect.right - WindowRect.left;
		dsth = WindowRect.bottom - WindowRect.top;
		break;
	case IRON_WINDOW_MODE_FULLSCREEN:
		dstw = display_mode.width;
		dsth = display_mode.height;
		break;
	}

	HWND hwnd = CreateWindowExW(getDwExStyle(windowMode, features), windowClassName, title, getDwStyle(windowMode, features), dstx, dsty, dstw, dsth, NULL,
	                            NULL, inst, NULL);

	SetCursor(LoadCursor(NULL, IDC_ARROW));
	DragAcceptFiles(hwnd, true);

	windows[0].handle        = hwnd;
	windows[0].x             = dstx;
	windows[0].y             = dsty;
	windows[0].mode          = windowMode;
	windows[0].display_index = display_index;
	windows[0].bpp           = bpp;
	windows[0].frequency     = frequency;
	windows[0].features      = features;
	windows[0].manualWidth   = width;
	windows[0].manualHeight  = height;

	// Dark mode
	char  vdata[4];
	DWORD cbdata = 4 * sizeof(char);
	RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme", RRF_RT_REG_DWORD, NULL, vdata,
	             &cbdata);
	BOOL use_dark_mode = (int)(vdata[3] << 24 | vdata[2] << 16 | vdata[1] << 8 | vdata[0]) != 1;
	DwmSetWindowAttribute(iron_windows_window_handle(), DWMWA_USE_IMMERSIVE_DARK_MODE, &use_dark_mode, sizeof(use_dark_mode));
}

void iron_window_resize(int width, int height) {
	WindowData *win   = &windows[0];
	win->manualWidth  = width;
	win->manualHeight = height;
	switch (win->mode) {
	case IRON_WINDOW_MODE_WINDOW: {
		RECT rect;
		rect.left   = 0;
		rect.top    = 0;
		rect.right  = width;
		rect.bottom = height;
		AdjustWindowRectEx(&rect, getDwStyle((iron_window_mode_t)win->mode, win->features), FALSE, getDwExStyle((iron_window_mode_t)win->mode, win->features));
		SetWindowPos(win->handle, NULL, iron_window_x(), iron_window_y(), rect.right - rect.left, rect.bottom - rect.top, 0);
		break;
	}
	}
}

void iron_window_move(int x, int y) {
	WindowData *win = &windows[0];

	if (win->mode != 0) {
		return;
	}

	win->x = x;
	win->y = y;

	RECT rect;
	rect.left   = 0;
	rect.top    = 0;
	rect.right  = iron_window_width();
	rect.bottom = iron_window_height();
	AdjustWindowRectEx(&rect, getDwStyle((iron_window_mode_t)win->mode, win->features), FALSE, getDwExStyle((iron_window_mode_t)win->mode, win->features));

	SetWindowPos(win->handle, NULL, x, y, rect.right - rect.left, rect.bottom - rect.top, 0);
}

void iron_window_change_mode(iron_window_mode_t mode) {
	WindowData         *win           = &windows[0];
	int                 display_index = iron_window_display();
	iron_display_mode_t display_mode  = iron_display_current_mode(display_index);
	switch (mode) {
	case IRON_WINDOW_MODE_WINDOW: {
		iron_windows_restore_display(display_index);
		SetWindowLongW(win->handle, GWL_STYLE, getStyle(win->features));
		SetWindowLongW(win->handle, GWL_EXSTYLE, getExStyle(win->features));
		SetWindowPos(win->handle, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED);
		iron_window_show();
		break;
	}
	case IRON_WINDOW_MODE_FULLSCREEN: {
		iron_windows_restore_display(display_index);
		SetWindowLongW(win->handle, GWL_STYLE, WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_POPUP);
		SetWindowLongW(win->handle, GWL_EXSTYLE, WS_EX_APPWINDOW);
		SetWindowPos(win->handle, NULL, display_mode.x, display_mode.y, display_mode.width, display_mode.height, 0);
		iron_window_show();
		break;
	}
	}
	win->mode = mode;
	DragAcceptFiles(win->handle, true);
}

iron_window_mode_t iron_window_get_mode() {
	return (iron_window_mode_t)windows[0].mode;
}

void iron_window_destroy() {
	WindowData *win = &windows[0];
	if (win->handle != NULL) {
		DestroyWindow(win->handle);
		win->handle = NULL;
	}
}

void iron_windows_hide_windows(void) {
	for (int i = 0; i < 1; ++i) {
		if (windows[i].handle != NULL) {
			ShowWindow(windows[i].handle, SW_HIDE);
			UpdateWindow(windows[i].handle);
		}
	}
}

void iron_windows_destroy_windows(void) {
	iron_window_destroy();
	UnregisterClassW(windowClassName, GetModuleHandleW(NULL));
}

void iron_window_show() {
	ShowWindow(windows[0].handle, SW_SHOWDEFAULT);
	UpdateWindow(windows[0].handle);
}

void iron_window_hide() {
	ShowWindow(windows[0].handle, SW_HIDE);
	UpdateWindow(windows[0].handle);
}

void iron_window_set_title(const char *title) {
	wchar_t buffer[1024];
	MultiByteToWideChar(CP_UTF8, 0, title, -1, buffer, 1024);
	SetWindowTextW(windows[0].handle, buffer);
}

void iron_window_create(iron_window_options_t *win) {
	wchar_t wbuffer[1024];
	MultiByteToWideChar(CP_UTF8, 0, win->title, -1, wbuffer, 1024);
	createWindow(wbuffer, win->x, win->y, win->width, win->height, win->color_bits, win->frequency, win->features, win->mode, win->display_index);
	gpu_init(win->depth_bits, win->vsync);
	if (win->visible) {
		iron_window_show();
	}
}

void iron_window_set_resize_callback(void (*callback)(int x, int y, void *data), void *data) {
	windows[0].resizeCallback     = callback;
	windows[0].resizeCallbackData = data;
}

void iron_window_set_close_callback(bool (*callback)(void *data), void *data) {
	windows[0].closeCallback     = callback;
	windows[0].closeCallbackData = data;
}

int iron_window_display() {
	return iron_windows_get_display_for_monitor(MonitorFromWindow(windows[0].handle, MONITOR_DEFAULTTOPRIMARY));
}

struct HWND__ *iron_windows_window_handle() {
	return windows[0].handle;
}

void iron_internal_call_resize_callback(int width, int height) {
	if (windows[0].resizeCallback != NULL) {
		windows[0].resizeCallback(width, height, windows[0].resizeCallbackData);
	}
}

bool iron_internal_call_close_callback() {
	if (windows[0].closeCallback != NULL) {
		return windows[0].closeCallback(windows[0].closeCallbackData);
	}
	return true;
}

extern void (*iron_save_and_quit)(bool);

bool _save_and_quit_callback_internal() {
	bool    save = false;
	wchar_t title[1024];
	GetWindowTextW(iron_windows_window_handle(), title, sizeof(title));
	bool dirty = wcsstr(title, L"* - ArmorPaint") != NULL;
	if (dirty) {
		int res = MessageBox(iron_windows_window_handle(), L"Project has been modified, save changes?", L"Save Changes?", MB_YESNOCANCEL | MB_ICONEXCLAMATION);
		if (res == IDYES) {
			save = true;
		}
		else if (res == IDNO) {
			save = false;
		}
		else { // Cancel
			return false;
		}
	}
	iron_save_and_quit(save);
	return false;
}

#ifdef WITH_GAMEPAD

bool iron_gamepad_connected(int num) {
	return isXInputGamepad(num) || isDirectInputGamepad(num);
}

void iron_gamepad_rumble(int gamepad, float left, float right) {
	if (isXInputGamepad(gamepad)) {
		XINPUT_VIBRATION vibration;
		memset(&vibration, 0, sizeof(XINPUT_VIBRATION));
		vibration.wLeftMotorSpeed  = (WORD)(65535.f * left);
		vibration.wRightMotorSpeed = (WORD)(65535.f * right);
		InputSetState(gamepad, &vibration);
	}
}

#include <XInput.h>
#include <dinput.h>

static float axes[12 * 6];
static float buttons[12 * 16];
typedef DWORD(WINAPI *XInputGetStateType)(DWORD dwUserIndex, XINPUT_STATE *pState);
typedef DWORD(WINAPI *XInputSetStateType)(DWORD dwUserIndex, XINPUT_VIBRATION *pVibration);
static XInputGetStateType InputGetState = NULL;
static XInputSetStateType InputSetState = NULL;

void loadXInput() {
	HMODULE lib = LoadLibraryA("xinput1_4.dll");
	if (lib == NULL) {
		lib = LoadLibraryA("xinput1_3.dll");
	}
	if (lib == NULL) {
		lib = LoadLibraryA("xinput9_1_0.dll");
	}

	if (lib != NULL) {
		InputGetState = (XInputGetStateType)GetProcAddress(lib, "XInputGetState");
		InputSetState = (XInputSetStateType)GetProcAddress(lib, "XInputSetState");
	}
}

static IDirectInput8W       *di_instance = NULL;
static IDirectInputDevice8W *di_pads[IRON_DINPUT_MAX_COUNT];
static DIJOYSTATE2           di_padState[IRON_DINPUT_MAX_COUNT];
static DIJOYSTATE2           di_lastPadState[IRON_DINPUT_MAX_COUNT];
static DIDEVCAPS             di_deviceCaps[IRON_DINPUT_MAX_COUNT];
static int                   padCount = 0;

static void cleanupPad(int padIndex) {
	if (di_pads[padIndex] != NULL) {
		di_pads[padIndex]->lpVtbl->Unacquire(di_pads[padIndex]);
		di_pads[padIndex]->lpVtbl->Release(di_pads[padIndex]);
		di_pads[padIndex] = 0;
	}
}

#ifndef SAFE_RELEASE
#define SAFE_RELEASE(x)        \
	if (x != NULL) {           \
		x->lpVtbl->Release(x); \
		x = NULL;              \
	}
#endif

// From
//-----------------------------------------------------------------------------
// Enum each PNP device using WMI and check each device ID to see if it contains
// "IG_" (ex. "VID_045E&PID_028E&IG_00").  If it does, then it's an XInput device
// Unfortunately this information can not be found by just using DirectInput
//-----------------------------------------------------------------------------
static BOOL IsXInputDevice(const GUID *pGuidProductFromDirectInput) {
	IWbemLocator         *pIWbemLocator   = NULL;
	IEnumWbemClassObject *pEnumDevices    = NULL;
	IWbemClassObject     *pDevices[20]    = {0};
	IWbemServices        *pIWbemServices  = NULL;
	BSTR                  bstrNamespace   = NULL;
	BSTR                  bstrDeviceID    = NULL;
	BSTR                  bstrClassName   = NULL;
	DWORD                 uReturned       = 0;
	bool                  bIsXinputDevice = false;
	UINT                  iDevice         = 0;
	VARIANT               var;
	HRESULT               hr;

	// CoInit if needed
	hr               = CoInitialize(NULL);
	bool bCleanupCOM = SUCCEEDED(hr);

	// Create WMI
	hr = CoCreateInstance(&CLSID_WbemLocator, NULL, CLSCTX_INPROC_SERVER, &IID_IWbemLocator, (LPVOID *)&pIWbemLocator);
	if (FAILED(hr) || pIWbemLocator == NULL)
		goto LCleanup;

	bstrNamespace = SysAllocString(L"\\\\.\\root\\cimv2");
	if (bstrNamespace == NULL)
		goto LCleanup;
	bstrClassName = SysAllocString(L"Win32_PNPEntity");
	if (bstrClassName == NULL)
		goto LCleanup;
	bstrDeviceID = SysAllocString(L"DeviceID");
	if (bstrDeviceID == NULL)
		goto LCleanup;

	// Connect to WMI
	hr = pIWbemLocator->lpVtbl->ConnectServer(pIWbemLocator, bstrNamespace, NULL, NULL, 0L, 0L, NULL, NULL, &pIWbemServices);
	if (FAILED(hr) || pIWbemServices == NULL)
		goto LCleanup;

	// Switch security level to IMPERSONATE.
	CoSetProxyBlanket((IUnknown *)pIWbemServices, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL,
	                  EOAC_NONE);

	hr = pIWbemServices->lpVtbl->CreateInstanceEnum(pIWbemServices, bstrClassName, 0, NULL, &pEnumDevices);
	if (FAILED(hr) || pEnumDevices == NULL)
		goto LCleanup;

	// Loop over all devices
	for (;;) {
		// Get 20 at a time
		hr = pEnumDevices->lpVtbl->Next(pEnumDevices, 10000, 20, pDevices, &uReturned);
		if (FAILED(hr))
			goto LCleanup;
		if (uReturned == 0)
			break;

		for (iDevice = 0; iDevice < uReturned; iDevice++) {
			// For each device, get its device ID
			hr = pDevices[iDevice]->lpVtbl->Get(pDevices[iDevice], bstrDeviceID, 0L, &var, NULL, NULL);
			if (SUCCEEDED(hr) && var.vt == VT_BSTR && var.bstrVal != NULL) {
				// Check if the device ID contains "IG_".  If it does, then it's an XInput device
				// This information can not be found from DirectInput
				// TODO: Doesn't work with an Xbox Series X|S controller
				if (wcsstr(var.bstrVal, L"IG_")) {
					// If it does, then get the VID/PID from var.bstrVal
					DWORD  dwPid = 0, dwVid = 0;
					WCHAR *strVid = wcsstr(var.bstrVal, L"VID_");

					if (strVid && swscanf(strVid, L"VID_%4X", &dwVid) != 1) {
						dwVid = 0;
					}
					WCHAR *strPid = wcsstr(var.bstrVal, L"PID_");
					if (strPid && swscanf(strPid, L"PID_%4X", &dwPid) != 1) {
						dwPid = 0;
					}

					// Compare the VID/PID to the DInput device
					DWORD dwVidPid = MAKELONG(dwVid, dwPid);
					if (dwVidPid == pGuidProductFromDirectInput->Data1) {
						bIsXinputDevice = true;
						goto LCleanup;
					}
				}
			}
			SAFE_RELEASE(pDevices[iDevice]);
		}
	}

LCleanup:
	if (bstrNamespace)
		SysFreeString(bstrNamespace);
	if (bstrDeviceID)
		SysFreeString(bstrDeviceID);
	if (bstrClassName)
		SysFreeString(bstrClassName);
	for (iDevice = 0; iDevice < 20; iDevice++)
		SAFE_RELEASE(pDevices[iDevice]);
	SAFE_RELEASE(pEnumDevices);
	SAFE_RELEASE(pIWbemLocator);
	SAFE_RELEASE(pIWbemServices);

	if (bCleanupCOM)
		CoUninitialize();

	return bIsXinputDevice;
}

static void cleanupDirectInput() {
	for (int padIndex = 0; padIndex < IRON_DINPUT_MAX_COUNT; ++padIndex) {
		cleanupPad(padIndex);
	}

	if (di_instance != NULL) {
		di_instance->lpVtbl->Release(di_instance);
		di_instance = NULL;
	}
}

static BOOL CALLBACK enumerateJoystickAxesCallback(LPCDIDEVICEOBJECTINSTANCEW ddoi, LPVOID context) {
	HWND hwnd = (HWND)context;

	DIPROPRANGE propertyRange;
	propertyRange.diph.dwSize       = sizeof(DIPROPRANGE);
	propertyRange.diph.dwHeaderSize = sizeof(DIPROPHEADER);
	propertyRange.diph.dwHow        = DIPH_BYID;
	propertyRange.diph.dwObj        = ddoi->dwType;
	propertyRange.lMin              = -32768;
	propertyRange.lMax              = 32768;

	HRESULT hr = di_pads[padCount]->lpVtbl->SetProperty(di_pads[padCount], DIPROP_RANGE, &propertyRange.diph);

	if (FAILED(hr)) {
		iron_log("DirectInput8 / Pad%i / SetProperty() failed (HRESULT=0x%x)", padCount, hr);
		return DIENUM_STOP;
	}

	return DIENUM_CONTINUE;
}

static BOOL CALLBACK enumerateJoysticksCallback(LPCDIDEVICEINSTANCEW ddi, LPVOID context) {
	if (padCount < XUSER_MAX_COUNT && IsXInputDevice(&ddi->guidProduct)) {
		++padCount;
		return DIENUM_CONTINUE;
	}

	HRESULT hr = di_instance->lpVtbl->CreateDevice(di_instance, &ddi->guidInstance, &di_pads[padCount], NULL);

	if (SUCCEEDED(hr)) {
		hr = di_pads[padCount]->lpVtbl->SetDataFormat(di_pads[padCount], &c_dfDIJoystick2);

		if (SUCCEEDED(hr)) {
			di_deviceCaps[padCount].dwSize = sizeof(DIDEVCAPS);
			hr                             = di_pads[padCount]->lpVtbl->GetCapabilities(di_pads[padCount], &di_deviceCaps[padCount]);

			if (SUCCEEDED(hr)) {
				hr = di_pads[padCount]->lpVtbl->EnumObjects(di_pads[padCount], enumerateJoystickAxesCallback, NULL, DIDFT_AXIS);

				if (SUCCEEDED(hr)) {
					hr = di_pads[padCount]->lpVtbl->Acquire(di_pads[padCount]);

					if (SUCCEEDED(hr)) {
						memset(&di_padState[padCount], 0, sizeof(DIJOYSTATE2));
						hr = di_pads[padCount]->lpVtbl->GetDeviceState(di_pads[padCount], sizeof(DIJOYSTATE2), &di_padState[padCount]);

						if (SUCCEEDED(hr)) {
							iron_log("DirectInput8 / Pad%i / initialized", padCount);
						}
						else {
							iron_log("DirectInput8 / Pad%i / GetDeviceState() failed (HRESULT=0x%x)", padCount, hr);
						}
					}
					else {
						iron_log("DirectInput8 / Pad%i / Acquire() failed (HRESULT=0x%x)", padCount, hr);
						cleanupPad(padCount);
					}
				}
				else {
					iron_log("DirectInput8 / Pad%i / EnumObjects(DIDFT_AXIS) failed (HRESULT=0x%x)", padCount, hr);
					cleanupPad(padCount);
				}
			}
			else {
				iron_log("DirectInput8 / Pad%i / GetCapabilities() failed (HRESULT=0x%x)", padCount, hr);
				cleanupPad(padCount);
			}
		}
		else {
			iron_log("DirectInput8 / Pad%i / SetDataFormat() failed (HRESULT=0x%x)", padCount, hr);
			cleanupPad(padCount);
		}

		++padCount;

		if (padCount >= IRON_DINPUT_MAX_COUNT) {
			return DIENUM_STOP;
		}
	}

	return DIENUM_CONTINUE;
}

static void initializeDirectInput() {
	HINSTANCE hinstance = GetModuleHandleW(NULL);

	memset(&di_pads, 0, sizeof(IDirectInputDevice8) * IRON_DINPUT_MAX_COUNT);
	memset(&di_padState, 0, sizeof(DIJOYSTATE2) * IRON_DINPUT_MAX_COUNT);
	memset(&di_lastPadState, 0, sizeof(DIJOYSTATE2) * IRON_DINPUT_MAX_COUNT);
	memset(&di_deviceCaps, 0, sizeof(DIDEVCAPS) * IRON_DINPUT_MAX_COUNT);

	// #define DIRECTINPUT_VERSION 0x0800
	HRESULT hr = DirectInput8Create(hinstance, 0x0800, &IID_IDirectInput8W, (void **)&di_instance, NULL);

	if (SUCCEEDED(hr)) {
		hr = di_instance->lpVtbl->EnumDevices(di_instance, DI8DEVCLASS_GAMECTRL, enumerateJoysticksCallback, NULL, DIEDFL_ATTACHEDONLY);

		if (SUCCEEDED(hr)) {
		}
		else {
			cleanupDirectInput();
		}
	}
	else {
		iron_log("DirectInput8Create failed (HRESULT=0x%x)", hr);
	}
}

bool handleDirectInputPad(int padIndex) {
	if (di_pads[padIndex] == NULL) {
		return false;
	}

	HRESULT hr = di_pads[padIndex]->lpVtbl->GetDeviceState(di_pads[padIndex], sizeof(DIJOYSTATE2), &di_padState[padIndex]);

	switch (hr) {
	case S_OK: {
		for (int axisIndex = 0; axisIndex < 2; ++axisIndex) {
			LONG *now  = NULL;
			LONG *last = NULL;

			switch (axisIndex) {
			case 0: {
				now  = &di_padState[padIndex].lX;
				last = &di_lastPadState[padIndex].lX;
			} break;
			case 1: {
				now  = &di_padState[padIndex].lY;
				last = &di_lastPadState[padIndex].lY;
			} break;
			case 2: {
				now  = &di_padState[padIndex].lZ;
				last = &di_lastPadState[padIndex].lZ;
			} break;
			}

			if (*now != *last) {
				iron_internal_gamepad_trigger_axis(padIndex, axisIndex, *now / 32768.0f);
			}
		}

		for (int buttonIndex = 0; buttonIndex < 128; ++buttonIndex) {
			BYTE *now  = &di_padState[padIndex].rgbButtons[buttonIndex];
			BYTE *last = &di_lastPadState[padIndex].rgbButtons[buttonIndex];

			if (*now != *last) {
				iron_internal_gamepad_trigger_button(padIndex, buttonIndex, *now / 255.0f);
			}
		}

		for (int povIndex = 0; povIndex < 4; ++povIndex) {
			DWORD *now  = &di_padState[padIndex].rgdwPOV[povIndex];
			DWORD *last = &di_lastPadState[padIndex].rgdwPOV[povIndex];

			bool up    = (*now == 0 || *now == 31500 || *now == 4500);
			bool down  = (*now == 18000 || *now == 13500 || *now == 22500);
			bool left  = (*now == 27000 || *now == 22500 || *now == 31500);
			bool right = (*now == 9000 || *now == 4500 || *now == 13500);

			bool lastUp    = (*last == 0 || *last == 31500 || *last == 4500);
			bool lastDown  = (*last == 18000 || *last == 13500 || *last == 22500);
			bool lastLeft  = (*last == 27000 || *last == 22500 || *last == 31500);
			bool lastRight = (*last == 9000 || *last == 4500 || *last == 13500);

			if (up != lastUp) {
				iron_internal_gamepad_trigger_button(padIndex, 12, up ? 1.0f : 0.0f);
			}
			if (down != lastDown) {
				iron_internal_gamepad_trigger_button(padIndex, 13, down ? 1.0f : 0.0f);
			}
			if (left != lastLeft) {
				iron_internal_gamepad_trigger_button(padIndex, 14, left ? 1.0f : 0.0f);
			}
			if (right != lastRight) {
				iron_internal_gamepad_trigger_button(padIndex, 15, right ? 1.0f : 0.0f);
			}
		}

		memcpy(&di_lastPadState[padIndex], &di_padState[padIndex], sizeof(DIJOYSTATE2));
		break;
	}
	case DIERR_INPUTLOST: // fall through
	case DIERR_NOTACQUIRED: {
		hr = di_pads[padIndex]->lpVtbl->Acquire(di_pads[padIndex]);
		break;
	}
	}

	return hr == S_OK;
}

static bool isXInputGamepad(int gamepad) {
	// if gamepad is greater than XInput max, treat it as DINPUT.
	if (gamepad >= XUSER_MAX_COUNT) {
		return false;
	}
	XINPUT_STATE state;
	memset(&state, 0, sizeof(XINPUT_STATE));
	DWORD dwResult = InputGetState(gamepad, &state);
	return dwResult == ERROR_SUCCESS;
}

static bool isDirectInputGamepad(int gamepad) {
	if (di_pads[gamepad] == NULL) {
		return false;
	}
	HRESULT hr = di_pads[gamepad]->lpVtbl->GetDeviceState(di_pads[gamepad], sizeof(DIJOYSTATE2), &di_padState[gamepad]);
	return hr == S_OK;
}

const char *iron_gamepad_vendor(int gamepad) {
	if (isXInputGamepad(gamepad)) {
		return "Microsoft";
	}
	else {
		return "DirectInput8";
	}
}

const char *iron_gamepad_product_name(int gamepad) {
	if (isXInputGamepad(gamepad)) {
		return "Xbox 360 Controller";
	}
	else {
		return "Generic Gamepad";
	}
}

void iron_gamepad_handle_messages() {
	if (InputGetState != NULL && (detectGamepad || gamepadFound)) {
		detectGamepad = false;
		for (DWORD i = 0; i < IRON_DINPUT_MAX_COUNT; ++i) {
			XINPUT_STATE state;
			memset(&state, 0, sizeof(XINPUT_STATE));
			DWORD dwResult = InputGetState(i, &state);

			if (dwResult == ERROR_SUCCESS) {
				gamepadFound = true;

				float newaxes[6];
				newaxes[0] = state.Gamepad.sThumbLX / 32768.0f;
				newaxes[1] = state.Gamepad.sThumbLY / 32768.0f;
				newaxes[2] = state.Gamepad.sThumbRX / 32768.0f;
				newaxes[3] = state.Gamepad.sThumbRY / 32768.0f;
				newaxes[4] = state.Gamepad.bLeftTrigger / 255.0f;
				newaxes[5] = state.Gamepad.bRightTrigger / 255.0f;
				for (int i2 = 0; i2 < 6; ++i2) {
					if (axes[i * 6 + i2] != newaxes[i2]) {
						iron_internal_gamepad_trigger_axis(i, i2, newaxes[i2]);
						axes[i * 6 + i2] = newaxes[i2];
					}
				}
				float newbuttons[16];
				newbuttons[0]  = (state.Gamepad.wButtons & XINPUT_GAMEPAD_A) ? 1.0f : 0.0f;
				newbuttons[1]  = (state.Gamepad.wButtons & XINPUT_GAMEPAD_B) ? 1.0f : 0.0f;
				newbuttons[2]  = (state.Gamepad.wButtons & XINPUT_GAMEPAD_X) ? 1.0f : 0.0f;
				newbuttons[3]  = (state.Gamepad.wButtons & XINPUT_GAMEPAD_Y) ? 1.0f : 0.0f;
				newbuttons[4]  = (state.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) ? 1.0f : 0.0f;
				newbuttons[5]  = (state.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) ? 1.0f : 0.0f;
				newbuttons[6]  = state.Gamepad.bLeftTrigger / 255.0f;
				newbuttons[7]  = state.Gamepad.bRightTrigger / 255.0f;
				newbuttons[8]  = (state.Gamepad.wButtons & XINPUT_GAMEPAD_BACK) ? 1.0f : 0.0f;
				newbuttons[9]  = (state.Gamepad.wButtons & XINPUT_GAMEPAD_START) ? 1.0f : 0.0f;
				newbuttons[10] = (state.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_THUMB) ? 1.0f : 0.0f;
				newbuttons[11] = (state.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB) ? 1.0f : 0.0f;
				newbuttons[12] = (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP) ? 1.0f : 0.0f;
				newbuttons[13] = (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) ? 1.0f : 0.0f;
				newbuttons[14] = (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) ? 1.0f : 0.0f;
				newbuttons[15] = (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) ? 1.0f : 0.0f;
				for (int i2 = 0; i2 < 16; ++i2) {
					if (buttons[i * 16 + i2] != newbuttons[i2]) {
						iron_internal_gamepad_trigger_button(i, i2, newbuttons[i2]);
						buttons[i * 16 + i2] = newbuttons[i2];
					}
				}
			}
			else {
				if (handleDirectInputPad(i)) {
					gamepadFound = true;
				}
			}
		}
	}
}

#endif

#include <process.h>

volatile int  iron_exec_async_done        = 1;
char         *iron_exec_async_output_file = NULL;
static HANDLE child_handle                = NULL;
static HANDLE wait_handle                 = NULL;

static void CALLBACK exec_wait_callback(PVOID lparam, BOOLEAN b) {
	iron_exec_async_done = 1;
}

void iron_exec_async(const char *path, char *argv[]) {
	iron_exec_async_done = 0;

	char cmd[4096] = {0};
	snprintf(cmd, sizeof(cmd), "\"%s\"", path);
	for (int i = 1; argv[i] != NULL; ++i) {
		strncat(cmd, " \"", sizeof(cmd) - strlen(cmd) - 1);
		strncat(cmd, argv[i], sizeof(cmd) - strlen(cmd) - 1);
		strncat(cmd, "\"", sizeof(cmd) - strlen(cmd) - 1);
	}
	wchar_t wcmd[4096];
	MultiByteToWideChar(CP_UTF8, 0, cmd, -1, wcmd, 4096);

	STARTUPINFOW        si = {0};
	PROCESS_INFORMATION pi = {0};
	si.cb                  = sizeof(si);
	si.dwFlags             = STARTF_USESHOWWINDOW;
	si.wShowWindow         = SW_HIDE;

	HANDLE out_handle = INVALID_HANDLE_VALUE;
	if (iron_exec_async_output_file != NULL) {
		wchar_t wpath[1024];
		MultiByteToWideChar(CP_UTF8, 0, iron_exec_async_output_file, -1, wpath, 1024);
		SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
		out_handle             = CreateFileW(wpath, GENERIC_WRITE, 0, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		si.dwFlags |= STARTF_USESTDHANDLES;
		si.hStdOutput = out_handle;
		si.hStdError  = out_handle;
	}

	BOOL success = CreateProcessW(NULL, wcmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
	if (out_handle != INVALID_HANDLE_VALUE) {
		CloseHandle(out_handle);
	}
	if (!success) {
		iron_exec_async_done = 1;
		return;
	}
	child_handle = pi.hProcess;
	CloseHandle(pi.hThread);
	RegisterWaitForSingleObject(&wait_handle, child_handle, exec_wait_callback, NULL, INFINITE, WT_EXECUTEONLYONCE);
}
