#import "macos_system.h"
#import <Cocoa/Cocoa.h>
#include <iron_gpu.h>
#include <iron_math.h>
#include <iron_system.h>
#include <iron_video.h>
#include <mach/mach_time.h>
#include <objc/runtime.h>
#include <stdbool.h>

struct window_data {
	id   handle;
	id   view;
	bool fullscreen;
	void (*resize_callback)(int width, int height, void *data);
	void *resize_callback_data;
	bool (*close_callback)(void *data);
	void *close_callback_data;
};

static struct window_data windows[1]              = {0};
static bool               controlkey_mouse_button = false;
static bool               keyboard_shown          = false;
static const char        *video_formats[]         = {"ogv", NULL};
static NSApplication     *myapp;
static NSWindow          *window;
static BasicMTKView      *view;
static char               language[3];
static int                current_cursor_index = 0;
static int                key_translated[128];

static void init_key_translation(void) {
	for (int i = 0; i < 128; ++i)
		key_translated[i] = KEY_CODE_UNKNOWN;
	key_translated[0]   = KEY_CODE_A;
	key_translated[1]   = KEY_CODE_S;
	key_translated[2]   = KEY_CODE_D;
	key_translated[3]   = KEY_CODE_F;
	key_translated[4]   = KEY_CODE_H;
	key_translated[5]   = KEY_CODE_G;
	key_translated[6]   = KEY_CODE_Z;
	key_translated[7]   = KEY_CODE_X;
	key_translated[8]   = KEY_CODE_C;
	key_translated[9]   = KEY_CODE_V;
	key_translated[11]  = KEY_CODE_B;
	key_translated[12]  = KEY_CODE_Q;
	key_translated[13]  = KEY_CODE_W;
	key_translated[14]  = KEY_CODE_E;
	key_translated[15]  = KEY_CODE_R;
	key_translated[16]  = KEY_CODE_Y;
	key_translated[17]  = KEY_CODE_T;
	key_translated[18]  = KEY_CODE_1;
	key_translated[19]  = KEY_CODE_2;
	key_translated[20]  = KEY_CODE_3;
	key_translated[21]  = KEY_CODE_4;
	key_translated[22]  = KEY_CODE_6;
	key_translated[23]  = KEY_CODE_5;
	key_translated[24]  = KEY_CODE_PLUS;
	key_translated[25]  = KEY_CODE_9;
	key_translated[26]  = KEY_CODE_7;
	key_translated[27]  = KEY_CODE_HYPHEN_MINUS;
	key_translated[28]  = KEY_CODE_8;
	key_translated[29]  = KEY_CODE_0;
	key_translated[30]  = KEY_CODE_CLOSE_BRACKET;
	key_translated[31]  = KEY_CODE_O;
	key_translated[32]  = KEY_CODE_U;
	key_translated[33]  = KEY_CODE_OPEN_BRACKET;
	key_translated[34]  = KEY_CODE_I;
	key_translated[35]  = KEY_CODE_P;
	key_translated[36]  = KEY_CODE_RETURN;
	key_translated[37]  = KEY_CODE_L;
	key_translated[38]  = KEY_CODE_J;
	key_translated[39]  = KEY_CODE_QUOTE;
	key_translated[40]  = KEY_CODE_K;
	key_translated[41]  = KEY_CODE_SEMICOLON;
	key_translated[42]  = KEY_CODE_BACK_SLASH;
	key_translated[43]  = KEY_CODE_COMMA;
	key_translated[44]  = KEY_CODE_SLASH;
	key_translated[45]  = KEY_CODE_N;
	key_translated[46]  = KEY_CODE_M;
	key_translated[47]  = KEY_CODE_PERIOD;
	key_translated[48]  = KEY_CODE_TAB;
	key_translated[49]  = KEY_CODE_SPACE;
	key_translated[50]  = KEY_CODE_BACK_QUOTE;
	key_translated[51]  = KEY_CODE_BACKSPACE;
	key_translated[53]  = KEY_CODE_ESCAPE;
	key_translated[64]  = KEY_CODE_F17;
	key_translated[65]  = KEY_CODE_DECIMAL;
	key_translated[67]  = KEY_CODE_MULTIPLY;
	key_translated[69]  = KEY_CODE_ADD;
	key_translated[71]  = KEY_CODE_NUM_LOCK;
	key_translated[75]  = KEY_CODE_DIVIDE;
	key_translated[76]  = KEY_CODE_RETURN;
	key_translated[78]  = KEY_CODE_SUBTRACT;
	key_translated[79]  = KEY_CODE_F18;
	key_translated[80]  = KEY_CODE_F19;
	key_translated[82]  = KEY_CODE_NUMPAD_0;
	key_translated[83]  = KEY_CODE_NUMPAD_1;
	key_translated[84]  = KEY_CODE_NUMPAD_2;
	key_translated[85]  = KEY_CODE_NUMPAD_3;
	key_translated[86]  = KEY_CODE_NUMPAD_4;
	key_translated[87]  = KEY_CODE_NUMPAD_5;
	key_translated[88]  = KEY_CODE_NUMPAD_6;
	key_translated[89]  = KEY_CODE_NUMPAD_7;
	key_translated[90]  = KEY_CODE_F20;
	key_translated[91]  = KEY_CODE_NUMPAD_8;
	key_translated[92]  = KEY_CODE_NUMPAD_9;
	key_translated[96]  = KEY_CODE_F5;
	key_translated[97]  = KEY_CODE_F6;
	key_translated[98]  = KEY_CODE_F7;
	key_translated[99]  = KEY_CODE_F3;
	key_translated[100] = KEY_CODE_F8;
	key_translated[101] = KEY_CODE_F9;
	key_translated[103] = KEY_CODE_F11;
	key_translated[105] = KEY_CODE_F13;
	key_translated[106] = KEY_CODE_F16;
	key_translated[107] = KEY_CODE_F14;
	key_translated[109] = KEY_CODE_F10;
	key_translated[111] = KEY_CODE_F12;
	key_translated[113] = KEY_CODE_F15;
	key_translated[114] = KEY_CODE_INSERT;
	key_translated[115] = KEY_CODE_HOME;
	key_translated[116] = KEY_CODE_PAGE_UP;
	key_translated[117] = KEY_CODE_DELETE;
	key_translated[118] = KEY_CODE_F4;
	key_translated[119] = KEY_CODE_END;
	key_translated[120] = KEY_CODE_F2;
	key_translated[121] = KEY_CODE_PAGE_DOWN;
	key_translated[122] = KEY_CODE_F1;
	key_translated[123] = KEY_CODE_LEFT;
	key_translated[124] = KEY_CODE_RIGHT;
	key_translated[125] = KEY_CODE_DOWN;
	key_translated[126] = KEY_CODE_UP;
}

