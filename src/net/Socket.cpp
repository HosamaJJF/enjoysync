#include "Socket.h"
#include "core/Settings.h"
#if !JUCE_WINDOWS
 #include <fcntl.h>
 #include <cerrno>
#endif
namespace enjoysync {
juce::String Address::host() const {
    char text[NI_MAXHOST] {};
    getnameinfo(data(), size, text, sizeof(text), nullptr, 0, NI_NUMERICHOST);
    return text;
}
int Address::port() const {
    if (storage.ss_family == AF_INET) return ntohs(reinterpret_cast<const sockaddr_in*>(&storage)->sin_port);
    return ntohs(reinterpret_cast<const sockaddr_in6*>(&storage)->sin6_port);
}
juce::String Address::key() const { return "[" + host() + "]:" + juce::String(port()); }
std::vector<Address> resolve(const juce::String& host, int port) {
    addrinfo hints {}; hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_NUMERICSERV;
    addrinfo* result = nullptr; std::vector<Address> addresses;
    if (getaddrinfo(normalizeHost(host).toRawUTF8(), juce::String(port).toRawUTF8(), &hints, &result) != 0) return addresses;
    for (auto* item = result; item; item = item->ai_next) {
        if ((item->ai_family != AF_INET && item->ai_family != AF_INET6) || item->ai_addrlen > sizeof(sockaddr_storage)) continue;
        Address address; address.size = static_cast<socklen_t>(item->ai_addrlen);
        std::memcpy(&address.storage, item->ai_addr, item->ai_addrlen);
        if (std::find(addresses.begin(), addresses.end(), address) == addresses.end()) addresses.push_back(address);
    }
    freeaddrinfo(result); return addresses;
}
UdpSocket::UdpSocket() {
   #if JUCE_WINDOWS
    WSADATA data {}; WSAStartup(MAKEWORD(2, 2), &data);
   #endif
}
UdpSocket::~UdpSocket() {
    close();
   #if JUCE_WINDOWS
    WSACleanup();
   #endif
}
void UdpSocket::close() {
    for (auto* handle : {&ipv4, &ipv6}) if (*handle != invalid) {
       #if JUCE_WINDOWS
        closesocket(static_cast<SOCKET>(*handle));
       #else
        ::close(static_cast<int>(*handle));
       #endif
        *handle = invalid;
    }
}
juce::Result UdpSocket::bind(int port) {
    close();
    for (int family : {AF_INET, AF_INET6}) {
        auto fd = ::socket(family, SOCK_DGRAM, IPPROTO_UDP);
        if ((std::intptr_t)fd == invalid) continue;
        int yes = 1;
        if (family == AF_INET6) setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&yes), sizeof(yes));
        int buffer = 1 << 20;
        setsockopt(fd, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&buffer), sizeof(buffer));
        Address addr;
        if (family == AF_INET) {
            auto* a = reinterpret_cast<sockaddr_in*>(&addr.storage);
            a->sin_family = AF_INET; a->sin_port = htons(static_cast<uint16_t>(port)); addr.size = sizeof(*a);
        } else {
            auto* a = reinterpret_cast<sockaddr_in6*>(&addr.storage);
            a->sin6_family = AF_INET6; a->sin6_port = htons(static_cast<uint16_t>(port)); addr.size = sizeof(*a);
        }
        if (::bind(fd, addr.data(), addr.size) != 0) {
           #if JUCE_WINDOWS
            closesocket(fd);
           #else
            ::close(fd);
           #endif
            // A port conflict on a supported family must not silently produce a partial listener.
            close(); return juce::Result::fail(u8"UDP 端口 "_es + juce::String(port) + u8" 无法监听，请检查端口占用或防火墙"_es);
        }
       #if JUCE_WINDOWS
        u_long mode = 1; ioctlsocket(fd, FIONBIO, &mode);
       #else
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
       #endif
        (family == AF_INET ? ipv4 : ipv6) = (Handle)fd;
    }
    return ipv4 != invalid || ipv6 != invalid ? juce::Result::ok() : juce::Result::fail(u8"没有可用的 IPv4 / IPv6 网络接口"_es);
}
int UdpSocket::receive(void* data, int capacity, Address& from) {
    for (int i = 0; i < 2; ++i) {
        auto handle = ((nextFamily++ & 1) == 0 ? ipv4 : ipv6); if (handle == invalid) continue;
        from.size = sizeof(from.storage);
        auto n = ::recvfrom(
           #if JUCE_WINDOWS
            static_cast<SOCKET>(handle),
           #else
            static_cast<int>(handle),
           #endif
            static_cast<char*>(data), capacity, 0, reinterpret_cast<sockaddr*>(&from.storage), &from.size);
        if (n > 0) return static_cast<int>(n);
    }
    return 0;
}
int UdpSocket::send(const void* data, int size, const Address& to) {
    auto handle = to.storage.ss_family == AF_INET6 ? ipv6 : ipv4; if (handle == invalid) return -1;
    return static_cast<int>(::sendto(
       #if JUCE_WINDOWS
        static_cast<SOCKET>(handle),
       #else
        static_cast<int>(handle),
       #endif
        static_cast<const char*>(data), size, 0, to.data(), to.size));
}
}
