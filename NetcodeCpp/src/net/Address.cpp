#include "Address.hpp"

#include <arpa/inet.h>
#include <cstring>
#include <sstream>

namespace netcode {

Address::Address() = default;

Address::Address(uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint16_t port)
    : host_((static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(b) << 16) |
            (static_cast<uint32_t>(c) << 8) | static_cast<uint32_t>(d)),
      port_(port) {}

Address::Address(uint32_t host_ipv4, uint16_t port) : host_(host_ipv4), port_(port) {}

Address::Address(const std::string& ip_str, uint16_t port) : port_(port) {
    in_addr addr{};
    if (::inet_pton(AF_INET, ip_str.c_str(), &addr) == 1) {
        host_ = ntohl(addr.s_addr);
    } else {
        host_ = 0;
    }
}

Address::Address(const sockaddr_in& addr) {
    host_ = ntohl(addr.sin_addr.s_addr);
    port_ = ntohs(addr.sin_port);
}

sockaddr_in Address::to_sockaddr() const {
    sockaddr_in sa{};
    std::memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(host_);
    sa.sin_port = htons(port_);
    return sa;
}

std::string Address::to_string() const {
    std::ostringstream ss;
    ss << static_cast<int>(a()) << "." << static_cast<int>(b()) << "." << static_cast<int>(c())
       << "." << static_cast<int>(d()) << ":" << port_;
    return ss.str();
}

}  // namespace netcode
