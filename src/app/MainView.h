#pragma once
#include "Appearance.h"
#include "Controller.h"

namespace enjoysync {
class PeerRow;
class SettingsWindow;
class MainView final : public juce::Component {
public:
    MainView(Controller&, Appearance&);
    ~MainView() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override;
    bool keyPressed(const juce::KeyPress&) override;
    void refresh();
    void showSettings();
    void hideSettings();
private:
    void report(const juce::Result&);
    Controller& controller;
    Appearance& appearance;
    juce::Label localCaption, localName, connectCaption, hostCaption, portCaption, keyCaption, peerCaption, feedback;
    juce::TextEditor host, remotePort, remoteKey;
    juce::ToggleButton automatic {u8"启动后自动连接"_es};
    juce::TextButton connectButton {u8"添加并连接"_es};
    IconButton share, mute, settingsButton;
    juce::Viewport viewport;
    juce::Component rowsContainer;
    std::vector<std::unique_ptr<PeerRow>> rows;
    std::unique_ptr<SettingsWindow> settingsWindow;
    juce::TooltipWindow tooltips {this, 500};
    bool failed = false;
};
}
