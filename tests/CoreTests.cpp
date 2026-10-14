#include "core/Settings.h"
#include "net/Transport.h"
#include <iostream>
#include <stdexcept>
#include <cmath>
#include <chrono>
#include <thread>
#include <utility>
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
static int availableTestPorts(int count) {
    // Stay below the default ephemeral range used by browsers and other apps,
    // and probe both IP families (including Windows excluded port ranges).
    for (int attempt = 0; attempt < 32; ++attempt) {
        const int base = 20000 + juce::Random::getSystemRandom().nextInt(16000);
        std::vector<std::unique_ptr<UdpSocket>> probes;
        for (int i = 0; i < count; ++i) {
            auto socket = std::make_unique<UdpSocket>();
            if (socket->bind(base + i).failed()) break;
            probes.push_back(std::move(socket));
        }
        if ((int)probes.size() == count) return base;
    }
    throw std::runtime_error("no free UDP test port group available");
}
struct Peaks { float a = 0, b = 0; };
class AudioPump {
public:
    AudioPump(AudioEngine& a, AudioEngine& b, int initialStallMs = 0, int periodicStallMs = 0)
        : engineA(a), engineB(b), worker([this, &a, &b, initialStallMs, periodicStallMs] {
        std::array<float, 480> tone {}, al {}, ar {}, bl {}, br {};
        std::array<Peaks, 20> history {}; int cursor = 0; double phase = 0;
        auto lastCallback = std::chrono::steady_clock::now() - std::chrono::milliseconds(10);
        std::this_thread::sleep_for(std::chrono::milliseconds(initialStallMs));
        // Simulate an audio device clock independently of the runner's scheduling.
        auto sampleTime = aoo_getCurrentNtpTime();
        while (running) {
            const auto callbackTime = std::chrono::steady_clock::now();
            if (callbackTime - lastCallback < std::chrono::milliseconds(5)) ++catchUpCallbacks;
            lastCallback = callbackTime;
            std::unique_lock lock(controlMutex);
            for (auto& sample : tone) { sample = 0.2f * (float)std::sin(phase); phase += 2 * 3.141592653589793 * 440 / 48000; }
            const auto time = sampleTime;
            sampleTime += aoo_ntpTimeFromSeconds(0.01);
            a.pushCapture(tone.data(), tone.data(), sourceAFrames, time); b.pushCapture(tone.data(), tone.data(), 480, time);
            a.render(al.data(), ar.data(), 480, time); b.render(bl.data(), br.data(), outputBFrames, time);
            Peaks current;
            for (int i = 0; i < 480; ++i) current.a = std::max(current.a, std::abs(al[(size_t)i]));
            for (int i = 0; i < outputBFrames; ++i) current.b = std::max(current.b, std::abs(bl[(size_t)i]));
            history[(size_t)(cursor++ % 20)] = current; Peaks peak;
            for (const auto& frame : history) { peak.a = std::max(peak.a, frame.a); peak.b = std::max(peak.b, frame.b); }
            peakA = peak.a; peakB = peak.b;
            lock.unlock();
            if (periodicStallMs > 0 && cursor % 8 == 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(periodicStallMs));
            // A relative wait also prevents short catch-up intervals after a late wakeup.
            // Sample timestamps above still advance by exactly one audio block.
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }) {}
    ~AudioPump() { running = false; worker.join(); }
    Peaks measure(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); return {peakA.load(), peakB.load()}; }
    Peaks waitForOutput(int timeoutMs = 4000) {
        waitFor([&] { return peakB.load() > 0.02f; }, timeoutMs);
        return {peakA.load(), peakB.load()};
    }
    unsigned catchUpCount() const { return catchUpCallbacks.load(); }
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
    std::atomic<unsigned> catchUpCallbacks {0};
    std::thread worker;
};
static void audioStartupTests() {
    for (auto delays : {std::pair{1700, 0}, {0, 80}}) {
        Settings a, b; a.port = availableTestPorts(2); b.port = a.port + 1;
        AudioEngine aa, ab; aa.setCapturing(true); ab.setCapturing(true);
        Transport ta(aa, a), tb(ab, b);
        check(ta.start().wasOk() && tb.start().wasOk(), "audio startup listeners unavailable");
        AudioPump pump(aa, ab, delays.first, delays.second);
        ta.addOrConnect(peerFor(b));
        check(waitFor([&] { return connected(ta) && connected(tb); }), "audio startup pairing failed");
        check(pump.waitForOutput().b > 0.02f, "Opus audio failed to start after a delayed synthetic callback");
        auto peak = pump.measure(600);
        std::cout << "startup stall_ms=" << delays.first << " periodic_stall_ms=" << delays.second
                  << " peak=" << peak.b << " sent=" << ta.mediaSent
                  << " received=" << tb.mediaReceived << " underruns=" << ab.underruns(0) << std::endl;
        check(peak.b > 0.02f, "Opus audio failed after a delayed synthetic callback");
        check(pump.catchUpCount() == 0, "delayed synthetic callbacks were replayed in a burst");
    }
}
static void settingsTests() {
    check(validKey("0123456789abcdef0123456789abcdef"), "valid pairing key rejected");
    check(!validKey("password"), "weak/invalid pairing key accepted");
    check(normalizeHost(" [FE80::1%en0] ") == "fe80::1%en0", "IPv6 scope parsing");
    struct EndpointCase { const char* input; int port; const char* host; int expectedPort; };
    for (const auto& item : std::initializer_list<EndpointCase> {
        {"192.168.101.108:51000", 51000, "192.168.101.108", 51000},
        {" 192.168.101.106:52000 ", 51000, "192.168.101.106", 52000},
        {"Studio.EXAMPLE.:52001", 51000, "studio.example", 52001},
        {"[FE80::1%en0]:52002", 51000, "fe80::1%en0", 52002},
        {"[::1]", 51000, "::1", 51000},
        {"2001:db8::20", 51000, "2001:db8::20", 51000},
        {"2001:db8::5100", 51000, "2001:db8::5100", 51000}}) {
        juce::String host = item.input; int port = item.port;
        check(normalizeEndpoint(host, port).wasOk() && host == item.host && port == item.expectedPort,
              "endpoint input was not split/canonicalized correctly");
    }
    for (auto input : {"", ":51000", "host:", "host:0", "host:65536", "host:abc", "host:999999999999",
                       "[::1", "[::1]extra", "[::1]:", "[::1]:-1", "[host]:51000", "https://host", "host name"}) {
        juce::String host = input; int port = 51000;
        check(normalizeEndpoint(host, port).failed(), "malformed endpoint accepted");
    }
    PeerSettings a, b; a.host = "Studio.EXAMPLE."; b.host = "studio.example";
    check(sameEndpoint(a, b), "DNS aliases differing in case/trailing dot not normalized");
    ++b.port; check(!sameEndpoint(a, b), "different ports incorrectly merged");
    Settings s; a.key = s.key; s.peers.push_back(a); s.peers.push_back(a);
    auto restored = Settings::fromJson(s.toJson());
    check(restored.peers.size() == 1, "duplicate saved records not merged");
    check(restored.deviceId == s.deviceId && restored.key == s.key, "identity not persisted");
    check(restored.theme == "system", "older settings should follow system appearance");
    for (auto mode : {"light", "dark", "system"}) {
        s.theme = mode; check(Settings::fromJson(s.toJson()).theme == mode, "appearance preference not persisted");
    }
    auto invalidTheme = s.toJson(); invalidTheme.getDynamicObject()->setProperty("theme", "unknown");
    check(Settings::fromJson(invalidTheme).theme == "system", "unknown appearance should fall back to system");
    invalidTheme.getDynamicObject()->removeProperty("theme");
    check(Settings::fromJson(invalidTheme).theme == "system", "missing appearance should follow system");
    PeerSettings pasted, separate; pasted.host = "192.168.101.108:52000"; pasted.key = s.key;
    separate = pasted; separate.host = "192.168.101.108"; separate.port = 52000;
    check(sameEndpoint(pasted, separate), "pasted endpoint not deduplicated against separate fields");
    s.peers = {pasted, separate}; restored = Settings::fromJson(s.toJson());
    check(restored.peers.size() == 1 && restored.peers[0].host == separate.host && restored.peers[0].port == 52000,
          "legacy pasted endpoint was not repaired on load");
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
    int base = availableTestPorts(3);
    a.port = base; b.port = base + 1; c.port = base + 2;
    AudioEngine aa, ab, ac;
    aa.setCapturing(true); ab.setCapturing(true);
    Transport ta(aa, a), tb(ab, b), tc(ac, c);
    check(ta.start().wasOk() && tb.start().wasOk() && tc.start().wasOk(), "test listeners unavailable");
    AudioPump pump(aa, ab);
    // Reproduce the screenshots through the same parser used by the connection form.
    auto peer = peerFor(b); peer.host += ":" + juce::String(b.port); peer.port = 51000;
    check(normalizeEndpoint(peer.host, peer.port).wasOk(), "pasted IPv4 endpoint rejected");
    ta.addOrConnect(peer); ta.addOrConnect(peer);
    auto paired = waitFor([&] { return connected(ta) && connected(tb); });
    if (!paired) for (auto* t : {&ta, &tb}) for (auto& p : t->snapshot())
        std::cerr << stateName(p.state) << ": " << p.detail << "\n";
    check(paired, "IPv4 DTLS pairing failed");
    check(ta.snapshot().size() == 1 && tb.snapshot().size() == 1, "repeat connect created duplicate devices");
    auto peak = pump.waitForOutput();
    if (peak.b <= 0.02f) std::cerr << "audio: input=" << aa.inputLevel() << " source=" << aa.captureResult
        << " receiver=" << ab.receiveResult << " level=" << ab.level(0) << " underruns=" << ab.underruns(0)
        << " buffer=" << ab.latencyMs(0) << " format=" << aa.formatResult << " attach=" << aa.attachResult
        << " sending=" << ta.anySending() << " sent=" << ta.mediaSent << " received=" << tb.mediaReceived
        << " catch_up_callbacks=" << pump.catchUpCount() << "\n";
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
    auto ipv6Peer = peerFor(b, "[::1]:" + juce::String(b.port)); ipv6Peer.port = 51000;
    check(normalizeEndpoint(ipv6Peer.host, ipv6Peer.port).wasOk(), "pasted bracketed IPv6 endpoint rejected");
    ta.addOrConnect(ipv6Peer);
    check(waitFor([&] { return connected(ta) && connected(tb) && ta.snapshot()[0].detail.contains("::1"); }, 5000), "IPv6 pairing failed");
    peak = pump.measure(1000); check(peak.b > 0.02f, "encrypted IPv6 PCM audio failed");
    ab.setCapturing(false); tb.stop(); check(waitFor([&] { return !connected(ta); }, 1500), "graceful BYE not detected promptly");
    aa.setCapturing(false);
    ta.stop();
    std::cout << "PASS manual disconnect, IPv6 audio and graceful close\n";
}
static void simultaneousTests() {
    Settings a, b; a.port = availableTestPorts(2); b.port = a.port + 1;
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
static void rapidRestartTests() {
    Settings a, b;
    // Force the original client to win the old duplicate-session ID arbitration.
    a.deviceId = "00000000-0000-0000-0000-000000000001";
    b.deviceId = "ffffffff-ffff-ffff-ffff-ffffffffffff";
    a.port = availableTestPorts(2); b.port = a.port + 1;
    AudioEngine aa, ab; aa.setCapturing(true); ab.setCapturing(true);
    Transport ta(aa, a);
    auto tb = std::make_unique<Transport>(ab, b);
    check(ta.start().wasOk() && tb->start().wasOk(), "rapid restart listeners unavailable");
    AudioPump pump(aa, ab);
    ta.addOrConnect(peerFor(b));
    check(waitFor([&] { return connected(ta) && connected(*tb); }), "rapid restart initial pairing failed");
    check(pump.measure(500).b > 0.02f, "rapid restart initial audio failed");
    tb->stop(false); tb.reset();
    auto invitation = peerFor(a); invitation.autoConnect = true;
    b.peers = {invitation};
    tb = std::make_unique<Transport>(ab, b);
    check(tb->start().wasOk(), "rapid restart listener failed");
    check(waitFor([&] { return connected(ta) && connected(*tb); }, 1800),
          "fresh authenticated session was blocked by the old same-endpoint session");
    check(pump.measure(600).b > 0.02f, "audio failed after rapid same-endpoint replacement");
    std::cout << "PASS same-endpoint restart replaces stale DTLS session without waiting for heartbeat timeout\n";
}
class DatagramRelay {
public:
    DatagramRelay(const Settings& a, const Settings& b, int port) {
        check(socket.bind(port).wasOk(), "UDP impairment relay listener unavailable");
        aAddress = resolve("127.0.0.1", a.port).at(0); bAddress = resolve("127.0.0.1", b.port).at(0);
        worker = std::thread([this] {
            std::array<unsigned char, 4096> packet {}; Address from;
            while (running) {
                for (int i = 0; i < 256; ++i) {
                    const int size = socket.receive(packet.data(), (int)packet.size(), from);
                    if (size <= 0) break;
                    const bool fromA = from == aAddress;
                    if (!fromA && !(from == bAddress)) continue;
                    const auto sequence = fromA ? ++sequenceA : ++sequenceB;
                    const int interval = dropEvery.load();
                    if (blockBoth || (fromA && blockAToB) || (interval > 0 && sequence % interval == 0)) continue;
                    socket.send(packet.data(), size, fromA ? bAddress : aAddress);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
    }
    ~DatagramRelay() { running = false; worker.join(); }
    std::atomic<bool> blockBoth {false}, blockAToB {false};
    std::atomic<int> dropEvery {0};
private:
    UdpSocket socket;
    Address aAddress, bAddress;
    uint64_t sequenceA = 0, sequenceB = 0;
    std::atomic<bool> running {true};
    std::thread worker;
};
static void impairmentTests() {
    Settings a, b;
    a.deviceId = "00000000-0000-0000-0000-000000000001";
    b.deviceId = "ffffffff-ffff-ffff-ffff-ffffffffffff";
    a.port = availableTestPorts(3); b.port = a.port + 1;
    AudioEngine aa, ab; aa.setCapturing(true); ab.setCapturing(true);
    Transport ta(aa, a), tb(ab, b);
    check(ta.start().wasOk() && tb.start().wasOk(), "impairment test listeners unavailable");
    DatagramRelay relay(a, b, a.port + 2);
    auto peer = peerFor(b); peer.port = a.port + 2;
    AudioPump pump(aa, ab);
    ta.addOrConnect(peer);
    check(waitFor([&] { return connected(ta) && connected(tb); }), "pairing through UDP relay failed");
    check(pump.measure(500).b > 0.02f, "audio through UDP relay failed");
    relay.dropEvery = 13;
    check(!waitFor([&] { return !connected(ta) || !connected(tb); }, 1500), "isolated UDP loss disconnected a healthy session");
    relay.dropEvery = 0; relay.blockBoth = true;
    check(!waitFor([&] { return !connected(ta) || !connected(tb); }, 1200), "short UDP outage disconnected a healthy session");
    relay.blockBoth = false;
    check(pump.measure(800).b > 0.02f, "audio failed to recover from a short UDP outage");
    std::cout << "PASS packet loss and a short bidirectional outage retain the live DTLS session\n";
    // Only B loses incoming traffic. A retains the old session while B retries.
    relay.blockAToB = true;
    check(waitFor([&] { return !connected(tb); }, 6500), "one-way outage did not reach heartbeat timeout");
    check(connected(ta), "healthy reverse path unexpectedly timed out");
    relay.blockAToB = false;
    const auto restoredAt = std::chrono::steady_clock::now();
    check(waitFor([&] { return connected(ta) && connected(tb); }, 2500), "one-way outage recovery remained blocked by a stale session");
    const auto recoveryMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - restoredAt).count();
    check(pump.measure(800).b > 0.02f, "audio failed after one-way outage recovery");
    std::cout << "PASS one-way timeout recovers connection in " << recoveryMs << " ms after the path returns\n";
}
int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    aoo_initialize(nullptr);
    int result = 0;
    try {
        if (argc > 1 && juce::String(argv[1]) == "--simultaneous") {
            for (int i = 0; i < 8; ++i) simultaneousTests();
        } else if (argc > 1 && juce::String(argv[1]) == "--recovery") { rapidRestartTests(); impairmentTests(); }
        else if (argc > 1 && juce::String(argv[1]) == "--audio-startup") { audioStartupTests(); }
        else { settingsTests(); sourceOverflowTests(); audioStartupTests(); transportTests(); simultaneousTests(); rapidRestartTests(); impairmentTests(); }
    }
    catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << "\n"; result = 1; }
    aoo_terminate(); return result;
}
