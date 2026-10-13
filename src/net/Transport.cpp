#include "Transport.h"
#include <algorithm>
#include <chrono>
#include <iostream>
namespace enjoysync {
static double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }
static juce::var message(const char* type) {
    auto* object = new juce::DynamicObject; object->setProperty("type", type); return juce::var(object);
}
juce::String stateName(ConnectionState state) {
    switch (state) {
        case ConnectionState::resolving: return u8"解析地址"_es;
        case ConnectionState::connecting: return u8"安全连接中"_es;
        case ConnectionState::connected: return u8"已连接"_es;
        case ConnectionState::retrying: return u8"等待重连"_es;
        case ConnectionState::error: return u8"连接失败"_es;
        default: return u8"离线"_es;
    }
}
struct Transport::Record {
    PeerSnapshot view;
    Session* session = nullptr;
    std::vector<Address> candidates;
    size_t candidate = 0;
    unsigned generation = 0, attempts = 0;
    bool wanted = false;
    double nextTry = 0;
};
struct Transport::Session {
    std::unique_ptr<SecureChannel> channel;
    juce::String recordId;
    int remoteSink = 0;
    bool dead = false, attached = false;
    double created = nowMs(), lastReceive = created, lastHello = 0, lastPing = 0, lastMedia = 0;
    Session(UdpSocket& socket, const Address& address, const juce::String& key, bool client)
        : channel(std::make_unique<SecureChannel>(socket, address, key, client)) {}
};
Transport::Transport(AudioEngine& engine, Settings settings) : audio(engine), local(std::move(settings)) {
    for (const auto& p : local.peers) if (auto* r = newRecord(p)) r->wanted = p.autoConnect;
    publish();
}
Transport::~Transport() { stop(); }
Transport::Record* Transport::newRecord(PeerSettings p) {
    if ((int)records.size() >= maxPeers) return nullptr;
    auto r = std::make_unique<Record>(); r->view.settings = std::move(p);
    for (int slot = 0; slot < maxPeers; ++slot) {
        if (std::none_of(records.begin(), records.end(), [&](auto& existing) { return existing->view.slot == slot; })) { r->view.slot = slot; break; }
    }
    auto* ptr = r.get(); records.push_back(std::move(r)); return ptr;
}
Transport::Record* Transport::find(const juce::String& id) {
    for (auto& r : records) if (r->view.settings.recordId == id) return r.get();
    return nullptr;
}
juce::Result Transport::start() {
    if (running) return juce::Result::ok();
    auto result = socket.bind(local.port); if (result.failed()) return result;
    {
        std::lock_guard lock(mutex);
        listening = "UDP " + juce::String(local.port) + u8" · "_es + (socket.hasIPv4() ? "IPv4 " : "")
                  + (socket.hasIPv6() ? "IPv6" : "") + u8" · DTLS 加密"_es;
    }
    gracefulStop = true; running = true; resolverRunning = true;
    pausedDeviceIds.clear();
    resolverThread = std::thread([this] { resolveLoop(); });
    networkThread = std::thread([this] { run(); }); return juce::Result::ok();
}
void Transport::stop(bool graceful) {
    gracefulStop = graceful; running = false;
    if (networkThread.joinable()) networkThread.join();
    { std::lock_guard lock(resolverMutex); resolverRunning = false; jobs.clear(); }
    resolverWake.notify_all(); if (resolverThread.joinable()) resolverThread.join();
    socket.close(); sending = false;
}
void Transport::post(std::function<void()> command) { std::lock_guard lock(mutex); commands.push_back(std::move(command)); }
void Transport::setLocalPreferences(const Settings& settings) {
    post([this, settings] {
        local.name = settings.name; local.bufferMs = settings.bufferMs; local.pcm = settings.pcm;
        for (auto& s : sessions) s->lastHello = 0;
    });
}
void Transport::addOrConnect(PeerSettings p) {
    post([this, p] {
        Record* record = nullptr;
        for (auto& r : records) if (sameEndpoint(r->view.settings, p)
            || (!p.deviceId.isEmpty() && p.deviceId == r->view.settings.deviceId)) { record = r.get(); break; }
        if (!record) record = newRecord(p);
        if (!record) return;
        record->view.settings.key = p.key;
        record->view.settings.autoConnect = p.autoConnect;
        pausedDeviceIds.erase(record->view.settings.deviceId);
        record->wanted = true;
        if (record->view.state != ConnectionState::connected && record->view.state != ConnectionState::connecting
            && record->view.state != ConnectionState::resolving) beginConnect(*record);
    });
}
void Transport::connect(const juce::String& id) {
    post([this, id] { if (auto* r = find(id)) {
        r->wanted = true;
        pausedDeviceIds.erase(r->view.settings.deviceId);
        if (r->view.state != ConnectionState::connected && r->view.state != ConnectionState::connecting
            && r->view.state != ConnectionState::resolving) beginConnect(*r);
    }});
}
void Transport::disconnect(const juce::String& id) {
    post([this, id] { if (auto* r = find(id)) {
        r->wanted = false; ++r->generation;
        if (r->view.settings.deviceId.isNotEmpty()) pausedDeviceIds.insert(r->view.settings.deviceId);
        for (auto& s : sessions) if (s->recordId == id) {
            auto bye = message("bye"); bye.getDynamicObject()->setProperty("reason", "manual");
            control(*s, bye); s->channel->bye(); detach(*s); s->dead = true;
        }
        r->view.state = ConnectionState::offline; r->view.detail = u8"已主动断开"_es;
    }});
}
void Transport::remove(const juce::String& id) {
    disconnect(id); post([this, id] { std::erase_if(records, [&](auto& r) { return r->view.settings.recordId == id; }); });
}
void Transport::updatePeer(PeerSettings p) {
    post([this, p] { if (auto* r = find(p.recordId)) {
        // UI snapshots can lag an authentication/identity update. Only apply user controls.
        r->view.settings.autoConnect = p.autoConnect; r->view.settings.receive = p.receive;
        r->view.settings.send = p.send; r->view.settings.muted = p.muted; r->view.settings.gain = p.gain;
        audio.setMix(r->view.slot, p.gain, p.muted);
        if (r->session) { r->session->lastHello = 0; hello(*r->session); }
    }});
}
void Transport::beginConnect(Record& r) {
    if (!validKey(r.view.settings.key)) { r.view.state = ConnectionState::error; r.view.detail = u8"请输入对方的 32 位连接密钥"_es; r.wanted = false; return; }
    r.view.state = ConnectionState::resolving; r.view.detail = u8"正在解析 "_es + r.view.settings.host;
    ++r.generation; r.candidates.clear(); r.candidate = 0;
    { std::lock_guard lock(resolverMutex); jobs.push_back({r.view.settings.recordId, r.view.settings.host, r.view.settings.port, r.generation}); }
    resolverWake.notify_one();
}
void Transport::resolveLoop() {
    for (;;) {
        ResolveJob job;
        { std::unique_lock lock(resolverMutex); resolverWake.wait(lock, [&] { return !resolverRunning || !jobs.empty(); });
            if (!resolverRunning) return; job = jobs.front(); jobs.pop_front(); }
        auto addresses = resolve(job.host, job.port);
        { std::lock_guard lock(resolverMutex); results.push_back({job.id, job.generation, std::move(addresses)}); }
    }
}
void Transport::tryAddress(Record& r) {
    if (r.candidate >= r.candidates.size()) { fail(r, u8"无法连接：检查地址、UDP 端口、防火墙和连接密钥"_es); return; }
    const auto address = r.candidates[r.candidate++];
    // DNS can complete after an incoming dial has already reached us. Adopt that
    // pending server session instead of opening two DTLS contexts for one endpoint.
    for (auto& existing : sessions) if (!existing->dead && !existing->channel->client
        && existing->channel->address == address && (existing->recordId.isEmpty() || existing->recordId == r.view.settings.recordId)) {
        existing->recordId = r.view.settings.recordId; r.session = existing.get();
        r.view.state = ConnectionState::connecting; r.view.detail = address.key();
        if (existing->channel->ready()) hello(*existing);
        return;
    }
    auto session = std::make_unique<Session>(socket, address, r.view.settings.key, true);
    session->recordId = r.view.settings.recordId; r.session = session.get();
    r.view.state = ConnectionState::connecting; r.view.detail = session->channel->address.key();
    // Generate/send ClientHello before receiving another endpoint's ClientHello,
    // so simultaneous-dial arbitration always has our public handshake random.
    session->channel->handshake();
    sessions.push_back(std::move(session));
}
void Transport::fail(Record& r, const juce::String& detail) {
    r.session = nullptr; r.view.remoteSharing = false; r.view.detail = detail;
    if (r.wanted) {
        r.view.state = ConnectionState::retrying;
        r.nextTry = nowMs() + retryDelayMs(r.attempts++) + juce::Random::getSystemRandom().nextInt(251);
    } else r.view.state = ConnectionState::offline;
}
void Transport::control(Session& session, const juce::var& json) {
    if (!session.channel->ready()) return;
    const auto serialized = juce::JSON::toString(json, true);
    const auto text = serialized.toUTF8();
    const int size = (int)text.sizeInBytes() - 1; if (size > 1050) return;
    std::array<char, 1100> data {}; data[0] = 1; std::memcpy(data.data() + 1, text.getAddress(), (size_t)size);
    session.channel->write(data.data(), size + 1);
}
void Transport::hello(Session& session) {
    auto* r = find(session.recordId); if (!r) return;
    auto json = message("hello"); auto* o = json.getDynamicObject();
    o->setProperty("version", protocolVersion); o->setProperty("id", local.deviceId); o->setProperty("name", local.name);
    o->setProperty("sink", r->view.slot + 1); o->setProperty("port", local.port);
    // A successfully paired device receives a return invitation over the authenticated channel.
    o->setProperty("key", local.key); o->setProperty("receive", r->view.settings.receive);
    o->setProperty("sharing", r->view.settings.send && captureActive.load());
    control(session, json); session.lastHello = nowMs();
}
void Transport::detach(Session& s) {
    if (s.attached) { audio.setSending(s.channel->address, s.remoteSink, false); s.attached = false; }
    if (auto* r = find(s.recordId); r && r->session == &s) {
        audio.activate(r->view.slot, false); r->session = nullptr;
    }
}
void Transport::process(Session& s, const void* bytes, int size) {
    if (size < 2) return;
    auto* data = static_cast<const unsigned char*>(bytes);
    if (data[0] == 2) {
        ++mediaReceived;
        if (auto* r = find(s.recordId); r && r->session == &s) {
            AooMsgType type; AooId streamId;
            if (aoo_parsePattern(data + 1, size - 1, &type, &streamId, nullptr) == kAooOk
                && (r->view.settings.receive || type == kAooMsgTypeSource)) {
                audio.handle(r->view.slot, data + 1, size - 1, s.channel->address); s.lastMedia = nowMs();
            }
        }
        return;
    }
    if (data[0] != 1 || size > 1100) return;
    auto json = juce::JSON::parse(juce::String::fromUTF8(reinterpret_cast<const char*>(data + 1), size - 1));
    if (!json.isObject()) return;
    const auto type = json["type"].toString();
    if (type == "hello" && juce::SystemStats::getEnvironmentVariable("ENJOYSYNC_TRACE", {}).isNotEmpty())
        std::cerr << "HELLO received " << s.channel->address.key() << "\n";
    if (type == "ping") { auto pong = message("pong"); pong.getDynamicObject()->setProperty("stamp", json["stamp"]); control(s, pong); return; }
    if (type == "pong") {
        if (auto* r = find(s.recordId)) { auto rtt = nowMs() - (double)json["stamp"]; if (rtt >= 0 && rtt < 10000) r->view.rttMs = rtt; }
        return;
    }
    if (type == "bye") {
        if (auto* r = find(s.recordId); r && r->session == &s) {
            if (json["reason"].toString() == "manual") {
                r->wanted = false;
            }
            detach(s); fail(*r, u8"对方已断开"_es);
        }
        s.dead = true; return;
    }
    if (type != "hello") return;
    auto id = json["id"].toString(); auto key = json["key"].toString(); int sink = (int)json["sink"];
    if ((int)json["version"] != protocolVersion || juce::Uuid(id).isNull() || id == local.deviceId
        || !validKey(key) || sink < 1 || sink > maxPeers) {
        if (auto* r = find(s.recordId)) { r->wanted = false; detach(s); r->view.state = ConnectionState::error; r->view.detail = u8"协议不兼容或连接到了本机"_es; }
        s.dead = true; return;
    }
    Record* r = find(s.recordId);
    if (r && r->view.settings.deviceId.isNotEmpty() && r->view.settings.deviceId != id) {
        r->wanted = false; detach(s); r->view.state = ConnectionState::error;
        r->view.detail = u8"地址对应的设备身份已变化，请删除后重新配对"_es; s.dead = true; return;
    }
    Record* existing = nullptr;
    for (auto& item : records) if (item->view.settings.deviceId == id) { existing = item.get(); break; }
    if (existing && existing != r) {
        // Retain the canonical record and its playback choices when an alias connects.
        if (r) {
            existing->wanted = existing->wanted || r->wanted;
            existing->view.settings.autoConnect = existing->view.settings.autoConnect || r->view.settings.autoConnect;
            existing->view.settings.host = r->view.settings.host;
            existing->view.settings.port = r->view.settings.port;
            auto obsolete = r->view.settings.recordId;
            std::erase_if(records, [&](auto& item) { return item->view.settings.recordId == obsolete; });
        }
        r = existing;
    }
    if (pausedDeviceIds.contains(id)) {
        if (r && r->wanted) pausedDeviceIds.erase(id);
        else {
            auto bye = message("bye"); bye.getDynamicObject()->setProperty("reason", "manual");
            control(s, bye); s.dead = true; return;
        }
    }
    if (!r) {
        PeerSettings p; p.deviceId = id; p.name = json["name"].toString().substring(0, 80);
        p.host = s.channel->address.host(); p.port = s.channel->address.port(); p.key = key;
        r = newRecord(p); if (!r) { s.dead = true; return; }
    }
    s.recordId = r->view.settings.recordId;
    if (r->session != &s && r->session && !r->session->dead && r->view.state == ConnectionState::connected) {
        // If both sides dial, prefer the connection initiated by the smaller stable ID.
        const bool preferClient = local.deviceId.compare(id) < 0;
        if (r->session->channel->client == preferClient || s.channel->client != preferClient) {
            // Return authenticated identity even when retaining the original session, so
            // the initiator can merge its address alias instead of retrying a ghost record.
            hello(s); s.dead = true; return;
        }
        auto* previous = r->session; detach(*previous); previous->dead = true;
    }
    const bool first = r->session != &s || r->view.state != ConnectionState::connected;
    r->session = &s; r->view.state = ConnectionState::connected; r->attempts = 0;
    r->view.settings.deviceId = id; r->view.settings.name = json["name"].toString().substring(0, 80);
    r->view.settings.key = key; r->view.remoteSharing = (bool)json["sharing"];
    r->view.detail = s.channel->address.key();
    if (first) { audio.activate(r->view.slot, true); audio.setMix(r->view.slot, r->view.settings.gain, r->view.settings.muted); }
    if (s.remoteSink != sink && s.attached) { audio.setSending(s.channel->address, s.remoteSink, false); s.attached = false; }
    s.remoteSink = sink;
    const bool shouldSend = r->view.settings.send && (bool)json["receive"];
    if (shouldSend != s.attached) { audio.setSending(s.channel->address, sink, shouldSend); s.attached = shouldSend; }
    if (first) hello(s);
}
AooInt32 AOO_CALL Transport::sendAudio(void* user, const AooByte* data, AooInt32 size, const void* address, AooAddrSize len, AooFlag) {
    auto& self = *static_cast<Transport*>(user); if (size > 1050 || len > sizeof(sockaddr_storage)) return -1;
    Address remote; remote.size = (socklen_t)len; std::memcpy(&remote.storage, address, len);
    for (auto& s : self.sessions) if (!s->dead && s->channel->ready() && s->channel->address == remote) {
        auto* r = self.find(s->recordId); if (!r || r->session != s.get()) continue;
        std::array<unsigned char, 1100> packet {}; packet[0] = 2; std::memcpy(packet.data() + 1, data, (size_t)size);
        int n = s->channel->write(packet.data(), size + 1); if (n > 0) ++self.mediaSent; return n > 0 ? size : -1;
    }
    return -1;
}
void Transport::publish() {
    std::vector<PeerSnapshot> copy;
    for (auto& r : records) copy.push_back(r->view);
    std::lock_guard lock(mutex); published = std::move(copy);
}
std::vector<PeerSnapshot> Transport::snapshot() const { std::lock_guard lock(mutex); return published; }
juce::String Transport::listenerDescription() const { std::lock_guard lock(mutex); return listening; }
bool Transport::anySending() const { return sending.load(); }
void Transport::run() {
    publish(); double lastPublish = 0;
    std::array<unsigned char, 4096> packet {};
    while (running) {
        std::vector<std::function<void()>> pending;
        { std::lock_guard lock(mutex); pending.swap(commands); }
        for (auto& command : pending) command();
        std::vector<ResolveResult> resolved;
        { std::lock_guard lock(resolverMutex); resolved.swap(results); }
        for (auto& result : resolved) if (auto* r = find(result.id); r && r->generation == result.generation
            && r->view.state == ConnectionState::resolving) {
            r->candidates = std::move(result.addresses); r->candidate = 0;
            if (r->candidates.empty()) fail(*r, u8"域名解析失败："_es + r->view.settings.host); else tryAddress(*r);
        }
        Address from;
        for (int count = 0; count < 256; ++count) {
            int size = socket.receive(packet.data(), (int)packet.size(), from); if (size <= 0) break;
            Session* target = nullptr;
            const bool handshake = packet[0] == 22;
            const bool clientHello = size >= 25 && handshake && packet[13] == 1;
            for (auto& s : sessions) if (!s->dead && s->channel->address == from) {
                if (!target || (handshake && !s->channel->ready())) target = s.get();
            }
            juce::String simultaneousRecord;
            if (clientHello && target && target->channel->client && !target->channel->ready()
                && target->channel->hasClientRandom && size >= 59) {
                // DTLS ClientHello randoms are public and identify these two concurrent dials.
                // Both ends make the same decision without trusting an unauthenticated device ID.
                if (std::lexicographical_compare(target->channel->clientRandom.begin(), target->channel->clientRandom.end(),
                                                 packet.begin() + 27, packet.begin() + 59)) continue;
                simultaneousRecord = target->recordId;
                detach(*target); target->dead = true; target = nullptr;
            }
            if (clientHello && target && target->channel->ready()) target = nullptr;
            if (!target && clientHello && sessions.size() < maxPeers * 2) {
                auto session = std::make_unique<Session>(socket, from, local.key, false);
                session->recordId = simultaneousRecord;
                if (auto* r = find(simultaneousRecord)) r->session = session.get();
                target = session.get(); sessions.push_back(std::move(session));
            }
            if (target) target->channel->enqueue(packet.data(), size);
        }
        double now = nowMs();
        for (size_t index = 0; index < sessions.size(); ++index) {
            auto& s = *sessions[index]; if (s.dead) continue;
            if (!s.channel->ready()) {
                auto result = s.channel->handshake();
                if ((result != 0 && !SecureChannel::wouldBlock(result)) || now - s.created > 3000) {
                    if (juce::SystemStats::getEnvironmentVariable("ENJOYSYNC_TRACE", {}).isNotEmpty())
                        std::cerr << "DTLS " << (s.channel->client ? "client " : "server ") << s.channel->address.key()
                                  << " handshake: " << result << " " << s.channel->error << "\n";
                    detach(s); s.dead = true;
                    if (auto* r = find(s.recordId); r && r->view.state == ConnectionState::connecting) tryAddress(*r);
                    continue;
                }
                if (s.channel->ready()) {
                    if (juce::SystemStats::getEnvironmentVariable("ENJOYSYNC_TRACE", {}).isNotEmpty())
                        std::cerr << "DTLS ready " << (s.channel->client ? "client " : "server ") << s.channel->address.key() << "\n";
                    if (!s.recordId.isEmpty()) hello(s);
                }
            }
            if (!s.channel->ready()) continue;
            for (int count = 0; count < 128; ++count) {
                int size = s.channel->read(packet.data(), (int)packet.size());
                if (size > 0) { s.lastReceive = now; process(s, packet.data(), size); if (s.dead) break; }
                else {
                    if (!SecureChannel::wouldBlock(size) && size != MBEDTLS_ERR_SSL_TIMEOUT) {
                        if (auto* r = find(s.recordId); r && r->session == &s) { detach(s); fail(*r, u8"对方关闭了连接"_es); }
                        s.dead = true;
                    }
                    break;
                }
            }
            if (s.dead) continue;
            if (now - s.lastReceive > 5000 || (s.recordId.isEmpty() && now - s.created > 6000)) {
                if (auto* r = find(s.recordId); r && r->session == &s) { detach(s); fail(*r, u8"心跳超时，设备已离线"_es); }
                s.dead = true; continue;
            }
            if (now - s.lastHello >= 1000) hello(s);
            if (now - s.lastPing >= 1000) {
                auto ping = message("ping"); ping.getDynamicObject()->setProperty("stamp", now); control(s, ping); s.lastPing = now;
            }
        }
        std::erase_if(sessions, [](auto& s) { return s->dead; });
        for (auto& r : records) if (r->wanted && (r->view.state == ConnectionState::offline
            || (r->view.state == ConnectionState::retrying && now >= r->nextTry))) beginConnect(*r);
        bool hasSend = false;
        for (auto& r : records) if (r->view.state == ConnectionState::connected && r->session && r->session->attached) hasSend = true;
        sending = hasSend;
        audio.pump(sendAudio, this);
        if (now - lastPublish >= 100) { publish(); lastPublish = now; }
        std::this_thread::sleep_for(std::chrono::milliseconds(sessions.empty() ? 10 : 2));
    }
    for (auto& s : sessions) { if (gracefulStop) { control(*s, message("bye")); s->channel->bye(); } detach(*s); }
    sessions.clear();
    for (auto& r : records) r->view.state = ConnectionState::offline;
    publish();
}
}
