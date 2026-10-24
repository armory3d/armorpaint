
#include <IOKit/IOKitLib.h>
#include <IOKit/hid/IOHIDKeys.h>
#include <IOKit/hid/IOHIDManager.h>
#import <MetalKit/MTKView.h>
#include <iron_global.h>

@interface BasicMTKView : MTKView {
@private
	id<MTLDevice>       device;
	id<MTLCommandQueue> commandQueue;
	id<MTLLibrary>      library;
}

- (CAMetalLayer *)metalLayer;
- (id<MTLDevice>)metalDevice;
- (id<MTLCommandQueue>)metalQueue;
- (void)keyDown:(NSEvent *)theEvent;
- (void)keyUp:(NSEvent *)theEvent;
- (void)mouseDown:(NSEvent *)theEvent;
- (void)mouseUp:(NSEvent *)theEvent;
- (void)mouseMoved:(NSEvent *)theEvent;
- (void)mouseDragged:(NSEvent *)theEvent;
- (void)rightMouseDown:(NSEvent *)theEvent;
- (void)rightMouseUp:(NSEvent *)theEvent;
- (void)rightMouseDragged:(NSEvent *)theEvent;
- (void)scrollWheel:(NSEvent *)theEvent;
- (void)magnifyWithEvent:(NSEvent *)theEvent;
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender;
- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender;
- (BOOL)acceptsFirstResponder;
- (BOOL)becomeFirstResponder;
- (BOOL)resignFirstResponder;
- (id)initWithFrame:(NSRect)frameRect;
- (void)resize:(NSSize)size;

@end

#ifdef WITH_GAMEPAD

struct HIDGamepad {
	int            padIndex;
	IOHIDDeviceRef hidDeviceRef;
	IOHIDQueueRef  hidQueueRef;
	int            hidDeviceVendorID;
	int            hidDeviceProductID;
	char           hidDeviceVendor[64];
	char           hidDeviceProduct[64];

	IOHIDElementCookie axis[6];
	IOHIDElementCookie buttons[15];
};

void HIDGamepad_init(struct HIDGamepad *gamepad);
void HIDGamepad_destroy(struct HIDGamepad *gamepad);
void HIDGamepad_bind(struct HIDGamepad *gamepad, IOHIDDeviceRef deviceRef, int padIndex);
void HIDGamepad_unbind(struct HIDGamepad *gamepad);

static const int IRON_MAX_HID_DEVICES = 8;

// Slots to hold details on connected devices
struct HIDManagerDeviceRecord {
	bool              connected; // = false;
	IOHIDDeviceRef    device;    // = NULL;
	struct HIDGamepad pad;
};

struct HIDManager {
	IOHIDManagerRef               managerRef;
	struct HIDManagerDeviceRecord devices[IRON_MAX_HID_DEVICES];
};

void HIDManager_init(struct HIDManager *manager);
void HIDManager_destroy(struct HIDManager *manager);

#endif
