#pragma once
#include <juce_core/juce_core.h>

namespace enjoysync {
enum class AutoStartState { disabled, enabled, requiresApproval };
// Read the OS registration instead of keeping a second copy in settings.json.
AutoStartState getAutoStartState();
juce::Result setAutoStartEnabled(bool enabled);
}
