#include "MainView.h"
#include "SettingsView.h"

namespace enjoysync {
class PeerRow final : public juce::Component {
public:
    PeerRow(Controller& c, Appearance& a, PeerSnapshot s)
        : controller(c), appearance(a), snapshot(std::move(s)),
          receive(a, Icon::receive, u8"接收"_es, true), play(a, Icon::speaker, u8"播放"_es, true),
          send(a, Icon::share, u8"共享"_es, true), automatic(a, Icon::automatic, u8"自动连接"_es, true),
          action(a, Icon::reconnect, u8"重连"_es), remove(a, Icon::remove, u8"移除"_es, false, true) {
        for (auto* component : std::initializer_list<juce::Component*> {
            &name, &state, &details, &receive, &play, &send, &automatic, &volume, &percentage, &action, &remove}) addAndMakeVisible(component);
        volume.setSliderStyle(juce::Slider::LinearHorizontal); volume.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        volume.setRange(0, 2, 0.01); volume.setTitle(u8"播放音量"_es); volume.setTooltip(u8"播放音量"_es);
        volume.setDoubleClickReturnValue(true, 0.8); volume.setScrollWheelEnabled(false);
        receive.onClick = [this] { snapshot.settings.receive = receive.getToggleState(); update(); };
        play.onClick = [this] {
            snapshot.settings.muted = !play.getToggleState();
            play.setIcon(snapshot.settings.muted ? Icon::muted : Icon::speaker, u8"播放"_es); update();
        };
        send.onClick = [this] { snapshot.settings.send = send.getToggleState(); update(); };
        automatic.onClick = [this] { snapshot.settings.autoConnect = automatic.getToggleState(); update(); };
        volume.onDragStart = [this] { adjustingVolume = true; };
        volume.onDragEnd = [this] { adjustingVolume = false; };
        volume.onValueChange = [this] { snapshot.settings.gain = (float)volume.getValue(); refreshPercentage(); update(); };
        action.onClick = [this] {
            if (isConnecting()) controller.transport->disconnect(snapshot.settings.recordId);
            else controller.transport->connect(snapshot.settings.recordId);
        };
        remove.onClick = [this] { controller.transport->remove(snapshot.settings.recordId); };
        percentage.setJustificationType(juce::Justification::centredRight);
        refresh(snapshot);
    }
    void refresh(const PeerSnapshot& value) {
        snapshot = value;
        setLabel(name, value.settings.name.isNotEmpty() ? value.settings.name : value.settings.host, 15, true);
        name.setTooltip(name.getText());
        setLabel(state, stateName(value.state), 12);
        const auto& p = appearance.colours();
        state.setColour(juce::Label::textColourId, value.state == ConnectionState::connected ? p.accent
            : value.state == ConnectionState::error ? p.danger : p.secondary);
        auto address = value.settings.host.containsChar(':') ? "[" + value.settings.host + "]" : value.settings.host;
        juce::String text = address + ":" + juce::String(value.settings.port);
        if (value.state == ConnectionState::connected) {
            text += u8"   ·   RTT "_es + juce::String(value.rttMs, 1) + " ms";
            if (value.slot >= 0 && value.slot < maxPeers)
                text += u8"   ·   缓冲 "_es + juce::String(controller.audio.latencyMs(value.slot)) + " ms";
        } else if (value.state == ConnectionState::error || value.state == ConnectionState::retrying) text += u8"   ·   "_es + value.detail;
        setLabel(details, text, 12); details.setColour(juce::Label::textColourId, p.secondary); details.setTooltip(text);
        receive.setToggleState(value.settings.receive, juce::dontSendNotification);
        play.setToggleState(!value.settings.muted, juce::dontSendNotification);
        play.setIcon(value.settings.muted ? Icon::muted : Icon::speaker, u8"播放"_es);
        send.setToggleState(value.settings.send, juce::dontSendNotification);
        automatic.setToggleState(value.settings.autoConnect, juce::dontSendNotification);
        if (!adjustingVolume) volume.setValue(value.settings.gain, juce::dontSendNotification);
        refreshPercentage();
        action.setIcon(isConnecting() ? Icon::disconnect : Icon::reconnect, isConnecting() ? u8"断开"_es : u8"重连"_es);
        repaint();
    }
    void lookAndFeelChanged() override { refresh(snapshot); }
    juce::String id() const { return snapshot.settings.recordId; }
    void paint(juce::Graphics& g) override {
        const auto& p = appearance.colours(); drawPanel(g, getLocalBounds(), p);
        g.setColour(p.field); g.fillRoundedRectangle(20, 20, 38, 38, 10);
        drawIcon(g, Icon::device, {28, 28, 22, 22}, p.secondary);
        g.setColour(state.findColour(juce::Label::textColourId)); g.fillEllipse(72, 51, 6, 6);
        g.setColour(p.outline); g.drawVerticalLine(getWidth() - 98, 23, 49);
        const auto meter = snapshot.state == ConnectionState::connected && snapshot.slot >= 0 && snapshot.slot < maxPeers
            ? juce::jlimit(0.0f, 1.0f, controller.audio.level(snapshot.slot)) : 0.0f;
        g.setColour(p.outline.withAlpha(0.45f)); g.fillRoundedRectangle(20, 99, (float)getWidth() - 40, 2, 1);
        if (meter > 0) { g.setColour(p.accent); g.fillRoundedRectangle(20, 99, meter * ((float)getWidth() - 40), 2, 1); }
    }
    void resized() override {
        const int w = getWidth(), controls = w - 420;
        name.setBounds(72, 21, controls - 88, 22); state.setBounds(86, 45, controls - 102, 18);
        details.setBounds(72, 73, w - 92, 18);
        receive.setBounds(controls, 20, 34, 34); play.setBounds(controls + 40, 20, 34, 34);
        send.setBounds(controls + 80, 20, 34, 34); automatic.setBounds(controls + 120, 20, 34, 34);
        volume.setBounds(controls + 166, 23, 100, 28); percentage.setBounds(controls + 272, 24, 44, 24);
        action.setBounds(w - 88, 20, 34, 34); remove.setBounds(w - 48, 20, 34, 34);
    }
private:
    bool isConnecting() const {
        return snapshot.state == ConnectionState::connected || snapshot.state == ConnectionState::connecting
            || snapshot.state == ConnectionState::resolving || snapshot.state == ConnectionState::retrying;
    }
    void update() { controller.transport->updatePeer(snapshot.settings); }
    void refreshPercentage() {
        setLabel(percentage, juce::String(juce::roundToInt(volume.getValue() * 100)) + "%", 12);
        percentage.setColour(juce::Label::textColourId, appearance.colours().secondary);
    }
    Controller& controller;
    Appearance& appearance;
    PeerSnapshot snapshot;
    juce::Label name, state, details, percentage;
    IconButton receive, play, send, automatic;
    juce::Slider volume;
    IconButton action, remove;
    bool adjustingVolume = false;
};

MainView::MainView(Controller& c, Appearance& a)
    : controller(c), appearance(a), share(a, Icon::share, u8"共享系统声音"_es, true),
      mute(a, Icon::speaker, u8"全部播放静音"_es, true), settingsButton(a, Icon::settings, u8"设置"_es) {
    for (auto* component : std::initializer_list<juce::Component*> {
        &localCaption, &localName, &connectCaption, &hostCaption, &portCaption, &keyCaption, &peerCaption, &feedback,
        &host, &remotePort, &remoteKey, &automatic, &connectButton, &share, &mute, &settingsButton, &viewport}) addAndMakeVisible(component);
    setLabel(localCaption, u8"本机设备"_es, 11); setLabel(connectCaption, u8"添加设备"_es, 14, true);
    setLabel(hostCaption, u8"IP / 域名"_es, 11); setLabel(portCaption, u8"端口"_es, 11);
    setLabel(keyCaption, u8"连接密钥"_es, 11);
    setupEditor(host, {}, u8"IP / 域名"_es); setupEditor(remotePort, "51000", u8"端口"_es, true);
    setupEditor(remoteKey, {}, u8"连接密钥"_es); remoteKey.setPasswordCharacter(0x2022);
    host.setTooltip(u8"IP:端口 / 域名:端口 / [IPv6]:端口"_es);
    share.onClick = [this] { controller.setSharingEnabled(share.getToggleState()); };
    mute.onClick = [this] {
        controller.audio.muteAll = mute.getToggleState();
        mute.setIcon(mute.getToggleState() ? Icon::muted : Icon::speaker, u8"全部播放静音"_es);
    };
    settingsButton.onClick = [this] { showSettings(); };
    connectButton.onClick = [this] {
        PeerSettings peer; peer.host = host.getText(); peer.port = remotePort.getText().getIntValue(); peer.key = remoteKey.getText();
        peer.autoConnect = automatic.getToggleState();
        const auto endpoint = normalizeEndpoint(peer.host, peer.port);
        if (endpoint.failed()) { report(endpoint); return; }
        const auto result = controller.addPeer(peer); report(result);
        if (result.wasOk()) { host.setText(peer.host, false); remotePort.setText(juce::String(peer.port), false); }
    };
    host.onReturnKey = remotePort.onReturnKey = remoteKey.onReturnKey = [this] { connectButton.triggerClick(); };
    viewport.setViewedComponent(&rowsContainer, false); viewport.setScrollBarsShown(true, false); viewport.setScrollBarThickness(8);
    controller.changed = [this] { refresh(); };
    lookAndFeelChanged(); refresh(); setSize(940, 620);
}
MainView::~MainView() { controller.changed = {}; settingsWindow.reset(); viewport.setViewedComponent(nullptr, false); }
void MainView::showSettings() {
    if (!settingsWindow) settingsWindow = std::make_unique<SettingsWindow>(controller, appearance);
    settingsWindow->setVisible(true); settingsWindow->setMinimised(false); settingsWindow->toFront(true);
}
void MainView::hideSettings() { if (settingsWindow) settingsWindow->setVisible(false); }
bool MainView::keyPressed(const juce::KeyPress& key) {
    if (key.getKeyCode() == ',' && key.getModifiers().isCommandDown()) { showSettings(); return true; }
    return false;
}
void MainView::lookAndFeelChanged() {
    const auto& p = appearance.colours();
    for (auto* item : {&localCaption, &hostCaption, &portCaption, &keyCaption}) item->setColour(juce::Label::textColourId, p.secondary);
    styleEditor(host, p, u8"IP / 域名"_es); styleEditor(remotePort, p, "51000"); styleEditor(remoteKey, p, u8"32 位连接密钥"_es);
    feedback.setColour(juce::Label::textColourId, failed ? p.danger : p.accent); repaint();
}
void MainView::report(const juce::Result& result) {
    failed = result.failed(); setLabel(feedback, failed ? result.getErrorMessage() : juce::String(), 12);
    feedback.setColour(juce::Label::textColourId, failed ? appearance.colours().danger : appearance.colours().accent);
    feedback.setTooltip(feedback.getText());
}
void MainView::refresh() {
    const auto snapshots = controller.peers();
    bool rebuild = rows.size() != snapshots.size();
    if (!rebuild) for (size_t i = 0; i < rows.size(); ++i) if (rows[i]->id() != snapshots[i].settings.recordId) rebuild = true;
    if (rebuild) {
        rows.clear();
        for (const auto& snapshot : snapshots) {
            auto row = std::make_unique<PeerRow>(controller, appearance, snapshot);
            rowsContainer.addAndMakeVisible(*row); rows.push_back(std::move(row));
        }
        resized();
    } else for (size_t i = 0; i < rows.size(); ++i) rows[i]->refresh(snapshots[i]);
    setLabel(localName, controller.settings.name, 18, true); localName.setTooltip(controller.settings.name);
    setLabel(peerCaption, u8"设备  ·  "_es + juce::String((int)snapshots.size()), 14, true);
    share.setToggleState(controller.sharingEnabled(), juce::dontSendNotification);
    mute.setToggleState(controller.audio.muteAll, juce::dontSendNotification);
    mute.setIcon(mute.getToggleState() ? Icon::muted : Icon::speaker, u8"全部播放静音"_es);
    if (controller.error().isNotEmpty()) report(juce::Result::fail(controller.error()));
    repaint();
}
void MainView::paint(juce::Graphics& g) {
    const auto& p = appearance.colours(); g.fillAll(p.background);
    g.setColour(p.accentSurface); g.fillRoundedRectangle(28, 24, 40, 40, 11);
    drawIcon(g, Icon::device, {37, 33, 22, 22}, p.accent);
    drawPanel(g, {24, 90, getWidth() - 48, 164}, p);
    if (rows.empty()) {
        const int centre = 302 + (getHeight() - 326) / 2;
        drawIcon(g, Icon::device, {(float)getWidth() / 2 - 16, (float)centre - 38, 32, 32}, p.secondary.withAlpha(0.5f));
        g.setColour(p.secondary); g.setFont(uiFont(13));
        g.drawText(u8"暂无设备"_es, 24, centre + 6, getWidth() - 48, 24, juce::Justification::centred);
    }
}
void MainView::resized() {
    const int w = getWidth(); localCaption.setBounds(82, 23, w - 280, 16); localName.setBounds(82, 41, w - 280, 27);
    share.setBounds(w - 156, 26, 36, 36); mute.setBounds(w - 112, 26, 36, 36); settingsButton.setBounds(w - 64, 26, 36, 36);
    connectCaption.setBounds(44, 105, w - 88, 24);
    const int hostWidth = (w - 320) * 45 / 100, keyX = 44 + hostWidth + 100;
    hostCaption.setBounds(44, 143, hostWidth, 16); host.setBounds(44, 165, hostWidth, 38);
    portCaption.setBounds(56 + hostWidth, 143, 76, 16); remotePort.setBounds(56 + hostWidth, 165, 76, 38);
    keyCaption.setBounds(keyX, 143, w - keyX - 182, 16); remoteKey.setBounds(keyX, 165, w - keyX - 182, 38);
    connectButton.setBounds(w - 170, 165, 126, 38); automatic.setBounds(40, 214, 190, 26);
    feedback.setBounds(242, 214, w - 286, 26);
    peerCaption.setBounds(28, 273, w - 56, 22); viewport.setBounds(24, 312, w - 48, getHeight() - 336);
    rowsContainer.setSize(viewport.getWidth() - 12, std::max(viewport.getHeight(), (int)rows.size() * 124));
    for (size_t i = 0; i < rows.size(); ++i) rows[i]->setBounds(0, (int)i * 124, rowsContainer.getWidth(), 112);
}
}
