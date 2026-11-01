#include "ios_system.h"
#import <Foundation/Foundation.h>
#ifdef IRON_AUDIO
#import <AVFoundation/AVFoundation.h>
#endif
#import <UIKit/UIKit.h>
#include <iron_gpu.h>
#include <iron_math.h>
#include <iron_system.h>
#include <mach/mach_time.h>
#include <objc/runtime.h>
#include <wchar.h>

#define MAX_TOUCH_COUNT 10

static char              ios_title[1024];
static void             *touches[MAX_TOUCH_COUNT];
static int               backing_width;
static int               backing_height;
static bool              shift_down = false;
static bool              visible;
static MyView           *my_view;
static MyViewController *my_view_controller;
static bool              keyboard_shown = false;
static char              language[3];
static char              sysid[512];
static const char       *video_formats[]                 = {"mp4", NULL};
static void (*resize_callback)(int x, int y, void *data) = NULL;
static void *resize_callback_data                        = NULL;

void iron_internal_call_resize_callback(int width, int height) {
	if (resize_callback != NULL) {
		resize_callback(width, height, resize_callback_data);
	}
}

static int get_touch_index(void *touch) {
	for (int i = 0; i < MAX_TOUCH_COUNT; ++i) {
		if (touches[i] == touch) {
			return i;
		}
	}
	return -1;
}

static int add_touch(void *touch) {
	for (int i = 0; i < MAX_TOUCH_COUNT; ++i) {
		if (touches[i] == NULL) {
			touches[i] = touch;
			return i;
		}
	}
	return -1;
}

static int remove_touch(void *touch) {
	for (int i = 0; i < MAX_TOUCH_COUNT; ++i) {
		if (touches[i] == touch) {
			touches[i] = NULL;
			return i;
		}
	}
	return -1;
}

int iron_window_width() {
	return backing_width;
}

int iron_window_height() {
	return backing_height;
}

CAMetalLayer *get_metal_layer(void) {
	return [my_view metal_layer];
}

id get_metal_device(void) {
	return [my_view metal_device];
}

id get_metal_queue(void) {
	return [my_view metal_queue];
}

void iron_display_init(void) {}

iron_display_mode_t iron_display_current_mode(int display) {
	UIScreen           *screen = [UIScreen mainScreen];
	iron_display_mode_t dm;
	dm.width           = iron_window_width();
	dm.height          = iron_window_height();
	dm.frequency       = (int)screen.maximumFramesPerSecond;
	dm.bits_per_pixel  = 32;
	dm.pixels_per_inch = UI_USER_INTERFACE_IDIOM() == UIUserInterfaceIdiomPad ? 326.0f : 460.0f; // Approx
	return dm;
}

int iron_count_displays(void) {
	return 1;
}

int iron_primary_display(void) {
	return 0;
}

bool iron_mouse_can_lock(void) {
	return false;
}

void iron_mouse_show(void) {}
void iron_mouse_hide(void) {}
void iron_mouse_set_position(int x, int y) {}
void iron_mouse_get_position(int *x, int *y) {}
void iron_mouse_set_cursor(iron_cursor_t cursor_index) {}

bool with_autoreleasepool(bool (*f)(void)) {
	@autoreleasepool {
		return f();
	}
}

const char *iron_get_resource_path(void) {
	return [[[NSBundle mainBundle] resourcePath] cStringUsingEncoding:1];
}

bool iron_internal_handle_messages(void) {
	SInt32 result;
	do {
		result = CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, TRUE);
	} while (result == kCFRunLoopRunHandledSource);
	return true;
}

void iron_set_keep_screen_on(bool on) {}

void iron_keyboard_show(void) {
	keyboard_shown = true;
	[my_view show_keyboard];
}

void iron_keyboard_hide(void) {
	keyboard_shown = false;
	[my_view hide_keyboard];
}

bool iron_keyboard_active(void) {
	return keyboard_shown;
}

