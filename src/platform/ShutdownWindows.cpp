#include "SystemShutdown.h"
#include <windows.h>
#include <commctrl.h>

namespace enjoysync {
static LRESULT CALLBACK sessionWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                                          UINT_PTR subclassId, DWORD_PTR) {
    // A query must return immediately. Do not start audio/network teardown here.
    if (message == WM_QUERYENDSESSION) return TRUE;
    if (message == WM_ENDSESSION) {
        if (wParam != FALSE)
            if (auto* app = juce::JUCEApplicationBase::getInstance()) app->systemRequestedQuit();
        return 0;
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(window, sessionWindowProc, subclassId);
    return DefSubclassProc(window, message, wParam, lParam);
}
void allowSystemShutdown(juce::Component& component) {
    auto* peer = component.getPeer();
    jassert(peer != nullptr);
    if (peer != nullptr && !SetWindowSubclass(static_cast<HWND>(peer->getNativeHandle()), sessionWindowProc, 1, 0))
        juce::Logger::writeToLog("Could not install Windows session shutdown handler");
}
}
