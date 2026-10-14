#include "Controller.h"
namespace enjoysync {
Controller::Controller(const juce::File& directory)
    : settings(Settings::load(directory.getChildFile("settings.json"))), settingsFile(directory.getChildFile("settings.json")),
      capture(createSystemCapture()) {
    auto result = audio.openOutput(settings.outputDevice);
    if (result.failed()) problem = u8"播放设备不可用："_es + result.getErrorMessage();
    audio.setCodec(settings.pcm); audio.setBufferMs(settings.bufferMs);
    transport = std::make_unique<Transport>(audio, settings);
    result = transport->start(); if (result.failed()) problem = result.getErrorMessage();
    save(); startTimerHz(5);
}
Controller::~Controller() {
    stopTimer(); capture->stop(); transport->stop(); save(); endAudioActivity();
}
std::vector<PeerSnapshot> Controller::peers() const { return transport->snapshot(); }
juce::Result Controller::addPeer(PeerSettings peer) {
    auto endpoint = normalizeEndpoint(peer.host, peer.port); if (endpoint.failed()) return endpoint;
    peer.key = peer.key.trim().toLowerCase();
    if (!validKey(peer.key)) return juce::Result::fail(u8"连接密钥应为对方提供的 32 位十六进制字符串"_es);
    auto snapshots = peers();
    if (snapshots.size() >= maxPeers && std::none_of(snapshots.begin(), snapshots.end(), [&](auto& p) { return sameEndpoint(p.settings, peer); }))
        return juce::Result::fail(u8"初版最多保存 16 台设备，请先移除不需要的设备"_es);
    transport->addOrConnect(std::move(peer)); return juce::Result::ok();
}
juce::Result Controller::applyPreferences(const juce::String& name, int port, int latency, bool pcm, bool hideDock, const juce::String& output) {
    if (name.trim().isEmpty() || name.length() > 80) return juce::Result::fail(u8"设备名称应为 1–80 个字符"_es);
    if (port < 1 || port > 65535) return juce::Result::fail(u8"监听端口范围为 1–65535"_es);
    if (latency < 20 || latency > 200) return juce::Result::fail(u8"缓冲范围为 20–200 ms"_es);
    settings.peers.clear(); for (auto& p : peers()) settings.peers.push_back(p.settings);
    const int previousPort = settings.port;
    settings.name = name.trim(); settings.bufferMs = latency; settings.pcm = pcm; settings.hideDock = hideDock;
    if (port != previousPort) {
        capture->stop(); transport->stop(); settings.port = port;
        transport = std::make_unique<Transport>(audio, settings);
        auto result = transport->start();
        if (result.failed()) {
            settings.port = previousPort; transport = std::make_unique<Transport>(audio, settings); transport->start();
            problem = result.getErrorMessage(); save(); return result;
        }
    }
    if (output != settings.outputDevice) {
        auto result = audio.openOutput(output);
        if (result.failed()) { audio.openOutput(settings.outputDevice); return result; }
        settings.outputDevice = output;
    }
    audio.setCodec(pcm); audio.setBufferMs(latency); transport->setLocalPreferences(settings);
    problem.clear(); captureFailed = false; save(); return juce::Result::ok();
}
void Controller::setSharingEnabled(bool enabled) {
    allowSharing = enabled; captureFailed = false;
    if (!enabled) { capture->stop(); endAudioActivity(); activeActivity = false; }
}
void Controller::save() {
    auto snapshots = peers();
    settings.peers.clear(); for (auto& p : snapshots) settings.peers.push_back(p.settings);
    auto text = juce::JSON::toString(settings.toJson(), true);
    if (text == lastSaved) return;
    auto result = settings.save(settingsFile);
    if (result.wasOk()) lastSaved = text; else problem = result.getErrorMessage();
}
void Controller::timerCallback() {
    bool desired = allowSharing && transport->anySending();
    if (desired && !capture->running() && !captureFailed) {
        auto result = capture->start(audio);
        if (result.failed()) { problem = result.getErrorMessage(); captureFailed = true; }
        else { problem.clear(); beginAudioActivity(); activeActivity = true; }
    } else if (!desired && capture->running()) { capture->stop(); }
    if (capture->running() && capture->error().isNotEmpty()) { problem = capture->error(); captureFailed = true; }
    if (!capture->running() && activeActivity) { endAudioActivity(); activeActivity = false; }
    transport->setCaptureActive(capture->running());
    if (++saveTicks >= 10) { saveTicks = 0; save(); }
    if (changed) changed();
}
}