void iron_load_url(const char *url) {
	NSURL *nsURL = [NSURL URLWithString:[NSString stringWithUTF8String:url]];
	[[UIApplication sharedApplication] openURL:nsURL options:@{} completionHandler:nil];
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

void iron_init(iron_window_options_t *win) {
	gpu_init(win->depth_bits, true);
}

const char *iron_system_id(void) {
	const char *name     = [[[UIDevice currentDevice] name] UTF8String];
	const char *vendorId = [[[[UIDevice currentDevice] identifierForVendor] UUIDString] UTF8String];
	strcpy(sysid, name);
	strcat(sysid, "-");
	strcat(sysid, vendorId);
	return sysid;
}

const char *iron_internal_save_path(void) {
	NSArray  *paths        = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES);
	NSString *resolvedPath = [paths objectAtIndex:0];
	NSString *appName      = [NSString stringWithUTF8String:iron_application_name()];
	resolvedPath           = [resolvedPath stringByAppendingPathComponent:appName];
	NSFileManager *fileMgr = [[NSFileManager alloc] init];
	NSError       *error;
	[fileMgr createDirectoryAtPath:resolvedPath withIntermediateDirectories:YES attributes:nil error:&error];
	resolvedPath = [resolvedPath stringByAppendingString:@"/"];
	return [resolvedPath cStringUsingEncoding:1];
}

const char **iron_video_formats(void) {
	return video_formats;
}

double iron_frequency(void) {
	mach_timebase_info_data_t info;
	mach_timebase_info(&info);
	return (double)info.denom / (double)info.numer / 1e-9;
}

uint64_t iron_timestamp(void) {
	uint64_t time = mach_absolute_time();
	return time;
}

int main(int argc, char *argv[]) {
	int res = 0;
	@autoreleasepool {
		[IronSceneDelegate description]; // otherwise removed by the linker
		res = UIApplicationMain(argc, argv, nil, nil);
	}
	return res;
}

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
	strcpy(ios_title, title);
}

void iron_window_set_resize_callback(void (*callback)(int x, int y, void *data), void *data) {
	resize_callback      = callback;
	resize_callback_data = data;
}

void iron_window_set_close_callback(bool (*callback)(void *), void *data) {}

iron_window_mode_t iron_window_get_mode() {
	return IRON_WINDOW_MODE_FULLSCREEN;
}

int iron_window_display() {
	return 0;
}

@implementation MyView

+ (Class)layerClass {
	return [CAMetalLayer class];
}

- (void)hoverGesture:(UIHoverGestureRecognizer *)recognizer {
	CGPoint point = [recognizer locationInView:self];
	float   x     = point.x * self.contentScaleFactor;
	float   y     = point.y * self.contentScaleFactor;
	iron_internal_pen_trigger_move(x, y, 0.0); // Pencil hover
}

- (id)initWithFrame:(CGRect)frame {
	self                    = [super initWithFrame:(CGRect)frame];
	self.contentScaleFactor = [UIScreen mainScreen].scale;
	backing_width           = frame.size.width * self.contentScaleFactor;
	backing_height          = frame.size.height * self.contentScaleFactor;

	for (int i = 0; i < MAX_TOUCH_COUNT; ++i) {
		touches[i] = NULL;
	}

	device  = MTLCreateSystemDefaultDevice();
	queue   = [device newCommandQueue];
	library = [device newDefaultLibrary];

	CAMetalLayer *layer   = (CAMetalLayer *)self.layer;
	layer.device          = device;
	layer.pixelFormat     = MTLPixelFormatBGRA8Unorm;
	layer.framebufferOnly = YES;
	layer.opaque          = YES;
	layer.backgroundColor = nil;

	[self addGestureRecognizer:[[UIHoverGestureRecognizer alloc] initWithTarget:self action:@selector(hoverGesture:)]];
	return self;
}

- (void)layoutSubviews {
	backing_width  = self.frame.size.width * self.contentScaleFactor;
	backing_height = self.frame.size.height * self.contentScaleFactor;
	gpu_resize(backing_width, backing_height);
	iron_internal_call_resize_callback(backing_width, backing_height);
}

