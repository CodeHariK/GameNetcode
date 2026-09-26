#pragma once

#include "Address.hpp"
#include "PacketHeader.hpp"
#include "ReliabilitySystem.hpp"
#include "Socket.hpp"

#include <cstddef>
#include <cstdint>

namespace netcode {

enum class ConnectionState { Disconnected, Connecting, Connected, Disconnecting };

inline const char* connection_state_to_string(ConnectionState state) {
    switch (state) {
        case ConnectionState::Disconnected:
            return "Disconnected";
        case ConnectionState::Connecting:
            return "Connecting";
        case ConnectionState::Connected:
            return "Connected";
        case ConnectionState::Disconnecting:
            return "Disconnecting";
    }
    return "Unknown";
}

/**
 * Connection manages a virtual client-server session over UDP: a state machine
 * (Disconnected -> Connecting -> Connected), keep-alive heartbeats, inactivity
 * timeouts, and packet-level reliability (sequence / ack / ack-bits, RTT, loss).
 *
 * Milestone 2: raw packets only. Channels (Milestone 3) and the network
 * simulator hook (Milestone 9) are layered on in later steps.
 */
class Connection {
public:
    Connection(Socket& socket, double timeout_sec = 5.0, double heartbeat_interval_sec = 0.25);

    void connect(const Address& address, double current_time);
    void accept(const Address& address, double current_time);
    void disconnect(double current_time);

    // Send a raw payload wrapped in a reliability header.
    bool send_packet(const void* payload, size_t size, double current_time);

    // Feed received wire data in; exposes the raw payload (after the header).
    bool process_packet(const Address& sender,
                        const uint8_t* data,
                        size_t size,
                        const uint8_t*& out_raw_payload,
                        size_t& out_raw_payload_bytes,
                        double current_time);

    // Call every frame to handle timeouts, heartbeats, and RTT/loss.
    void update(double current_time);

    [[nodiscard]] ConnectionState state() const { return state_; }
    [[nodiscard]] const Address& remote_address() const { return remote_address_; }
    [[nodiscard]] bool is_connected() const { return state_ == ConnectionState::Connected; }
    [[nodiscard]] const ReliabilitySystem& reliability() const { return reliability_; }
    [[nodiscard]] ReliabilitySystem& reliability() { return reliability_; }
    [[nodiscard]] float rtt_ms() const { return reliability_.rtt_ms(); }
    [[nodiscard]] float packet_loss() const { return reliability_.packet_loss(); }

private:
    void send_heartbeat(double current_time);

    Socket& socket_;
    Address remote_address_;
    ConnectionState state_{ConnectionState::Disconnected};

    double timeout_sec_{5.0};
    double heartbeat_interval_sec_{0.25};
    double last_packet_sent_time_{0.0};
    double last_packet_received_time_{0.0};

    ReliabilitySystem reliability_;
};

}  // namespace netcode
