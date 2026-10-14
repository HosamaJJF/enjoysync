#pragma once
#include "audio/AudioEngine.h"
#include <memory>
namespace enjoysync {
class SystemCapture {
public:
    virtual ~SystemCapture() = default;
    virtual juce::Result start(AudioEngine&) = 0;
    virtual void stop() = 0;
    virtual bool running() const = 0;
    virtual juce::String error() const = 0;
};
std::unique_ptr<SystemCapture> createSystemCapture();
void setDockVisible(bool);
void setApplicationTheme(const juce::String& mode);
void beginAudioActivity();
void endAudioActivity();
}
