#include "MainView.h"
namespace enjoysync {
static const juce::Colour background {0xff10171e}, panel {0xff19232d}, ink {0xffe7f0f5}, mutedInk {0xff94a7b6}, mint {0xff71e7bd};
static juce::Font font(float size, bool bold = false) {
   #if JUCE_MAC
    return juce::Font(juce::FontOptions("PingFang SC", size, bold ? juce::Font::bold : juce::Font::plain));
   #else
    return juce::Font(juce::FontOptions("Microsoft YaHei UI", size, bold ? juce::Font::bold : juce::Font::plain));
   #endif
}
static void label(juce::Label& item, const juce::String& text, float size = 13, juce::Colour colour = mutedInk) {
    item.setText(text, juce::dontSendNotification); item.setFont(font(size)); item.setColour(juce::Label::textColourId, colour);
}
class PeerRow final : public juce::Component {
public:
    PeerRow(Controller& c, PeerSnapshot s) : controller(c), snapshot(std::move(s)) {
        for (auto* component : std::initializer_list<juce::Component*> {&name, &state, &details, &receive, &play, &send, &automatic, &volume, &action, &remove}) addAndMakeVisible(component);
        receive.setButtonText(u8"接收"_es); play.setButtonText(u8"播放"_es); send.setButtonText(u8"共享"_es); automatic.setButtonText(u8"自动连接"_es);
        volume.setSliderStyle(juce::Slider::LinearHorizontal); volume.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        volume.setRange(0, 1.5, 0.01); volume.setTooltip(u8"此设备在本机的播放音量"_es);
        receive.onClick = [this] { snapshot.settings.receive = receive.getToggleState(); update(); };
        play.onClick = [this] { snapshot.settings.muted = !play.getToggleState(); update(); };
        send.onClick = [this] { snapshot.settings.send = send.getToggleState(); update(); };
        automatic.onClick = [this] { snapshot.settings.autoConnect = automatic.getToggleState(); update(); };
        volume.onValueChange = [this] { snapshot.settings.gain = (float)volume.getValue(); update(); };
        action.onClick = [this] {
            if (snapshot.state == ConnectionState::connected || snapshot.state == ConnectionState::connecting || snapshot.state == ConnectionState::resolving)
                controller.transport->disconnect(snapshot.settings.recordId);
            else controller.transport->connect(snapshot.settings.recordId);
        };
        remove.setButtonText(u8"移除"_es); remove.onClick = [this] { controller.transport->remove(snapshot.settings.recordId); };
        refresh(snapshot);
    }
    void update() { controller.transport->updatePeer(snapshot.settings); }
    void refresh(const PeerSnapshot& s) {
        snapshot = s;
        label(name, s.settings.name.isNotEmpty() ? s.settings.name : s.settings.host, 16, ink);
        label(state, stateName(s.state), 12, s.state == ConnectionState::connected ? mint : mutedInk);
        juce::String text = s.detail;
        if (s.state == ConnectionState::connected) {
            text += u8" · RTT "_es + juce::String(s.rttMs, 0) + " ms";
            text += u8" · 缓冲 "_es + juce::String(controller.audio.latencyMs(s.slot)) + " ms";
            text += s.remoteSharing ? u8" · 对方正在共享"_es : u8" · 对方未共享"_es;
        }
        label(details, text, 11); details.setTooltip(text);
        receive.setToggleState(s.settings.receive, juce::dontSendNotification);
        play.setToggleState(!s.settings.muted, juce::dontSendNotification);
        send.setToggleState(s.settings.send, juce::dontSendNotification);
        automatic.setToggleState(s.settings.autoConnect, juce::dontSendNotification);
        if (!volume.isMouseButtonDown()) volume.setValue(s.settings.gain, juce::dontSendNotification);
        bool online = s.state == ConnectionState::connected || s.state == ConnectionState::connecting || s.state == ConnectionState::resolving;
        action.setButtonText(online ? u8"断开"_es : u8"重试"_es); repaint();
    }
    juce::String id() const { return snapshot.settings.recordId; }
    void paint(juce::Graphics& g) override {
        g.setColour(panel); g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(0, 4), 10);
        auto meter = juce::jlimit(0.0f, 1.0f, controller.audio.level(snapshot.slot));
        g.setColour(mint.withAlpha(0.75f)); g.fillRoundedRectangle(14, 73, meter * 270, 3, 1.5f);
    }
    void resized() override {
        int width = getWidth(); name.setBounds(14, 12, 216, 26); state.setBounds(230, 15, 92, 20);
        details.setBounds(14, 43, width - 28, 22);
        int x = std::max(335, width - 610);
        receive.setBounds(x, 12, 64, 28); play.setBounds(x + 64, 12, 64, 28); send.setBounds(x + 128, 12, 64, 28);
        automatic.setBounds(x + 194, 12, 100, 28); volume.setBounds(x + 299, 13, 108, 26);
        action.setBounds(width - 158, 12, 68, 28); remove.setBounds(width - 82, 12, 68, 28);
    }
private:
    Controller& controller;
    PeerSnapshot snapshot;
    juce::Label name, state, details;
    juce::ToggleButton receive, play, send, automatic;
    juce::Slider volume;
    juce::TextButton action, remove;
};
MainView::MainView(Controller& c) : controller(c) {
    auto& look = getLookAndFeel();
    look.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a3b48));
    look.setColour(juce::TextButton::textColourOffId, ink);
    look.setColour(juce::ToggleButton::textColourId, ink); look.setColour(juce::ToggleButton::tickColourId, mint);
    look.setColour(juce::Slider::thumbColourId, mint); look.setColour(juce::Slider::trackColourId, mint);
    look.setColour(juce::ComboBox::backgroundColourId, panel); look.setColour(juce::ComboBox::textColourId, ink);
    for (auto* component : std::initializer_list<juce::Component*> {&title, &subtitle, &localCaption, &keyCaption, &listener, &statusLabel,
        &peerCaption, &feedback, &nameCaption, &portCaption, &bufferCaption, &outputCaption,
        &name, &port, &buffer, &host, &remotePort, &remoteKey, &localKey, &codec, &output, &hideDock,
        &automatic, &apply, &connectButton, &copyKey, &share, &mute, &viewport}) addAndMakeVisible(component);
    label(title, "EnjoySync", 30, ink); title.setFont(font(30, true));
    label(subtitle, u8"让系统声音，抵达另一台设备。"_es, 13);
    label(localCaption, u8"本机设备"_es, 16, ink); label(keyCaption, u8"本机连接密钥 · 将密钥交给要连接你的设备"_es, 12);
    label(nameCaption, u8"设备名称"_es); label(portCaption, u8"UDP 监听端口"_es); label(bufferCaption, u8"最低缓冲 / ms"_es); label(outputCaption, u8"播放输出设备"_es);
    field(name, controller.settings.name, u8"设备名称"_es); field(port, juce::String(controller.settings.port), "51000", true);
    field(buffer, juce::String(controller.settings.bufferMs), "30", true);
    field(host, {}, u8"对方 IP / 域名（IPv6 可直接填写）"_es); field(remotePort, "51000", u8"端口"_es, true);
    field(remoteKey, {}, u8"对方的 32 位连接密钥"_es); field(localKey, controller.settings.key, {}); localKey.setReadOnly(true);
    remoteKey.setPasswordCharacter(0x2022); localKey.setPasswordCharacter(0x2022);
    localKey.setTooltip(u8"持有此密钥的设备可以与你配对；音频共享仍由每台设备自行开启"_es);
    codec.addItem(u8"Opus · 192 kbps"_es, 1); codec.addItem(u8"PCM · 24-bit"_es, 2); codec.setSelectedId(controller.settings.pcm ? 2 : 1);
    output.addItem(u8"系统默认输出"_es, 1);
    auto names = controller.audio.outputDevices(); int id = 2;
    for (const auto& device : names) { output.addItem(device, id); if (device == controller.settings.outputDevice) output.setSelectedId(id); ++id; }
    if (output.getSelectedId() == 0) output.setSelectedId(1);
    hideDock.setToggleState(controller.settings.hideDock, juce::dontSendNotification);
   #if !JUCE_MAC
    hideDock.setVisible(false);
   #endif
    share.setToggleState(true, juce::dontSendNotification); share.onClick = [this] { controller.setSharingEnabled(share.getToggleState()); };
    mute.onClick = [this] { controller.audio.muteAll = mute.getToggleState(); };
    copyKey.onClick = [this] { juce::SystemClipboard::copyTextToClipboard(controller.settings.key); label(feedback, u8"本机连接密钥已复制"_es, 12, mint); };
    apply.onClick = [this] {
        report(controller.applyPreferences(name.getText(), port.getText().getIntValue(), buffer.getText().getIntValue(),
            codec.getSelectedId() == 2, hideDock.getToggleState(), output.getSelectedId() == 1 ? juce::String() : output.getText()));
        port.setText(juce::String(controller.settings.port), false);
    };
    connectButton.setColour(juce::TextButton::buttonColourId, mint); connectButton.setColour(juce::TextButton::textColourOffId, background);
    connectButton.onClick = [this] {
        PeerSettings peer; peer.host = host.getText(); peer.port = remotePort.getText().getIntValue(); peer.key = remoteKey.getText();
        peer.autoConnect = automatic.getToggleState(); report(controller.addPeer(peer));
    };
    viewport.setViewedComponent(&rowsContainer, false); viewport.setScrollBarsShown(true, false);
    controller.changed = [this] { if (isShowing()) refresh(); };
    refresh(); setSize(1080, 760);
}
MainView::~MainView() { controller.changed = {}; viewport.setViewedComponent(nullptr, false); }
void MainView::field(juce::TextEditor& editor, const juce::String& value, const juce::String& placeholder, bool numeric) {
    editor.setFont(font(13)); editor.setColour(juce::TextEditor::backgroundColourId, background);
    editor.setColour(juce::TextEditor::textColourId, ink); editor.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff344552));
    editor.setColour(juce::TextEditor::focusedOutlineColourId, mint); editor.setText(value);
    editor.setTextToShowWhenEmpty(placeholder, mutedInk); editor.setIndents(9, 8);
    if (numeric) editor.setInputRestrictions(5, "0123456789");
}
void MainView::report(const juce::Result& result) { label(feedback, result.wasOk() ? u8"已应用"_es : result.getErrorMessage(), 12, result.wasOk() ? mint : juce::Colour(0xffffa192)); }
void MainView::refresh() {
    auto snapshots = controller.peers();
    bool rebuild = rows.size() != snapshots.size();
    if (!rebuild) for (size_t i = 0; i < rows.size(); ++i) if (rows[i]->id() != snapshots[i].settings.recordId) rebuild = true;
    if (rebuild) {
        rows.clear();
        for (auto& snapshot : snapshots) { auto row = std::make_unique<PeerRow>(controller, snapshot); rowsContainer.addAndMakeVisible(*row); rows.push_back(std::move(row)); }
        resized();
    } else for (size_t i = 0; i < rows.size(); ++i) rows[i]->refresh(snapshots[i]);
    label(listener, controller.transport->listenerDescription(), 12, mint);
    label(statusLabel, controller.status(), 12, controller.capturing() ? mint : mutedInk); statusLabel.setTooltip(controller.status());
    label(peerCaption, u8"设备  /  "_es + juce::String((int)snapshots.size()) + u8"    接收与共享可独立选择"_es, 16, ink);
    share.setToggleState(controller.sharingEnabled(), juce::dontSendNotification);
    mute.setToggleState(controller.audio.muteAll, juce::dontSendNotification);
    repaint();
}
void MainView::paint(juce::Graphics& g) {
    g.fillAll(background); g.setColour(panel);
    g.fillRoundedRectangle(24, 108, (float)getWidth() - 48, 250, 14);
    g.fillRoundedRectangle(24, 374, (float)getWidth() - 48, 112, 14);
    if (rows.empty()) {
        g.setColour(mutedInk); g.setFont(font(15));
        g.drawFittedText(u8"还没有设备。填写对方地址和连接密钥，开始共享声音。"_es, 40, 555, getWidth() - 80, 60, juce::Justification::centred, 2);
    }
}
void MainView::resized() {
    int w = getWidth(); title.setBounds(24, 24, 260, 42); subtitle.setBounds(26, 70, 450, 22);
    listener.setBounds(w - 420, 39, 390, 28); listener.setJustificationType(juce::Justification::centredRight);
    localCaption.setBounds(40, 120, 150, 26); share.setBounds(w - 400, 120, 195, 26); mute.setBounds(w - 200, 120, 160, 26);
    nameCaption.setBounds(40, 158, 220, 20); portCaption.setBounds(282, 158, 145, 20); bufferCaption.setBounds(446, 158, 142, 20);
    outputCaption.setBounds(608, 158, w - 650, 20);
    name.setBounds(40, 184, 222, 36); port.setBounds(282, 184, 144, 36); buffer.setBounds(446, 184, 142, 36);
    output.setBounds(608, 184, w - 648, 36);
    keyCaption.setBounds(40, 232, 500, 20); localKey.setBounds(40, 258, 348, 34); copyKey.setBounds(400, 258, 94, 34);
    codec.setBounds(514, 258, 172, 34); hideDock.setBounds(704, 260, 160, 30); apply.setBounds(w - 156, 258, 116, 34);
    statusLabel.setBounds(40, 310, w - 80, 28);
    host.setBounds(40, 390, 300, 36); remotePort.setBounds(350, 390, 90, 36);
    remoteKey.setBounds(452, 390, w - 646, 36); connectButton.setBounds(w - 180, 390, 140, 36);
    automatic.setBounds(40, 441, 190, 26); feedback.setBounds(240, 441, w - 280, 26);
    peerCaption.setBounds(28, 503, w - 56, 30); viewport.setBounds(24, 541, w - 48, getHeight() - 565);
    rowsContainer.setSize(viewport.getWidth() - 14, std::max(viewport.getHeight(), (int)rows.size() * 90));
    for (size_t i = 0; i < rows.size(); ++i) rows[i]->setBounds(0, (int)i * 90, rowsContainer.getWidth(), 86);
}
}
