#pragma once
#include "Socket.h"
#include <mbedtls/ssl.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/ssl_cookie.h>
#include <mbedtls/timing.h>
#include <deque>
#include <array>

namespace enjoysync {
// One DTLS 1.2 PSK session per remote endpoint, exclusively owned by the network thread.
class SecureChannel {
public:
    SecureChannel(UdpSocket&, Address, const juce::String& hexKey, bool client);
    ~SecureChannel();
    void enqueue(const void*, int);
    int handshake();
    int read(void*, int);
    int write(const void*, int);
    void bye();
    bool ready() const { return established; }
    const Address address;
    const bool client;
    juce::String error;
    std::array<unsigned char, 32> clientRandom {};
    bool hasClientRandom = false;
    uint64_t queueDrops = 0;
    static bool wouldBlock(int);
    static juce::String describeError(int);
private:
    static int send(void*, const unsigned char*, size_t);
    static int receive(void*, unsigned char*, size_t);
    void resetCookie();
    UdpSocket& socket;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config config;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context rng;
    mbedtls_ssl_cookie_ctx cookie;
    mbedtls_timing_delay_context timer {};
    std::deque<std::vector<unsigned char>> packets;
    bool established = false;
    int initError = 0;
};
}
