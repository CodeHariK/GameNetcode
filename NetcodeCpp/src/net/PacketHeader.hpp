#pragma once

#include <cstdint>
#include <cstring>
#include <arpa/inet.h>

namespace netcode {

/**
 * Protocol Magic ID to reject stray/alien packets.
 * 'NETC' = 0x4E455443
 */
constexpr uint32_t PROTOCOL_ID = 0x4E455443;

/**
 * Standard Wire Packet Header (12 bytes, packed):
 *  - protocol_id : 4 bytes (Magic ID)
 *  - sequence    : 2 bytes (16-bit sequence number of this packet)
 *  - ack         : 2 bytes (Highest sequence number received from remote peer)
 *  - ack_bits    : 4 bytes (Bitfield of 32 packets preceding `ack`. Bit 0 = ack-1, Bit 31 = ack-32)
 */
#pragma pack(push, 1)
struct PacketHeader {
    uint32_t protocol_id{PROTOCOL_ID};
    uint16_t sequence{0};
    uint16_t ack{0};
    uint32_t ack_bits{0};

    // Serialize header to network byte order
    void serialize(uint8_t* dest) const {
        uint32_t net_proto = htonl(protocol_id);
        uint16_t net_seq = htons(sequence);
        uint16_t net_ack = htons(ack);
        uint32_t net_bits = htonl(ack_bits);

        std::memcpy(dest + 0, &net_proto, 4);
        std::memcpy(dest + 4, &net_seq, 2);
        std::memcpy(dest + 6, &net_ack, 2);
        std::memcpy(dest + 8, &net_bits, 4);
    }

    // Deserialize header from network byte order. Returns false if magic protocol_id mismatches.
    bool deserialize(const uint8_t* src, size_t size) {
        if (size < HEADER_SIZE) return false;

        uint32_t net_proto;
        uint16_t net_seq;
        uint16_t net_ack;
        uint32_t net_bits;

        std::memcpy(&net_proto, src + 0, 4);
        std::memcpy(&net_seq, src + 4, 2);
        std::memcpy(&net_ack, src + 6, 2);
        std::memcpy(&net_bits, src + 8, 4);

        protocol_id = ntohl(net_proto);
        sequence = ntohs(net_seq);
        ack = ntohs(net_ack);
        ack_bits = ntohl(net_bits);

        return (protocol_id == PROTOCOL_ID);
    }

    static constexpr size_t HEADER_SIZE = 12;
};
#pragma pack(pop)

/**
 * Handles 16-bit sequence number wrapping (modulo 65536).
 * Returns true if s1 is more recent than s2.
 */
inline bool sequence_greater_than(uint16_t s1, uint16_t s2) {
    return ((s1 > s2) && (s1 - s2 <= 32768)) || ((s1 < s2) && (s2 - s1 > 32768));
}

}  // namespace netcode