@implementation BasicMTKView

static bool shift    = false;
static bool ctrl     = false;
static bool alt      = false;
static bool cmd      = false;
static bool capslock = false;

- (void)flagsChanged:(NSEvent *)theEvent {
	if (shift) {
		iron_internal_keyboard_trigger_key_up(KEY_CODE_SHIFT);
		shift = false;
	}
	if (ctrl) {
		iron_internal_keyboard_trigger_key_up(KEY_CODE_CONTROL);
		ctrl = false;
	}
	if (alt) {
		iron_internal_keyboard_trigger_key_up(KEY_CODE_ALT);
		alt = false;
	}
	if (cmd) {
		iron_internal_keyboard_trigger_key_up(KEY_CODE_META);
		cmd = false;
	}
	if (capslock) {
		iron_internal_keyboard_trigger_key_up(KEY_CODE_CAPS_LOCK);
		capslock = false;
	}

	if ([theEvent modifierFlags] & NSShiftKeyMask) {
		iron_internal_keyboard_trigger_key_down(KEY_CODE_SHIFT);
		shift = true;
	}
	if ([theEvent modifierFlags] & NSControlKeyMask) {
		iron_internal_keyboard_trigger_key_down(KEY_CODE_CONTROL);
		ctrl = true;
	}
	if ([theEvent modifierFlags] & NSAlternateKeyMask) {
		iron_internal_keyboard_trigger_key_down(KEY_CODE_ALT);
		alt = true;
	}
	if ([theEvent modifierFlags] & NSCommandKeyMask) {
		iron_internal_keyboard_trigger_key_down(KEY_CODE_META);
		cmd = true;
	}
	if ([theEvent modifierFlags] & NSAlphaShiftKeyMask) {
		iron_internal_keyboard_trigger_key_down(KEY_CODE_CAPS_LOCK);
		capslock = true;
	}
}

- (void)keyDown:(NSEvent *)theEvent {
	if ([theEvent isARepeat])
		return;

	unsigned short kc   = [theEvent keyCode];
	int            code = (kc < 128) ? key_translated[kc] : KEY_CODE_UNKNOWN;
	if (code != KEY_CODE_UNKNOWN) {
		iron_internal_keyboard_trigger_key_down(code);
	}

	NSString *chars = [theEvent characters];
	if (![chars length])
		return;
	unichar ch = [chars characterAtIndex:0];

	if (ch >= 0xF700)
		return;
	if (ch == 27)
		return;

	if ([theEvent modifierFlags] & NSCommandKeyMask) {
		NSString *base = [theEvent charactersIgnoringModifiers];
		unichar   bc   = [base length] ? [base characterAtIndex:0] : ch;
		if (bc == 'x') {
			iron_internal_cut_callback();
		}
		if (bc == 'c') {
			iron_internal_copy_callback();
		}
		if (bc == 'v') {
			NSPasteboard *board = [NSPasteboard generalPasteboard];
			NSString     *data  = [board stringForType:NSStringPboardType];
			if (data != nil) {
				char charData[4096];
				strcpy(charData, [data UTF8String]);
				iron_internal_paste_callback(charData);
			}
		}
		return;
	}

	if (ch == NSCarriageReturnCharacter || ch == NSNewlineCharacter || ch == NSEnterCharacter) {
		iron_internal_keyboard_trigger_key_press('\n');
		return;
	}
	if (ch == 0x7f) {
		iron_internal_keyboard_trigger_key_press('\x08');
		return;
	}

	iron_internal_keyboard_trigger_key_press(ch);
}

- (void)keyUp:(NSEvent *)theEvent {
	unsigned short kc   = [theEvent keyCode];
	int            code = (kc < 128) ? key_translated[kc] : KEY_CODE_UNKNOWN;
	if (code != KEY_CODE_UNKNOWN) {
		iron_internal_keyboard_trigger_key_up(code);
	}
}

static int get_mouse_x(NSEvent *event) {
	NSWindow *window = [[NSApplication sharedApplication] mainWindow];
	float     scale  = [window backingScaleFactor];
	return (int)([event locationInWindow].x * scale);
}

static int get_mouse_y(NSEvent *event) {
	NSWindow *window = [[NSApplication sharedApplication] mainWindow];
	float     scale  = [window backingScaleFactor];
	return (int)(iron_window_height() - [event locationInWindow].y * scale);
}

- (void)mouseDown:(NSEvent *)theEvent {
	if ([theEvent modifierFlags] & NSControlKeyMask) {
		controlkey_mouse_button = true;
		iron_internal_mouse_trigger_press(1, get_mouse_x(theEvent), get_mouse_y(theEvent));
	}
	else {
		controlkey_mouse_button = false;
		iron_internal_mouse_trigger_press(0, get_mouse_x(theEvent), get_mouse_y(theEvent));
	}

	if ([theEvent subtype] == NSTabletPointEventSubtype) {
		iron_internal_pen_trigger_press(get_mouse_x(theEvent), get_mouse_y(theEvent), theEvent.pressure);
	}
}

