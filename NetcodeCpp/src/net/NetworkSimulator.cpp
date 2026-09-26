#include "NetworkSimulator.hpp"
#include <algorithm>

namespace netcode {

NetworkSimulator::NetworkSimulator(Socket& socket, NetworkSimulatorConfig config)
    : socket_(socket), config_(config), rng_(std::random_device{}()) {}

bool NetworkSimulator::send_packet(const Address& destination,
                                   const void* data,
                                   size_t size,
                                   double current_time) {
    if (!data || size == 0) return false;

    stats_.packets_sent_attempted++;

    // 1. Packet Loss Simulation
    if (config_.packet_loss_rate > 0.0f && unit_dist_(rng_) < config_.packet_loss_rate) {
        stats_.packets_dropped++;
        return true;  // Dropped by network
    }

    // 2. Latency & Jitter Calculation
    float jitter = 0.0f;
    if (config_.jitter_ms > 0.0f) {
        std::uniform_real_distribution<float> jitter_dist(-config_.jitter_ms, config_.jitter_ms);
        jitter = jitter_dist(rng_);
    }

    float delay_ms = std::max(0.0f, config_.latency_ms + jitter);
    double delivery_time = current_time + (delay_ms / 1000.0);

    const auto* bytes = static_cast<const uint8_t*>(data);
    std::vector<uint8_t> payload(bytes, bytes + size);

    // 3. Packet Duplication Simulation
    if (config_.duplicate_rate > 0.0f && unit_dist_(rng_) < config_.duplicate_rate) {
        stats_.packets_duplicated++;
        // Duplicate arrives slightly later (e.g. 5ms after original)
        delay_queue_.push(SimulatedPacket{destination, payload, delivery_time + 0.005});
    }

    // 4. Enqueue packet for delivery
    delay_queue_.push(SimulatedPacket{destination, std::move(payload), delivery_time});

    return true;
}

void NetworkSimulator::update(double current_time) {
    while (!delay_queue_.empty()) {
        const auto& packet = delay_queue_.top();
        if (packet.delivery_time > current_time) {
            break;  // Next packet is not due yet
        }

        socket_.send(packet.destination, packet.data.data(), packet.data.size());
        stats_.packets_transmitted++;
        delay_queue_.pop();
    }
}

}  // namespace netcode
