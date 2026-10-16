#pragma once
#include <juce_events/juce_events.h>
#include <functional>

namespace enjoysync {
#if JUCE_MAC
// Unlike callAsync(), this does not run inside AppKit's event-tracking loops.
void afterNativeEventTracking(std::function<void()> action);
#endif
}
