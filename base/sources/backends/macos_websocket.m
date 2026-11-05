#import <Foundation/Foundation.h>
#include <iron_websocket.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_SOCKETS  8
#define PING_SECONDS 30
#define MAX_MESSAGE  (64 * 1024 * 1024)

@interface IronWebSocket : NSObject <NSURLSessionWebSocketDelegate>
@end

@implementation IronWebSocket {
@public
	_Atomic int                state;
	NSURLSession              *session;
	NSURLSessionWebSocketTask *task;
	NSMutableArray<NSData *>  *messages;
	char                      *msg;
	time_t                     last_send;
}

- (void)receiveNext {
	__weak IronWebSocket *weak = self;
	[task receiveMessageWithCompletionHandler:^(NSURLSessionWebSocketMessage *message, NSError *error) {
		IronWebSocket *ws = weak;
		if (ws == nil) {
			return;
		}
		if (error != nil || message == nil) {
			ws->state = IRON_WEBSOCKET_CLOSED;
			return;
		}
		NSData *data = message.type == NSURLSessionWebSocketMessageTypeString ? [message.string dataUsingEncoding:NSUTF8StringEncoding] : message.data;
		@synchronized(ws->messages) {
			[ws->messages addObject:data != nil ? data : [NSData data]];
		}
		[ws receiveNext];
	}];
}

- (void)URLSession:(NSURLSession *)session webSocketTask:(NSURLSessionWebSocketTask *)webSocketTask didOpenWithProtocol:(NSString *)protocol {
	int connecting = IRON_WEBSOCKET_CONNECTING;
	atomic_compare_exchange_strong(&state, &connecting, IRON_WEBSOCKET_OPEN);
}

- (void)URLSession:(NSURLSession *)session
       webSocketTask:(NSURLSessionWebSocketTask *)webSocketTask
    didCloseWithCode:(NSURLSessionWebSocketCloseCode)closeCode
              reason:(NSData *)reason {
	state = IRON_WEBSOCKET_CLOSED;
}

- (void)URLSession:(NSURLSession *)session task:(NSURLSessionTask *)task didCompleteWithError:(NSError *)error {
	state = IRON_WEBSOCKET_CLOSED;
}

- (void)dealloc {
	free(msg);
}

@end

static IronWebSocket *sockets[MAX_SOCKETS];

i32 iron_websocket_open(char *url) {
	if (strncmp(url, "wss://", 6) != 0 && strncmp(url, "ws://", 5) != 0) {
		return -1;
	}
	int slot = 0;
	while (slot < MAX_SOCKETS && sockets[slot] != nil) {
		slot++;
	}
	if (slot == MAX_SOCKETS) {
		return -1;
	}
	NSURL *nsurl = [NSURL URLWithString:[NSString stringWithUTF8String:url]];
	if (nsurl == nil) {
		return -1;
	}
	IronWebSocket *ws                 = [[IronWebSocket alloc] init];
	ws->state                         = IRON_WEBSOCKET_CONNECTING;
	ws->messages                      = [NSMutableArray array];
	ws->last_send                     = time(NULL);
	NSOperationQueue *queue           = [[NSOperationQueue alloc] init];
	queue.maxConcurrentOperationCount = 1;
	ws->session = [NSURLSession sessionWithConfiguration:[NSURLSessionConfiguration defaultSessionConfiguration] delegate:ws delegateQueue:queue];
	ws->task    = [ws->session webSocketTaskWithURL:nsurl];
	ws->task.maximumMessageSize = MAX_MESSAGE;
	[ws->task resume];
	[ws receiveNext];
	sockets[slot] = ws;
	return slot;
}

static IronWebSocket *websocket_get(i32 handle) {
	return handle >= 0 && handle < MAX_SOCKETS ? sockets[handle] : nil;
}

void iron_websocket_send(i32 handle, char *text) {
	IronWebSocket *ws = websocket_get(handle);
	if (ws == nil || text == NULL || ws->state != IRON_WEBSOCKET_OPEN) {
		return;
	}
	NSString *s = [NSString stringWithUTF8String:text];
	if (s == nil) {
		return;
	}
	__weak IronWebSocket *weak = ws;
	[ws->task sendMessage:[[NSURLSessionWebSocketMessage alloc] initWithString:s]
	    completionHandler:^(NSError *error) {
		    IronWebSocket *w = weak;
		    if (error != nil && w != nil) {
			    w->state = IRON_WEBSOCKET_CLOSED;
		    }
	    }];
	ws->last_send = time(NULL);
}

char *iron_websocket_receive(i32 handle) {
	IronWebSocket *ws = websocket_get(handle);
	if (ws == nil || ws->state != IRON_WEBSOCKET_OPEN) {
		return NULL;
	}
	if (time(NULL) - ws->last_send >= PING_SECONDS) {
		[ws->task sendPingWithPongReceiveHandler:^(NSError *error){
		}];
		ws->last_send = time(NULL);
	}
	NSData *data = nil;
	@synchronized(ws->messages) {
		if (ws->messages.count > 0) {
			data = ws->messages[0];
			[ws->messages removeObjectAtIndex:0];
		}
	}
	if (data == nil) {
		return NULL;
	}
	free(ws->msg);
	ws->msg = malloc(data.length + 1);
	memcpy(ws->msg, data.bytes, data.length);
	ws->msg[data.length] = '\0';
	return ws->msg;
}

i32 iron_websocket_state(i32 handle) {
	IronWebSocket *ws = websocket_get(handle);
	return ws != nil ? ws->state : IRON_WEBSOCKET_CLOSED;
}

void iron_websocket_close(i32 handle) {
	IronWebSocket *ws = websocket_get(handle);
	if (ws == nil) {
		return;
	}
	sockets[handle] = nil;
	ws->state       = IRON_WEBSOCKET_CLOSED;
	[ws->task cancelWithCloseCode:NSURLSessionWebSocketCloseCodeNormalClosure reason:nil];
	[ws->session invalidateAndCancel];
}