- (void)mouseUp:(NSEvent *)theEvent {
	if (controlkey_mouse_button) {
		iron_internal_mouse_trigger_release(1, get_mouse_x(theEvent), get_mouse_y(theEvent));
	}
	else {
		iron_internal_mouse_trigger_release(0, get_mouse_x(theEvent), get_mouse_y(theEvent));
	}
	controlkey_mouse_button = false;

	if ([theEvent subtype] == NSTabletPointEventSubtype) {
		iron_internal_pen_trigger_release(get_mouse_x(theEvent), get_mouse_y(theEvent), theEvent.pressure);
	}
}

- (void)mouseMoved:(NSEvent *)theEvent {
	iron_internal_mouse_trigger_move(get_mouse_x(theEvent), get_mouse_y(theEvent));
}

- (void)mouseDragged:(NSEvent *)theEvent {
	iron_internal_mouse_trigger_move(get_mouse_x(theEvent), get_mouse_y(theEvent));

	if ([theEvent subtype] == NSTabletPointEventSubtype) {
		iron_internal_pen_trigger_move(get_mouse_x(theEvent), get_mouse_y(theEvent), theEvent.pressure);
	}
}

- (void)rightMouseDown:(NSEvent *)theEvent {
	iron_internal_mouse_trigger_press(1, get_mouse_x(theEvent), get_mouse_y(theEvent));
}

- (void)rightMouseUp:(NSEvent *)theEvent {
	iron_internal_mouse_trigger_release(1, get_mouse_x(theEvent), get_mouse_y(theEvent));
}

- (void)rightMouseDragged:(NSEvent *)theEvent {
	iron_internal_mouse_trigger_move(get_mouse_x(theEvent), get_mouse_y(theEvent));
}

- (void)otherMouseDown:(NSEvent *)theEvent {
	iron_internal_mouse_trigger_press(2, get_mouse_x(theEvent), get_mouse_y(theEvent));
}

- (void)otherMouseUp:(NSEvent *)theEvent {
	iron_internal_mouse_trigger_release(2, get_mouse_x(theEvent), get_mouse_y(theEvent));
}

- (void)otherMouseDragged:(NSEvent *)theEvent {
	iron_internal_mouse_trigger_move(get_mouse_x(theEvent), get_mouse_y(theEvent));
}

- (void)scrollWheel:(NSEvent *)theEvent {
	float delta = [theEvent deltaY];
	iron_internal_mouse_trigger_scroll(-delta);
}

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
	NSPasteboard   *pboard         = [sender draggingPasteboard];
	NSDragOperation sourceDragMask = [sender draggingSourceOperationMask];
	if ([[pboard types] containsObject:NSURLPboardType]) {
		if (sourceDragMask & NSDragOperationLink) {
			return NSDragOperationLink;
		}
	}
	return NSDragOperationNone;
}

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
	NSPasteboard *pboard = [sender draggingPasteboard];
	if ([[pboard types] containsObject:NSURLPboardType]) {
		NSArray *urls = [pboard readObjectsForClasses:@[ [NSURL class] ] options:nil];
		for (NSURL *fileURL in urls) {
			const char *filePath = [fileURL.path cStringUsingEncoding:NSUTF8StringEncoding];
			iron_internal_drop_files_callback(filePath);
		}
	}
	return YES;
}

- (id)initWithFrame:(NSRect)frameRect {
	self = [super initWithFrame:frameRect];

	device       = MTLCreateSystemDefaultDevice();
	commandQueue = [device newCommandQueue];
	library      = [device newDefaultLibrary];

	CAMetalLayer *metalLayer   = (CAMetalLayer *)self.layer;
	metalLayer.device          = device;
	metalLayer.pixelFormat     = MTLPixelFormatBGRA8Unorm;
	metalLayer.framebufferOnly = YES;
	metalLayer.opaque          = YES;
	metalLayer.backgroundColor = nil;
	return self;
}

- (BOOL)acceptsFirstResponder {
	return YES;
}

- (BOOL)becomeFirstResponder {
	return YES;
}

- (BOOL)resignFirstResponder {
	return YES;
}

- (void)resize:(NSSize)size {
	[self setFrameSize:size];
}

- (CAMetalLayer *)metalLayer {
	return (CAMetalLayer *)self.layer;
}

- (id<MTLDevice>)metalDevice {
	return device;
}

- (id<MTLCommandQueue>)metalQueue {
	return commandQueue;
}

@end

void iron_copy_to_clipboard(const char *text) {
	NSPasteboard *board = [NSPasteboard generalPasteboard];
	[board clearContents];
	[board setString:[NSString stringWithUTF8String:text] forType:NSStringPboardType];
}

int iron_count_displays(void) {
	NSArray *screens = [NSScreen screens];
	return (int)[screens count];
}

int iron_primary_display(void) {
	NSArray  *screens      = [NSScreen screens];
	NSScreen *mainScreen   = [NSScreen mainScreen];
	int       max_displays = 8;
	for (int i = 0; i < max_displays; ++i) {
		if (mainScreen == screens[i]) {
			return i;
		}
	}
	return -1;
}

void iron_display_init(void) {}

iron_display_mode_t iron_display_current_mode(int display) {
	NSArray            *screens    = [NSScreen screens];
	NSScreen           *screen     = screens[display];
	NSRect              screenRect = [screen frame];
	iron_display_mode_t dm;
	dm.width          = screenRect.size.width;
	dm.height         = screenRect.size.height;
	dm.frequency      = 60;
	dm.bits_per_pixel = 32;

	NSDictionary *description         = [screen deviceDescription];
	NSSize        displayPixelSize    = [[description objectForKey:NSDeviceSize] sizeValue];
	NSNumber     *screenNumber        = [description objectForKey:@"NSScreenNumber"];
	CGSize        displayPhysicalSize = CGDisplayScreenSize([screenNumber unsignedIntValue]);            // in millimeters
	double        ppi                 = displayPixelSize.width / (displayPhysicalSize.width * 0.039370); // Convert MM to INCH
	dm.pixels_per_inch                = round(ppi);

	return dm;
}

