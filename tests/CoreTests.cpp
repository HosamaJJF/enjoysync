#include "core/Settings.h"
#include "net/Transport.h"
#include <iostream>
#include <stdexcept>
#include <cmath>
#include <chrono>
#include <thread>
using namespace enjoysync;
static void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
template<class Predicate> static bool waitFor(Predicate predicate, int timeoutMs = 4000) {
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(timeoutMs)) {
        if (predicate()) return true; std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return predicate();
}
static bool connected(Transport& t) { auto p = t.snapshot(); return p.size() == 1 && p[0].state == ConnectionState::connected; }
static PeerSettings peerFor(const Settings& target, juce::String host = "127.0.0.1") {
    PeerSettings p; p.host = host; p.port = target.port; p.key = target.key; p.send = true; return p;
}
struct Peaks { float a = 0, b = 0; };
class AudioPump {
public:
    AudioPump(AudioEngine& a, AudioEngine& b) : engineA(a), engineB(b), worker([this, &a, &b] {
        std::array<float, 480> tone {}, al {}, ar {}, bl {}, br {};
        std::array<Peaks, 20> history {}; int cursor = 0; double phase = 0;
        auto next = std::chrono::steady_clock::now();
        while (running) {
            std::unique_lock lock(controlMutex);
            for (auto& sample : tone) { sample = 0.2f * (float)std::sin(phase); phase += 2 * 3.141592653589793 * 440 / 48000; }
            auto time = aoo_getCurrentNtpTime();
            a.pushCapture(tone.data(), tone.data(), sourceAFrames, time); b.pushCapture(tone.data(), tone.data(), 480, time);
            a.render(al.data(), ar.data(), 480, time); b.render(bl.data(), br.data(), outputBFrames, time);
            Peaks current;
            for (int i = 0; i < 480; ++i) current.a = std::max(current.a, std::abs(al[(size_t)i]));
            for (int i = 0; i < outputBFrames; ++i) current.b = std::max(current.b, std::abs(bl[(size_t)i]));
            history[(size_t)(cursor++ % 20)] = current; Peaks peak;
            for (const auto& frame : history) { peak.a = std::max(peak.a, frame.a); peak.b = std::max(peak.b, frame.b); }
            peakA = peak.a; peakB = peak.b;
            lock.unlock();
            next += std::chrono::milliseconds(10); std::this_thread::sleep_until(next);
        }
    }) {}
    ~AudioPump() { running = false; worker.join(); }
    Peaks measure(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); return {peakA.load(), peakB.load()}; }
    void use44100() {
        std::lock_guard lock(controlMutex); sourceAFrames = outputBFrames = 441;
        engineA.configureCapture(44100); engineB.configureOutput(44100, 441);
    }
