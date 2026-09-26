#pragma once

#include <cstdint>
#include <vector>
#include <string>

namespace netcode {

/**
 * Game network channel delivery modes:
 *
 * 1. UnreliableUnordered:
 *    - Fire and forget. No guarantee of delivery, no ordering.
 *    - Lowest latency and zero buffer overhead.
 *    - Best for: VoIP, ambient effects, high-frequency stats.
 *
 * 2. UnreliableSequenced:
 *    - Unreliable delivery, but out-of-order packets are dropped.
 *    - Only the newest message is accepted by receiver.
 *    - Best for: Uncompressed entity positions, camera rotation, analog input.
 *
 * 3. ReliableOrdered:
 *    - Guaranteed delivery and strictly sequential reassembly.
 *    - Retransmitted until acknowledged; receiver buffers out-of-order messages
 *      and delivers them only in contiguous order (HOL blocking isolated per channel).
 *    - Best for: Chat, player joins/leaves, game state phase transitions, inventory actions.
 */
enum class ChannelType : uint8_t {
    UnreliableUnordered = 0,
    UnreliableSequenced = 1,
    ReliableOrdered = 2,
    Count
};

inline const char* channel_type_to_string(ChannelType type) {
    switch (type) {
        case ChannelType::UnreliableUnordered:
            return "UnreliableUnordered";
        case ChannelType::UnreliableSequenced:
            return "UnreliableSequenced";
        case ChannelType::ReliableOrdered:
            return "ReliableOrdered";
        default:
            return "Unknown";
    }
}

/**
 * Wire header prefix for each message packed into a UDP packet:
 *  - channel_id : 1 byte (Which channel this message belongs to)
 *  - message_id : 2 bytes (Per-channel sequence/message index)
 *  - payload_size: 2 bytes (Length of payload bytes following this header)
 * Total: 5 bytes
 */
#pragma pack(push, 1)
struct MessageHeader {
    uint8_t channel_id{0};
    uint16_t message_id{0};
    uint16_t payload_size{0};

    static constexpr size_t HEADER_SIZE = 5;
};
#pragma pack(pop)

struct Message {
    uint8_t channel_id{0};
    uint16_t message_id{0};
    std::vector<uint8_t> payload;

    Message() = default;
    Message(uint8_t ch, uint16_t id, std::vector<uint8_t> data)
        : channel_id(ch), message_id(id), payload(std::move(data)) {}
};

}  // namespace netcode
