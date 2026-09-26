#include "Socket.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <cerrno>
#include <iostream>

namespace netcode {

Socket::Socket() = default;

Socket::~Socket() {
    close();
}

Socket::Socket(Socket&& other) noexcept : handle_(other.handle_) {
    other.handle_ = -1;
}

Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        close();
        handle_ = other.handle_;
        other.handle_ = -1;
    }
    return *this;
}

bool Socket::open(uint16_t port) {
    close();

    handle_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (handle_ < 0) {
        std::cerr << "[Socket] Failed to create UDP socket: " << strerror(errno) << "\n";
        return false;
    }

    // Set non-blocking mode
    int flags = ::fcntl(handle_, F_GETFL, 0);
    if (flags < 0 || ::fcntl(handle_, F_SETFL, flags | O_NONBLOCK) < 0) {
        std::cerr << "[Socket] Failed to set non-blocking: " << strerror(errno) << "\n";
        close();
        return false;
    }

    // Allow address reuse
    int optval = 1;
    ::setsockopt(handle_, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    // Bind to address and port
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);

    if (::bind(handle_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) < 0) {
        std::cerr << "[Socket] Failed to bind to port " << port << ": " << strerror(errno) << "\n";
        close();
        return false;
    }

    return true;
}

void Socket::close() {
    if (handle_ >= 0) {
        ::close(handle_);
        handle_ = -1;
    }
}

bool Socket::send(const Address& destination, const void* data, size_t size) {
    if (handle_ < 0 || !data || size == 0) {
        return false;
    }

    sockaddr_in dest_addr = destination.to_sockaddr();
    ssize_t sent_bytes = ::sendto(handle_,
                                  data,
                                  size,
                                  0,
                                  reinterpret_cast<const sockaddr*>(&dest_addr),
                                  sizeof(dest_addr));

    return (sent_bytes == static_cast<ssize_t>(size));
}

int Socket::receive(Address& sender, void* data, size_t max_size) {
    if (handle_ < 0 || !data || max_size == 0) {
        return -1;
    }

    sockaddr_in from_addr{};
    socklen_t from_len = sizeof(from_addr);

    ssize_t bytes_read =
        ::recvfrom(handle_, data, max_size, 0, reinterpret_cast<sockaddr*>(&from_addr), &from_len);

    if (bytes_read > 0) {
        sender = Address(from_addr);
        return static_cast<int>(bytes_read);
    }

    if (bytes_read < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;  // No packets ready to read right now
        }
        std::cerr << "[Socket] recvfrom error: " << strerror(errno) << "\n";
        return -1;
    }

    return 0;
}

uint16_t Socket::bound_port() const {
    if (handle_ < 0) return 0;

    sockaddr_in sin{};
    socklen_t len = sizeof(sin);
    if (::getsockname(handle_, reinterpret_cast<sockaddr*>(&sin), &len) == 0) {
        return ntohs(sin.sin_port);
    }
    return 0;
}

bool Socket::set_buffer_sizes(int send_bytes, int recv_bytes) {
    if (handle_ < 0) return false;

    if (send_bytes > 0) {
        if (::setsockopt(handle_, SOL_SOCKET, SO_SNDBUF, &send_bytes, sizeof(send_bytes)) < 0) {
            return false;
        }
    }
    if (recv_bytes > 0) {
        if (::setsockopt(handle_, SOL_SOCKET, SO_RCVBUF, &recv_bytes, sizeof(recv_bytes)) < 0) {
            return false;
        }
    }
    return true;
}

}  // namespace netcode