bool iron_mouse_can_lock(void) {
	return true;
}

void iron_mouse_show(void) {
	CGDisplayShowCursor(kCGDirectMainDisplay);
}

void iron_mouse_hide(void) {
	CGDisplayHideCursor(kCGDirectMainDisplay);
}

void iron_mouse_set_position(int x, int y) {
	NSWindow *window      = windows[0].handle;
	float     scale       = [window backingScaleFactor];
	NSRect    rect        = [[window contentView] bounds];
	NSPoint   windowpoint = NSMakePoint(x / scale, rect.size.height - y / scale);
	NSPoint   screenpoint = [window convertPointToScreen:windowpoint];
	CGPoint   cgpoint     = CGPointMake(screenpoint.x, [[NSScreen mainScreen] frame].size.height - screenpoint.y);
	CGDisplayMoveCursorToPoint(kCGDirectMainDisplay, cgpoint);
	CGAssociateMouseAndMouseCursorPosition(true);
}

void iron_mouse_get_position(int *x, int *y) {
	NSWindow *window = windows[0].handle;
	float     scale  = [window backingScaleFactor];
	NSRect    rect   = [[window contentView] bounds];
	NSPoint   point  = [window mouseLocationOutsideOfEventStream];
	*x               = (int)(point.x * scale);
	*y               = (int)((rect.size.height - point.y) * scale);
}

void iron_mouse_set_cursor(iron_cursor_t cursor_index) {
	if (current_cursor_index == cursor_index) {
		return;
	}
	current_cursor_index = cursor_index;
	if (cursor_index == IRON_CURSOR_HAND) {
		[[NSCursor pointingHandCursor] set];
	}
	else if (cursor_index == IRON_CURSOR_IBEAM) {
		[[NSCursor IBeamCursor] set];
	}
	else if (cursor_index == IRON_CURSOR_SIZEWE) {
		[[NSCursor resizeLeftRightCursor] set];
	}
	else if (cursor_index == IRON_CURSOR_SIZENS) {
		[[NSCursor resizeUpDownCursor] set];
	}
	else {
		[[NSCursor arrowCursor] set];
	}
}

void iron_keyboard_show(void) {
	keyboard_shown = true;
}

void iron_keyboard_hide(void) {
	keyboard_shown = false;
}

bool iron_keyboard_active(void) {
	return keyboard_shown;
}

const char *iron_system_id(void) {
	return "macOS";
}

const char **iron_video_formats(void) {
	return video_formats;
}

void iron_set_keep_screen_on(bool on) {}

double iron_frequency(void) {
	mach_timebase_info_data_t info;
	mach_timebase_info(&info);
	return (double)info.denom / (double)info.numer / 1e-9;
}

uint64_t iron_timestamp(void) {
	return mach_absolute_time();
}

bool with_autoreleasepool(bool (*f)(void)) {
	@autoreleasepool {
		return f();
	}
}

const char *iron_get_resource_path(void) {
	return [[[NSBundle mainBundle] resourcePath] cStringUsingEncoding:NSUTF8StringEncoding];
}

@interface IronApplication : NSApplication {
}
- (void)terminate:(id)sender;
@end

@interface IronAppDelegate : NSObject <NSWindowDelegate> {
}
- (void)windowWillClose:(NSNotification *)notification;
- (void)windowDidResize:(NSNotification *)notification;
- (void)windowWillMiniaturize:(NSNotification *)notification;
- (void)windowDidDeminiaturize:(NSNotification *)notification;
- (void)windowDidResignMain:(NSNotification *)notification;
- (void)windowDidBecomeMain:(NSNotification *)notification;
@end

static IronAppDelegate *delegate;

CAMetalLayer *get_metal_layer(void) {
	return [view metalLayer];
}

id get_metal_device(void) {
	return [view metalDevice];
}

id get_metal_queue(void) {
	return [view metalQueue];
}

bool iron_internal_handle_messages(void) {
	NSEvent *event = [myapp nextEventMatchingMask:NSAnyEventMask
	                                    untilDate:[NSDate distantPast]
	                                       inMode:NSDefaultRunLoopMode
	                                      dequeue:YES]; // distantPast: non-blocking
	if (event != nil) {
		[myapp sendEvent:event];
		[myapp updateWindows];
	}

	// Sleep for a frame to limit the calls when the window is not visible.
	if (!window.visible) {
		[NSThread sleepForTimeInterval:1.0 / 60];
	}
	return true;
}

static void create_window(iron_window_options_t *options) {
	int width     = options->width / [[NSScreen mainScreen] backingScaleFactor];
	int height    = options->height / [[NSScreen mainScreen] backingScaleFactor];
	int styleMask = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable;
	if ((options->features & IRON_WINDOW_FEATURES_RESIZABLE) || (options->features & IRON_WINDOW_FEATURES_MAXIMIZABLE)) {
		styleMask |= NSWindowStyleMaskResizable;
	}
	if (options->features & IRON_WINDOW_FEATURES_MINIMIZABLE) {
		styleMask |= NSWindowStyleMaskMiniaturizable;
	}

	view = [[BasicMTKView alloc] initWithFrame:NSMakeRect(0, 0, width, height)];
	[view registerForDraggedTypes:[NSArray arrayWithObjects:NSURLPboardType, nil]];
	window   = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, width, height) styleMask:styleMask backing:NSBackingStoreBuffered defer:TRUE];
	delegate = [IronAppDelegate alloc];
	[window setDelegate:delegate];
	[window setTitle:[NSString stringWithCString:options->title encoding:NSUTF8StringEncoding]];
	[window setAcceptsMouseMovedEvents:YES];
	[[window contentView] addSubview:view];
	[window center];

	windows[0].handle = window;
	windows[0].view   = view;

	[window makeKeyAndOrderFront:nil];

	if (options->mode == IRON_WINDOW_MODE_FULLSCREEN) {
		[window toggleFullScreen:nil];
		windows[0].fullscreen = true;
	}
}

