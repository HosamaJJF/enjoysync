#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace enjoysync {
// Install after the native peer is created, before any tray WndProc hook.
#if JUCE_WINDOWS
void allowSystemShutdown(juce::Component&);
#else
inline void allowSystemShutdown(juce::Component&) {}
#endif
}
