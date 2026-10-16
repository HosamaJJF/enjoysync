#include "platform/SystemShutdown.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <windows.h>
#include <iostream>
#include <stdexcept>

static void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
class TestApplication final : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "EnjoySync desktop tests"; }
    const juce::String getApplicationVersion() override { return "0"; }
    void initialise(const juce::String&) override {}
    void shutdown() override {}
    void systemRequestedQuit() override { ++quitRequests; }
    int quitRequests = 0;
};
static void shutdownTests(TestApplication& app) {
    juce::DocumentWindow main("Main", juce::Colours::black, juce::DocumentWindow::allButtons);
    juce::DocumentWindow settings("Settings", juce::Colours::black, juce::DocumentWindow::closeButton);
    juce::SystemTrayIconComponent tray;
    // Match the application: install before JUCE adds its native tray hook.
    enjoysync::allowSystemShutdown(tray);
    juce::Image icon(juce::Image::ARGB, 16, 16, true, juce::SoftwareImageType{});
    icon.setPixelAt(8, 8, juce::Colours::green); tray.setIconImage(icon, icon);
    enjoysync::allowSystemShutdown(main); enjoysync::allowSystemShutdown(settings);
    // Reinstalling must not stack duplicate callbacks.
    enjoysync::allowSystemShutdown(settings);
    for (bool visible : {true, false}) {
        main.setVisible(visible); settings.setVisible(visible);
        for (auto* component : {static_cast<juce::Component*>(&main), static_cast<juce::Component*>(&settings),
                                static_cast<juce::Component*>(&tray)}) {
            const auto window = static_cast<HWND>(component->getPeer()->getNativeHandle());
            for (LPARAM flags : {LPARAM{0}, static_cast<LPARAM>(ENDSESSION_LOGOFF), static_cast<LPARAM>(ENDSESSION_CLOSEAPP)}) {
                const int before = app.quitRequests;
                check(SendMessageW(window, WM_QUERYENDSESSION, 0, flags) == TRUE, "session query blocked shutdown");
                check(app.quitRequests == before, "session query prematurely requested exit");
                check(!juce::MessageManager::getInstance()->hasStopMessageBeenSent(), "session query stopped message dispatch");
                check(SendMessageW(window, WM_ENDSESSION, FALSE, flags) == 0, "canceled shutdown was not handled");
                check(app.quitRequests == before, "canceled shutdown requested exit");
                check(SendMessageW(window, WM_ENDSESSION, TRUE, flags) == 0, "confirmed shutdown was not handled");
                check(app.quitRequests == before + 1, "confirmed shutdown did not request exactly one exit");
            }
        }
    }
    // Exercise native hook removal when JUCE destroys/recreates a peer.
    settings.removeFromDesktop(); settings.addToDesktop(0); enjoysync::allowSystemShutdown(settings);
    const auto window = static_cast<HWND>(settings.getPeer()->getNativeHandle());
    const int before = app.quitRequests;
    check(SendMessageW(window, WM_QUERYENDSESSION, 0, 0) == TRUE, "recreated peer blocked shutdown");
    check(app.quitRequests == before, "recreated peer exited during query");
    std::cout << "PASS Windows shutdown query, cancellation and confirmation for visible/hidden windows and tray\n";
}
int main() {
    juce::JUCEApplicationBase::createInstance = []() -> juce::JUCEApplicationBase* { return new TestApplication; };
    juce::ScopedJuceInitialiser_GUI runtime;
    TestApplication app;
    try { shutdownTests(app); }
    catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
    return 0;
}
