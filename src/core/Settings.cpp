#include "Settings.h"
#include <algorithm>
#include <cmath>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <stdexcept>
#if !JUCE_WINDOWS
 #include <sys/stat.h>
#endif
namespace enjoysync {
juce::String generateKey() {
    mbedtls_entropy_context entropy; mbedtls_ctr_drbg_context rng;
    mbedtls_entropy_init(&entropy); mbedtls_ctr_drbg_init(&rng);
    const unsigned char purpose[] = "enjoysync-invitation-key";
    unsigned char key[16] {};
    int result = mbedtls_ctr_drbg_seed(&rng, mbedtls_entropy_func, &entropy, purpose, sizeof(purpose) - 1);
    if (result == 0) result = mbedtls_ctr_drbg_random(&rng, key, sizeof(key));
    mbedtls_ctr_drbg_free(&rng); mbedtls_entropy_free(&entropy);
    if (result != 0) throw std::runtime_error("Secure random generator unavailable");
    return juce::String::toHexString(key, sizeof(key), 0);
}
static juce::DynamicObject* object() { return new juce::DynamicObject; }
static juce::var get(const juce::var& v, const char* name, juce::var fallback) {
    return v.hasProperty(name) ? v[name] : fallback;
}
bool validKey(const juce::String& key) {
    return key.length() == 32 && key.containsOnly("0123456789abcdefABCDEF");
}
juce::String normalizeHost(juce::String host) {
    host = host.trim().toLowerCase();
    if (host.startsWithChar('[') && host.endsWithChar(']')) host = host.substring(1, host.length() - 1);
    if (!host.containsChar(':')) host = host.trimCharactersAtEnd(".");
    return host;
}
juce::Result normalizeEndpoint(juce::String& host, int& port) {
    auto value = host.trim();
    if (value.isEmpty() || value.containsAnyOf(" /\\\t\r\n"))
        return juce::Result::fail(u8"请输入对方 IP 或域名，也可填写 IP:端口"_es);
    juce::String hostname = value, portText;
    bool embeddedPort = false;
    if (value.startsWithChar('[')) {
        const auto closing = value.indexOfChar(']');
        if (closing < 0) return juce::Result::fail(u8"IPv6 方括号不完整，例如 [fe80::1%en0]:51000"_es);
        hostname = value.substring(1, closing);
        const auto suffix = value.substring(closing + 1);
        if (!hostname.containsChar(':') || (suffix.isNotEmpty() && !suffix.startsWithChar(':')))
            return juce::Result::fail(u8"地址格式无效，例如 192.168.1.20:51000 或 [::1]:51000"_es);
        embeddedPort = suffix.isNotEmpty(); portText = suffix.substring(1);
    } else if (value.indexOfChar(':') >= 0 && value.indexOfChar(':') == value.lastIndexOfChar(':')) {
        const auto colon = value.indexOfChar(':');
        hostname = value.substring(0, colon); portText = value.substring(colon + 1); embeddedPort = true;
    }
    if (hostname.isEmpty() || hostname.containsAnyOf("[]"))
        return juce::Result::fail(u8"地址格式无效，请填写对方 IP 或域名"_es);
    if (embeddedPort) {
        if (portText.isEmpty() || portText.length() > 5 || !portText.containsOnly("0123456789"))
            return juce::Result::fail(u8"地址中的端口应为 1–65535，例如 192.168.1.20:51000"_es);
        port = portText.getIntValue();
    }
    if (port < 1 || port > 65535) return juce::Result::fail(u8"端口范围为 1–65535"_es);
    host = normalizeHost(hostname);
    if (host.isEmpty()) return juce::Result::fail(u8"请输入对方 IP 或域名"_es);
    return juce::Result::ok();
}
bool sameEndpoint(const PeerSettings& a, const PeerSettings& b) {
    auto hostA = a.host, hostB = b.host; auto portA = a.port, portB = b.port;
    return normalizeEndpoint(hostA, portA).wasOk() && normalizeEndpoint(hostB, portB).wasOk()
        && hostA == hostB && portA == portB;
}
int retryDelayMs(unsigned attempt) { return 1000 * (1 << std::min(attempt, 5u)); }
juce::var PeerSettings::toJson() const {
    auto* o = object();
    o->setProperty("recordId", recordId); o->setProperty("deviceId", deviceId);
    o->setProperty("name", name); o->setProperty("host", host); o->setProperty("port", port);
    o->setProperty("key", key); o->setProperty("autoConnect", autoConnect);
    o->setProperty("receive", receive); o->setProperty("send", send);
    o->setProperty("muted", muted); o->setProperty("gain", gain);
    return juce::var(o);
}
PeerSettings PeerSettings::fromJson(const juce::var& v) {
    PeerSettings p;
    p.recordId = get(v, "recordId", p.recordId).toString(); p.deviceId = v["deviceId"].toString();
    p.name = v["name"].toString().substring(0, 80); p.host = normalizeHost(v["host"].toString());
    p.port = juce::jlimit(1, 65535, (int)get(v, "port", 51000)); p.key = v["key"].toString();
    // Repair records saved by versions that treated host:port as a DNS name.
    if (normalizeEndpoint(p.host, p.port).failed()) p.host.clear();
    p.autoConnect = (bool)get(v, "autoConnect", false); p.receive = (bool)get(v, "receive", true);
    p.send = (bool)get(v, "send", false); p.muted = (bool)get(v, "muted", false);
    auto gain = (double)get(v, "gain", 0.8);
    p.gain = std::isfinite(gain) ? (float)juce::jlimit(0.0, 2.0, gain) : 0.8f;
    return p;
}
juce::var Settings::toJson() const {
    auto* o = object(); o->setProperty("version", 2); o->setProperty("deviceId", deviceId);
    o->setProperty("name", name); o->setProperty("key", key); o->setProperty("port", port);
    o->setProperty("bufferMs", bufferMs); o->setProperty("hideDock", hideDock);
    o->setProperty("hideOnAutoStart", hideOnAutoStart);
    o->setProperty("pcm", pcm); o->setProperty("outputDevice", outputDevice);
    o->setProperty("theme", theme);
    juce::Array<juce::var> items;
    for (const auto& p : peers) items.add(p.toJson());
    o->setProperty("peers", items); return juce::var(o);
}
Settings Settings::fromJson(const juce::var& v) {
    Settings s;
    if (!v.isObject()) return s;
    auto id = v["deviceId"].toString(); if (!juce::Uuid(id).isNull()) s.deviceId = id;
    // v1 was an internal development format with UUID-based invitation keys.
    auto key = v["key"].toString(); if ((int)v["version"] >= 2 && validKey(key)) s.key = key.toLowerCase();
    s.name = get(v, "name", s.name).toString().substring(0, 80);
    s.port = juce::jlimit(1, 65535, (int)get(v, "port", 51000));
    s.bufferMs = juce::jlimit(20, 200, (int)get(v, "bufferMs", 30));
    s.hideDock = (bool)get(v, "hideDock", true); s.pcm = (bool)get(v, "pcm", false);
    s.hideOnAutoStart = (bool)get(v, "hideOnAutoStart", false);
    s.outputDevice = v["outputDevice"].toString();
    const auto theme = v["theme"].toString();
    if (theme == "light" || theme == "dark") s.theme = theme;
    if (auto* items = v["peers"].getArray()) for (const auto& item : *items) {
        if ((int)s.peers.size() >= maxPeers) break;
        auto p = PeerSettings::fromJson(item);
        if (p.host.isEmpty() || !validKey(p.key)) continue;
        if (std::none_of(s.peers.begin(), s.peers.end(), [&](auto& old) {
            return sameEndpoint(old, p) || (!p.deviceId.isEmpty() && old.deviceId == p.deviceId);
        })) s.peers.push_back(p);
    }
    return s;
}
Settings Settings::load(const juce::File& file) { return fromJson(juce::JSON::parse(file)); }
juce::Result Settings::save(const juce::File& file) const {
    auto result = file.getParentDirectory().createDirectory(); if (result.failed()) return result;
    juce::TemporaryFile temporary(file);
    if (!temporary.getFile().replaceWithText(juce::JSON::toString(toJson(), true)))
        return juce::Result::fail(u8"无法写入设备配置"_es);
   #if !JUCE_WINDOWS
    temporary.getFile().setReadOnly(false);
    ::chmod(temporary.getFile().getFullPathName().toRawUTF8(), 0600);
   #endif
    if (!temporary.overwriteTargetFileWithTemporary()) return juce::Result::fail(u8"无法保存设备配置"_es);
    return juce::Result::ok();
}
}
