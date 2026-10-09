#import <Foundation/Foundation.h>
#include <iron_net.h>
#import <objc/runtime.h>

volatile uint64_t    iron_net_bytes_downloaded = 0;
static NSURLSession *download_session          = nil;

@interface IronDownloadDelegate : NSObject <NSURLSessionDownloadDelegate>
@end

@implementation IronDownloadDelegate

- (void)URLSession:(NSURLSession *)session
                 downloadTask:(NSURLSessionDownloadTask *)downloadTask
                 didWriteData:(int64_t)bytesWritten
            totalBytesWritten:(int64_t)totalBytesWritten
    totalBytesExpectedToWrite:(int64_t)totalBytesExpectedToWrite {
	iron_net_bytes_downloaded += bytesWritten;
}

- (void)URLSession:(NSURLSession *)session downloadTask:(NSURLSessionDownloadTask *)downloadTask didFinishDownloadingToURL:(NSURL *)location {
	NSString *dstPath = objc_getAssociatedObject(downloadTask, @"dst_path");
	if (dstPath) {
		NSError *error   = nil;
		NSURL   *destURL = [NSURL fileURLWithPath:dstPath];
		[[NSFileManager defaultManager] moveItemAtURL:location toURL:destURL error:&error];
		// todo
		if ([dstPath hasSuffix:@"iris"] || [dstPath hasSuffix:@"llama_metal"]) {
			NSDictionary *attrs      = @{NSFilePosixPermissions : @0755};
			NSError      *chmodError = nil;
			[[NSFileManager defaultManager] setAttributes:attrs ofItemAtPath:dstPath error:&chmodError];
		}
	}
	else {
		NSData        *fileData   = [NSData dataWithContentsOfURL:location options:NSDataReadingMappedIfSafe error:nil];
		NSMutableData *memoryData = objc_getAssociatedObject(downloadTask, @"memoryData");
		[memoryData appendData:fileData];
	}
}

- (void)URLSession:(NSURLSession *)session task:(NSURLSessionTask *)task didCompleteWithError:(NSError *)error {
	NSURLSessionDownloadTask *downloadTask = (NSURLSessionDownloadTask *)task;
	NSValue                  *cbVal        = objc_getAssociatedObject(task, @"callback");
	NSValue                  *dataVal      = objc_getAssociatedObject(task, @"callbackdata");
	NSString                 *dstPath      = objc_getAssociatedObject(task, @"dst_path");
	NSMutableData            *memoryData   = objc_getAssociatedObject(task, @"memoryData");
	iron_https_callback_t     callback     = cbVal ? [cbVal pointerValue] : NULL;
	void                     *callbackdata = dataVal ? [dataVal pointerValue] : NULL;
	int                       status       = error == nil && [task.response isKindOfClass:[NSHTTPURLResponse class]] ? (int)((NSHTTPURLResponse *)task.response).statusCode : 0;
	dispatch_async(dispatch_get_main_queue(), ^{
		if (dstPath) {
			callback(status, NULL, callbackdata);
		}
		else {
			char *buffer = malloc(memoryData.length + 1);
			memcpy(buffer, memoryData.bytes, memoryData.length);
			buffer[memoryData.length] = '\0';
			callback(status, buffer, callbackdata);
		}
	});
}

@end

void iron_net_request(const char *url_base, const char *url_path, const char *data, int port, int method, const char *headers,
                      iron_https_callback_t callback, void *callbackdata, const char *dst_path) {
	if (!download_session) {
		NSURLSessionConfiguration *config = [NSURLSessionConfiguration defaultSessionConfiguration];
		NSOperationQueue          *queue  = [[NSOperationQueue alloc] init];
		queue.qualityOfService            = NSQualityOfServiceUtility;

		static IronDownloadDelegate *delegate = nil;
		static dispatch_once_t       onceToken;
		dispatch_once(&onceToken, ^{
			delegate = [[IronDownloadDelegate alloc] init];
		});
		download_session = [NSURLSession sessionWithConfiguration:config delegate:delegate delegateQueue:queue];
	}

	NSString *urlStr =
	    [NSString stringWithFormat:@"https://%@:%d/%@", [NSString stringWithUTF8String:url_base], port, [NSString stringWithUTF8String:url_path]];
	NSURL               *url     = [NSURL URLWithString:urlStr];
	NSMutableURLRequest *request = [NSMutableURLRequest requestWithURL:url];
	request.HTTPMethod           = (method == IRON_HTTPS_GET) ? @"GET" : @"POST";
	if (data) {
		request.HTTPBody = [NSData dataWithBytes:data length:strlen(data)];
	}
	if (headers) {
		for (NSString *line in [[NSString stringWithUTF8String:headers] componentsSeparatedByString:@"\r\n"]) {
			NSRange colon = [line rangeOfString:@":"];
			if (colon.location != NSNotFound) {
				NSString *value = [[line substringFromIndex:colon.location + 1] stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
				[request setValue:value forHTTPHeaderField:[line substringToIndex:colon.location]];
			}
		}
	}

	NSURLSessionDownloadTask *task = [download_session downloadTaskWithRequest:request];
	objc_setAssociatedObject(task, @"callback", [NSValue valueWithPointer:callback], OBJC_ASSOCIATION_RETAIN_NONATOMIC);
	objc_setAssociatedObject(task, @"callbackdata", [NSValue valueWithPointer:callbackdata], OBJC_ASSOCIATION_RETAIN_NONATOMIC);
	objc_setAssociatedObject(task, @"dst_path", dst_path ? [NSString stringWithUTF8String:dst_path] : nil, OBJC_ASSOCIATION_COPY_NONATOMIC);
	if (!dst_path) {
		objc_setAssociatedObject(task, @"memoryData", [NSMutableData data], OBJC_ASSOCIATION_RETAIN_NONATOMIC);
	}
	[task resume];
}

void iron_net_update() {}
