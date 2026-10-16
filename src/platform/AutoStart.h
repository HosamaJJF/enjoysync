#pragma once
#include <juce_core/juce_core.h>
#include <functional>

namespace enjoysync {
enum class AutoStartState { disabled, enabled, requiresApproval };
// Read the OS registration instead of keeping a second copy in settings.json.
AutoStartState getAutoStartState();
juce::Result setAutoStartEnabled(bool enabled);
// macOS delivers the login-item Apple event when AppKit finishes launching.
void handleStartupLaunch(const juce::String& commandLine, std::function<void(bool startedAtLogin)>);
}
