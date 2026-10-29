// iOS lifecycle: UIKit says when the app stops being active (Home button, notification shade, a call, the app
// switcher). SDL's own background events only arrive once the app is suspended, which is too late for the renderer: the
// Metal drawable is already gone. These notifications arrive first, so the renderer stops presenting before that.
#import <UIKit/UIKit.h>

#include "lifecycle.h"

extern "C" void BlackSetForeground(int on);

void IosLifecycleStart() {
    NSNotificationCenter* center = [NSNotificationCenter defaultCenter];
    [center addObserverForName:UIApplicationWillResignActiveNotification
                        object:nil
                         queue:[NSOperationQueue mainQueue]
                    usingBlock:^(NSNotification*) {
                        BlackSetForeground(0);
                    }];
    [center addObserverForName:UIApplicationDidBecomeActiveNotification
                        object:nil
                         queue:[NSOperationQueue mainQueue]
                    usingBlock:^(NSNotification*) {
                        BlackSetForeground(1);
                    }];
}
