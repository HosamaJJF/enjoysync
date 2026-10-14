#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include <cstdint>
#if JUCE_WINDOWS
 #include <winsock2.h>
 #include <ws2tcpip.h>
#else
 #include <sys/socket.h>
 #include <netdb.h>
 #include <netinet/in.h>
 #include <arpa/inet.h>
 #include <unistd.h>
#endif
namespace enjoysync {
#if JUCE_WINDOWS
using socklen_t = int;
#endif
struct Address {
    sockaddr_storage storage {};
    socklen_t size = 0;
    const sockaddr* data() const { return reinterpret_cast<const sockaddr*>(&storage); }
    juce::String host() const;
    int port() const;
    juce::String key() const;
    bool operator==(const Address& other) const { return key() == other.key(); }
};
std::vector<Address> resolve(const juce::String& host, int port);
class UdpSocket {
public:
    // A transient local send failure is equivalent to a lost UDP datagram.
    static constexpr int temporarySendFailure = -2;
    struct Diagnostics {
        uint64_t sendDrops = 0, sendErrors = 0, receiveErrors = 0;
        int lastSendError = 0, lastReceiveError = 0;
    };
    UdpSocket(); ~UdpSocket();
    juce::Result bind(int port);
    void close();
    int receive(void*, int, Address&);
    int send(const void*, int, const Address&);
    bool hasIPv4() const { return ipv4 != invalid; }
    bool hasIPv6() const { return ipv6 != invalid; }
    const Diagnostics& diagnostics() const { return stats; }
private:
    using Handle = std::intptr_t;
    static constexpr Handle invalid = -1;
    Handle ipv4 = invalid, ipv6 = invalid;
    int nextFamily = 0;
    Diagnostics stats;
};
}
