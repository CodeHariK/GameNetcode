#pragma once

#include <cstdint>
#include <string>
#include <netinet/in.h>

namespace netcode {

/**
 * Represents an IPv4 network endpoint (IP address and port).
 */
class Address {
public:
    Address();
    Address(uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint16_t port);
    Address(uint32_t host_ipv4, uint16_t port);
    Address(const std::string& ip_str, uint16_t port);
    explicit Address(const sockaddr_in& addr);

    [[nodiscard]] uint32_t host() const { return host_; }
    [[nodiscard]] uint16_t port() const { return port_; }

    [[nodiscard]] uint8_t a() const { return static_cast<uint8_t>(host_ >> 24); }
    [[nodiscard]] uint8_t b() const { return static_cast<uint8_t>(host_ >> 16); }
    [[nodiscard]] uint8_t c() const { return static_cast<uint8_t>(host_ >> 8); }
    [[nodiscard]] uint8_t d() const { return static_cast<uint8_t>(host_); }

    [[nodiscard]] sockaddr_in to_sockaddr() const;
    [[nodiscard]] std::string to_string() const;

    bool operator==(const Address& other) const {
        return host_ == other.host_ && port_ == other.port_;
    }

    bool operator!=(const Address& other) const { return !(*this == other); }

    bool operator<(const Address& other) const {
        if (host_ < other.host_) return true;
        if (host_ > other.host_) return false;
        return port_ < other.port_;
    }

private:
    uint32_t host_{0};  // IPv4 in host byte order (e.g. 127.0.0.1 = 0x7F000001)
    uint16_t port_{0};  // Port in host byte order
};

}  // namespace netcode

namespace std {
template <>
struct hash<netcode::Address> {
    size_t operator()(const netcode::Address& addr) const noexcept {
        return (static_cast<size_t>(addr.host()) << 16) ^ addr.port();
    }
};
}  // namespace std
