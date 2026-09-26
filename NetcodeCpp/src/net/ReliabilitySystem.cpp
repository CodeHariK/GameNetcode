#include "ReliabilitySystem.hpp"
#include <algorithm>
#include <cmath>

namespace netcode {

ReliabilitySystem::ReliabilitySystem(size_t buffer_size)
    : buffer_size_(buffer_size), sent_packets_(buffer_size) {
    reset();
}

void ReliabilitySystem::reset() {
    local_sequence_ = 0;
    remote_sequence_ = 0;
    has_received_remote_packet_ = false;
    remote_ack_bits_ = 0;
    rtt_ms_ = 0.0f;
    packet_loss_ = 0.0f;
    total_sent_ = 0;
    total_received_ = 0;
    total_acked_ = 0;

    for (auto& record : sent_packets_) {
        record.time_sent = 0.0;
        record.acked = false;
    }
}

void ReliabilitySystem::generate_packet_header(PacketHeader& header, double current_time) {
    header.protocol_id = PROTOCOL_ID;
    header.sequence = local_sequence_;
    header.ack = remote_sequence_;
    header.ack_bits = remote_ack_bits_;

    // Store record in sent history buffer for RTT measurement
    size_t index = local_sequence_ % buffer_size_;
    sent_packets_[index].time_sent = current_time;
    sent_packets_[index].acked = false;

    local_sequence_++;
    total_sent_++;
}

void ReliabilitySystem::packet_received(const PacketHeader& header, double current_time) {
    total_received_++;

    // 1. Update remote sequence and incoming ACK bitfield
    if (!has_received_remote_packet_) {
        remote_sequence_ = header.sequence;
        has_received_remote_packet_ = true;
        remote_ack_bits_ = 0;
    } else {
        advance_remote_sequence(header.sequence);
    }

    // 2. Process acknowledgments for our packets sent to peer:
    // Direct ACK:
    process_ack(header.ack, current_time);

    // Sliding bitfield ACKs (header.ack - 1 down to header.ack - 32):
    for (int i = 0; i < 32; ++i) {
        if (header.ack_bits & (1u << i)) {
            uint16_t acked_seq = static_cast<uint16_t>(header.ack - 1 - i);
            process_ack(acked_seq, current_time);
        }
    }
}

void ReliabilitySystem::advance_remote_sequence(uint16_t new_seq) {
    if (new_seq == remote_sequence_) {
        // Duplicate of the most recent packet, ignore
        return;
    }

    if (sequence_greater_than(new_seq, remote_sequence_)) {
        // Shift bitfield by the jump difference
        uint16_t diff = static_cast<uint16_t>(new_seq - remote_sequence_);
        if (diff <= 32) {
            remote_ack_bits_ <<= diff;
            // Set bit for the previous remote_sequence (which was 1 packet behind the new one)
            remote_ack_bits_ |= (1u << (diff - 1));
        } else {
            // Huge gap, more than 32 packets jumped
            remote_ack_bits_ = 0;
        }
        remote_sequence_ = new_seq;
    } else {
        // Out-of-order older packet: check if it falls within the 32-packet sliding window
        uint16_t diff = static_cast<uint16_t>(remote_sequence_ - new_seq);
        if (diff >= 1 && diff <= 32) {
            remote_ack_bits_ |= (1u << (diff - 1));
        }
    }
}

void ReliabilitySystem::process_ack(uint16_t ack_sequence, double current_time) {
    size_t index = ack_sequence % buffer_size_;
    PacketRecord& record = sent_packets_[index];

    // Only process if this slot actually recorded a sent packet and was not already acknowledged
    if (record.time_sent > 0.0 && !record.acked) {
        record.acked = true;
        total_acked_++;

        // Calculate sample RTT
        double sample_rtt_ms = (current_time - record.time_sent) * 1000.0;
        if (sample_rtt_ms >= 0.0 && sample_rtt_ms < 10000.0) {  // sanity check
            if (rtt_ms_ <= 0.0f) {
                rtt_ms_ = static_cast<float>(sample_rtt_ms);  // First sample initialization
            } else {
                // Exponential Moving Average: 90% history, 10% new sample
                constexpr float EMA_ALPHA = 0.10f;
                rtt_ms_ =
                    (1.0f - EMA_ALPHA) * rtt_ms_ + EMA_ALPHA * static_cast<float>(sample_rtt_ms);
            }
        }

        // Notify callback (e.g. channel ACK routing)
        if (ack_callback_) {
            ack_callback_(ack_sequence);
        }
    }
}

void ReliabilitySystem::update(double current_time) {
    // Calculate packet loss over recent sent packets window (e.g. past 100 packets older than RTT +
    // safety margin) A packet is considered lost if it was sent longer than (rtt_ms * 1.5 + 50ms)
    // ago and not acked.
    const double loss_threshold_sec =
        std::max(0.100, static_cast<double>(rtt_ms_ * 1.5f + 50.0f) / 1000.0);

    size_t evaluated_count = 0;
    size_t lost_count = 0;

    const size_t check_window = std::min(buffer_size_, size_t(128));
    for (size_t i = 0; i < check_window; ++i) {
        uint16_t seq = static_cast<uint16_t>(local_sequence_ - 1 - i);
        size_t index = seq % buffer_size_;
        const auto& record = sent_packets_[index];

        if (record.time_sent > 0.0) {
            double age = current_time - record.time_sent;
            if (age >= loss_threshold_sec) {
                evaluated_count++;
                if (!record.acked) {
                    lost_count++;
                }
            }
        }
    }

    if (evaluated_count > 10) {
        float sample_loss = static_cast<float>(lost_count) / static_cast<float>(evaluated_count);
        // Smooth packet loss estimation with EMA
        constexpr float LOSS_EMA_ALPHA = 0.05f;
        packet_loss_ = (1.0f - LOSS_EMA_ALPHA) * packet_loss_ + LOSS_EMA_ALPHA * sample_loss;
    }
}

}  // namespace netcode
