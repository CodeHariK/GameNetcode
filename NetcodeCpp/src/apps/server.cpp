#include "core/Timer.hpp"
#include "game/GameTypes.hpp"
#include "net/Connection.hpp"
#include "net/ReliableOrderedChannel.hpp"
#include "net/Socket.hpp"
#include "net/UnreliableSequencedChannel.hpp"
#include "net/UnreliableUnorderedChannel.hpp"

#include <atomic>
#include <cmath>
#include <csignal>
#include <cstring>
#include <iostream>
#include <memory>
#include <unordered_map>
#include <vector>

// Milestone 4: authoritative server. Simulates a world at 60Hz and broadcasts
// raw snapshots (SnapshotHeader + EntityState[]) at 20Hz. No client input yet
// (that arrives in m05); the one entity moves on a scripted path.

namespace {
std::atomic<bool> g_running{true};
void handle_signal(int) { g_running = false; }
}  // namespace

int main(int argc, char* argv[]) {
    uint16_t port = 40000;
    if (argc > 1) port = static_cast<uint16_t>(std::atoi(argv[1]));

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    netcode::Socket socket;
    if (!socket.open(port)) return 1;

    std::cout << "=== Game Netcode Server (Milestone 4: snapshots) ===\n";
    std::cout << "Listening on port " << port << " | 60Hz sim, 20Hz snapshots\n";

    std::unordered_map<netcode::Address, std::unique_ptr<netcode::Connection>> clients;

    // One scripted moving entity for clients to interpolate.
    game::EntityState mover{1, {0.0f, 0.0f}, {0.0f, 0.0f}, 0x00FF00};

    const double tick_dt = 1.0 / 60.0;
    const uint32_t SNAPSHOT_RATE = 3;  // 20Hz
    uint32_t server_tick = 0;

    double current_time = netcode::Timer::now_seconds();
    const double start_time = current_time;
    double accumulator = 0.0;

    while (g_running) {
        const double new_time = netcode::Timer::now_seconds();
        accumulator += (new_time - current_time);
        current_time = new_time;

        // Accept clients / drain their packets (heartbeats keep them alive).
        netcode::Address sender;
        uint8_t buffer[2048];
        int bytes = 0;
        while ((bytes = socket.receive(sender, buffer, sizeof(buffer))) > 0) {
            auto it = clients.find(sender);
            if (it == clients.end()) {
                std::cout << "[Server] Client connected: " << sender.to_string() << "\n";
                auto conn = std::make_unique<netcode::Connection>(socket, 5.0, 0.25);
                conn->create_channel<netcode::UnreliableUnorderedChannel>(game::CH_UNRELIABLE);
                conn->create_channel<netcode::UnreliableSequencedChannel>(game::CH_STATE);
                conn->create_channel<netcode::ReliableOrderedChannel>(game::CH_RELIABLE);
                conn->accept(sender, current_time);
                it = clients.emplace(sender, std::move(conn)).first;
            }
            const uint8_t* raw = nullptr;
            size_t raw_bytes = 0;
            it->second->process_packet(sender, buffer, bytes, raw, raw_bytes, current_time);
        }

        // Fixed-timestep simulation + broadcast.
        while (accumulator >= tick_dt) {
            ++server_tick;
            accumulator -= tick_dt;

            const float t = static_cast<float>(current_time - start_time);
            mover.position = {60.0f * std::sinf(t * 1.5f), 20.0f * std::cosf(t * 1.5f)};

            if (server_tick % SNAPSHOT_RATE == 0) {
                const std::vector<game::EntityState> entities = {mover};
                game::SnapshotHeader hdr{server_tick, static_cast<uint32_t>(entities.size())};
                std::vector<uint8_t> snap(sizeof(hdr) + entities.size() * sizeof(game::EntityState));
                std::memcpy(snap.data(), &hdr, sizeof(hdr));
                std::memcpy(snap.data() + sizeof(hdr), entities.data(),
                            entities.size() * sizeof(game::EntityState));
                for (auto& [addr, conn] : clients) {
                    if (conn->is_connected()) {
                        conn->send_message(game::CH_STATE, snap.data(), snap.size(), current_time);
                    }
                }
            }
        }

        for (auto it = clients.begin(); it != clients.end();) {
            it->second->update(current_time);
            if (it->second->state() == netcode::ConnectionState::Disconnected) {
                std::cout << "[Server] Client timed out: " << it->first.to_string() << "\n";
                it = clients.erase(it);
            } else {
                ++it;
            }
        }

        netcode::Timer::sleep_ms(1.0);
    }

    socket.close();
    return 0;
}
