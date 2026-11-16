// Terminal window placed next to the pad window

bool terminal_show = false;

#ifdef IRON_LINUX

#include <signal.h>
#include <sys/wait.h>

// The compositor decides where the terminal goes
static pid_t terminal_pid = 0;

static bool terminal_exited() {
	if (terminal_pid > 0 && waitpid(terminal_pid, NULL, WNOHANG) != 0) {
		terminal_pid = 0;
	}
	return terminal_pid == 0;
}

static void terminal_open() {
	terminal_pid = fork();
	if (terminal_pid == 0) {
		if (pad_project_dir != NULL && chdir(pad_project_dir) != 0) {
			_exit(127);
		}
		char *terminal = getenv("TERMINAL");
		if (terminal != NULL && terminal[0] != '\0') {
			execlp(terminal, terminal, (char *)NULL);
		}
		execlp("foot", "foot", (char *)NULL);
		execlp("xfce4-terminal", "xfce4-terminal", "--disable-server", (char *)NULL); // Own process, exits with the window
		_exit(127);
	}
	terminal_show = terminal_pid > 0;
}

void terminal_toggle() {
	if (!terminal_show) {
		terminal_open();
	}
	else if (terminal_pid > 0) {
		kill(terminal_pid, SIGTERM);
	}
}

void terminal_update() {
	if (terminal_show && terminal_exited()) { // Closed from here or from the terminal
		terminal_show = false;
	}
}

// Keeps the frames coming until the terminal is closed
bool terminal_idle_wake() {
	return terminal_show && terminal_exited();
}

void terminal_shutdown() {
	if (terminal_pid > 0) {
		kill(terminal_pid, SIGTERM);
	}
}

#elif defined(IRON_WINDOWS)

#include <dwmapi.h>

#define TERMINAL_SPLIT     0.7 // Pad takes the left part of the screen
#define TERMINAL_KNOWN_MAX 64

struct HWND__ *iron_windows_window_handle();

static HANDLE          terminal_process = NULL;
static HWND            terminal_window  = NULL; // Placed once the window shows up
static i32             terminal_settle  = 0;    // Frames until the windows are placed again
static WINDOWPLACEMENT terminal_pad_placement;  // Pad window before the split
static bool            terminal_pad_fullscreen = false;
static HWND            terminal_known[TERMINAL_KNOWN_MAX]; // Terminal windows opened before ours
static i32             terminal_known_count = 0;

// Console host or windows terminal
static bool terminal_is_console(HWND window) {
	char name[64] = "";
	GetClassNameA(window, name, sizeof(name));
	return strcmp(name, "ConsoleWindowClass") == 0 || strcmp(name, "CASCADIA_HOSTING_WINDOW_CLASS") == 0;
}

static BOOL CALLBACK terminal_enum_known(HWND window, LPARAM param) {
	if (terminal_is_console(window) && terminal_known_count < TERMINAL_KNOWN_MAX) {
		terminal_known[terminal_known_count++] = window;
	}
	return TRUE;
}

// The window of the terminal process belongs to its host, take the first one that shows up
static BOOL CALLBACK terminal_enum_new(HWND window, LPARAM param) {
	if (!terminal_is_console(window) || !IsWindowVisible(window)) {
		return TRUE;
	}
	for (i32 i = 0; i < terminal_known_count; ++i) {
		if (terminal_known[i] == window) {
			return TRUE;
		}
	}
	terminal_window = window;
	return FALSE;
}

// Moves the window so its visible frame covers the rect
static void terminal_place(HWND window, i32 x, i32 y, i32 w, i32 h) {
	if (IsZoomed(window)) {
		ShowWindow(window, SW_SHOWNOACTIVATE);
	}
	RECT rect;
	RECT frame; // Without the invisible resize borders
	if (GetWindowRect(window, &rect) && SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame)))) {
		x -= frame.left - rect.left;
		y -= frame.top - rect.top;
		w += (frame.left - rect.left) + (rect.right - frame.right);
		h += (frame.top - rect.top) + (rect.bottom - frame.bottom);
	}
	SetWindowPos(window, NULL, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
}