- (void)touchesBegan:(NSSet *)touches withEvent:(UIEvent *)event {
	for (UITouch *touch in touches) {
		int index = get_touch_index((__bridge void *)touch);
		if (index == -1) {
			index = add_touch((__bridge void *)touch);
		}
		if (index >= 0) {
			CGPoint point = [touch locationInView:self];
			float   x     = point.x * self.contentScaleFactor;
			float   y     = point.y * self.contentScaleFactor;
			if (index == 0) {
				iron_internal_mouse_trigger_press(event.buttonMask == UIEventButtonMaskSecondary ? 1 : 0, x, y);
			}
			iron_internal_surface_trigger_touch_start(index, x, y);
			if (touch.type == UITouchTypePencil) {
				iron_internal_pen_trigger_press(x, y, 0.0);
			}
		}
	}
}

- (void)touchesMoved:(NSSet *)touches withEvent:(UIEvent *)event {
	for (UITouch *touch in touches) {
		int index = get_touch_index((__bridge void *)touch);
		if (index >= 0) {
			CGPoint point = [touch locationInView:self];
			float   x     = point.x * self.contentScaleFactor;
			float   y     = point.y * self.contentScaleFactor;
			if (index == 0) {
				iron_internal_mouse_trigger_move(x, y);
			}
			iron_internal_surface_trigger_move(index, x, y);
			if (touch.type == UITouchTypePencil) {
				if (!(touch.estimatedProperties & UITouchPropertyForce)) {
					// usb-c pencil with no pressure
					iron_internal_pen_trigger_move(x, y, touch.force);
				}
			}
		}
	}
}

- (void)touchesEstimatedPropertiesUpdated:(NSSet<UITouch *> *)touches {
	for (UITouch *touch in touches) {
		if (touch.type == UITouchTypePencil) {
			CGPoint point = [touch locationInView:self];
			float   x     = point.x * self.contentScaleFactor;
			float   y     = point.y * self.contentScaleFactor;
			// pencil with pressure
			iron_internal_pen_trigger_move(x, y, touch.force);
		}
	}
}

- (void)touchesEnded:(NSSet *)touches withEvent:(UIEvent *)event {
	for (UITouch *touch in touches) {
		int index = remove_touch((__bridge void *)touch);
		if (index >= 0) {
			CGPoint point = [touch locationInView:self];
			float   x     = point.x * self.contentScaleFactor;
			float   y     = point.y * self.contentScaleFactor;
			if (index == 0) {
				iron_internal_mouse_trigger_release(event.buttonMask == UIEventButtonMaskSecondary ? 1 : 0, x, y);
			}
			iron_internal_surface_trigger_touch_end(index, x, y);
			if (touch.type == UITouchTypePencil) {
				iron_internal_pen_trigger_release(x, y, 0.0);
			}
		}
	}
}

- (void)touchesCancelled:(NSSet *)touches withEvent:(UIEvent *)event {
	for (UITouch *touch in touches) {
		int index = remove_touch((__bridge void *)touch);
		if (index >= 0) {
			CGPoint point = [touch locationInView:self];
			float   x     = point.x * self.contentScaleFactor;
			float   y     = point.y * self.contentScaleFactor;
			if (index == 0) {
				iron_internal_mouse_trigger_release(event.buttonMask == UIEventButtonMaskSecondary ? 1 : 0, x, y);
			}
			iron_internal_surface_trigger_touch_end(index, x, y);
			if (touch.type == UITouchTypePencil) {
				iron_internal_pen_trigger_release(x, y, 0.0);
			}
		}
	}
}

- (void)show_keyboard {
	[self becomeFirstResponder];
}

- (void)hide_keyboard {
	[self resignFirstResponder];
}

- (BOOL)hasText {
	return YES;
}

