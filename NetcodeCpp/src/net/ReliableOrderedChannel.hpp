#pragma once

#include "Channel.hpp"
#include "PacketHeader.hpp"  // For sequence_greater_than
#include <map>
#include <vector>
#include <cstring>
#include <arpa/inet.h>
#include <algorithm>

namespace netcode {

/**
 * ReliableOrderedChannel:
 * Ensures all messages arrive, without loss, and are delivered strictly in order.
 *
 * Algorithm (ENet / Yojimbo pattern):
 *  1. Sender assigns 16-bit message_id.
 *  2. Unacknowledged messages are stored in sent_messages_ map and retransmitted
 *     periodically (based on RTT) until acknowledged.
 *  3. When an underlying UDP packet sequence is ACKed, all messages packed into that
 *     packet are marked acknowledged and removed from retransmit queue.
 *  4. Receiver buffers incoming messages that arrived out-of-order in a reassembly map,
 *     and yields them sequentially to receive_message() as contiguous gaps are filled.
 */
class ReliableOrderedChannel : public Channel {
public:
    struct OutgoingMessage {
        Message msg;
        double last_sent_time{0.0};
        int send_count{0};
    };

    explicit ReliableOrderedChannel(uint8_t channel_id)
        : Channel(channel_id, ChannelType::ReliableOrdered) {}

    void send_message(const void* data, size_t size, double /*current_time*/) override {
        const auto* bytes = static_cast<const uint8_t*>(data);
        uint16_t msg_id = local_sequence_++;

        OutgoingMessage outgoing;
        outgoing.msg = Message(channel_id_, msg_id, std::vector<uint8_t>(bytes, bytes + size));
        outgoing.last_sent_time = 0.0;  // Ready to send immediately
        outgoing.send_count = 0;

        unacked_messages_[msg_id] = std::move(outgoing);
    }

    size_t write_outgoing_messages(uint8_t* buffer,
                                   size_t max_bytes,
                                   double current_time) override {
        size_t bytes_written = 0;

        for (auto& [msg_id, outgoing] : unacked_messages_) {
            // Determine if message is ready for initial send or retransmission
            // Retransmit interval = smoothed RTT + 25ms margin, minimum 50ms
            double rto_sec = std::max(0.050, static_cast<double>(current_rtt_ms_ + 25.0f) / 1000.0);

            if (outgoing.send_count > 0 && (current_time - outgoing.last_sent_time < rto_sec)) {
                continue;  // Not yet due for retransmission
            }

            size_t total_msg_size = MessageHeader::HEADER_SIZE + outgoing.msg.payload.size();
            if (bytes_written + total_msg_size > max_bytes) {
                break;  // MTU budget exceeded
            }

            // Write MessageHeader
            uint8_t* dest = buffer + bytes_written;
            dest[0] = outgoing.msg.channel_id;
            uint16_t net_mid = htons(outgoing.msg.message_id);
            uint16_t net_len = htons(static_cast<uint16_t>(outgoing.msg.payload.size()));
            std::memcpy(dest + 1, &net_mid, 2);
            std::memcpy(dest + 3, &net_len, 2);

            // Write Payload
            std::memcpy(dest + MessageHeader::HEADER_SIZE,
                        outgoing.msg.payload.data(),
                        outgoing.msg.payload.size());

            bytes_written += total_msg_size;
            outgoing.last_sent_time = current_time;
            outgoing.send_count++;

            // Track which packet sequence contains this message (populated by Connection)
            if (current_packet_sequence_ != 0xFFFF) {
                packet_to_messages_[current_packet_sequence_].push_back(msg_id);
            }
        }

        return bytes_written;
    }

    // Set context of which UDP packet sequence is currently being assembled
    void set_current_packet_sequence(uint16_t seq) { current_packet_sequence_ = seq; }

    void on_packet_acked(uint16_t packet_sequence) override {
        auto it = packet_to_messages_.find(packet_sequence);
        if (it != packet_to_messages_.end()) {
            for (uint16_t msg_id : it->second) {
                unacked_messages_.erase(msg_id);
            }
            packet_to_messages_.erase(it);
        }
    }

    bool process_incoming_message(uint16_t message_id,
                                  const uint8_t* payload,
                                  size_t size) override {
        // If message is older than already delivered sequence, ignore duplicate
        if (!sequence_greater_than(message_id, expected_receive_sequence_) &&
            message_id != expected_receive_sequence_) {
            return false;
        }

        // Buffer into reassembly map if not already present
        if (reassembly_buffer_.find(message_id) == reassembly_buffer_.end()) {
            reassembly_buffer_[message_id] =
                Message(channel_id_, message_id, std::vector<uint8_t>(payload, payload + size));
        }

        return true;
    }

    bool receive_message(Message& out_message) override {
        // Deliver in strict contiguous order
        auto it = reassembly_buffer_.find(expected_receive_sequence_);
        if (it != reassembly_buffer_.end()) {
            out_message = std::move(it->second);
            reassembly_buffer_.erase(it);
            expected_receive_sequence_++;
            return true;
        }
        return false;
    }

    void update(double /*current_time*/, float rtt_ms) override { current_rtt_ms_ = rtt_ms; }

    [[nodiscard]] bool has_outgoing_messages() const override { return !unacked_messages_.empty(); }

    [[nodiscard]] size_t pending_unacked_count() const { return unacked_messages_.size(); }

    [[nodiscard]] size_t reassembly_buffer_count() const { return reassembly_buffer_.size(); }

private:
    uint16_t local_sequence_{0};
    uint16_t expected_receive_sequence_{0};
    uint16_t current_packet_sequence_{0xFFFF};
    float current_rtt_ms_{40.0f};

    // message_id -> OutgoingMessage
    std::map<uint16_t, OutgoingMessage> unacked_messages_;

    // packet_sequence -> list of message_ids packed in that datagram
    std::map<uint16_t, std::vector<uint16_t>> packet_to_messages_;

    // Out-of-order reassembly buffer: message_id -> Message
    std::map<uint16_t, Message> reassembly_buffer_;
};

}  // namespace netcode
