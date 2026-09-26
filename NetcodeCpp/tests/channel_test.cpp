#include "net/UnreliableUnorderedChannel.hpp"
#include "net/UnreliableSequencedChannel.hpp"
#include "net/ReliableOrderedChannel.hpp"
#include "core/Timer.hpp"

#include <iostream>
#include <cassert>
#include <string>
#include <vector>

void test_unreliable_unordered_channel() {
    netcode::UnreliableUnorderedChannel ch(0);

    const std::string msg1 = "AUDIO_CLIP_01";
    const std::string msg2 = "AUDIO_CLIP_02";

    ch.send_message(msg1.data(), msg1.size(), 0.0);
    ch.send_message(msg2.data(), msg2.size(), 0.0);
    assert(ch.has_outgoing_messages());

    uint8_t buffer[512];
    size_t written = ch.write_outgoing_messages(buffer, sizeof(buffer), 0.0);
    assert(written > 0);
    assert(!ch.has_outgoing_messages());

    // Simulate receiver processing
    netcode::UnreliableUnorderedChannel recv_ch(0);
    recv_ch.process_incoming_message(0, reinterpret_cast<const uint8_t*>(msg1.data()), msg1.size());
    recv_ch.process_incoming_message(0, reinterpret_cast<const uint8_t*>(msg2.data()), msg2.size());

    netcode::Message out;
    assert(recv_ch.receive_message(out));
    assert(std::string(out.payload.begin(), out.payload.end()) == msg1);

    assert(recv_ch.receive_message(out));
    assert(std::string(out.payload.begin(), out.payload.end()) == msg2);

    assert(!recv_ch.receive_message(out));
    std::cout << "[PASS] UnreliableUnorderedChannel verified.\n";
}

void test_unreliable_sequenced_channel() {
    netcode::UnreliableSequencedChannel ch(1);

    ch.send_message("POS_1", 5, 0.0);
    ch.send_message("POS_2", 5, 0.0);
    ch.send_message("POS_3", 5, 0.0);

    // Receiver should accept newer sequence numbers and drop older out-of-order ones
    netcode::UnreliableSequencedChannel recv_ch(1);

    // Receive seq 2 first (newer)
    assert(recv_ch.process_incoming_message(2, reinterpret_cast<const uint8_t*>("POS_2"), 5));

    // Receive seq 1 later (older out-of-order) -> MUST BE DROPPED!
    assert(!recv_ch.process_incoming_message(1, reinterpret_cast<const uint8_t*>("POS_1"), 5));
    assert(recv_ch.dropped_count() == 1);

    // Receive seq 3 (newest) -> Accepted
    assert(recv_ch.process_incoming_message(3, reinterpret_cast<const uint8_t*>("POS_3"), 5));

    netcode::Message out;
    assert(recv_ch.receive_message(out));
    assert(out.message_id == 2);

    assert(recv_ch.receive_message(out));
    assert(out.message_id == 3);

    // POS_1 was dropped, so no more messages
    assert(!recv_ch.receive_message(out));

    std::cout << "[PASS] UnreliableSequencedChannel verified (out-of-order packet dropped).\n";
}

void test_reliable_ordered_channel_reassembly() {
    netcode::ReliableOrderedChannel sender_ch(2);
    netcode::ReliableOrderedChannel receiver_ch(2);

    // Sender sends 3 messages: 0, 1, 2
    sender_ch.send_message("MSG_0", 5, 0.0);
    sender_ch.send_message("MSG_1", 5, 0.0);
    sender_ch.send_message("MSG_2", 5, 0.0);

    // Simulate network arriving OUT-OF-ORDER: MSG_2 arrives first, then MSG_1, then MSG_0
    receiver_ch.process_incoming_message(2, reinterpret_cast<const uint8_t*>("MSG_2"), 5);
    receiver_ch.process_incoming_message(1, reinterpret_cast<const uint8_t*>("MSG_1"), 5);

    // Receiver cannot deliver MSG_1 or MSG_2 yet because MSG_0 hasn't arrived (Head-of-Line
    // isolation)
    netcode::Message out;
    assert(!receiver_ch.receive_message(out));
    assert(receiver_ch.reassembly_buffer_count() == 2);

    // Now MSG_0 arrives!
    receiver_ch.process_incoming_message(0, reinterpret_cast<const uint8_t*>("MSG_0"), 5);

    // All 3 messages must now be delivered strictly in order 0, 1, 2
    assert(receiver_ch.receive_message(out));
    assert(out.message_id == 0 && std::string(out.payload.begin(), out.payload.end()) == "MSG_0");

    assert(receiver_ch.receive_message(out));
    assert(out.message_id == 1 && std::string(out.payload.begin(), out.payload.end()) == "MSG_1");

    assert(receiver_ch.receive_message(out));
    assert(out.message_id == 2 && std::string(out.payload.begin(), out.payload.end()) == "MSG_2");

    assert(!receiver_ch.receive_message(out));
    assert(receiver_ch.reassembly_buffer_count() == 0);

    std::cout << "[PASS] ReliableOrderedChannel out-of-order reassembly verified.\n";
}

void test_reliable_ordered_retransmission_and_ack() {
    netcode::ReliableOrderedChannel sender(2);
    sender.send_message("IMPORTANT_EVENT", 15, 10.0);

    // Write outgoing messages into packet sequence 100
    uint8_t buffer[256];
    sender.set_current_packet_sequence(100);
    size_t written = sender.write_outgoing_messages(buffer, sizeof(buffer), 10.0);
    assert(written > 0);
    assert(sender.pending_unacked_count() == 1);

    // If packet sequence 100 is NOT acknowledged, message stays in unacked list and will retransmit
    // Try write before RTO window -> should NOT write duplicate yet
    size_t early_write = sender.write_outgoing_messages(buffer, sizeof(buffer), 10.010);
    assert(early_write == 0);

    // Now simulate acknowledgment of packet 100 arriving
    sender.on_packet_acked(100);
    assert(sender.pending_unacked_count() == 0);

    std::cout << "[PASS] ReliableOrderedChannel ACK and retransmission lifecycle verified.\n";
}

int main() {
    std::cout << ">>> Running Milestone 3 Channel Multiplexing Unit Tests <<<\n";
    test_unreliable_unordered_channel();
    test_unreliable_sequenced_channel();
    test_reliable_ordered_channel_reassembly();
    test_reliable_ordered_retransmission_and_ack();
    std::cout << ">>> ALL CHANNEL TESTS PASSED! <<<\n";
    return 0;
}