- (void)insertText:(NSString *)text {
	if ([text length] == 1) {
		unichar ch = [text characterAtIndex:[text length] - 1];
		if (ch == 8212) {
			ch = '_';
		}
		if (ch == L'\n') {
			iron_internal_keyboard_trigger_key_down(KEY_CODE_RETURN);
			iron_internal_keyboard_trigger_key_up(KEY_CODE_RETURN);
			return;
		}
		if (ch == L'.') {
			iron_internal_keyboard_trigger_key_down(KEY_CODE_PERIOD);
			iron_internal_keyboard_trigger_key_up(KEY_CODE_PERIOD);
		}
		else if (ch == L'%') {
			iron_internal_keyboard_trigger_key_down(KEY_CODE_PERCENT);
			iron_internal_keyboard_trigger_key_up(KEY_CODE_PERCENT);
		}
		else if (ch == L'(') {
			iron_internal_keyboard_trigger_key_down(KEY_CODE_OPEN_PAREN);
			iron_internal_keyboard_trigger_key_up(KEY_CODE_OPEN_PAREN);
		}
		else if (ch == L'&') {
			iron_internal_keyboard_trigger_key_down(KEY_CODE_AMPERSAND);
			iron_internal_keyboard_trigger_key_up(KEY_CODE_AMPERSAND);
		}
		else if (ch == L'$') {
			iron_internal_keyboard_trigger_key_down(KEY_CODE_DOLLAR);
			iron_internal_keyboard_trigger_key_up(KEY_CODE_DOLLAR);
		}
		else if (ch == L'#') {
			iron_internal_keyboard_trigger_key_down(KEY_CODE_HASH);
			iron_internal_keyboard_trigger_key_up(KEY_CODE_HASH);
		}
		else if (ch >= L'a' && ch <= L'z') {
			if (shift_down) {
				iron_internal_keyboard_trigger_key_up(KEY_CODE_SHIFT);
				shift_down = false;
			}
			iron_internal_keyboard_trigger_key_down(ch + KEY_CODE_A - L'a');
			iron_internal_keyboard_trigger_key_up(ch + KEY_CODE_A - L'a');
		}
		else {
			if (!shift_down) {
				iron_internal_keyboard_trigger_key_down(KEY_CODE_SHIFT);
				shift_down = true;
			}
			iron_internal_keyboard_trigger_key_down(ch + KEY_CODE_A - L'A');
			iron_internal_keyboard_trigger_key_up(ch + KEY_CODE_A - L'A');
		}
		iron_internal_keyboard_trigger_key_press(ch);
	}
}

- (void)deleteBackward {
	iron_internal_keyboard_trigger_key_down(KEY_CODE_BACKSPACE);
	iron_internal_keyboard_trigger_key_up(KEY_CODE_BACKSPACE);
}

- (BOOL)canBecomeFirstResponder {
	return YES;
}

- (void)onKeyboardHide:(NSNotification *)notification {
	iron_keyboard_hide();
}

- (CAMetalLayer *)metal_layer {
	return (CAMetalLayer *)self.layer;
}

- (id<MTLDevice>)metal_device {
	return device;
}

- (id<MTLCommandQueue>)metal_queue {
	return queue;
}

@end

@implementation MyViewController

- (void)loadView {
	visible   = true;
	self.view = my_view = [[MyView alloc] initWithFrame:[[UIScreen mainScreen] bounds]];
	[self.view addInteraction:[[UIDropInteraction alloc] initWithDelegate:self]];
	[self setNeedsUpdateOfScreenEdgesDeferringSystemGestures];
}

- (void)setVisible:(BOOL)value {
	visible = value;
}

void importFile(NSURL *url) {
	NSArray  *paths       = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
	NSString *folder_name = [NSString stringWithUTF8String:ios_title];
	NSString *file_path   = [[paths objectAtIndex:0] stringByAppendingPathComponent:folder_name];
	if (![[NSFileManager defaultManager] fileExistsAtPath:file_path]) {
		[[NSFileManager defaultManager] createDirectoryAtPath:file_path withIntermediateDirectories:YES attributes:nil error:nil];
	}
	NSString *suggestedName = url.lastPathComponent;
	file_path               = [file_path stringByAppendingPathComponent:suggestedName];
	[url startAccessingSecurityScopedResource];
	NSFileCoordinator *coordinator = [[NSFileCoordinator alloc] initWithFilePresenter:nil];
	[coordinator coordinateReadingItemAtURL:url
	                                options:NSFileCoordinatorReadingWithoutChanges
	                                  error:nil
	                             byAccessor:^(NSURL *new_url) {
		                             if ([[NSFileManager defaultManager] fileExistsAtPath:file_path]) {
			                             [[NSFileManager defaultManager] removeItemAtPath:file_path error:nil];
		                             }
		                             [[NSFileManager defaultManager] copyItemAtURL:new_url toURL:[NSURL fileURLWithPath:file_path] error:nil];
	                             }];

	[url stopAccessingSecurityScopedResource];
	const char *cpath = [file_path cStringUsingEncoding:NSUTF8StringEncoding];
	iron_internal_drop_files_callback(cpath);
}

