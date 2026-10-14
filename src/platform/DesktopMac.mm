#include "Capture.h"
#import <AppKit/AppKit.h>
namespace enjoysync {
static id activity;
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