void iron_window_change_window_mode(iron_window_mode_t mode) {
	switch (mode) {
	case IRON_WINDOW_MODE_WINDOW:
		if (windows[0].fullscreen) {
			[window toggleFullScreen:nil];
			windows[0].fullscreen = false;
		}
		break;
	case IRON_WINDOW_MODE_FULLSCREEN:
		if (!windows[0].fullscreen) {
			[window toggleFullScreen:nil];
			windows[0].fullscreen = true;
		}
		break;
	}
}

void iron_window_set_close_callback(bool (*callback)(void *), void *data) {
	windows[0].close_callback      = callback;
	windows[0].close_callback_data = data;
}

static void add_menubar(void) {
	NSString *appName = [[NSProcessInfo processInfo] processName];

	NSMenu     *appMenu      = [NSMenu new];
	NSString   *quitTitle    = [@"Quit " stringByAppendingString:appName];
	NSMenuItem *quitMenuItem = [[NSMenuItem alloc] initWithTitle:quitTitle action:@selector(terminate:) keyEquivalent:@"q"];
	[appMenu addItem:quitMenuItem];

	NSMenuItem *appMenuItem = [NSMenuItem new];
	[appMenuItem setSubmenu:appMenu];

	NSMenu *menubar = [NSMenu new];
	[menubar addItem:appMenuItem];
	[NSApp setMainMenu:menubar];
}

void iron_init(iron_window_options_t *win) {
	init_key_translation();
	@autoreleasepool {
		myapp = [IronApplication sharedApplication];
		[myapp finishLaunching];
		[[NSRunningApplication currentApplication] activateWithOptions:(NSApplicationActivateAllWindows | NSApplicationActivateIgnoringOtherApps)];
		NSApp.activationPolicy = NSApplicationActivationPolicyRegular;
		add_menubar();

#ifdef WITH_GAMEPAD
		hidManager = (struct HIDManager *)malloc(sizeof(struct HIDManager));
		HIDManager_init(hidManager);
#endif
	}

	create_window(win);
	gpu_init(win->depth_bits, true);
}

int iron_window_width() {
	NSWindow *window = windows[0].handle;
	float     scale  = [window backingScaleFactor];
	return [[window contentView] frame].size.width * scale;
}

int iron_window_height() {
	NSWindow *window = windows[0].handle;
	float     scale  = [window backingScaleFactor];
	return [[window contentView] frame].size.height * scale;
}

void iron_load_url(const char *url) {
	[[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:[NSString stringWithUTF8String:url]]];
}

const char *iron_language(void) {
	NSString   *nsstr = [[NSLocale preferredLanguages] objectAtIndex:0];
	const char *lang  = [nsstr UTF8String];
	language[0]       = lang[0];
	language[1]       = lang[1];
	language[2]       = 0;
	return language;
}

void iron_internal_shutdown(void) {}

const char *iron_internal_save_path(void) {
	NSArray  *paths        = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES);
	NSString *resolvedPath = [paths objectAtIndex:0];
	NSString *appName      = [NSString stringWithUTF8String:iron_application_name()];
	resolvedPath           = [resolvedPath stringByAppendingPathComponent:appName];

	NSFileManager *fileMgr = [[NSFileManager alloc] init];

	NSError *error;
	[fileMgr createDirectoryAtPath:resolvedPath withIntermediateDirectories:YES attributes:nil error:&error];

	resolvedPath = [resolvedPath stringByAppendingString:@"/"];
	return [resolvedPath cStringUsingEncoding:NSUTF8StringEncoding];
}

int main(int argc, char **argv) {
	return kickstart(argc, argv);
}

@implementation IronApplication

- (void)terminate:(id)sender {
	iron_stop();
}

@end

@implementation IronAppDelegate
- (BOOL)windowShouldClose:(NSWindow *)sender {
	if (windows[0].close_callback != NULL) {
		if (windows[0].close_callback(windows[0].close_callback_data)) {
			return YES;
		}
		else {
			return NO;
		}
	}
	return YES;
}

- (void)windowWillClose:(NSNotification *)notification {
	iron_stop();
}

void iron_internal_call_resize_callback(int width, int height) {
	if (windows[0].resize_callback != NULL) {
		windows[0].resize_callback(width, height, windows[0].resize_callback_data);
	}
}

- (void)windowDidResize:(NSNotification *)notification {
	NSWindow *window = [notification object];
	NSSize    size   = [[window contentView] frame].size;
	[view resize:size];

	float scale = [window backingScaleFactor];
	int   w     = size.width * scale;
	int   h     = size.height * scale;

	gpu_resize(w, h);
	iron_internal_call_resize_callback(w, h);
}

- (void)windowWillMiniaturize:(NSNotification *)notification {
	iron_internal_background_callback();
}

- (void)windowDidDeminiaturize:(NSNotification *)notification {
	iron_internal_foreground_callback();
}

- (void)windowDidResignMain:(NSNotification *)notification {
}

- (void)windowDidBecomeMain:(NSNotification *)notification {
}

@end

int iron_window_x() {
	return 0;
}

int iron_window_y() {
	return 0;
}

void iron_window_resize(int width, int height) {}
void iron_window_move(int x, int y) {}
void iron_window_change_mode(iron_window_mode_t mode) {}
void iron_window_destroy() {}
void iron_window_show() {}
void iron_window_hide() {}
void iron_window_create(iron_window_options_t *win) {}

void iron_window_set_title(const char *title) {
	NSWindow *window = windows[0].handle;
	[window setTitle:[NSString stringWithUTF8String:title]];
}

void iron_window_set_resize_callback(void (*callback)(int x, int y, void *data), void *data) {
	windows[0].resize_callback      = callback;
	windows[0].resize_callback_data = data;
}

