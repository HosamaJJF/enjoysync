#pragma once
#include "Controller.h"
#include <juce_gui_extra/juce_gui_extra.h>
namespace enjoysync {
class PeerRow;
class MainView final : public juce::Component {
public:
    explicit MainView(Controller&);
    ~MainView() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void refresh();
private:
    void field(juce::TextEditor&, const juce::String& value, const juce::String& placeholder, bool numeric = false);
    void report(const juce::Result&);
    Controller& controller;
    juce::Label title, subtitle, localCaption, keyCaption, listener, statusLabel, peerCaption, feedback;
    juce::Label nameCaption, portCaption, bufferCaption, outputCaption;
    juce::TextEditor name, port, buffer, host, remotePort, remoteKey, localKey;
    juce::ComboBox codec, output;
    juce::ToggleButton hideDock {u8"后台隐藏 Dock"_es}, automatic {u8"启动后自动连接"_es};
    juce::TextButton apply {u8"应用设置"_es}, connectButton {u8"添加并连接"_es}, copyKey {u8"复制密钥"_es};
    juce::ToggleButton share {u8"允许共享系统声音"_es}, mute {u8"全部播放静音"_es};
    juce::Viewport viewport;
    juce::Component rowsContainer;
    std::vector<std::unique_ptr<PeerRow>> rows;
    juce::TooltipWindow tooltips {this, 600};
};
}
