#include "net/Socket.hpp"
#include "net/Connection.hpp"
#include "net/NetworkSimulator.hpp"
#include "net/UnreliableUnorderedChannel.hpp"
#include "core/Timer.hpp"
#include "game/GameTypes.hpp"

#include <iostream>
#include <cassert>
#include <cstring>
#include <cstdint>

// Polls a non-blocking socket for up to timeout_ms; returns bytes received (0 if none).
static int wait_for_packet(netcode::Socket& sock,
                           netcode::Address& sender,
                           uint8_t* buf,
                           size_t cap,
                           double timeout_ms) {
    const double deadline = netcode::Timer::now_seconds() + timeout_ms / 1000.0;
    while (netcode::Timer::now_seconds() < deadline) {
        int n = sock.receive(sender, buf, cap);
        if (n > 0) return n;
        netcode::Timer::sleep_ms(1.0);
    }
    return 0;
}

void test_connection_latency_via_simulator() {
    netcode::Socket server_sock;
    assert(server_sock.open(55655));
    netcode::Address server_addr("127.0.0.1", 55655);

    netcode::Socket client_sock;
    assert(client_sock.open(0));

    // 80ms one-way latency, no loss.
    netcode::NetworkSimulator sim(client_sock, {80.0f, 0.0f, 0.0f, 0.0f});

    netcode::Connection conn(client_sock, 5.0, 0.25);
    conn.create_channel<netcode::UnreliableUnorderedChannel>(game::CH_UNRELIABLE);
    conn.set_network_simulator(&sim);

    double t = netcode::Timer::now_seconds();
    conn.connect(server_addr, t);

    const char* payload = "PING";
    conn.send_message(game::CH_UNRELIABLE, payload, std::strlen(payload) + 1, t);
    conn.update(t);  // flush -> routed into the simulator's delay queue

    // The packet is in transit, not on the wire yet.
    assert(sim.pending_packets_count() >= 1);

    // Nothing should arrive before the 80ms delay elapses.
    netcode::Address sender;
    uint8_t buf[512];
    sim.update(t + 0.02);  // 20ms: not due
    assert(server_sock.receive(sender, buf, sizeof(buf)) == 0);
    std::cout << "[PASS] Connection routed through simulator: no early delivery at 20ms.\n";

    // After the delay, flushing the simulator puts the packet on the wire.
    sim.update(t + 0.10);  // 100ms: due
    int n = wait_for_packet(server_sock, sender, buf, sizeof(buf), 200.0);
    assert(n > 0);
    std::cout << "[PASS] Packet delivered after latency window elapsed (" << n << " bytes).\n";

    server_sock.close();
    client_sock.close();
}

void test_connection_packet_loss_via_simulator() {
    netcode::Socket server_sock;
    assert(server_sock.open(55656));
    netcode::Address server_addr("127.0.0.1", 55656);

    netcode::Socket client_sock;
    assert(client_sock.open(0));

    // 100% loss: nothing should ever reach the server.
    netcode::NetworkSimulator sim(client_sock, {10.0f, 0.0f, 1.0f, 0.0f});

    netcode::Connection conn(client_sock, 5.0, 0.25);
    conn.create_channel<netcode::UnreliableUnorderedChannel>(game::CH_UNRELIABLE);
    conn.set_network_simulator(&sim);

    double t = netcode::Timer::now_seconds();
    conn.connect(server_addr, t);
    conn.send_message(game::CH_UNRELIABLE, "DROP", 5, t);
    conn.update(t);

    // Dropped packets are never queued for delivery.
    for (int i = 0; i < 5; ++i) sim.update(t + 0.05 * i);

    netcode::Address sender;
    uint8_t buf[512];
    int n = wait_for_packet(server_sock, sender, buf, sizeof(buf), 60.0);
    assert(n == 0);
    assert(sim.stats().packets_dropped >= 1);
    std::cout << "[PASS] 100% loss: no packet reached the server (dropped by simulator).\n";

    server_sock.close();
    client_sock.close();
}

int main() {
    std::cout << ">>> Running Milestone 9 Network Simulator Integration Tests <<<\n";
    test_connection_latency_via_simulator();
    test_connection_packet_loss_via_simulator();
    std::cout << ">>> ALL NETWORK SIMULATOR TESTS PASSED! <<<\n";
    return 0;
}