// Pad takes the left part of the screen, the terminal gets the rest
static void terminal_layout() {
	HWND        pad = iron_windows_window_handle();
	MONITORINFO mi  = {sizeof(mi)};
	GetMonitorInfoW(MonitorFromWindow(pad, MONITOR_DEFAULTTONEAREST), &mi);
	i32 x     = mi.rcWork.left; // Screen without the taskbar
	i32 y     = mi.rcWork.top;
	i32 w     = mi.rcWork.right - mi.rcWork.left;
	i32 h     = mi.rcWork.bottom - mi.rcWork.top;
	i32 pad_w = math_floor(w * TERMINAL_SPLIT);
	terminal_place(pad, x, y, pad_w, h);
	if (terminal_window != NULL) {
		terminal_place(terminal_window, x + pad_w, y, w - pad_w, h);
	}
}

static bool terminal_exited() {
	if (terminal_process != NULL &&
	    (WaitForSingleObject(terminal_process, 0) == WAIT_OBJECT_0 || (terminal_window != NULL && !IsWindow(terminal_window)))) {
		CloseHandle(terminal_process);
		terminal_process = NULL;
	}
	return terminal_process == NULL;
}

static void terminal_open() {
	terminal_known_count = 0;
	EnumWindows(terminal_enum_known, 0);

	wchar_t cmd[MAX_PATH] = L"";
	if (GetEnvironmentVariableW(L"TERMINAL", cmd, MAX_PATH) == 0) {
		GetEnvironmentVariableW(L"ComSpec", cmd, MAX_PATH);
	}
	wchar_t  dir[1024];
	wchar_t *wdir = NULL;
	if (pad_project_dir != NULL && MultiByteToWideChar(CP_UTF8, 0, pad_project_dir, -1, dir, 1024) > 0) {
		wdir = dir;
	}
	STARTUPINFOW        si = {0};
	PROCESS_INFORMATION pi = {0};
	si.cb                  = sizeof(si);
	if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NEW_CONSOLE, NULL, wdir, &si, &pi)) {
		return;
	}
	CloseHandle(pi.hThread);
	terminal_process = pi.hProcess;

	terminal_pad_fullscreen = iron_window_get_mode() == IRON_WINDOW_MODE_FULLSCREEN;
	if (terminal_pad_fullscreen) {
		iron_window_change_mode(IRON_WINDOW_MODE_WINDOW);
	}
	terminal_pad_placement.length = sizeof(terminal_pad_placement);
	GetWindowPlacement(iron_windows_window_handle(), &terminal_pad_placement); // Keeps the maximized state
	terminal_window = NULL;
	terminal_settle = 0;
	terminal_layout();
	terminal_show = true;
}

// Pad window goes back to where it was
static void terminal_restore() {
	HWND pad = iron_windows_window_handle();
	SetWindowPlacement(pad, &terminal_pad_placement);
	if (terminal_pad_fullscreen) {
		iron_window_change_mode(IRON_WINDOW_MODE_FULLSCREEN);
	}
	SetForegroundWindow(pad);
	terminal_show = false;
}

static void terminal_close() {
	if (terminal_window != NULL) {
		PostMessageW(terminal_window, WM_CLOSE, 0, 0);
	}
	if (terminal_process != NULL) {
		TerminateProcess(terminal_process, 0);
	}
}

void terminal_toggle() {
	if (!terminal_show) {
		terminal_open();
	}
	else {
		terminal_close(); // Restored in terminal_update
	}
}

void terminal_update() {
	if (!terminal_show) {
		return;
	}
	if (terminal_exited()) { // Closed from here or from the terminal
		terminal_restore();
		return;
	}
	if (terminal_window == NULL) {
		EnumWindows(terminal_enum_new, 0);
		if (terminal_window != NULL) {
			terminal_layout();
			terminal_settle = 10;
		}
	}
	else if (terminal_settle > 0 && --terminal_settle == 0) {
		terminal_layout(); // Terminal has applied its startup size by now
	}
}

// Keeps the frames coming until the terminal is placed, and once it is closed
bool terminal_idle_wake() {
	return terminal_show && (terminal_window == NULL || terminal_settle > 0 || terminal_exited());
}

void terminal_shutdown() {
	terminal_close();
}

#else

void terminal_toggle() {}

void terminal_update() {}

bool terminal_idle_wake() {
	return false;
}

void terminal_shutdown() {}

#endif