- (void)documentPicker:(UIDocumentPickerViewController *)controller didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls {
	for (NSURL *url in urls) {
		importFile(url);
	}
}

- (void)dropInteraction:(UIDropInteraction *)interaction performDrop:(id<UIDropSession>)session {
	CGPoint point = [session locationInView:self.view];
	float   x     = point.x * my_view.contentScaleFactor;
	float   y     = point.y * my_view.contentScaleFactor;
	iron_internal_mouse_trigger_move(x, y);
	iron_internal_surface_trigger_move(0, x, y);
	for (UIDragItem *item in session.items) {
		[item.itemProvider loadInPlaceFileRepresentationForTypeIdentifier:item.itemProvider.registeredTypeIdentifiers[0]
		                                                completionHandler:^(NSURL *_Nullable url, BOOL isInPlace, NSError *_Nullable error) {
			                                                importFile(url);
		                                                }];
	}
}

- (UIDropProposal *)dropInteraction:(UIDropInteraction *)interaction sessionDidUpdate:(id<UIDropSession>)session {
	return [[UIDropProposal alloc] initWithDropOperation:UIDropOperationCopy];
}

- (UIRectEdge)preferredScreenEdgesDeferringSystemGestures {
	return UIRectEdgeAll;
}

@end

@implementation IronSceneDelegate

- (void)mainLoop {
	@autoreleasepool {
		kickstart(0, NULL);
	}
}

- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)connectionOptions {
#ifdef IRON_AUDIO
	AVAudioSession *sessionInstance = [AVAudioSession sharedInstance];
	NSError        *error;
	NSString       *category = AVAudioSessionCategoryAmbient;
	[sessionInstance setCategory:category error:&error];
#endif

	UIWindowScene *windowScene = (UIWindowScene *)scene;
	self.window                = [[UIWindow alloc] initWithWindowScene:windowScene];
	self.window.frame          = windowScene.coordinateSpace.bounds;
	[self.window setBackgroundColor:[UIColor blackColor]];

	my_view_controller                           = [[MyViewController alloc] init];
	my_view_controller.view.multipleTouchEnabled = YES;
	[self.window setRootViewController:my_view_controller];
	[self.window makeKeyAndVisible];
	[my_view_controller setVisible:YES];
	[self performSelectorOnMainThread:@selector(mainLoop) withObject:nil waitUntilDone:NO];
}

- (void)sceneDidDisconnect:(UIScene *)scene {
	iron_internal_shutdown_callback();
}

- (void)sceneDidBecomeActive:(UIScene *)scene {
}

- (void)sceneWillResignActive:(UIScene *)scene {
}

- (void)sceneWillEnterForeground:(UIScene *)scene {
	iron_internal_foreground_callback();
}

- (void)sceneDidEnterBackground:(UIScene *)scene {
	iron_internal_background_callback();
}

@end

#ifdef WITH_GAMEPAD

const char *iron_gamepad_vendor(int gamepad) {
	return "nobody";
}

const char *iron_gamepad_product_name(int gamepad) {
	return "none";
}

bool iron_gamepad_connected(int num) {
	return true;
}

void iron_gamepad_rumble(int gamepad, float left, float right) {}

#endif

bool _save_and_quit_callback_internal() {
	return false;
}

volatile int iron_exec_async_done        = 1;
char        *iron_exec_async_output_file = NULL;

void iron_exec_async(const char *path, char *argv[]) {}
