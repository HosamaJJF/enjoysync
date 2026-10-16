#include "SettingsView.h"
#include "platform/AutoStart.h"
#include "platform/SystemShutdown.h"

namespace enjoysync {
class SettingsView final : public juce::Component {
public:
    SettingsView(Controller& c, Appearance& a) : controller(c), appearance(a), copyKey(a, Icon::copy, u8"复制连接密钥"_es) {
        for (auto* component : std::initializer_list<juce::Component*> {
            &generalCaption, &audioCaption, &connectionCaption, &nameCaption, &themeCaption, &outputCaption,
            &codecCaption, &bufferCaption, &portCaption, &keyCaption, &name, &port, &buffer, &localKey,
            &theme, &codec, &output, &autoStart, &hideOnAutoStart, &hideDock, &copyKey, &apply, &feedback}) addAndMakeVisible(component);
        setLabel(generalCaption, u8"通用"_es, 15, true); setLabel(audioCaption, u8"音频"_es, 15, true);
        setLabel(connectionCaption, u8"连接"_es, 15, true);
        setLabel(nameCaption, u8"设备名称"_es); setLabel(themeCaption, u8"外观"_es);
        setLabel(outputCaption, u8"播放输出设备"_es); setLabel(codecCaption, u8"音频编码"_es);
        setLabel(bufferCaption, u8"最低缓冲 / ms"_es); setLabel(portCaption, u8"监听端口"_es);
        setLabel(keyCaption, u8"本机连接密钥"_es);
        setupEditor(name, c.settings.name, u8"设备名称"_es);
        setupEditor(port, juce::String(c.settings.port), u8"监听端口"_es, true);
        setupEditor(buffer, juce::String(c.settings.bufferMs), u8"最低缓冲 / ms"_es, true);
        setupEditor(localKey, c.settings.key, u8"本机连接密钥"_es);
        localKey.setReadOnly(true); localKey.setPasswordCharacter(0x2022);
        theme.setTitle(u8"外观"_es); theme.addItem(u8"跟随系统"_es, 1); theme.addItem(u8"浅色"_es, 2); theme.addItem(u8"深色"_es, 3);
        theme.setSelectedId(c.settings.theme == "light" ? 2 : c.settings.theme == "dark" ? 3 : 1, juce::dontSendNotification);
        theme.onChange = [this] {
            controller.settings.theme = theme.getSelectedId() == 2 ? "light" : theme.getSelectedId() == 3 ? "dark" : "system";
            appearance.setMode(controller.settings.theme); controller.save();
        };
        codec.setTitle(u8"音频编码"_es); codec.addItem(u8"Opus · 192 kbps"_es, 1); codec.addItem(u8"PCM · 24-bit"_es, 2);
        codec.setSelectedId(c.settings.pcm ? 2 : 1, juce::dontSendNotification);
        output.setTitle(u8"播放输出设备"_es); output.addItem(u8"系统默认输出"_es, 1);
        int id = 2;
        for (const auto& device : c.audio.outputDevices()) {
            output.addItem(device, id);
            if (device == c.settings.outputDevice) output.setSelectedId(id, juce::dontSendNotification);
            ++id;
        }
        // Preserve a temporarily disconnected output until the user chooses a replacement.
        if (output.getSelectedId() == 0 && c.settings.outputDevice.isNotEmpty()) {
            output.addItem(c.settings.outputDevice, id); output.setSelectedId(id, juce::dontSendNotification);
        }
        if (output.getSelectedId() == 0) output.setSelectedId(1, juce::dontSendNotification);
        autoStart.onClick = [this] {
            const auto result = setAutoStartEnabled(autoStart.getToggleState());
            const auto state = refreshAutoStart();
            report(result, state == AutoStartState::requiresApproval ? autoStartApprovalMessage()
                : state == AutoStartState::enabled ? u8"已开启开机自启动"_es : u8"已关闭开机自启动"_es);
        };
        refreshAutoStart();
        hideOnAutoStart.setToggleState(c.settings.hideOnAutoStart, juce::dontSendNotification);
        hideOnAutoStart.setTooltip(u8"仅在已启用开机自启动且由系统登录启动时隐藏界面，切换后立即保存"_es);
        hideOnAutoStart.onClick = [this] {
            // Refresh older Windows Run entries so they also pass the login-launch marker.
            auto result = getAutoStartState() == AutoStartState::enabled ? setAutoStartEnabled(true)
                : juce::Result::fail(u8"请先启用开机自启动"_es);
            if (result.wasOk()) {
                controller.settings.hideOnAutoStart = hideOnAutoStart.getToggleState(); controller.save();
            } else hideOnAutoStart.setToggleState(controller.settings.hideOnAutoStart, juce::dontSendNotification);
            refreshAutoStart();
            report(result, u8"已保存"_es);
        };
        hideDock.setToggleState(c.settings.hideDock, juce::dontSendNotification);
       #if !JUCE_MAC
        hideDock.setVisible(false);
       #endif
        copyKey.onClick = [this] {
            juce::SystemClipboard::copyTextToClipboard(controller.settings.key);
            report(juce::Result::ok(), u8"连接密钥已复制"_es);
        };
        apply.onClick = [this] {
            const auto result = controller.applyPreferences(name.getText(), port.getText().getIntValue(), buffer.getText().getIntValue(),
                codec.getSelectedId() == 2, hideDock.getToggleState(), output.getSelectedId() == 1 ? juce::String() : output.getText());
            port.setText(juce::String(controller.settings.port), false);
            if (result.wasOk()) name.setText(controller.settings.name, false);
            report(result, u8"已保存"_es);
            if (controller.changed) controller.changed();
        };
        lookAndFeelChanged(); setSize(580, 736);
    }
    AutoStartState refreshAutoStart() {
        const auto state = getAutoStartState();
        const bool pending = state == AutoStartState::requiresApproval;
        autoStart.setToggleState(state != AutoStartState::disabled, juce::dontSendNotification);
        autoStart.setButtonText(pending ? u8"开机自启动（待批准）"_es : u8"开机自启动"_es);
        autoStart.setTooltip(pending ? autoStartApprovalMessage() : u8"登录当前系统账户后启动 EnjoySync，切换后立即生效"_es);
        hideOnAutoStart.setEnabled(state == AutoStartState::enabled);
        return state;
    }
    void lookAndFeelChanged() override {
        const auto& p = appearance.colours();
        for (auto* caption : {&nameCaption, &themeCaption, &outputCaption, &codecCaption, &bufferCaption, &portCaption, &keyCaption})
            caption->setColour(juce::Label::textColourId, p.secondary);
        for (auto* editor : {&name, &port, &buffer, &localKey}) styleEditor(*editor, p);
        feedback.setColour(juce::Label::textColourId, failed ? p.danger : p.accent);
        repaint();
    }
    void paint(juce::Graphics& g) override {
        const auto& p = appearance.colours(); g.fillAll(p.surface); g.setColour(p.outline);
        g.drawHorizontalLine(304, 28, (float)getWidth() - 28);
        g.drawHorizontalLine(508, 28, (float)getWidth() - 28);
        g.drawHorizontalLine(664, 0, (float)getWidth());
    }
    void resized() override {
        const int width = getWidth() - 56, half = (width - 20) / 2, right = 28 + half + 20;
        generalCaption.setBounds(28, 22, width, 24);
        nameCaption.setBounds(28, 60, width, 20); name.setBounds(28, 86, width, 38);
        themeCaption.setBounds(28, 142, width, 20); theme.setBounds(28, 168, width, 38);
        autoStart.setBounds(28, 222, half, 34); hideDock.setBounds(right, 222, half, 34);
        hideOnAutoStart.setBounds(28, 262, width, 34);
        audioCaption.setBounds(28, 322, width, 24);
        outputCaption.setBounds(28, 360, width, 20); output.setBounds(28, 386, width, 38);
        codecCaption.setBounds(28, 438, half, 20); codec.setBounds(28, 464, half, 32);
        bufferCaption.setBounds(right, 438, half, 20); buffer.setBounds(right, 464, half, 32);
        connectionCaption.setBounds(28, 526, width, 24);
        portCaption.setBounds(28, 566, 140, 20); port.setBounds(28, 592, 140, 38);
        keyCaption.setBounds(188, 566, width - 160, 20); localKey.setBounds(188, 592, width - 206, 38);
        copyKey.setBounds(getWidth() - 66, 592, 38, 38);
        feedback.setBounds(28, 685, width - 132, 28); apply.setBounds(getWidth() - 140, 680, 112, 36);
    }
private:
    static juce::String autoStartApprovalMessage() { return u8"请在系统设置的“通用 → 登录项”中允许 EnjoySync"_es; }
    void report(const juce::Result& result, const juce::String& success) {
        failed = result.failed(); setLabel(feedback, failed ? result.getErrorMessage() : success, 12);
        feedback.setColour(juce::Label::textColourId, failed ? appearance.colours().danger : appearance.colours().accent);
        feedback.setTooltip(feedback.getText());
    }
    Controller& controller;
    Appearance& appearance;
    juce::Label generalCaption, audioCaption, connectionCaption, nameCaption, themeCaption, outputCaption,
                codecCaption, bufferCaption, portCaption, keyCaption, feedback;
    juce::TextEditor name, port, buffer, localKey;
    juce::ComboBox theme, codec, output;
    juce::ToggleButton autoStart {u8"开机自启动"_es};
    juce::ToggleButton hideOnAutoStart {u8"开机自启动后隐藏到后台"_es};
    juce::ToggleButton hideDock {u8"后台隐藏 Dock"_es};
    IconButton copyKey;
    juce::TextButton apply {u8"保存设置"_es};
    juce::TooltipWindow tooltips {this, 500};
    bool failed = false;
};

SettingsWindow::SettingsWindow(Controller& controller, Appearance& appearance)
    : DocumentWindow(u8"设置"_es, appearance.colours().surface, closeButton) {
    setUsingNativeTitleBar(true); setResizable(false, false);
    setContentOwned(new SettingsView(controller, appearance), true); centreWithSize(getWidth(), getHeight());
    setApplicationTheme(controller.settings.theme);
    allowSystemShutdown(*this);
}
void SettingsWindow::closeButtonPressed() { setVisible(false); }
void SettingsWindow::visibilityChanged() {
    DocumentWindow::visibilityChanged();
    if (isVisible()) if (auto* view = dynamic_cast<SettingsView*>(getContentComponent())) view->refreshAutoStart();
}
bool SettingsWindow::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::escapeKey) { closeButtonPressed(); return true; }
    return DocumentWindow::keyPressed(key);
}
}
