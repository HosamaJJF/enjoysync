#include "MainView.h"
#include <aoo.h>
namespace enjoysync {
class EnjoySyncApplication final : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "EnjoySync"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return false; }
    void initialise(const juce::String& commandLine) override {
        aoo_initialize(nullptr);
        auto directory = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
           #if JUCE_MAC
            .getChildFile("Application Support/EnjoySync");
           #else
            .getChildFile("EnjoySync");
           #endif
        auto arguments = juce::StringArray::fromTokens(commandLine, true);
        auto index = arguments.indexOf("--profile-dir");
        if (index >= 0 && index + 1 < arguments.size()) directory = juce::File(arguments[index + 1].unquoted());
        controller = std::make_unique<Controller>(directory);
        window = std::make_unique<Window>(*this, *controller);
        tray = std::make_unique<Tray>(*this);
        show();
    }
    void shutdown() override { tray.reset(); window.reset(); controller.reset(); aoo_terminate(); }
    void systemRequestedQuit() override { quit(); }
    void anotherInstanceStarted(const juce::String&) override { show(); }
    void show() {
        setDockVisible(true); window->setMinimised(false); window->setVisible(true); window->toFront(true);
        if (auto* view = dynamic_cast<MainView*>(window->getContentComponent())) view->refresh();
    }
    void hide() { window->setVisible(false); if (controller->settings.hideDock) setDockVisible(false); }
private:
    class Window final : public juce::DocumentWindow {
    public:
        Window(EnjoySyncApplication& owner_, Controller& c)
            : DocumentWindow("EnjoySync", juce::Colour(0xff10171e), allButtons), owner(owner_) {
            setUsingNativeTitleBar(true); setResizable(true, false); setResizeLimits(1020, 700, 1800, 1400);
            setContentOwned(new MainView(c), true); centreWithSize(getWidth(), getHeight());
        }
        void closeButtonPressed() override { owner.hide(); }
        void minimisationStateChanged(bool minimised) override {
           #if JUCE_WINDOWS
            if (minimised) owner.hide();
           #else
            juce::DocumentWindow::minimisationStateChanged(minimised);
           #endif
        }
    private: EnjoySyncApplication& owner;
    };
    class Tray final : public juce::SystemTrayIconComponent {
    public:
        explicit Tray(EnjoySyncApplication& owner_) : owner(owner_) {
            juce::Image icon(juce::Image::ARGB, 64, 64, true); juce::Graphics g(icon);
            g.setColour(juce::Colour(0xff71e7bd));
            for (int i = 0; i < 5; ++i) { float h = i == 2 ? 48.0f : (i == 0 || i == 4 ? 18.0f : 34.0f); g.fillRoundedRectangle(8.0f + i * 10.0f, (64 - h) / 2, 6, h, 3); }
            setIconImage(icon, icon); setIconTooltip(u8"EnjoySync · 系统声音共享"_es);
        }
        void mouseDown(const juce::MouseEvent& event) override {
           #if JUCE_WINDOWS
            if (!event.mods.isPopupMenu()) { owner.show(); return; }
           #endif
            juce::PopupMenu menu;
            menu.addItem(u8"打开 EnjoySync"_es, [this] { owner.show(); }); menu.addSeparator();
            menu.addItem(u8"允许共享系统声音"_es, true, owner.controller->sharingEnabled(), [this] { owner.controller->setSharingEnabled(!owner.controller->sharingEnabled()); });
            menu.addItem(u8"全部播放静音"_es, true, owner.controller->audio.muteAll.load(), [this] { owner.controller->audio.muteAll = !owner.controller->audio.muteAll.load(); });
            juce::PopupMenu devices;
            for (const auto& peer : owner.controller->peers()) {
                auto id = peer.settings.recordId;
                devices.addItem(peer.settings.name.isEmpty() ? peer.settings.host : peer.settings.name, [this, id] { owner.controller->transport->connect(id); });
            }
            menu.addSubMenu(u8"快捷连接"_es, devices, devices.getNumItems() > 0); menu.addSeparator();
            menu.addItem(u8"退出 EnjoySync"_es, [this] { owner.quit(); });
           #if JUCE_MAC
            showDropdownMenu(menu);
           #else
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this));
           #endif
        }
    private: EnjoySyncApplication& owner;
    };
    std::unique_ptr<Controller> controller;
    std::unique_ptr<Window> window;
    std::unique_ptr<Tray> tray;
};
}
START_JUCE_APPLICATION(enjoysync::EnjoySyncApplication)