iron_window_mode_t iron_window_get_mode() {
	return IRON_WINDOW_MODE_WINDOW;
}

int iron_window_display() {
	return 0;
}

#ifdef WITH_GAMEPAD

static struct HIDManager *hidManager;

bool iron_gamepad_connected(int num) {
	return true;
}

void iron_gamepad_rumble(int gamepad, float left, float right) {}

static void inputValueCallback(void *inContext, IOReturn inResult, void *inSender, IOHIDValueRef inIOHIDValueRef);
static void valueAvailableCallback(void *inContext, IOReturn inResult, void *inSender);
static void reset(struct HIDGamepad *gamepad);
static void initDeviceElements(struct HIDGamepad *gamepad, CFArrayRef elements);
static void buttonChanged(struct HIDGamepad *gamepad, IOHIDElementRef elementRef, IOHIDValueRef valueRef, int buttonIndex);
static void axisChanged(struct HIDGamepad *gamepad, IOHIDElementRef elementRef, IOHIDValueRef valueRef, int axisIndex);

static int iron_mini(int a, int b) {
	return a > b ? b : a;
}

static void cstringFromCFStringRef(CFStringRef string, char *cstr, size_t clen) {
	cstr[0] = '\0';
	if (string != NULL) {
		char temp[256];
		if (CFStringGetCString(string, temp, 256, kCFStringEncodingUTF8)) {
			temp[iron_mini(255, (int)(clen - 1))] = '\0';
			strncpy(cstr, temp, clen);
		}
	}
}

void HIDGamepad_init(struct HIDGamepad *gamepad) {
	reset(gamepad);
}

void HIDGamepad_destroy(struct HIDGamepad *gamepad) {
	HIDGamepad_unbind(gamepad);
}

