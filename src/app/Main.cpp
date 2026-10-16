#include "MainView.h"
#include "TrayIcon.h"
#include "platform/AutoStart.h"
#include "platform/SystemShutdown.h"
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
        networkLog = std::make_unique<juce::FileLogger>(directory.getChildFile("logs/transport.log"),
            "EnjoySync " + getApplicationVersion() + " " + juce::SystemStats::getOperatingSystemName(), 512 * 1024);
        juce::Logger::setCurrentLogger(networkLog.get());
        controller = std::make_unique<Controller>(directory);
        appearance = std::make_unique<Appearance>();
        appearance->setMode(controller->settings.theme);
        juce::LookAndFeel::setDefaultLookAndFeel(appearance.get());
        window = std::make_unique<Window>(*this, *controller, *appearance);
        tray = std::make_unique<Tray>(*this);
        handleStartupLaunch(commandLine, [this](bool startedAtLogin) {
            if (startedAtLogin && controller->settings.hideOnAutoStart && getAutoStartState() == AutoStartState::enabled) hide();
            else show();
        });
    }
    void shutdown() override {
        tray.reset(); window.reset(); controller.reset();
        juce::Logger::setCurrentLogger(nullptr); networkLog.reset();
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr); appearance.reset(); aoo_terminate();
    }
    void systemRequestedQuit() override {
        // Windows may terminate us as soon as WM_ENDSESSION returns.
        if (controller) controller->save();
        quit();
    }
    void anotherInstanceStarted(const juce::String&) override { show(); }
    void show() {
        setDockVisible(true); window->setMinimised(false); window->setVisible(true); window->toFront(true);
        if (auto* view = dynamic_cast<MainView*>(window->getContentComponent())) view->refresh();
    }
    void showSettings() {
        setDockVisible(true);
        if (auto* view = dynamic_cast<MainView*>(window->getContentComponent())) view->showSettings();
    }
    void hide() {
        if (auto* view = dynamic_cast<MainView*>(window->getContentComponent())) view->hideSettings();
        window->setVisible(false); if (controller->settings.hideDock) setDockVisible(false);
    }
private:
    class Window final : public juce::DocumentWindow {
    public:
        Window(EnjoySyncApplication& owner_, Controller& c, Appearance& appearance)
            : DocumentWindow("EnjoySync", appearance.colours().background, allButtons), owner(owner_) {
            setUsingNativeTitleBar(true); setResizable(true, false); setResizeLimits(820, 470, 1800, 1400);
            setContentOwned(new MainView(c, appearance), true); centreWithSize(getWidth(), getHeight());
            setApplicationTheme(c.settings.theme);
            allowSystemShutdown(*this);
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
    class Tray final : public juce::SystemTrayIconComponent, private juce::MenuBarModel {
    public:
        explicit Tray(EnjoySyncApplication& owner_) : owner(owner_) {
            allowSystemShutdown(*this);
            const auto icon = createTrayIcon();
            setIconImage(icon, icon); setIconTooltip("EnjoySync");
           #if JUCE_MAC
            // JUCE's native status menu requires a live Mac main-menu model.
            applicationMenu.addItem(u8"设置…"_es, [this] { owner.showSettings(); });
            applicationMenu.addSeparator();
            juce::MenuBarModel::setMacMainMenu(this, &applicationMenu);
           #endif
        }
        ~Tray() override {
           #if JUCE_MAC
            juce::MenuBarModel::setMacMainMenu(nullptr);
           #endif
        }
        void mouseDown(const juce::MouseEvent& event) override {
            if (!event.mods.isPopupMenu()) { owner.show(); return; }
            auto menu = createMenu();
           #if JUCE_MAC
            showDropdownMenu(menu);
           #else
            // The hidden Windows tray peer has no on-screen icon bounds.
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this)
                .withMousePosition().withStandardItemHeight(30));
           #endif
        }
    private:
        juce::StringArray getMenuBarNames() override { return {u8"设备"_es}; }
        juce::PopupMenu getMenuForIndex(int, const juce::String&) override { return createMenu(); }
        void menuItemSelected(int, int) override {}
        juce::PopupMenu createMenu() {
            juce::PopupMenu menu;
            menu.addItem(u8"打开 EnjoySync"_es, [this] { owner.show(); });
            menu.addItem(u8"设置…"_es, [this] { owner.showSettings(); }); menu.addSeparator();
            menu.addItem(u8"允许共享系统声音"_es, true, owner.controller->sharingEnabled(), [this] { owner.controller->setSharingEnabled(!owner.controller->sharingEnabled()); });
            menu.addItem(u8"全部播放静音"_es, true, owner.controller->audio.muteAll.load(), [this] { owner.controller->audio.muteAll = !owner.controller->audio.muteAll.load(); });
            juce::PopupMenu devices;
            for (const auto& peer : owner.controller->peers()) {
                auto id = peer.settings.recordId;
                devices.addItem(peer.settings.name.isEmpty() ? peer.settings.host : peer.settings.name, [this, id] { owner.controller->transport->connect(id); });
            }
            menu.addSubMenu(u8"快捷连接"_es, devices, devices.getNumItems() > 0); menu.addSeparator();
            menu.addItem(u8"退出 EnjoySync"_es, [this] { owner.quit(); });
            return menu;
        }
        EnjoySyncApplication& owner;
       #if JUCE_MAC
        juce::PopupMenu applicationMenu;
       #endif
    };
    std::unique_ptr<juce::FileLogger> networkLog;
    std::unique_ptr<Controller> controller;
    std::unique_ptr<Appearance> appearance;
    std::unique_ptr<Window> window;
    std::unique_ptr<Tray> tray;
};
}
START_JUCE_APPLICATION(enjoysync::EnjoySyncApplication)
