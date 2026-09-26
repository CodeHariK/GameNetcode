#include "net/PacketHeader.hpp"
#include "net/ReliabilitySystem.hpp"
#include "core/Timer.hpp"

#include <iostream>
#include <cassert>
#include <vector>
#include <cmath>

void test_sequence_greater_than() {
    using netcode::sequence_greater_than;

    assert(sequence_greater_than(1, 0));
    assert(sequence_greater_than(100, 50));
    assert(!sequence_greater_than(50, 100));

    // Test 16-bit wraparound near 65535 -> 0
    assert(sequence_greater_than(0, 65535));
    assert(sequence_greater_than(5, 65530));
    assert(!sequence_greater_than(65530, 5));

    std::cout << "[PASS] Sequence number comparison & wraparound test.\n";
}

void test_packet_header_serialization() {
    netcode::PacketHeader header{};
    header.protocol_id = netcode::PROTOCOL_ID;
    header.sequence = 12345;
    header.ack = 12300;
    header.ack_bits = 0b10101010101010101010101010101010;

    uint8_t buffer[netcode::PacketHeader::HEADER_SIZE];
    header.serialize(buffer);

    netcode::PacketHeader deserialized{};
    assert(deserialized.deserialize(buffer, sizeof(buffer)));

    assert(deserialized.protocol_id == netcode::PROTOCOL_ID);
    assert(deserialized.sequence == 12345);
    assert(deserialized.ack == 12300);
    assert(deserialized.ack_bits == 0b10101010101010101010101010101010);

    // Reject packet with invalid magic ID
    buffer[0] = 0x00;
    netcode::PacketHeader invalid_header{};
    assert(!invalid_header.deserialize(buffer, sizeof(buffer)));

    std::cout << "[PASS] PacketHeader serialization and magic validation test.\n";
}

void test_reliability_sliding_ack() {
    netcode::ReliabilitySystem client_rel(256);
    netcode::ReliabilitySystem server_rel(256);

    double time = 100.0;
    const double one_way_delay = 0.020;  // 20ms one-way -> 40ms RTT

    // Simulate sending 10 packets from client to server, and echoing immediately back
    for (int i = 0; i < 10; ++i) {
        netcode::PacketHeader client_header;
        client_rel.generate_packet_header(client_header, time);

        // Server receives client packet after one_way_delay
        server_rel.packet_received(client_header, time + one_way_delay);

        // Server immediately sends an acknowledgment packet back
        netcode::PacketHeader server_header;
        server_rel.generate_packet_header(server_header, time + one_way_delay);

        // Client receives server acknowledgment after another one_way_delay (Total RTT = 40ms)
        client_rel.packet_received(server_header, time + (one_way_delay * 2.0));

        time += 0.016;  // advance 60Hz tick
    }

    assert(server_rel.remote_sequence() == 9);
    assert(client_rel.total_acked() == 10);

    // Give it a tiny bit of floating point tolerance for checking EMA stabilization
    assert(client_rel.rtt_ms() >= 35.0f && client_rel.rtt_ms() <= 45.0f);

    std::cout << "[PASS] Sliding ACK bitfield and RTT measurement test (RTT = "
              << client_rel.rtt_ms() << "ms).\n";
}

int main() {
    std::cout << ">>> Running Milestone 2 Protocol Unit Tests <<<\n";
    test_sequence_greater_than();
    test_packet_header_serialization();
    test_reliability_sliding_ack();
    std::cout << ">>> ALL PROTOCOL TESTS PASSED! <<<\n";
    return 0;
}
