#pragma once
#include "net/Transport.h"
#include "platform/Capture.h"
#include <juce_gui_basics/juce_gui_basics.h>
namespace enjoysync {
class Controller : private juce::Timer {
public:
    explicit Controller(const juce::File& profileDirectory);
    ~Controller() override;
    Settings settings;
    AudioEngine audio;
    std::unique_ptr<Transport> transport;
    std::vector<PeerSnapshot> peers() const;
    juce::Result addPeer(PeerSettings);
    juce::Result applyPreferences(const juce::String& name, int port, int latency, bool pcm,
                                  bool hideDock, const juce::String& output);
    juce::String error() const { return problem; }
    void setSharingEnabled(bool);
    bool sharingEnabled() const { return allowSharing; }
    bool capturing() const { return capture->running(); }
    void save();
    std::function<void()> changed;
private:
    void timerCallback() override;
    juce::File settingsFile;
    std::unique_ptr<SystemCapture> capture;
    bool allowSharing = true, captureFailed = false, activeActivity = false;
    juce::String problem, lastSaved;
    int saveTicks = 0;
};
}
