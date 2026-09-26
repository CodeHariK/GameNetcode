#include "core/Timer.hpp"
#include "game/GameTypes.hpp"
#include "game/SnapshotBuffer.hpp"
#include "net/Connection.hpp"
#include "net/ReliableOrderedChannel.hpp"
#include "net/Socket.hpp"
#include "net/UnreliableSequencedChannel.hpp"
#include "net/UnreliableUnorderedChannel.hpp"

#include <atomic>
#include <csignal>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <map>
#include <string>
#include <vector>

// Milestone 4: an observer client. It receives 20Hz raw snapshots and renders
// the world by interpolating ~100ms in the past (SnapshotBuffer), so motion is
// smooth even though updates are infrequent and arrive with jitter.

namespace {
std::atomic<bool> g_running{true};
void handle_signal(int) { g_running = false; }
}  // namespace

int main(int argc, char* argv[]) {
    uint16_t port = 40000;
    std::string ip = "127.0.0.1";
    if (argc > 1) port = static_cast<uint16_t>(std::atoi(argv[1]));
    if (argc > 2) ip = argv[2];

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    netcode::Socket socket;
    if (!socket.open(0)) return 1;

    netcode::Address server_addr(ip, port);
    netcode::Connection conn(socket, 5.0, 0.25);
    conn.create_channel<netcode::UnreliableUnorderedChannel>(game::CH_UNRELIABLE);
    conn.create_channel<netcode::UnreliableSequencedChannel>(game::CH_STATE);
    conn.create_channel<netcode::ReliableOrderedChannel>(game::CH_RELIABLE);

    std::cout << "=== Game Netcode Client (Milestone 4: interpolation) ===\n";
    std::cout << "Connecting to " << server_addr.to_string() << "\n";

    double current_time = netcode::Timer::now_seconds();
    conn.connect(server_addr, current_time);

    game::SnapshotBuffer snapshots;  // 100ms interpolation delay by default
    uint32_t snapshots_received = 0;
    double last_stat = current_time;
    uint8_t buffer[2048];

    while (g_running) {
        current_time = netcode::Timer::now_seconds();

        netcode::Address sender;
        int bytes = 0;
        while ((bytes = socket.receive(sender, buffer, sizeof(buffer))) > 0) {
            const uint8_t* raw = nullptr;
            size_t raw_bytes = 0;
            if (!conn.process_packet(sender, buffer, bytes, raw, raw_bytes, current_time)) continue;

            netcode::Message msg;
            while (conn.receive_message(msg)) {
                if (msg.channel_id != game::CH_STATE) continue;
                if (msg.payload.size() < sizeof(game::SnapshotHeader)) continue;
                game::SnapshotHeader hdr{};
                std::memcpy(&hdr, msg.payload.data(), sizeof(hdr));
                const size_t expected =
                    sizeof(hdr) + hdr.entity_count * sizeof(game::EntityState);
                if (msg.payload.size() != expected) continue;
                const auto* states = reinterpret_cast<const game::EntityState*>(
                    msg.payload.data() + sizeof(hdr));
                snapshots.add_snapshot(hdr, states, current_time);
                ++snapshots_received;
            }
        }

        conn.update(current_time);
        if (conn.state() == netcode::ConnectionState::Disconnected) {
            std::cout << "[Client] Disconnected.\n";
            break;
        }

        if (current_time - last_stat >= 1.0) {
            std::map<uint32_t, game::EntityState> interp;
            const bool ok = snapshots.get_interpolated_state(current_time, interp);
            std::cout << "[Client] Ping: " << static_cast<int>(conn.rtt_ms())
                      << "ms | Snapshots: " << snapshots_received << " | ";
            if (ok && interp.count(1)) {
                const auto& e = interp.at(1);
                std::cout << "Interpolated entity 1: (" << std::fixed << std::setprecision(2)
                          << e.position.x << ", " << e.position.y << ")\n";
            } else {
                std::cout << "buffering...\n";
            }
            last_stat = current_time;
        }

        netcode::Timer::sleep_ms(1.0);
    }

    conn.disconnect(current_time);
    socket.close();
    return 0;
}
