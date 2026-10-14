#include "Capture.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <windows.h>
#include <dwmapi.h>
namespace enjoysync {
void setDockVisible(bool) {}
void setApplicationTheme(const juce::String& mode) {
    auto& desktop = juce::Desktop::getInstance();
    const BOOL dark = mode == "dark" || (mode == "system" && desktop.isDarkModeActive());
    for (int i = 0; i < desktop.getNumComponents(); ++i) {
        if (auto* peer = desktop.getComponent(i)->getPeer()) {
            auto handle = static_cast<HWND>(peer->getNativeHandle());
            DwmSetWindowAttribute(handle, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
            RedrawWindow(handle, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME);
        }
    }
}
void beginAudioActivity() {}
void endAudioActivity() {}
}
