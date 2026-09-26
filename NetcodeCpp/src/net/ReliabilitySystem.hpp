#pragma once

#include "PacketHeader.hpp"
#include <vector>
#include <cstdint>
#include <cassert>
#include <functional>

namespace netcode {

/**
 * ReliabilitySystem implements Glenn Fiedler's sliding ACK bitfield algorithm.
 *
 * It manages:
 *  - Monotonically increasing local sequence numbers (with 16-bit wraparound).
 *  - Remote sequence tracking and 32-bit sliding ack bitfield.
 *  - Round-trip time (RTT) tracking using Exponential Moving Average (EMA).
 *  - Packet loss detection and statistics.
 *  - Acknowledgment callback dispatch to notify channels.
 */
class ReliabilitySystem {
public:
    struct PacketRecord {
        double time_sent{0.0};
        bool acked{false};
    };

    using AckCallback = std::function<void(uint16_t packet_sequence)>;

    explicit ReliabilitySystem(size_t buffer_size = 1024);

    // Call before sending a packet: generates sequence, ack, and ack_bits for the header.
    // Also records time_sent for RTT measurement.
    void generate_packet_header(PacketHeader& header, double current_time);

    // Call when a packet header is received from the remote peer.
    // Updates remote sequence, sliding ack bitfield, and processes acknowledgments.
    void packet_received(const PacketHeader& header, double current_time);

    // Register callback triggered when a specific packet sequence is acknowledged
    void set_ack_callback(AckCallback cb) { ack_callback_ = std::move(cb); }

    // Call periodically in the tick loop to calculate packet loss over a window
    void update(double current_time);

    [[nodiscard]] uint16_t local_sequence() const { return local_sequence_; }
    [[nodiscard]] uint16_t remote_sequence() const { return remote_sequence_; }
    [[nodiscard]] float rtt_ms() const { return rtt_ms_; }
    [[nodiscard]] float packet_loss() const { return packet_loss_; }
    [[nodiscard]] uint64_t total_sent() const { return total_sent_; }
    [[nodiscard]] uint64_t total_received() const { return total_received_; }
    [[nodiscard]] uint64_t total_acked() const { return total_acked_; }

    void reset();

private:
    void advance_remote_sequence(uint16_t new_seq);
    void process_ack(uint16_t ack_sequence, double current_time);

    size_t buffer_size_{1024};
    uint16_t local_sequence_{0};
    uint16_t remote_sequence_{0};
    bool has_received_remote_packet_{false};

    // Sliding bitfield representing received packets [remote_sequence - 1 ... remote_sequence - 32]
    uint32_t remote_ack_bits_{0};

    // Circular buffer of sent packets to measure RTT and loss
    std::vector<PacketRecord> sent_packets_;

    float rtt_ms_{0.0f};       // Smoothed RTT in milliseconds (EMA)
    float packet_loss_{0.0f};  // Observed loss rate in [0.0, 1.0]

    uint64_t total_sent_{0};
    uint64_t total_received_{0};
    uint64_t total_acked_{0};

    AckCallback ack_callback_;
};

}  // namespace netcode
