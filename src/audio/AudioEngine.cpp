#include "AudioEngine.h"
#include <codec/aoo_opus.h>
#include <codec/aoo_pcm.h>
#include <cmath>
#include <mutex>
#include <stdexcept>
namespace enjoysync {
AudioEngine::AudioEngine() {
    // The application owns one AOO lifetime; tests likewise initialize once.
    source = AooSource_new(1);
    if (!source) throw std::runtime_error("AOO source allocation failed");
    AooSource_setup(source, 2, 48000, 480, kAooFixedBlockSize); applyCodec();
    AooSource_setPacketSize(source, 1000);
    for (size_t i = 0; i < slots.size(); ++i) {
        auto& s = slots[i]; s.sink = AooSink_new((AooId)i + 1);
        if (!s.sink) throw std::runtime_error("AOO sink allocation failed");
        AooSink_setup(s.sink, 2, 48000, 480, kAooFixedBlockSize);
        AooSink_setLatency(s.sink, 0.03); AooSink_setDynamicResampling(s.sink, kAooTrue);
        AooSink_setPacketSize(s.sink, 1000); AooSink_setSourceTimeout(s.sink, 5.0);
        AooSink_setEventHandler(s.sink, sinkEvent, &s, kAooEventModePoll);
    }
}
AudioEngine::~AudioEngine() {
    closeOutput();
    for (auto& s : slots) AooSink_free(s.sink);
    AooSource_free(source);
}
juce::Result AudioEngine::openOutput(const juce::String& name) {
    closeOutput();
    auto error = devices.initialise(0, 2, nullptr, true, name);
    if (error.isNotEmpty()) return juce::Result::fail(error);
    auto setup = devices.getAudioDeviceSetup();
    setup.bufferSize = 256; setup.sampleRate = 48000;
    devices.setAudioDeviceSetup(setup, true); // Retain the supported default if the preferred format fails.
    devices.addAudioCallback(this);
    return juce::Result::ok();
}
void AudioEngine::closeOutput() { devices.removeAudioCallback(this); devices.closeAudioDevice(); }
juce::StringArray AudioEngine::outputDevices() {
    juce::StringArray names;
    for (auto* type : devices.getAvailableDeviceTypes()) { type->scanForDevices(); names.addArray(type->getDeviceNames(false)); }
    names.removeDuplicates(false); return names;
}
juce::String AudioEngine::outputName() const { auto* device = devices.getCurrentAudioDevice(); return device ? device->getName() : juce::String(); }
void AudioEngine::configureCapture(double rate) {
    std::unique_lock lock(lifecycle);
    captureRate = rate; captureBlock = juce::jlimit(1, 8192, (int)std::round(rate / 100.0)); captureFill = 0;
    AooSource_setup(source, 2, rate, captureBlock, kAooFixedBlockSize); applyCodec();
}
void AudioEngine::applyCodec() {
    if (pcm) {
        AooFormatPcm format {}; AooFormatPcm_init(&format, 2, 48000, 480, kAooPcmInt24);
        formatResult = AooSource_setFormat(source, &format.header);
    } else {
        AooFormatOpus format {}; AooFormatOpus_init(&format, 2, 48000, 480, OPUS_APPLICATION_RESTRICTED_LOWDELAY);
        formatResult = AooSource_setFormat(source, &format.header);
        AooSource_setOpusBitrate(source, nullptr, 192000); AooSource_setOpusComplexity(source, nullptr, 5);
    }
}
void AudioEngine::setCodec(bool usePcm) { std::unique_lock lock(lifecycle); pcm = usePcm; applyCodec(); }
void AudioEngine::setCapturing(bool running) {
    std::unique_lock lock(lifecycle);
    captureEnabled = running;
    if (running) { captureFill = 0; AooSource_startStream(source, 0, nullptr); }
    else {
        AooSource_stopStream(source, 0);
        // Flush the stop transition after the hardware callback has been stopped.
        AooSource_process(source, nullptr, captureBlock, aoo_getCurrentNtpTime());
        captureFill = 0; captureLevel = 0.0f;
    }
}
void AudioEngine::pushCapture(const float* left, const float* right, int samples, AooNtpTime time) {
    std::shared_lock lock(lifecycle, std::try_to_lock); if (!lock.owns_lock() || !captureEnabled || sendingSinks == 0 || samples <= 0 || samples > 8192) return;
    for (int offset = 0; offset < samples;) {
        if (captureFill == 0) captureTime = time + aoo_ntpTimeFromSeconds(offset / captureRate);
        int count = std::min(captureBlock - captureFill, samples - offset);
        std::copy_n(left + offset, count, captureLeft.data() + captureFill);
        std::copy_n((right ? right : left) + offset, count, captureRight.data() + captureFill);
        captureFill += count; offset += count;
        if (captureFill == captureBlock) {
            float* channels[] {captureLeft.data(), captureRight.data()};
            captureResult = AooSource_process(source, channels, captureBlock, captureTime);
            if (captureResult.load() == kAooErrorOverflow) {
                // This AOO revision checks resampler.write() before draining it.
                // A full resampler otherwise stays full even after send() resumes.
                // Start a fresh stream on the next callback to discard stale audio.
                AooSource_startStream(source, 0, nullptr);
            }
            captureFill = 0;
        }
    }
    float peak = 0; for (int i = 0; i < samples; ++i) peak = std::max(peak, std::abs(left[i]));
    captureLevel.store(peak);
}
void AudioEngine::setBufferMs(int ms) {
    std::shared_lock lock(lifecycle); baseLatency = juce::jlimit(20, 200, ms);
    for (auto& s : slots) { s.latency = baseLatency.load(); AooSink_setLatency(s.sink, s.latency.load() / 1000.0); }
}
void AudioEngine::activate(int index, bool enabled) {
    std::unique_lock lock(lifecycle); auto& s = slots[(size_t)index];
    s.active = false; AooSink_reset(s.sink); s.level = 0.0f; s.underruns = 0;
    // This AOO revision exposes resetDll in its API but does not implement the control.
    // setup with the existing format resets the DLL timer under our exclusive lock.
    AooSink_setup(s.sink, 2, outputRate, outputBlock, kAooFixedBlockSize);
    s.latency = baseLatency.load(); AooSink_setLatency(s.sink, s.latency.load() / 1000.0);
    s.lastUnderrun = s.lastDecrease = juce::Time::getMillisecondCounterHiRes();
    s.smoothGain = 0; s.active = enabled;
}
void AudioEngine::setMix(int index, float gain, bool muted) { auto& s = slots[(size_t)index]; s.gain = gain; s.muted = muted; }
void AudioEngine::setSending(const Address& address, int id, bool enabled) {
    std::unique_lock lock(lifecycle); AooEndpoint endpoint { address.data(), (AooAddrSize)address.size, id };
    if (enabled) {
        auto result = AooSource_addSink(source, &endpoint, kAooTrue); attachResult = result;
        if (result == kAooOk && sendingSinks.fetch_add(1) == 0) {
            captureFill = 0; AooSource_startStream(source, 0, nullptr);
        }
    } else if (AooSource_removeSink(source, &endpoint) == kAooOk) {
        if (sendingSinks.fetch_sub(1) == 1) {
            AooSource_stopStream(source, 0);
            AooSource_process(source, nullptr, captureBlock, aoo_getCurrentNtpTime());
            captureFill = 0;
        }
    }
}
void AudioEngine::handle(int index, const void* data, int size, const Address& address) {
    std::shared_lock lock(lifecycle); AooMsgType type; AooId id;
    auto* bytes = static_cast<const AooByte*>(data);
    if (aoo_parsePattern(bytes, size, &type, &id, nullptr) != kAooOk) return;
    if (type == kAooMsgTypeSource && id == 1) AooSource_handleMessage(source, bytes, size, address.data(), address.size);
    else if (type == kAooMsgTypeSink && id == index + 1 && slots[(size_t)index].active)
        receiveResult = AooSink_handleMessage(slots[(size_t)index].sink, bytes, size, address.data(), address.size);
}
void AudioEngine::pump(AooSendFunc fn, void* user) {
    std::shared_lock lock(lifecycle); AooSource_send(source, fn, user);
    const double now = juce::Time::getMillisecondCounterHiRes();
    for (auto& s : slots) if (s.active) {
        AooSink_send(s.sink, fn, user); AooSink_pollEvents(s.sink);
        if (now - s.lastUnderrun > 30000 && now - s.lastDecrease > 10000 && s.latency > baseLatency) {
            s.latency = std::max(baseLatency.load(), s.latency.load() - 5); s.lastDecrease = now;
            AooSink_setLatency(s.sink, s.latency.load() / 1000.0);
        }
    }
}
void AOO_CALL AudioEngine::sinkEvent(void* user, const AooEvent* event, AooThreadLevel) {
    auto& s = *static_cast<Slot*>(user);
    if (event->type == kAooEventBufferUnderrun) {
        ++s.underruns; auto now = juce::Time::getMillisecondCounterHiRes();
        if (now - s.lastUnderrun > 1000) {
            s.latency = std::min(200, s.latency.load() + 10);
            AooSink_setLatency(s.sink, s.latency.load() / 1000.0);
        }
        s.lastUnderrun = now;
    }
}
void AudioEngine::configureOutput(double rate, int blockSize) {
    std::unique_lock lock(lifecycle);
    outputRate = rate; outputBlock = blockSize;
    for (auto& s : slots) AooSink_setup(s.sink, 2, rate, blockSize, kAooFixedBlockSize);
}
void AudioEngine::audioDeviceAboutToStart(juce::AudioIODevice* device) { configureOutput(device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples()); }
void AudioEngine::render(float* left, float* right, int samples, AooNtpTime time) {
    std::fill_n(left, samples, 0.0f); std::fill_n(right, samples, 0.0f);
    std::shared_lock lock(lifecycle, std::try_to_lock); if (!lock.owns_lock() || samples > 8192) return;
    float* channels[] { scratchLeft.data(), scratchRight.data() };
    for (auto& s : slots) if (s.active) {
        std::fill_n(channels[0], samples, 0.0f); std::fill_n(channels[1], samples, 0.0f);
        AooSink_process(s.sink, channels, samples, time, nullptr, nullptr);
        const float target = s.muted || muteAll ? 0.0f : s.gain.load();
        float peak = 0;
        for (int i = 0; i < samples; ++i) {
            s.smoothGain += (target - s.smoothGain) * 0.005f;
            left[i] += channels[0][i] * s.smoothGain; right[i] += channels[1][i] * s.smoothGain;
            peak = std::max(peak, std::abs(channels[0][i]));
        }
        s.level = peak;
    }
    for (int i = 0; i < samples; ++i) { left[i] = juce::jlimit(-1.0f, 1.0f, left[i]); right[i] = juce::jlimit(-1.0f, 1.0f, right[i]); }
}
void AudioEngine::audioDeviceIOCallbackWithContext(const float* const*, int, float* const* outputs, int count, int samples,
                                                 const juce::AudioIODeviceCallbackContext&) {
    for (int channel = 0; channel < count; ++channel) if (outputs[channel]) std::fill_n(outputs[channel], samples, 0.0f);
    if (count >= 2 && outputs[0] && outputs[1]) render(outputs[0], outputs[1], samples, aoo_getCurrentNtpTime());
    else if (count == 1 && outputs[0] && samples <= 8192) {
        render(monoLeft.data(), monoRight.data(), samples, aoo_getCurrentNtpTime());
        for (int i = 0; i < samples; ++i) outputs[0][i] = (monoLeft[(size_t)i] + monoRight[(size_t)i]) * 0.5f;
    }
}
}
