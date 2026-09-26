#pragma once

#include "Channel.hpp"
#include "PacketHeader.hpp"  // For sequence_greater_than
#include <queue>
#include <cstring>
#include <arpa/inet.h>

namespace netcode {

/**
 * UnreliableSequencedChannel:
 * Messages are tagged with a 16-bit sequence number.
 * Messages sent are unreliable (no retransmission), but the receiver drops any
 * arriving message whose sequence number is older than the newest received message.
 */
class UnreliableSequencedChannel : public Channel {
public:
    explicit UnreliableSequencedChannel(uint8_t channel_id)
        : Channel(channel_id, ChannelType::UnreliableSequenced) {}

    void send_message(const void* data, size_t size, double /*current_time*/) override {
        const auto* bytes = static_cast<const uint8_t*>(data);
        outgoing_queue_.emplace(channel_id_,
                                local_sequence_++,
                                std::vector<uint8_t>(bytes, bytes + size));
    }

    size_t write_outgoing_messages(uint8_t* buffer,
                                   size_t max_bytes,
                                   double /*current_time*/) override {
        size_t bytes_written = 0;

        while (!outgoing_queue_.empty()) {
            const Message& msg = outgoing_queue_.front();
            size_t total_msg_size = MessageHeader::HEADER_SIZE + msg.payload.size();

            if (bytes_written + total_msg_size > max_bytes) {
                break;
            }

            uint8_t* dest = buffer + bytes_written;
            dest[0] = msg.channel_id;
            uint16_t net_mid = htons(msg.message_id);
            uint16_t net_len = htons(static_cast<uint16_t>(msg.payload.size()));
            std::memcpy(dest + 1, &net_mid, 2);
            std::memcpy(dest + 3, &net_len, 2);

            std::memcpy(dest + MessageHeader::HEADER_SIZE, msg.payload.data(), msg.payload.size());

            bytes_written += total_msg_size;
            outgoing_queue_.pop();
        }

        return bytes_written;
    }

    bool process_incoming_message(uint16_t message_id,
                                  const uint8_t* payload,
                                  size_t size) override {
        if (!has_received_message_) {
            has_received_message_ = true;
            remote_sequence_ = message_id;
            incoming_queue_.emplace(channel_id_,
                                    message_id,
                                    std::vector<uint8_t>(payload, payload + size));
            return true;
        }

        // Only accept message if it is newer than the highest received message
        if (sequence_greater_than(message_id, remote_sequence_)) {
            remote_sequence_ = message_id;
            incoming_queue_.emplace(channel_id_,
                                    message_id,
                                    std::vector<uint8_t>(payload, payload + size));
            return true;
        }

        // Discard older / out-of-order message
        dropped_count_++;
        return false;
    }

    bool receive_message(Message& out_message) override {
        if (incoming_queue_.empty()) return false;
        out_message = std::move(incoming_queue_.front());
        incoming_queue_.pop();
        return true;
    }

    [[nodiscard]] bool has_outgoing_messages() const override { return !outgoing_queue_.empty(); }

    [[nodiscard]] uint64_t dropped_count() const { return dropped_count_; }

private:
    uint16_t local_sequence_{0};
    uint16_t remote_sequence_{0};
    bool has_received_message_{false};
    uint64_t dropped_count_{0};

    std::queue<Message> outgoing_queue_;
    std::queue<Message> incoming_queue_;
};

}  // namespace netcode
