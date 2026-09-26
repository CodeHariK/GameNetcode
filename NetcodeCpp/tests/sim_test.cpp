#include "net/Socket.hpp"
#include "net/NetworkSimulator.hpp"
#include "core/Timer.hpp"

#include <iostream>
#include <cassert>
#include <vector>
#include <cmath>
#include <cstring>

int main() {
    std::cout << ">>> Running NetworkSimulator and Socket Verification Test <<<\n";

    // 1. Basic Socket send/receive test
    netcode::Socket server_sock;
    assert(server_sock.open(55555));
    uint16_t server_port = server_sock.bound_port();
    assert(server_port == 55555);

    netcode::Socket client_sock;
    assert(client_sock.open(0));  // ephemeral port
    assert(client_sock.bound_port() > 0);

    const char* hello = "HELLO_NETCODE";
    netcode::Address server_addr("127.0.0.1", server_port);
    assert(client_sock.send(server_addr, hello, std::strlen(hello) + 1));

    // Yield a fraction to allow loopback delivery
    netcode::Timer::sleep_ms(5.0);

    netcode::Address from_addr;
    char buffer[256]{};
    int bytes = server_sock.receive(from_addr, buffer, sizeof(buffer));
    assert(bytes > 0);
    assert(std::string(buffer) == hello);
    std::cout << "[PASS] Basic non-blocking UDP loopback exchange verified.\n";

    // 2. NetworkSimulator Latency and Packet Loss Test
    netcode::NetworkSimulatorConfig sim_config{};
    sim_config.latency_ms = 30.0f;        // 30ms latency
    sim_config.jitter_ms = 0.0f;          // No jitter for deterministic delay test
    sim_config.packet_loss_rate = 0.20f;  // 20% loss rate
    sim_config.duplicate_rate = 0.0f;

    netcode::NetworkSimulator sim(client_sock, sim_config);

    const int TOTAL_PACKETS = 500;
    std::cout << "[INFO] Sending " << TOTAL_PACKETS
              << " packets with 30ms latency and 20% simulated loss...\n";

    for (int i = 0; i < TOTAL_PACKETS; ++i) {
        uint32_t seq = i;
        sim.send_packet(server_addr, &seq, sizeof(seq), netcode::Timer::now_seconds());
    }

    // Packet should NOT have reached server immediately (latency = 30ms)
    sim.update(netcode::Timer::now_seconds());
    int early_bytes = server_sock.receive(from_addr, buffer, sizeof(buffer));
    assert(early_bytes == 0);  // Must be zero because 30ms has not passed!
    std::cout << "[PASS] Packet delay verified: 0 packets arrived prematurely.\n";

    // Now advance time and pump until all valid packets arrive
    uint64_t received_count = 0;
    double run_until = netcode::Timer::now_seconds() + 0.250;  // 250ms window
    while (netcode::Timer::now_seconds() < run_until) {
        double now = netcode::Timer::now_seconds();
        sim.update(now);

        uint32_t seq = 0;
        while (server_sock.receive(from_addr, &seq, sizeof(seq)) > 0) {
            received_count++;
        }
        netcode::Timer::sleep_ms(1.0);
    }

    const auto& stats = sim.stats();
    std::cout << "[STATS] Sent: " << stats.packets_sent_attempted
              << ", Dropped by simulator: " << stats.packets_dropped
              << ", Transmitted: " << stats.packets_transmitted
              << ", Received by server: " << received_count << "\n";

    assert(received_count == stats.packets_transmitted);
    double observed_drop_rate = static_cast<double>(stats.packets_dropped) / TOTAL_PACKETS;
    std::cout << "[STATS] Observed drop rate: " << (observed_drop_rate * 100.0)
              << "% (Target: 20%)\n";

    // Check that loss rate is roughly within statistical expectation (+- 7%)
    assert(std::abs(observed_drop_rate - 0.20) < 0.07);
    std::cout << "[PASS] Statistical packet drop rate verified.\n";

    std::cout << "\n>>> ALL MILESTONE 1 VERIFICATION TESTS PASSED SUCCESSFULLY! <<<\n";
    return 0;
}
