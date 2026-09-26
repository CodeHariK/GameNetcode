#pragma once

#include "Channel.hpp"
#include <queue>
#include <cstring>
#include <arpa/inet.h>

namespace netcode {

/**
 * UnreliableUnorderedChannel:
 * Messages are sent once. No tracking of sequence, no ordering, no retransmissions.
 */
class UnreliableUnorderedChannel : public Channel {
public:
    explicit UnreliableUnorderedChannel(uint8_t channel_id)
        : Channel(channel_id, ChannelType::UnreliableUnordered) {}

    void send_message(const void* data, size_t size, double /*current_time*/) override {
        const auto* bytes = static_cast<const uint8_t*>(data);
        outgoing_queue_.emplace(channel_id_, 0, std::vector<uint8_t>(bytes, bytes + size));
    }

    size_t write_outgoing_messages(uint8_t* buffer,
                                   size_t max_bytes,
                                   double /*current_time*/) override {
        size_t bytes_written = 0;

        while (!outgoing_queue_.empty()) {
            const Message& msg = outgoing_queue_.front();
            size_t total_msg_size = MessageHeader::HEADER_SIZE + msg.payload.size();

            if (bytes_written + total_msg_size > max_bytes) {
                break;  // Exceeds packet MTU budget
            }

            // Write MessageHeader
            MessageHeader hdr;
            hdr.channel_id = msg.channel_id;
            hdr.message_id = 0;  // Unused for unordered
            hdr.payload_size = static_cast<uint16_t>(msg.payload.size());

            uint8_t* dest = buffer + bytes_written;
            dest[0] = hdr.channel_id;
            uint16_t net_mid = htons(hdr.message_id);
            uint16_t net_len = htons(hdr.payload_size);
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
        incoming_queue_.emplace(channel_id_,
                                message_id,
                                std::vector<uint8_t>(payload, payload + size));
        return true;
    }

    bool receive_message(Message& out_message) override {
        if (incoming_queue_.empty()) return false;
        out_message = std::move(incoming_queue_.front());
        incoming_queue_.pop();
        return true;
    }

    [[nodiscard]] bool has_outgoing_messages() const override { return !outgoing_queue_.empty(); }

private:
    std::queue<Message> outgoing_queue_;
    std::queue<Message> incoming_queue_;
};

}  // namespace netcode
