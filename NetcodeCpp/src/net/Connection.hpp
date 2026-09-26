#pragma once

#include "Address.hpp"
#include "Socket.hpp"
#include "PacketHeader.hpp"
#include "ReliabilitySystem.hpp"
#include "Channel.hpp"
#include "UnreliableUnorderedChannel.hpp"
#include "UnreliableSequencedChannel.hpp"
#include "ReliableOrderedChannel.hpp"

#include <cstdint>
#include <vector>
#include <memory>
#include <unordered_map>

namespace netcode {

class NetworkSimulator;  // optional outgoing-path impairment (Milestone 9)

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
 * Connection manages a virtual peer-to-peer or client-server session over UDP.
 * It tracks state transitions, timeouts, heartbeats, and multiplexes multiple
 * delivery channels (Unreliable, Sequenced, Reliable-Ordered) into single UDP packets.
 */
class Connection {
public:
    Connection(Socket& socket, double timeout_sec = 5.0, double heartbeat_interval_sec = 0.25);

    // Channel creation / registration
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

    // Client initiates connection to remote endpoint
    void connect(const Address& address, double current_time);

    // Server accepts connection from remote endpoint
    void accept(const Address& address, double current_time);

    // Disconnect connection
    void disconnect(double current_time);

    // High-level: Enqueue message to a specific channel
    bool send_message(uint8_t channel_id, const void* data, size_t size, double current_time);

    // High-level: Receive the next delivered message from any channel
    bool receive_message(Message& out_message);

    // Low-level raw packet send (for custom raw payloads without channel header)
    bool send_packet(const void* payload, size_t size, double current_time);

    // Packs all pending channel messages into a single UDP datagram and transmits it
    bool flush_channels(double current_time);

    // Feeds received wire data into the connection.
    // Automatically unpacks multiplexed channel messages and routes them to their channels.
    bool process_packet(const Address& sender,
                        const uint8_t* data,
                        size_t size,
                        const uint8_t*& out_raw_payload,
                        size_t& out_raw_payload_bytes,
                        double current_time);

    // Call every frame/tick to handle timeouts, heartbeats, and RTT/loss calculations
    void update(double current_time);

    // Attach an optional NetworkSimulator. When set, all outgoing packets are routed
    // through it (latency/jitter/loss/duplication) instead of straight to the socket.
    // The owner must call the simulator's update() each frame to flush due packets.
    void set_network_simulator(NetworkSimulator* sim) { sim_ = sim; }

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
    NetworkSimulator* sim_{nullptr};
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
