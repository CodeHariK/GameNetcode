#pragma once

#include "ChannelTypes.hpp"
#include <cstdint>
#include <vector>
#include <cstddef>

namespace netcode {

/**
 * Base abstract interface for all channel delivery modes.
 */
class Channel {
public:
    virtual ~Channel() = default;

    // Send a message over this channel. Queues or prepares for transmission.
    virtual void send_message(const void* data, size_t size, double current_time) = 0;

    // Pulls outgoing messages to be packed into the current UDP datagram.
    // Must respect remaining_bytes budget in packet.
    virtual size_t write_outgoing_messages(uint8_t* buffer,
                                           size_t max_bytes,
                                           double current_time) = 0;

    // Process a received message wire slice for this channel.
    // Returns true if successfully parsed and handled.
    virtual bool process_incoming_message(uint16_t message_id,
                                          const uint8_t* payload,
                                          size_t size) = 0;

    // Receive the next available, deliverable message to application logic.
    // Returns true if a message was popped into out_message.
    virtual bool receive_message(Message& out_message) = 0;

    // Called when the underlying ReliabilitySystem acknowledges a packet sequence
    virtual void on_packet_acked(uint16_t packet_sequence) { (void)packet_sequence; }

    // Tick update (retransmissions, timeouts)
    virtual void update(double current_time, float rtt_ms) {
        (void)current_time;
        (void)rtt_ms;
    }

    // Check if channel has pending outgoing messages ready to transmit
    [[nodiscard]] virtual bool has_outgoing_messages() const = 0;

    [[nodiscard]] uint8_t channel_id() const { return channel_id_; }
    [[nodiscard]] ChannelType type() const { return type_; }

protected:
    Channel(uint8_t channel_id, ChannelType type) : channel_id_(channel_id), type_(type) {}

    uint8_t channel_id_;
    ChannelType type_;
};

}  // namespace netcode
