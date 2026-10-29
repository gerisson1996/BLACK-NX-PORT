// iOS: the player's own disc image, fetched over the system's URL session. Nothing of the game ships with the app:
// the link comes from game_url in black.toml, on the player's device.
#import <Foundation/Foundation.h>

#include "download.h"

@interface BlackDownload : NSObject <NSURLSessionDownloadDelegate>
@property(nonatomic, assign) std::atomic<int>* percent;
@property(nonatomic, assign) BOOL finished;
@property(nonatomic, assign) BOOL ok;
@property(nonatomic, strong) NSString* dest;
@end

@implementation BlackDownload

- (void)URLSession:(NSURLSession*)session
              downloadTask:(NSURLSessionDownloadTask*)task
              didWriteData:(int64_t)bytesWritten
         totalBytesWritten:(int64_t)totalBytesWritten
 totalBytesExpectedToWrite:(int64_t)totalBytesExpectedToWrite {
    if (totalBytesExpectedToWrite > 0)
        self.percent->store(int((totalBytesWritten * 100) / totalBytesExpectedToWrite));
}

- (void)URLSession:(NSURLSession*)session
                 downloadTask:(NSURLSessionDownloadTask*)task
                 didFinishDownloadingToURL:(NSURL*)location {
    NSFileManager* fm = [NSFileManager defaultManager];
    [fm removeItemAtPath:self.dest error:nil];
    self.ok = [fm moveItemAtURL:location toURL:[NSURL fileURLWithPath:self.dest] error:nil];
}

- (void)URLSession:(NSURLSession*)session task:(NSURLSessionTask*)task didCompleteWithError:(NSError*)error {
    if (error) self.ok = NO;
    self.finished = YES;
}

@end

bool IosDownloadFile(const std::string& url, const std::string& dest_path, std::atomic<int>& percent, std::string& error) {
    @autoreleasepool {
        NSURL* source = [NSURL URLWithString:[NSString stringWithUTF8String:url.c_str()]];
        if (!source) {
            error = "invalid URL";
            return false;
        }
        BlackDownload* download = [BlackDownload new];
        download.percent = &percent;
        download.dest = [NSString stringWithUTF8String:dest_path.c_str()];
        NSURLSessionConfiguration* config = [NSURLSessionConfiguration defaultSessionConfiguration];
        config.timeoutIntervalForRequest = 60;
        config.timeoutIntervalForResource = 3600;
        NSOperationQueue* queue = [NSOperationQueue new];
        queue.maxConcurrentOperationCount = 1;
        NSURLSession* session = [NSURLSession sessionWithConfiguration:config delegate:download delegateQueue:queue];
        NSURLSessionDownloadTask* task = [session downloadTaskWithURL:source];
        [task resume];
        // the delegate runs on the queue above: this thread only waits, the app's main thread keeps its event loop
        while (!download.finished) [NSThread sleepForTimeInterval:0.1];
        [session finishTasksAndInvalidate];
        if (!download.ok) {
            error = "download failed";
            return false;
        }
        return true;
    }
}
