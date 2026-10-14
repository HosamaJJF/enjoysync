#include "Capture.h"
#include "AutoStart.h"
#include "core/Settings.h"
#import <AppKit/AppKit.h>
#import <ServiceManagement/ServiceManagement.h>
namespace enjoysync {
static id activity;
AutoStartState getAutoStartState() {
    switch (SMAppService.mainAppService.status) {
        case SMAppServiceStatusEnabled: return AutoStartState::enabled;
        case SMAppServiceStatusRequiresApproval: return AutoStartState::requiresApproval;
        default: return AutoStartState::disabled;
    }
}
juce::Result setAutoStartEnabled(bool enabled) {
    auto* service = SMAppService.mainAppService;
    const auto state = getAutoStartState();
    if (enabled ? state != AutoStartState::disabled : state == AutoStartState::disabled)
        return juce::Result::ok();
    NSError* error = nil;
    const BOOL success = enabled ? [service registerAndReturnError:&error] : [service unregisterAndReturnError:&error];
    if (!success) {
        if (enabled && getAutoStartState() == AutoStartState::requiresApproval) return juce::Result::ok();
        const auto detail = error ? juce::String::fromUTF8(error.localizedDescription.UTF8String) : juce::String();
        return juce::Result::fail(u8"无法更改开机自启动："_es + detail);
    }
    return juce::Result::ok();
}
void setDockVisible(bool visible) {
    [NSApp setActivationPolicy:visible ? NSApplicationActivationPolicyRegular : NSApplicationActivationPolicyAccessory];
    if (visible) [NSApp activateIgnoringOtherApps:YES];
}
void setApplicationTheme(const juce::String& mode) {
    NSApp.appearance = mode == "system" ? nil : [NSAppearance appearanceNamed:mode == "dark" ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua];
}
void beginAudioActivity() {
    if (!activity) activity = [NSProcessInfo.processInfo beginActivityWithOptions:NSActivityUserInitiatedAllowingIdleSystemSleep
                                                                        reason:@"Streaming system audio"];
}
void endAudioActivity() { if (activity) { [NSProcessInfo.processInfo endActivity:activity]; activity = nil; } }
}
