#pragma once

#include "Address.hpp"
#include "Channel.hpp"
#include "PacketHeader.hpp"
#include "ReliabilitySystem.hpp"
#include "ReliableOrderedChannel.hpp"
#include "Socket.hpp"
#include "UnreliableSequencedChannel.hpp"
#include "UnreliableUnorderedChannel.hpp"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

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
 * Connection manages a virtual client-server session over UDP: state machine,
 * heartbeats, timeouts, packet reliability, and (Milestone 3) multiplexing of
 * several delivery channels (Unreliable, Sequenced, Reliable-Ordered) into a
 * single UDP datagram.
 */
class Connection {
public:
    Connection(Socket& socket, double timeout_sec = 5.0, double heartbeat_interval_sec = 0.25);

    template <typename T, typename... Args>
    T* create_channel(uint8_t channel_id, Args&&... args) {
        auto ch = std::make_unique<T>(channel_id, std::forward<Args>(args)...);
        T* ptr = ch.get();
        channels_[channel_id] = std::move(ch);
        return ptr;
    }

    Channel* get_channel(uint8_t channel_id) {
        auto it = channels_.find(channel_id);
        return (it != channels_.end()) ? it->second.get() : nullptr;
    }

    void connect(const Address& address, double current_time);
    void accept(const Address& address, double current_time);
    void disconnect(double current_time);

    bool send_message(uint8_t channel_id, const void* data, size_t size, double current_time);
    bool receive_message(Message& out_message);

    bool send_packet(const void* payload, size_t size, double current_time);
    bool flush_channels(double current_time);

    bool process_packet(const Address& sender,
                        const uint8_t* data,
                        size_t size,
                        const uint8_t*& out_raw_payload,
                        size_t& out_raw_payload_bytes,
                        double current_time);

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
    void setup_callbacks();

    Socket& socket_;
    Address remote_address_;
    ConnectionState state_{ConnectionState::Disconnected};

    double timeout_sec_{5.0};
    double heartbeat_interval_sec_{0.25};
    double last_packet_sent_time_{0.0};
    double last_packet_received_time_{0.0};

    ReliabilitySystem reliability_;
    std::unordered_map<uint8_t, std::unique_ptr<Channel>> channels_;
};

}  // namespace netcode
