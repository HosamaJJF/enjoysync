#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <aoo.h>
#include <aoo_source.h>
#include <aoo_sink.h>
#include "core/Settings.h"
#include "net/Socket.h"
#include <array>
#include <atomic>
#include <shared_mutex>

namespace enjoysync {
class AudioEngine : private juce::AudioIODeviceCallback {
public:
    AudioEngine(); ~AudioEngine() override;
    juce::Result openOutput(const juce::String& name = {});
    void closeOutput();
    juce::StringArray outputDevices();
    juce::String outputName() const;
    void configureCapture(double sampleRate);
    void pushCapture(const float* left, const float* right, int samples, AooNtpTime time);
    void setCapturing(bool);
    void setCodec(bool pcm);
    void setBufferMs(int);
    void activate(int slot, bool);
    void setMix(int slot, float gain, bool mute);
    void setSending(const Address&, int remoteSink, bool);
    void handle(int slot, const void*, int, const Address&);
    void pump(AooSendFunc, void*);
    void render(float* left, float* right, int samples, AooNtpTime time);
    void configureOutput(double sampleRate, int maxBlock);
    float inputLevel() const { return captureLevel.load(); }
    float level(int slot) const { return slots[(size_t)slot].level.load(); }
    int latencyMs(int slot) const { return slots[(size_t)slot].latency.load(); }
    unsigned underruns(int slot) const { return slots[(size_t)slot].underruns.load(); }
    std::atomic<int> captureResult { 0 }, receiveResult { 0 }, formatResult { 0 }, attachResult { 0 };
    std::atomic<bool> muteAll { false };
private:
    struct Slot {
        AooSink* sink = nullptr;
        std::atomic<bool> active { false }, muted { false };
        std::atomic<float> gain { 0.8f }, level { 0.0f };
        std::atomic<int> latency { 30 };
        std::atomic<unsigned> underruns { 0 };
        float smoothGain = 0.0f;
        double lastUnderrun = 0.0, lastDecrease = 0.0;
    };
    static void AOO_CALL sinkEvent(void*, const AooEvent*, AooThreadLevel);
    void applyCodec();
    void audioDeviceAboutToStart(juce::AudioIODevice*) override;
    void audioDeviceStopped() override {}
    void audioDeviceIOCallbackWithContext(const float* const*, int, float* const*, int, int,
                                          const juce::AudioIODeviceCallbackContext&) override;
    juce::AudioDeviceManager devices;
    std::shared_mutex lifecycle;
    AooSource* source = nullptr;
    std::array<Slot, maxPeers> slots;
    std::array<float, 8192> scratchLeft {}, scratchRight {};
    std::array<float, 8192> monoLeft {}, monoRight {};
    std::array<float, 8192> captureLeft {}, captureRight {};
    int captureBlock = 480, captureFill = 0;
    double captureRate = 48000;
    double outputRate = 48000;
    int outputBlock = 480;
    AooNtpTime captureTime = 0;
    std::atomic<float> captureLevel { 0.0f };
    std::atomic<bool> captureEnabled { false };
    std::atomic<int> sendingSinks { 0 };
    std::atomic<int> baseLatency { 30 };
    bool pcm = false;
};
}