void HIDGamepad_bind(struct HIDGamepad *gamepad, IOHIDDeviceRef inDeviceRef, int inPadIndex) {
	gamepad->hidDeviceRef = inDeviceRef;
	gamepad->padIndex     = inPadIndex;

	IOHIDDeviceOpen(gamepad->hidDeviceRef, kIOHIDOptionsTypeSeizeDevice);
	IOHIDDeviceRegisterInputValueCallback(gamepad->hidDeviceRef, inputValueCallback, gamepad);
	IOHIDDeviceScheduleWithRunLoop(gamepad->hidDeviceRef, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);

	gamepad->hidQueueRef = IOHIDQueueCreate(kCFAllocatorDefault, gamepad->hidDeviceRef, 32, kIOHIDOptionsTypeNone);
	if (CFGetTypeID(gamepad->hidQueueRef) == IOHIDQueueGetTypeID()) {
		IOHIDQueueStart(gamepad->hidQueueRef);
		IOHIDQueueRegisterValueAvailableCallback(gamepad->hidQueueRef, valueAvailableCallback, gamepad);
		IOHIDQueueScheduleWithRunLoop(gamepad->hidQueueRef, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
	}

	CFArrayRef elementCFArrayRef = IOHIDDeviceCopyMatchingElements(gamepad->hidDeviceRef, NULL, kIOHIDOptionsTypeNone);
	initDeviceElements(gamepad, elementCFArrayRef);

	{
		CFNumberRef vendorIdRef = (CFNumberRef)IOHIDDeviceGetProperty(gamepad->hidDeviceRef, CFSTR(kIOHIDVendorIDKey));
		CFNumberGetValue(vendorIdRef, kCFNumberIntType, &gamepad->hidDeviceVendorID);

		CFNumberRef productIdRef = (CFNumberRef)IOHIDDeviceGetProperty(gamepad->hidDeviceRef, CFSTR(kIOHIDProductIDKey));
		CFNumberGetValue(productIdRef, kCFNumberIntType, &gamepad->hidDeviceProductID);

		CFStringRef vendorRef = (CFStringRef)IOHIDDeviceGetProperty(gamepad->hidDeviceRef, CFSTR(kIOHIDManufacturerKey));
		cstringFromCFStringRef(vendorRef, gamepad->hidDeviceVendor, sizeof(gamepad->hidDeviceVendor));

		CFStringRef productRef = (CFStringRef)IOHIDDeviceGetProperty(gamepad->hidDeviceRef, CFSTR(kIOHIDProductKey));
		cstringFromCFStringRef(productRef, gamepad->hidDeviceProduct, sizeof(gamepad->hidDeviceProduct));
	}
}

static void initDeviceElements(struct HIDGamepad *gamepad, CFArrayRef elements) {

	for (CFIndex i = 0, count = CFArrayGetCount(elements); i < count; ++i) {
		IOHIDElementRef  elementRef = (IOHIDElementRef)CFArrayGetValueAtIndex(elements, i);
		IOHIDElementType elemType   = IOHIDElementGetType(elementRef);

		IOHIDElementCookie cookie = IOHIDElementGetCookie(elementRef);

		uint32_t usagePage = IOHIDElementGetUsagePage(elementRef);
		uint32_t usage     = IOHIDElementGetUsage(elementRef);

		// Match up items
		switch (usagePage) {
		case kHIDPage_GenericDesktop:
			switch (usage) {
			case kHIDUsage_GD_X: // Left stick X
				gamepad->axis[0] = cookie;
				break;
			case kHIDUsage_GD_Y: // Left stick Y
				gamepad->axis[1] = cookie;
				break;
			case kHIDUsage_GD_Z: // Left trigger
				gamepad->axis[4] = cookie;
				break;
			case kHIDUsage_GD_Rx: // Right stick X
				gamepad->axis[2] = cookie;
				break;
			case kHIDUsage_GD_Ry: // Right stick Y
				gamepad->axis[3] = cookie;
				break;
			case kHIDUsage_GD_Rz: // Right trigger
				gamepad->axis[5] = cookie;
				break;
			case kHIDUsage_GD_Hatswitch:
				break;
			default:
				break;
			}
			break;
		case kHIDPage_Button:
			if ((usage >= 1) && (usage <= 15)) {
				// Button 1-11
				gamepad->buttons[usage - 1] = cookie;
			}
			break;
		default:
			break;
		}

		if (elemType == kIOHIDElementTypeInput_Misc || elemType == kIOHIDElementTypeInput_Button || elemType == kIOHIDElementTypeInput_Axis) {
			if (!IOHIDQueueContainsElement(gamepad->hidQueueRef, elementRef))
				IOHIDQueueAddElement(gamepad->hidQueueRef, elementRef);
		}
	}
}

void HIDGamepad_unbind(struct HIDGamepad *gamepad) {
	if (gamepad->hidQueueRef) {
		IOHIDQueueStop(gamepad->hidQueueRef);
		IOHIDQueueUnscheduleFromRunLoop(gamepad->hidQueueRef, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
	}
	if (gamepad->hidDeviceRef) {
		IOHIDDeviceUnscheduleFromRunLoop(gamepad->hidDeviceRef, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
		IOHIDDeviceClose(gamepad->hidDeviceRef, kIOHIDOptionsTypeSeizeDevice);
	}
	reset(gamepad);
}

static void reset(struct HIDGamepad *gamepad) {
	gamepad->padIndex            = -1;
	gamepad->hidDeviceRef        = NULL;
	gamepad->hidQueueRef         = NULL;
	gamepad->hidDeviceVendor[0]  = '\0';
	gamepad->hidDeviceProduct[0] = '\0';
	gamepad->hidDeviceVendorID   = 0;
	gamepad->hidDeviceProductID  = 0;
	memset(gamepad->axis, 0, sizeof(gamepad->axis));
	memset(gamepad->buttons, 0, sizeof(gamepad->buttons));
}

static void buttonChanged(struct HIDGamepad *gamepad, IOHIDElementRef elementRef, IOHIDValueRef valueRef, int buttonIndex) {
	double rawValue  = IOHIDValueGetScaledValue(valueRef, kIOHIDValueScaleTypePhysical);
	double min       = IOHIDElementGetLogicalMin(elementRef);
	double max       = IOHIDElementGetLogicalMax(elementRef);
	double normalize = (rawValue - min) / (max - min);
	iron_internal_gamepad_trigger_button(gamepad->padIndex, buttonIndex, normalize);
}

static void axisChanged(struct HIDGamepad *gamepad, IOHIDElementRef elementRef, IOHIDValueRef valueRef, int axisIndex) {
	double rawValue  = IOHIDValueGetScaledValue(valueRef, kIOHIDValueScaleTypePhysical);
	double min       = IOHIDElementGetPhysicalMin(elementRef);
	double max       = IOHIDElementGetPhysicalMax(elementRef);
	double normalize = normalize = (((rawValue - min) / (max - min)) * 2) - 1;
	if (axisIndex % 2 == 1) {
		normalize = -normalize;
	}
	iron_internal_gamepad_trigger_axis(gamepad->padIndex, axisIndex, normalize);
}

static void inputValueCallback(void *inContext, IOReturn inResult, void *inSender, IOHIDValueRef inIOHIDValueRef) {}

static void valueAvailableCallback(void *inContext, IOReturn inResult, void *inSender) {
	struct HIDGamepad *pad = (struct HIDGamepad *)inContext;
	do {
		IOHIDValueRef valueRef = IOHIDQueueCopyNextValueWithTimeout((IOHIDQueueRef)inSender, 0.);
		if (!valueRef) {
			break;
		}

		IOHIDElementRef    elementRef = IOHIDValueGetElement(valueRef);
		IOHIDElementCookie cookie     = IOHIDElementGetCookie(elementRef);

		for (int i = 0, c = sizeof(pad->buttons); i < c; ++i) {
			if (cookie == pad->buttons[i]) {
				buttonChanged(pad, elementRef, valueRef, i);
				break;
			}
		}

		for (int i = 0, c = sizeof(pad->axis); i < c; ++i) {
			if (cookie == pad->axis[i]) {
				axisChanged(pad, elementRef, valueRef, i);
				break;
			}
		}

		CFRelease(valueRef);
	} while (1);
}

const char *iron_gamepad_vendor(int gamepad) {
	return "unknown";
}

const char *iron_gamepad_product_name(int gamepad) {
	return "unknown";
}

static int                    initHIDManager(struct HIDManager *manager);
static bool                   addMatchingArray(struct HIDManager *manager, CFMutableArrayRef matchingCFArrayRef, CFDictionaryRef matchingCFDictRef);
static CFMutableDictionaryRef createDeviceMatchingDictionary(struct HIDManager *manager, uint32_t inUsagePage, uint32_t inUsage);
static void                   deviceConnected(void *inContext, IOReturn inResult, void *inSender, IOHIDDeviceRef inIOHIDDeviceRef);
static void                   deviceRemoved(void *inContext, IOReturn inResult, void *inSender, IOHIDDeviceRef inIOHIDDeviceRef);

void HIDManager_init(struct HIDManager *manager) {
	manager->managerRef = 0x0;
	initHIDManager(manager);
}

void HIDManager_destroy(struct HIDManager *manager) {
	if (manager->managerRef) {
		IOHIDManagerUnscheduleFromRunLoop(manager->managerRef, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
		IOHIDManagerClose(manager->managerRef, kIOHIDOptionsTypeNone);
	}
}

static int initHIDManager(struct HIDManager *manager) {
	manager->managerRef = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
	if (CFGetTypeID(manager->managerRef) == IOHIDManagerGetTypeID()) {

		CFMutableArrayRef matchingCFArrayRef = CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks);
		if (matchingCFArrayRef) {
			CFDictionaryRef matchingCFDictRef = createDeviceMatchingDictionary(manager, kHIDPage_GenericDesktop, kHIDUsage_GD_Joystick);
			addMatchingArray(manager, matchingCFArrayRef, matchingCFDictRef);
			matchingCFDictRef = createDeviceMatchingDictionary(manager, kHIDPage_GenericDesktop, kHIDUsage_GD_GamePad);
			addMatchingArray(manager, matchingCFArrayRef, matchingCFDictRef);
		}
		else {
			iron_error("%s: CFArrayCreateMutable failed.", __PRETTY_FUNCTION__);
			return -1;
		}

		IOHIDManagerSetDeviceMatchingMultiple(manager->managerRef, matchingCFArrayRef);
		CFRelease(matchingCFArrayRef);
		IOHIDManagerOpen(manager->managerRef, kIOHIDOptionsTypeNone);
		IOHIDManagerRegisterDeviceMatchingCallback(manager->managerRef, deviceConnected, manager);
		IOHIDManagerRegisterDeviceRemovalCallback(manager->managerRef, deviceRemoved, manager);
		IOHIDManagerScheduleWithRunLoop(manager->managerRef, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
		return 0;
	}
	return -1;
}

bool addMatchingArray(struct HIDManager *manager, CFMutableArrayRef matchingCFArrayRef, CFDictionaryRef matchingCFDictRef) {
	if (matchingCFDictRef) {
		CFArrayAppendValue(matchingCFArrayRef, matchingCFDictRef);
		CFRelease(matchingCFDictRef);
		return true;
	}
	return false;
}

CFMutableDictionaryRef createDeviceMatchingDictionary(struct HIDManager *manager, uint32_t inUsagePage, uint32_t inUsage) {
	CFMutableDictionaryRef result = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	if (result) {
		if (inUsagePage) {
			CFNumberRef pageCFNumberRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &inUsagePage);
			if (pageCFNumberRef) {
				CFDictionarySetValue(result, CFSTR(kIOHIDDeviceUsagePageKey), pageCFNumberRef);
				CFRelease(pageCFNumberRef);
				if (inUsage) {
					CFNumberRef usageCFNumberRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &inUsage);
					if (usageCFNumberRef) {
						CFDictionarySetValue(result, CFSTR(kIOHIDDeviceUsageKey), usageCFNumberRef);
						CFRelease(usageCFNumberRef);
					}
				}
			}
		}
	}
	return result;
}

void deviceConnected(void *inContext, IOReturn inResult, void *inSender, IOHIDDeviceRef inIOHIDDeviceRef) {
	struct HIDManager             *manager = (struct HIDManager *)inContext;
	struct HIDManagerDeviceRecord *device  = &manager->devices[0];
	for (int i = 0; i < IRON_MAX_HID_DEVICES; ++i, ++device) {
		if (!device->connected) {
			device->connected = true;
			device->device    = inIOHIDDeviceRef;
			HIDGamepad_bind(&device->pad, inIOHIDDeviceRef, i);
			break;
		}
	}
}

void deviceRemoved(void *inContext, IOReturn inResult, void *inSender, IOHIDDeviceRef inIOHIDDeviceRef) {
	struct HIDManager             *manager = (struct HIDManager *)inContext;
	struct HIDManagerDeviceRecord *device  = &manager->devices[0];
	for (int i = 0; i < IRON_MAX_HID_DEVICES; ++i, ++device) {
		if (device->connected && device->device == inIOHIDDeviceRef) {
			device->connected = false;
			device->device    = NULL;
			HIDGamepad_unbind(&device->pad);
			break;
		}
	}
}

#endif

extern void (*iron_save_and_quit)(bool);

bool _save_and_quit_callback_internal() {
	bool      save  = false;
	NSString *title = [window title];
	bool      dirty = [title rangeOfString:@"* - ArmorPaint"].location != NSNotFound;
	if (dirty) {
		NSAlert *alert = [[NSAlert alloc] init];
		[alert setMessageText:@"Project has been modified, save changes?"];
		[alert addButtonWithTitle:@"Yes"];
		[alert addButtonWithTitle:@"Cancel"];
		[alert addButtonWithTitle:@"No"];
		[alert setAlertStyle:NSAlertStyleWarning];
		NSInteger res = [alert runModal];
		if (res == NSAlertFirstButtonReturn) {
			save = true;
		}
		else if (res == NSAlertThirdButtonReturn) {
			save = false;
		}
		else { // Cancel
			return false;
		}
	}
	iron_save_and_quit(save);
	return false;
}

volatile int iron_exec_async_done        = 1;
char        *iron_exec_async_output_file = NULL;

void iron_exec_async(const char *path, char *argv[]) {
	iron_exec_async_done = 0;
	NSTask         *task   = [[NSTask alloc] init];
	NSString       *launch = [NSString stringWithUTF8String:path];
	NSMutableArray *args   = [NSMutableArray array];
	if (![launch containsString:@"/"]) {
		[args addObject:launch];
		launch                    = @"/usr/bin/env";
		NSMutableDictionary *env  = [NSMutableDictionary dictionaryWithDictionary:[[NSProcessInfo processInfo] environment]];
		NSString            *home = NSHomeDirectory();
		env[@"PATH"]              = [NSString stringWithFormat:@"%@/.local/bin:%@/bin:/usr/local/bin:%@", home, home, env[@"PATH"]];
		[task setEnvironment:env];
	}
	[task setLaunchPath:launch];
	int i = 1;
	while (argv[i] != NULL) {
		[args addObject:[NSString stringWithUTF8String:argv[i]]];
		i++;
	}
	[task setArguments:args];
	if (iron_exec_async_output_file != NULL) {
		NSString *output_path = [NSString stringWithUTF8String:iron_exec_async_output_file];
		[[NSFileManager defaultManager] createFileAtPath:output_path contents:nil attributes:nil];
		NSFileHandle *file_handle = [NSFileHandle fileHandleForWritingAtPath:output_path];
		[task setStandardOutput:file_handle];
	}
	task.terminationHandler = ^(NSTask *t) {
		iron_exec_async_done = 1;
	};
	[task launch];
}
