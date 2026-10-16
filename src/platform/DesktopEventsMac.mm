#include "DesktopEvents.h"
#import <CoreFoundation/CoreFoundation.h>

namespace enjoysync {
void afterNativeEventTracking(std::function<void()> action) {
    const auto loop = CFRunLoopGetMain();
    CFRunLoopPerformBlock(loop, kCFRunLoopDefaultMode, ^{ action(); });
    CFRunLoopWakeUp(loop);
}
}
