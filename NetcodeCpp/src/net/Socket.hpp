#pragma once

#include "Address.hpp"
#include <cstddef>
#include <cstdint>

namespace netcode {

/**
 * RAII Wrapper for a Non-blocking POSIX UDP Socket.
 */
class Socket {
public:
    Socket();
    ~Socket();

    // Sockets are non-copyable but movable
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;

    // Opens and binds a UDP socket to the given port.
    // If port is 0, OS assigns an ephemeral port.
    bool open(uint16_t port = 0);

    // Closes socket handle
    void close();

    // Check if socket is open
    [[nodiscard]] bool is_open() const { return handle_ >= 0; }

    // Send packet to destination. Returns true on success.
    bool send(const Address& destination, const void* data, size_t size);

    // Receive packet non-blockingly.
    // Returns:
    //   > 0 : number of bytes received
    //   = 0 : no packet available (EWOULDBLOCK / EAGAIN)
    //   < 0 : socket error
    int receive(Address& sender, void* data, size_t max_size);

    // Returns the port the socket is currently bound to.
    [[nodiscard]] uint16_t bound_port() const;

    // Configure OS socket buffer sizes
    bool set_buffer_sizes(int send_bytes, int recv_bytes);

private:
    int handle_{-1};
};

}  // namespace netcode
