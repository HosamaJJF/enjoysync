#include "SecureChannel.h"
#include "core/Settings.h"
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
namespace enjoysync {
SecureChannel::SecureChannel(UdpSocket& socket_, Address remote, const juce::String& key, bool isClient)
    : address(remote), client(isClient), socket(socket_) {
    mbedtls_ssl_init(&ssl); mbedtls_ssl_config_init(&config);
    mbedtls_entropy_init(&entropy); mbedtls_ctr_drbg_init(&rng); mbedtls_ssl_cookie_init(&cookie);
    auto check = [&](int result) { if (result != 0 && initError == 0) initError = result; };
    const char personalization[] = "enjoysync-dtls-v1";
    check(mbedtls_ctr_drbg_seed(&rng, mbedtls_entropy_func, &entropy,
        reinterpret_cast<const unsigned char*>(personalization), sizeof(personalization) - 1));
    check(mbedtls_ssl_config_defaults(&config, client ? MBEDTLS_SSL_IS_CLIENT : MBEDTLS_SSL_IS_SERVER,
        MBEDTLS_SSL_TRANSPORT_DATAGRAM, MBEDTLS_SSL_PRESET_DEFAULT));
    mbedtls_ssl_conf_rng(&config, mbedtls_ctr_drbg_random, &rng);
    static const int suites[] = { MBEDTLS_TLS_PSK_WITH_AES_128_GCM_SHA256, 0 };
    mbedtls_ssl_conf_ciphersuites(&config, suites);
    mbedtls_ssl_conf_min_tls_version(&config, MBEDTLS_SSL_VERSION_TLS1_2);
    mbedtls_ssl_conf_max_tls_version(&config, MBEDTLS_SSL_VERSION_TLS1_2);
    mbedtls_ssl_conf_handshake_timeout(&config, 250, 2000);
    unsigned char psk[16] {};
    if (!validKey(key)) initError = MBEDTLS_ERR_SSL_BAD_INPUT_DATA;
    for (int i = 0; i < 16; ++i) psk[i] = (unsigned char)key.substring(i * 2, i * 2 + 2).getHexValue32();
    const unsigned char identity[] = "enjoysync-v1";
    check(mbedtls_ssl_conf_psk(&config, psk, sizeof(psk), identity, sizeof(identity) - 1));
    if (!client) {
        check(mbedtls_ssl_cookie_setup(&cookie, mbedtls_ctr_drbg_random, &rng));
        mbedtls_ssl_conf_dtls_cookies(&config, mbedtls_ssl_cookie_write, mbedtls_ssl_cookie_check, &cookie);
    }
    check(mbedtls_ssl_setup(&ssl, &config));
    mbedtls_ssl_set_mtu(&ssl, 1200);
    mbedtls_ssl_set_bio(&ssl, this, send, receive, nullptr);
    mbedtls_ssl_set_timer_cb(&ssl, &timer, mbedtls_timing_set_delay, mbedtls_timing_get_delay);
    if (!client) resetCookie();
}
SecureChannel::~SecureChannel() {
    mbedtls_ssl_free(&ssl); mbedtls_ssl_config_free(&config); mbedtls_ssl_cookie_free(&cookie);
    mbedtls_ctr_drbg_free(&rng); mbedtls_entropy_free(&entropy);
}
void SecureChannel::resetCookie() {
    const auto id = address.key().toStdString();
    mbedtls_ssl_set_client_transport_id(&ssl, reinterpret_cast<const unsigned char*>(id.data()), id.size());
}
void SecureChannel::enqueue(const void* data, int size) {
    if (size <= 0 || size > 4096 || packets.size() >= 128) return;
    auto* bytes = static_cast<const unsigned char*>(data); packets.emplace_back(bytes, bytes + size);
}
int SecureChannel::send(void* user, const unsigned char* data, size_t size) {
    auto& self = *static_cast<SecureChannel*>(user);
    if (self.client && size >= 59 && data[0] == 22 && data[13] == 1) {
        std::copy_n(data + 27, 32, self.clientRandom.begin()); self.hasClientRandom = true;
    }
    int n = self.socket.send(data, (int)size, self.address);
    return n < 0 ? MBEDTLS_ERR_NET_SEND_FAILED : n;
}
int SecureChannel::receive(void* user, unsigned char* data, size_t size) {
    auto& self = *static_cast<SecureChannel*>(user);
    if (self.packets.empty()) return MBEDTLS_ERR_SSL_WANT_READ;
    auto packet = std::move(self.packets.front()); self.packets.pop_front();
    if (packet.size() > size) return MBEDTLS_ERR_SSL_BUFFER_TOO_SMALL;
    std::memcpy(data, packet.data(), packet.size()); return (int)packet.size();
}
bool SecureChannel::wouldBlock(int n) { return n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE; }
int SecureChannel::handshake() {
    if (established) return 0;
    int result = initError != 0 ? initError : mbedtls_ssl_handshake(&ssl);
    if (result == MBEDTLS_ERR_SSL_HELLO_VERIFY_REQUIRED) {
        mbedtls_ssl_session_reset(&ssl); resetCookie(); return MBEDTLS_ERR_SSL_WANT_READ;
    }
    if (result == 0) established = true;
    else if (!wouldBlock(result)) {
        char text[180] {}; mbedtls_strerror(result, text, sizeof(text)); error = text;
    }
    return result;
}
int SecureChannel::read(void* data, int size) { return mbedtls_ssl_read(&ssl, static_cast<unsigned char*>(data), (size_t)size); }
int SecureChannel::write(const void* data, int size) { return mbedtls_ssl_write(&ssl, static_cast<const unsigned char*>(data), (size_t)size); }
void SecureChannel::bye() { if (established) mbedtls_ssl_close_notify(&ssl); }
}
