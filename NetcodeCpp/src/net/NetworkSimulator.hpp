#pragma once

#include "Address.hpp"
#include "Socket.hpp"
#include <vector>
#include <queue>
#include <random>
#include <cstdint>

namespace netcode {

struct NetworkSimulatorConfig {
    float latency_ms{0.0f};        // Additional one-way delay in ms (e.g. 50.0f)
    float jitter_ms{0.0f};         // Jitter in ms (+/- jitter_ms)
    float packet_loss_rate{0.0f};  // [0.0, 1.0] probability of dropping packet
    float duplicate_rate{0.0f};    // [0.0, 1.0] probability of sending duplicate
};

struct SimulatorStats {
    uint64_t packets_sent_attempted{0};
    uint64_t packets_dropped{0};
    uint64_t packets_transmitted{0};
    uint64_t packets_duplicated{0};
};

struct SimulatedPacket {
    Address destination;
    std::vector<uint8_t> data;
    double delivery_time{0.0};

    // Min-heap comparator based on delivery_time
    bool operator>(const SimulatedPacket& other) const {
        return delivery_time > other.delivery_time;
    }
};

/**
 * NetworkSimulator wraps a Socket to artificially inject:
 * - Latency (ping)
 * - Jitter (latency variance causing out-of-order packets)
 * - Packet loss
 * - Duplicate packets
 */
class NetworkSimulator {
public:
    explicit NetworkSimulator(Socket& socket, NetworkSimulatorConfig config = {});

    void set_config(const NetworkSimulatorConfig& config) { config_ = config; }
    [[nodiscard]] const NetworkSimulatorConfig& config() const { return config_; }
    [[nodiscard]] const SimulatorStats& stats() const { return stats_; }

    // Schedules packet for simulated transmission.
    // Packet may be dropped, delayed, or duplicated according to config.
    bool send_packet(const Address& destination,
                     const void* data,
                     size_t size,
                     double current_time);

    // Call regularly in game loop to flush packets whose delivery_time has arrived.
    void update(double current_time);

    // Direct pass-through to receive from underlying socket
    int receive_packet(Address& sender, void* data, size_t max_size) {
        return socket_.receive(sender, data, max_size);
    }

    // Number of packets currently in transit in the delay buffer
    [[nodiscard]] size_t pending_packets_count() const { return delay_queue_.size(); }

private:
    Socket& socket_;
    NetworkSimulatorConfig config_;
    SimulatorStats stats_;

    std::priority_queue<SimulatedPacket,
                        std::vector<SimulatedPacket>,
                        std::greater<SimulatedPacket> >
        delay_queue_;

    std::mt19937 rng_;
    std::uniform_real_distribution<float> unit_dist_{0.0f, 1.0f};
};

}  // namespace netcode
