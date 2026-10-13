#pragma once
#include "SecureChannel.h"
#include "audio/AudioEngine.h"
#include <condition_variable>
#include <thread>
#include <mutex>
#include <functional>
#include <set>

namespace enjoysync {
enum class ConnectionState { offline, resolving, connecting, connected, retrying, error };
struct PeerSnapshot {
    PeerSettings settings;
    ConnectionState state = ConnectionState::offline;
    juce::String detail;
    int slot = -1;
    double rttMs = 0;
    bool remoteSharing = false;
};
class Transport {
public:
    Transport(AudioEngine&, Settings);
    ~Transport();
    juce::Result start();
    void stop(bool graceful = true);
    void setLocalPreferences(const Settings&);
    void addOrConnect(PeerSettings);
    void connect(const juce::String& recordId);
    void disconnect(const juce::String& recordId);
    void remove(const juce::String& recordId);
    void updatePeer(PeerSettings);
    std::vector<PeerSnapshot> snapshot() const;
    juce::String listenerDescription() const;
    bool anySending() const;
    void setCaptureActive(bool active) { captureActive = active; }
    std::atomic<uint64_t> mediaSent { 0 }, mediaReceived { 0 };
private:
    struct Record;
    struct Session;
    struct ResolveJob { juce::String id, host; int port; unsigned generation; };
    struct ResolveResult { juce::String id; unsigned generation; std::vector<Address> addresses; };
    void post(std::function<void()>);
    void run();
    void resolveLoop();
    void beginConnect(Record&);
    void tryAddress(Record&);
    void fail(Record&, const juce::String&);
    Record* find(const juce::String&);
    Record* newRecord(PeerSettings);
    void process(Session&, const void*, int);
    void hello(Session&);
    void control(Session&, const juce::var&);
    void detach(Session&);
    void publish();
    static AooInt32 AOO_CALL sendAudio(void*, const AooByte*, AooInt32, const void*, AooAddrSize, AooFlag);
    AudioEngine& audio;
    Settings local;
    UdpSocket socket;
    std::atomic<bool> running { false }, gracefulStop { true }, sending { false };
    std::atomic<bool> captureActive { false };
    std::thread networkThread, resolverThread;
    mutable std::mutex mutex;
    std::vector<std::function<void()>> commands;
    std::vector<PeerSnapshot> published;
    juce::String listening;
    std::vector<std::unique_ptr<Record>> records;
    std::vector<std::unique_ptr<Session>> sessions;
    std::set<juce::String> pausedDeviceIds;
    std::mutex resolverMutex;
    std::condition_variable resolverWake;
    bool resolverRunning = false;
    std::deque<ResolveJob> jobs;
    std::vector<ResolveResult> results;
};
juce::String stateName(ConnectionState);
}
