#include "Capture.h"
#include "AutoStart.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <windows.h>
#include <dwmapi.h>
#include <cwchar>
namespace enjoysync {
static const juce::String autoStartValue {"HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run\\EnjoySync"};
AutoStartState getAutoStartState() {
    return juce::WindowsRegistry::getValue(autoStartValue).isNotEmpty() ? AutoStartState::enabled : AutoStartState::disabled;
}
juce::Result setAutoStartEnabled(bool enabled) {
    if (enabled) {
        // Quote the executable path so spaces and non-ASCII installation paths work.
        const auto command = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName().quoted();
        if (std::wcslen(command.toWideCharPointer()) > 260)
            return juce::Result::fail(u8"应用路径过长，无法启用开机自启动，请将 EnjoySync 移至较短的路径"_es);
        if (!juce::WindowsRegistry::setValue(autoStartValue, command))
            return juce::Result::fail(u8"无法启用开机自启动，请检查当前账户的启动项权限"_es);
    } else if (juce::WindowsRegistry::valueExists(autoStartValue) && !juce::WindowsRegistry::deleteValue(autoStartValue)) {
        return juce::Result::fail(u8"无法关闭开机自启动，请检查当前账户的启动项权限"_es);
    }
    return juce::Result::ok();
}
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
