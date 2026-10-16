#include "platform/DesktopEvents.h"
#include <juce_gui_extra/juce_gui_extra.h>
#import <AppKit/AppKit.h>
#include <iostream>
#include <stdexcept>

void nativeEventTrackingTests() {
    const auto loop = CFRunLoopGetMain();
    const auto trackingMode = (__bridge CFStringRef)NSEventTrackingRunLoopMode;
    // This test does not call NSApplication::run(), which normally registers
    // AppKit tracking as a common mode for sources such as JUCE's message queue.
    CFRunLoopAddCommonMode(loop, trackingMode);
    CFRunLoopSourceContext context {};
    context.perform = [](void*) {};
    const auto source = CFRunLoopSourceCreate(nullptr, 0, &context);
    CFRunLoopAddSource(loop, source, trackingMode);

    bool ordinaryAsyncRan = false, quitRanDuringTracking = false;
    int menuOpened = 0, quitRequested = 0;
    juce::MessageManager::callAsync([&] { ordinaryAsyncRan = true; });
    enjoysync::afterNativeEventTracking([&] {
        ++menuOpened;
        // A selected menu action can itself arrive while NSMenu is tracking.
        enjoysync::afterNativeEventTracking([&] { ++quitRequested; });
        CFRunLoopRunInMode(trackingMode, 0, true);
        quitRanDuringTracking = quitRequested != 0;
    });

    CFRunLoopRunInMode(trackingMode, 0, true);
    const bool ordinaryAsyncRanDuringTracking = ordinaryAsyncRan;
    const bool deferredDuringTracking = menuOpened == 0 && quitRequested == 0;
    const auto started = juce::Time::getMillisecondCounterHiRes();
    for (int i = 0; i < 8 && quitRequested == 0; ++i)
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, true);
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, true);
    CFRunLoopRemoveSource(loop, source, trackingMode);
    CFRelease(source);

    if (!ordinaryAsyncRanDuringTracking) throw std::runtime_error("test did not exercise JUCE callbacks in native tracking mode");
    if (!deferredDuringTracking) throw std::runtime_error("menu opened inside status-button tracking");
    if (quitRanDuringTracking) throw std::runtime_error("quit ran inside native menu tracking");
    if (menuOpened != 1 || quitRequested != 1) throw std::runtime_error("deferred menu/quit did not run exactly once");
    std::cout << "PASS macOS menu/quit defer until native tracking ends, elapsed_ms="
              << juce::Time::getMillisecondCounterHiRes() - started << '\n';
}
