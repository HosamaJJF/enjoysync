#include "platform/SystemShutdown.h"
#include "app/TrayIcon.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <iostream>
#include <stdexcept>
#if JUCE_WINDOWS
 #include <windows.h>
#endif
#if JUCE_MAC
void nativeEventTrackingTests();
#endif

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
static void trayIconTests() {
    const auto icon = enjoysync::createTrayIcon();
    check(icon.getPixelAt(0, 0).getAlpha() == 0, "tray background is not transparent");
    check(icon.getPixelAt(31, 32) == juce::Colour(0xff71e7bd), "tray waveform was not rendered before use");
    for (int size : {16, 20, 24, 32, 64}) {
        const auto scaled = icon.rescaled(size, size, juce::Graphics::highResamplingQuality);
        int coloured = 0;
        for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x) {
            const auto pixel = scaled.getPixelAt(x, y);
            if (pixel.getAlpha() > 128 && pixel.getGreen() > 150 && pixel.getRed() > 60) ++coloured;
        }
        check(coloured > size * size / 16, "scaled tray icon is blank or black");
        check(scaled.getPixelAt(0, 0).getAlpha() == 0, "scaled tray icon lost transparency");
       #if JUCE_WINDOWS
        // ComponentPeer::setIcon uses the same JUCE HICON converter as the tray.
        // Read the actual native bitmap back to catch alpha/renderer regressions.
        juce::Component component; component.addToDesktop(0); component.getPeer()->setIcon(scaled);
        const auto window = static_cast<HWND>(component.getPeer()->getNativeHandle());
        const auto nativeIcon = reinterpret_cast<HICON>(SendMessageW(window, WM_GETICON, ICON_BIG, 0));
        ICONINFO info {};
        check(nativeIcon != nullptr && GetIconInfo(nativeIcon, &info), "native tray icon conversion failed");
        BITMAPINFO bitmap {};
        bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmap.bmiHeader.biWidth = size; bitmap.bmiHeader.biHeight = -size;
        bitmap.bmiHeader.biPlanes = 1; bitmap.bmiHeader.biBitCount = 32; bitmap.bmiHeader.biCompression = BI_RGB;
        std::vector<uint32_t> pixels(static_cast<size_t>(size * size));
        const auto dc = GetDC(nullptr);
        const int rows = GetDIBits(dc, info.hbmColor, 0, size, pixels.data(), &bitmap, DIB_RGB_COLORS);
        ReleaseDC(nullptr, dc);
        DeleteObject(info.hbmColor); DeleteObject(info.hbmMask);
        check(rows == size, "native tray bitmap could not be read");
        check((pixels.front() >> 24) == 0, "native tray icon lost transparent background");
        int nativeColoured = 0;
        for (const auto pixel : pixels)
            if ((pixel >> 24) > 128 && ((pixel >> 8) & 0xff) > 150 && ((pixel >> 16) & 0xff) > 60) ++nativeColoured;
        check(nativeColoured > size * size / 16, "native tray bitmap is blank or black");
       #endif
    }
    std::cout << "PASS tray icon pixels, transparency and small/high-DPI sizes\n";
}
#if JUCE_WINDOWS
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
#endif
int main() {
    juce::JUCEApplicationBase::createInstance = []() -> juce::JUCEApplicationBase* { return new TestApplication; };
    juce::ScopedJuceInitialiser_GUI runtime;
    TestApplication app;
    try {
        trayIconTests();
       #if JUCE_MAC
        nativeEventTrackingTests();
       #endif
       #if JUCE_WINDOWS
        shutdownTests(app);
       #endif
    }
    catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
    return 0;
}
