#pragma once
#include <juce_core/juce_core.h>
#include <vector>

namespace enjoysync {
inline juce::String operator""_es(const char8_t* text, size_t size) { return juce::String(text, size); }
constexpr int maxPeers = 16;
constexpr int protocolVersion = 1;
juce::String generateKey();
struct PeerSettings {
    juce::String recordId = juce::Uuid().toString();
    juce::String deviceId, name, host, key;
    int port = 51000;
    bool autoConnect = false, receive = true, send = false, muted = false;
    float gain = 0.8f;
    juce::var toJson() const;
    static PeerSettings fromJson(const juce::var&);
};
struct Settings {
    juce::String deviceId = juce::Uuid().toString();
    juce::String name = juce::SystemStats::getComputerName();
    juce::String key = generateKey();
    int port = 51000, bufferMs = 30;
    bool hideDock = true, pcm = false;
    juce::String outputDevice;
    std::vector<PeerSettings> peers;
    juce::var toJson() const;
    static Settings fromJson(const juce::var&);
    static Settings load(const juce::File&);
    juce::Result save(const juce::File&) const;
};
bool validKey(const juce::String&);
juce::String normalizeHost(juce::String);
bool sameEndpoint(const PeerSettings&, const PeerSettings&);
int retryDelayMs(unsigned attempt);
}
