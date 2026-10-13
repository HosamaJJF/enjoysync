#include "Capture.h"
#import <CoreAudio/CoreAudio.h>
#import <CoreAudio/AudioHardwareTapping.h>
#import <CoreAudio/CATapDescription.h>
#import <Foundation/Foundation.h>
#include <array>
#include <unistd.h>

namespace enjoysync {
class MacCapture final : public SystemCapture {
public:
    ~MacCapture() override { stop(); }
    juce::Result start(AudioEngine& target) override {
        stop(); engine = &target; lastError.clear();
        AudioObjectID ownProcess = kAudioObjectUnknown;
        AudioObjectPropertyAddress property { kAudioHardwarePropertyTranslatePIDToProcessObject, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
        pid_t pid = getpid(); UInt32 size = sizeof(ownProcess);
        auto status = AudioObjectGetPropertyData(kAudioObjectSystemObject, &property, sizeof(pid), &pid, &size, &ownProcess);
        if (status != noErr || ownProcess == kAudioObjectUnknown) return failure(u8"无法识别本机音频进程，已停止采集以防止声音回传"_es, status);
        CATapDescription* description = [[CATapDescription alloc] initStereoGlobalTapButExcludeProcesses:@[@(ownProcess)]];
        description.name = @"EnjoySync System Audio"; description.privateTap = YES;
        description.muteBehavior = CATapUnmuted;
        status = AudioHardwareCreateProcessTap(description, &tap);
        if (status != noErr) return failure(u8"无法采集系统声音，请在系统设置中允许 EnjoySync 录制系统音频"_es, status);
        property.mSelector = kAudioTapPropertyFormat; size = sizeof(format);
        status = AudioObjectGetPropertyData(tap, &property, 0, nullptr, &size, &format);
        if (status != noErr || format.mFormatID != kAudioFormatLinearPCM || !(format.mFormatFlags & kAudioFormatFlagIsFloat)
            || (format.mBitsPerChannel != 32 && format.mBitsPerChannel != 64)) return failure(u8"系统返回了不支持的音频格式"_es, status);
        NSDictionary* aggregateDescription = @{
            @kAudioAggregateDeviceNameKey: @"EnjoySync Private Capture",
            @kAudioAggregateDeviceUIDKey: NSUUID.UUID.UUIDString,
            @kAudioAggregateDeviceIsPrivateKey: @YES,
            @kAudioAggregateDeviceTapAutoStartKey: @YES,
            @kAudioAggregateDeviceTapListKey: @[@{
                @kAudioSubTapUIDKey: description.UUID.UUIDString,
                @kAudioSubTapDriftCompensationKey: @YES
            }]
        };
        status = AudioHardwareCreateAggregateDevice((__bridge CFDictionaryRef)aggregateDescription, &aggregate);
        if (status != noErr) return failure(u8"无法创建系统音频采集会话"_es, status);
        engine->configureCapture(format.mSampleRate); engine->setCapturing(true);
        status = AudioDeviceCreateIOProcID(aggregate, io, this, &proc);
        if (status == noErr) status = AudioDeviceStart(aggregate, proc);
        if (status != noErr) return failure(u8"无法启动系统音频采集，请检查系统音频录制权限"_es, status);
        active = true; return juce::Result::ok();
    }
    void stop() override {
        active = false;
        if (aggregate != kAudioObjectUnknown && proc) { AudioDeviceStop(aggregate, proc); AudioDeviceDestroyIOProcID(aggregate, proc); }
        proc = nullptr;
        if (aggregate != kAudioObjectUnknown) AudioHardwareDestroyAggregateDevice(aggregate);
        aggregate = kAudioObjectUnknown;
        if (tap != kAudioObjectUnknown) AudioHardwareDestroyProcessTap(tap);
        tap = kAudioObjectUnknown;
        if (engine) engine->setCapturing(false); engine = nullptr;
    }
    bool running() const override { return active; }
    juce::String error() const override { return lastError; }
private:
    juce::Result failure(const juce::String& text, OSStatus code) {
        lastError = text + u8"（"_es + juce::String((int)code) + u8"）"_es; stop(); return juce::Result::fail(lastError);
    }
    static OSStatus io(AudioObjectID, const AudioTimeStamp*, const AudioBufferList* input,
                       const AudioTimeStamp*, AudioBufferList*, const AudioTimeStamp*, void* user) {
        auto& self = *static_cast<MacCapture*>(user);
        if (!self.engine || !input || input->mNumberBuffers == 0) return noErr;
        const auto& first = input->mBuffers[0]; if (!first.mData || first.mNumberChannels == 0) return noErr;
        int bytesPerSample = (int)self.format.mBitsPerChannel / 8;
        int frames = (int)first.mDataByteSize / ((int)first.mNumberChannels * bytesPerSample);
        auto time = aoo_getCurrentNtpTime();
        for (int offset = 0; offset < frames; offset += 8192) {
            int count = std::min(8192, frames - offset);
            for (int channel = 0; channel < 2; ++channel) {
                auto& buffer = input->mBuffers[input->mNumberBuffers > 1 ? std::min((UInt32)channel, input->mNumberBuffers - 1) : 0];
                int stride = (int)buffer.mNumberChannels;
                int index = input->mNumberBuffers > 1 ? 0 : std::min(channel, stride - 1);
                auto* destination = channel == 0 ? self.left.data() : self.right.data();
                for (int i = 0; i < count; ++i) {
                    const int sample = (offset + i) * stride + index;
                    if (!buffer.mData || (size_t)(sample + 1) * bytesPerSample > buffer.mDataByteSize) destination[i] = 0;
                    else destination[i] = bytesPerSample == 4 ? static_cast<const float*>(buffer.mData)[sample]
                                                             : (float)static_cast<const double*>(buffer.mData)[sample];
                }
            }
            self.engine->pushCapture(self.left.data(), self.right.data(), count,
                                     time + aoo_ntpTimeFromSeconds(offset / self.format.mSampleRate));
        }
        return noErr;
    }
    AudioEngine* engine = nullptr;
    AudioObjectID tap = kAudioObjectUnknown, aggregate = kAudioObjectUnknown;
    AudioDeviceIOProcID proc = nullptr;
    AudioStreamBasicDescription format {};
    std::array<float, 8192> left {}, right {};
    bool active = false;
    juce::String lastError;
};
std::unique_ptr<SystemCapture> createSystemCapture() { return std::make_unique<MacCapture>(); }
}