private:
    AudioEngine& engineA;
    AudioEngine& engineB;
    std::mutex controlMutex;
    int sourceAFrames = 480, outputBFrames = 480;
    std::atomic<bool> running {true};
    std::atomic<float> peakA {0}, peakB {0};
    std::thread worker;
};
static void settingsTests() {
    check(validKey("0123456789abcdef0123456789abcdef"), "valid pairing key rejected");
    check(!validKey("password"), "weak/invalid pairing key accepted");
    check(normalizeHost(" [FE80::1%en0] ") == "fe80::1%en0", "IPv6 scope parsing");
    PeerSettings a, b; a.host = "Studio.EXAMPLE."; b.host = "studio.example";
    check(sameEndpoint(a, b), "DNS aliases differing in case/trailing dot not normalized");
    ++b.port; check(!sameEndpoint(a, b), "different ports incorrectly merged");
    Settings s; a.key = s.key; s.peers.push_back(a); s.peers.push_back(a);
    auto restored = Settings::fromJson(s.toJson());
    check(restored.peers.size() == 1, "duplicate saved records not merged");
    check(restored.deviceId == s.deviceId && restored.key == s.key, "identity not persisted");
    auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("enjoysync-settings", ".json");
    check(s.save(file).wasOk(), "atomic settings save failed");
    check(Settings::load(file).deviceId == s.deviceId, "settings reload failed"); file.deleteFile();
    check(retryDelayMs(0) == 1000 && retryDelayMs(100) <= 32000, "retry backoff unbounded");
    std::cout << "PASS settings, endpoint normalization and bounded backoff\n";
}
static void sourceOverflowTests() {
    AudioEngine engine;
    engine.setCodec(true); engine.configureCapture(44100); engine.setCapturing(true);
    Address endpoint; endpoint.size = sizeof(sockaddr_in);
    auto* address = reinterpret_cast<sockaddr_in*>(&endpoint.storage);
    address->sin_family = AF_INET; address->sin_port = htons(51000);
    engine.setSending(endpoint, 1, true);
    std::array<float, 441> tone; tone.fill(0.1f);
    bool overflowed = false;
    // A stalled sender fills both the source queue and the non-bypass resampler.
    for (int i = 0; i < 32; ++i) {
        engine.pushCapture(tone.data(), tone.data(), 441, aoo_getCurrentNtpTime());
        overflowed |= engine.captureResult.load() == kAooErrorOverflow;
    }
    check(overflowed, "source overflow regression did not fill the buffer");
    auto discard = [](void*, const AooByte*, AooInt32 size, const void*, AooAddrSize, AooFlag) -> AooInt32 { return size; };
    engine.pump(discard, nullptr);
    engine.pushCapture(tone.data(), tone.data(), 441, aoo_getCurrentNtpTime());
    check(engine.captureResult.load() == kAooOk, "44.1 kHz source remained stuck after send buffer overflow");
    engine.pump(discard, nullptr);
    std::cout << "PASS 44.1 kHz source recovers from sender buffer overflow\n";
}
static void transportTests() {
    Settings a, b, c;
    int base = 53000 + juce::Random::getSystemRandom().nextInt(6000);
    a.port = base; b.port = base + 1; c.port = base + 2;
    AudioEngine aa, ab, ac;
    aa.setCapturing(true); ab.setCapturing(true);
    Transport ta(aa, a), tb(ab, b), tc(ac, c);
    check(ta.start().wasOk() && tb.start().wasOk() && tc.start().wasOk(), "test listeners unavailable");
    AudioPump pump(aa, ab);
    auto peer = peerFor(b); ta.addOrConnect(peer); ta.addOrConnect(peer);
    auto paired = waitFor([&] { return connected(ta) && connected(tb); });
    if (!paired) for (auto* t : {&ta, &tb}) for (auto& p : t->snapshot())
        std::cerr << stateName(p.state) << ": " << p.detail << "\n";
    check(paired, "IPv4 DTLS pairing failed");
    check(ta.snapshot().size() == 1 && tb.snapshot().size() == 1, "repeat connect created duplicate devices");
    auto peak = pump.measure(1300);
    if (peak.b <= 0.02f) std::cerr << "audio: input=" << aa.inputLevel() << " source=" << aa.captureResult
        << " receiver=" << ab.receiveResult << " level=" << ab.level(0) << " underruns=" << ab.underruns(0)
        << " buffer=" << ab.latencyMs(0) << " format=" << aa.formatResult << " attach=" << aa.attachResult
        << " sending=" << ta.anySending() << " sent=" << ta.mediaSent << " received=" << tb.mediaReceived << "\n";
    check(peak.b > 0.02f, "Opus audio not decoded through encrypted IPv4 transport");
    std::cout << "PASS IPv4 DTLS pairing, duplicate connect, Opus audio; output peak=" << peak.b << "\n";
    auto pb = tb.snapshot()[0].settings; pb.send = true; tb.updatePeer(pb);
    peak = pump.measure(1300);
    check(peak.a > 0.02f && peak.b > 0.02f, "bidirectional audio failed");
    pb.muted = true; tb.updatePeer(pb); peak = pump.measure(400);
    check(peak.b < 0.0001f && peak.a > 0.02f, "per-device mute broke independent directions");
    pb.muted = false; tb.updatePeer(pb);
    aa.setCodec(true); peak = pump.measure(1300);
    if (peak.b <= 0.02f) std::cerr << "PCM: source=" << aa.captureResult << " format=" << aa.formatResult
        << " receiver=" << ab.receiveResult << " sent=" << ta.mediaSent << " received=" << tb.mediaReceived
        << " underruns=" << ab.underruns(0) << " buffer=" << ab.latencyMs(0) << "\n";
    check(peak.b > 0.02f, "PCM codec switch failed");
    std::cout << "PASS bidirectional audio, per-device mute and PCM format change\n";
    pump.use44100(); peak = pump.measure(1000);
    check(peak.b > 0.02f, "44.1 kHz capture/output resampling failed");
    std::cout << "PASS 44.1 kHz capture and output around a 48 kHz network stream\n";
    auto alias = peerFor(b, "localhost"); ta.addOrConnect(alias);
    check(waitFor([&] { auto p = ta.snapshot(); return p.size() == 1 && p[0].state == ConnectionState::connected; }, 5000), "hostname alias did not merge");
    auto wrong = peerFor(b); wrong.key = juce::Uuid().toString().removeCharacters("-"); tc.addOrConnect(wrong);
    std::this_thread::sleep_for(std::chrono::milliseconds(3500));
    check(!connected(tc), "incorrect pairing key authenticated"); tc.stop();
    check(ta.snapshot().size() == 1 && tb.snapshot().size() == 1, "hostname alias left a ghost device");
    check(ta.snapshot()[0].settings.host == "localhost", "successful address update was not remembered");
    std::cout << "PASS hostname identity merge and wrong-key rejection\n";
    ab.setCapturing(false); tb.stop(false);
    check(waitFor([&] { return !connected(ta); }, 6500), "crashed peer not detected within heartbeat deadline");
    check(tb.start().wasOk(), "listener restart failed");
    ab.configureCapture(48000); ab.setCapturing(true);
    check(waitFor([&] { return connected(ta) && connected(tb); }, 6000), "automatic reconnection after peer restart failed");
    peak = pump.measure(800);
    if (peak.b <= 0.02f) std::cerr << "recovery: source=" << aa.captureResult << " receiver=" << ab.receiveResult
        << " attached=" << aa.attachResult << " tx=" << ta.mediaSent << " rx=" << tb.mediaReceived
        << " buffer=" << ab.latencyMs(0) << " underruns=" << ab.underruns(0) << "\n";
    check(peak.b > 0.02f, "audio did not recover after reconnect");
    std::cout << "PASS crash detection, same-port restart and automatic audio recovery\n";
    ta.disconnect(ta.snapshot()[0].settings.recordId);
    check(waitFor([&] { return ta.snapshot()[0].state == ConnectionState::offline; }), "manual disconnect failed");
    std::this_thread::sleep_for(std::chrono::milliseconds(1600));
    check(ta.snapshot()[0].state == ConnectionState::offline, "manual disconnect unexpectedly reconnected");
    ta.addOrConnect(peerFor(b, "::1"));
    check(waitFor([&] { return connected(ta) && connected(tb) && ta.snapshot()[0].detail.contains("::1"); }, 5000), "IPv6 pairing failed");
    peak = pump.measure(1000); check(peak.b > 0.02f, "encrypted IPv6 PCM audio failed");
    ab.setCapturing(false); tb.stop(); check(waitFor([&] { return !connected(ta); }, 1500), "graceful BYE not detected promptly");
    aa.setCapturing(false);
    ta.stop();
    std::cout << "PASS manual disconnect, IPv6 audio and graceful close\n";
}
static void simultaneousTests() {
    Settings a, b; a.port = 60000 + juce::Random::getSystemRandom().nextInt(2000); b.port = a.port + 1;
    AudioEngine aa, ab; Transport ta(aa, a), tb(ab, b);
    check(ta.start().wasOk() && tb.start().wasOk(), "simultaneous test listener failed");
    ta.addOrConnect(peerFor(b)); tb.addOrConnect(peerFor(a));
    check(waitFor([&] { return connected(ta) && connected(tb); }, 6000), "simultaneous dialing did not converge");
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    check(connected(ta) && connected(tb), "simultaneous dialing produced an unstable session");
    std::cout << "PASS simultaneous dialing converges to one device per side\n";
    auto remembered = ta.snapshot()[0].settings; remembered.autoConnect = true;
    a.peers = {remembered}; tb.disconnect(tb.snapshot()[0].settings.recordId);
    check(waitFor([&] { return tb.snapshot()[0].state == ConnectionState::offline; }), "startup test cleanup failed");
    std::this_thread::sleep_for(std::chrono::milliseconds(1600));
    check(ta.snapshot()[0].state == ConnectionState::offline && tb.snapshot()[0].state == ConnectionState::offline,
          "peer auto-reconnect overrode an explicit manual disconnect");
    ta.stop(); tb.stop();
    Transport restored(aa, Settings::fromJson(a.toJson()));
    check(tb.start().wasOk() && restored.start().wasOk(), "startup listener failed");
    check(waitFor([&] { return connected(restored) && connected(tb); }), "saved automatic startup connection failed");
    std::cout << "PASS persisted device automatically connects on startup\n";
}
int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    aoo_initialize(nullptr);
    int result = 0;
    try {
        if (argc > 1 && juce::String(argv[1]) == "--simultaneous") {
            for (int i = 0; i < 8; ++i) simultaneousTests();
    } else { settingsTests(); sourceOverflowTests(); transportTests(); simultaneousTests(); }
    }
    catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << "\n"; result = 1; }
    aoo_terminate(); return result;
}
